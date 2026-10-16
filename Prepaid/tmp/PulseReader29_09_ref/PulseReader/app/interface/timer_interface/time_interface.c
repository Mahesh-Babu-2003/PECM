#include "time_interface.h"

/** @addtogroup SYS_TIME
  * @{
  */

/* Private defines -----------------------------------------------------------*/
/**
 * @defgroup SYS_TIME_private_defines SYS_TIME private defines
 *  @{
 */

/**
  * @brief number of day in leap year up to the end of February
  *
  */
#define END_OF_FEBRUARY_LEAP             60 //31+29

/**
  * @brief number of day in leap year up to the end of july
  *
  */
#define END_OF_JULY_LEAP              213 //31+29+...

/**
  * @brief number of day in normal year up to the end of February
  *
  */
#define END_OF_FEBRUARY_NORM             59 //31+28

/**
  * @brief number of day in normal year up to the end of july
  *
  */
#define END_OF_JULY_NORM              212 //31+28+...

/**
  * @brief delta is referenced to Unix time
  * @note UNIX time 0 = starts at 01:00:00, 01/01/1970
  * but calculation is easier from Monday 1st January 1968
  *
  */
#define CALC_REF_YEAR                  68

/**
  * @brief delta is referenced to Unix time
  * @note UNIX time 0 = starts at 01:00:00, 01/01/1970
  * but calculation is easier from Monday 1st January 1968
  *
  */
#define CALC_REF_YEAR_TO_UNIX_REF_YEAR_COMPENSATION_IN_SECONDS  ( ( TM_DAYS_IN_LEAP_YEAR + TM_DAYS_IN_YEAR ) * TM_SECONDS_IN_1DAY )

/**
  * @brief month correction table of a normal year. To calculate the day number within a year
  * @note error compensation is between 0 and 2 days. 2 bits per month
  *
  */
#define  DAYS_IN_MONTH_CORRECTION_NORM        ( (uint32_t )0x99AAA0 )

/**
  * @brief month correction table of a leap year. To calculate the day number within a year
  * @note error compensation is between 0 and 2 days. 2 bits per month
  *
  */
#define  DAYS_IN_MONTH_CORRECTION_LEAP        ( (uint32_t )0x445550 )

/**
  * @brief find  X/365.25
  *
  */
/* 365.25 = (366 + 365 + 365 + 365)/4 */
#define DIV_365_25( X )                 ( ( ( X ) * 91867 + 22750 ) >> 25 )

/**
  * @brief find the nearest quotient of X/86400 (8640 number of seconds in one week)
  *
  */
#define DIV_APPROX_86400( X )             ( ( ( X ) >> 18 ) + ( ( X ) >> 17 ) )

/**
  * @brief find the nearest quotient of X/1000
  *
  */
#define DIV_APPROX_1000( X )            ( ( ( X ) >> 10 ) +( ( X ) >> 16 ) + ( ( X ) >> 17 ) )

/**
  * @brief find the nearest quotient of X/60
  *
  */
#define DIV_APPROX_60( X )              ( ( ( X ) * 17476 ) >> 20 )

/**
  * @brief find the nearest quotient of X/61
  *
  */
#define DIV_APPROX_61( X )              ( ( ( X ) * 68759 ) >> 22 )

/**
  * @brief Calculates mod(x,7)
  *
  */
#define MODULO_7( X )                 ( ( X ) -( ( ( ( ( X ) + 1 ) * 299593 ) >> 21 ) * 7 ) )

/**
  * @brief Calculates ceiling( X / N )
  *
  */
#define DIVC( X, N )                ( ( ( X ) + ( N ) -1 ) / ( N ) )

/**
  * @brief Calculates ceiling( X / 4 )
  *
  */
#define DIVC_BY_4( X )                ( ( ( X ) + 3 ) >>2 )

/**
  * @brief Calculates ceiling( X / 2 )
  *
  */
#define DIVC_BY_2( X )                ( ( ( X ) + 1 ) >> 1 )
/**
  *  @}
  */

/* Private constants -----------------------------------------------------------*/
/**
 * @defgroup SYSTIME_private_variable SYSTIME private constants
 *  @{
 */
const char *WeekDayString[]={ "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
/**
  *  @}
  */

/* Private function prototypes -----------------------------------------------*/

/**
 * @defgroup SYSTIME_private_function_prototypes SYSTIME private function prototypes
 *  @{
 */
static uint32_t CalendarGetMonth( uint32_t days, uint32_t year );
static void CalendarDiv86400( uint32_t in, uint32_t* out, uint32_t* remainder );
static uint32_t CalendarDiv61( uint32_t in );
static void CalendarDiv60( uint32_t in, uint32_t* out, uint32_t* remainder );
static uint32_t CalendarGetMonth( uint32_t days, uint32_t year )
{
  uint32_t month;
  if( ( year % 4 ) == 0 )
  {   /*leap year*/
    if( days < END_OF_FEBRUARY_LEAP )
    {   // January or February
      // month =  days * 2 / ( 30 + 31 );
      month = CalendarDiv61( days * 2 );
    }
    else if( days < END_OF_JULY_LEAP )
    {
      month = CalendarDiv61( ( days - END_OF_FEBRUARY_LEAP ) * 2 ) + 2;
    }
    else
    {
      month = CalendarDiv61( ( days - END_OF_JULY_LEAP ) * 2 ) + 7;
    }
  }
  else
  {
    if( days < END_OF_FEBRUARY_NORM )
    {   // January or February
      month = CalendarDiv61( days * 2 );
    }
    else if( days < END_OF_JULY_NORM )
    {
      month = CalendarDiv61( ( days - END_OF_FEBRUARY_NORM ) * 2 ) + 2;
    }
    else
    {
      month = CalendarDiv61( ( days - END_OF_JULY_NORM ) * 2 ) + 7;
    }
  }
  return month;
}

static void CalendarDiv86400( uint32_t in, uint32_t* out, uint32_t* remainder )
{
#if 0
  *remainder = in % SECONDS_IN_1DAY;
  *out     = in / SECONDS_IN_1DAY;
#else
  uint32_t outTemp = 0;
  uint32_t divResult = DIV_APPROX_86400( in );

  while( divResult >=1 )
  {
    outTemp += divResult;
    in -= divResult * 86400;
    divResult= DIV_APPROX_86400( in );
  }
  if( in >= 86400 )
  {
    outTemp += 1;
    in -= 86400;
  }

  *remainder = in;
  *out = outTemp;
#endif
}

static uint32_t CalendarDiv61( uint32_t in )
{
#if 0
  return( in / 61 );
#else
  uint32_t outTemp = 0;
  uint32_t divResult = DIV_APPROX_61( in );
  while( divResult >=1 )
  {
    outTemp += divResult;
    in -= divResult * 61;
    divResult = DIV_APPROX_61( in );
  }
  if( in >= 61 )
  {
    outTemp += 1;
    in -= 61;
  }
  return outTemp;
#endif
}

static void CalendarDiv60( uint32_t in, uint32_t* out, uint32_t* remainder )
{
#if 0
  *remainder = in % 60;
  *out     = in / 60;
#else
  uint32_t outTemp = 0;
  uint32_t divResult = DIV_APPROX_60( in );

  while( divResult >=1 )
  {
    outTemp += divResult;
    in -= divResult * 60;
    divResult = DIV_APPROX_60( in );
  }
  if( in >= 60 )
  {
    outTemp += 1;
    in -= 60;
  }
  *remainder = in;
  *out = outTemp;
#endif
}
uint32_t get_timestamp_from_time( const struct tm* localtime )
{
  uint32_t nbdays;
  uint32_t nbsecs;
  uint32_t year = localtime->tm_year - CALC_REF_YEAR;
  uint32_t correctionMonth[4] =
  {
    DAYS_IN_MONTH_CORRECTION_LEAP,
    DAYS_IN_MONTH_CORRECTION_NORM,
    DAYS_IN_MONTH_CORRECTION_NORM,
    DAYS_IN_MONTH_CORRECTION_NORM
  };

  nbdays = DIVC( ( TM_DAYS_IN_YEAR * 3 + TM_DAYS_IN_LEAP_YEAR ) * year, 4 );

  nbdays += ( DIVC_BY_2( ( localtime->tm_mon ) * ( 30 + 31 ) ) -
        ( ( ( correctionMonth[year % 4] >> ( ( localtime->tm_mon ) * 2 ) ) & 0x03 ) ) );

  nbdays += ( localtime->tm_mday - 1 );

  // Convert from days to seconds
  nbsecs = nbdays * TM_SECONDS_IN_1DAY;

  nbsecs += ( ( uint32_t )localtime->tm_sec +
        ( ( uint32_t )localtime->tm_min * TM_SECONDS_IN_1MINUTE ) +
        ( ( uint32_t )localtime->tm_hour * TM_SECONDS_IN_1HOUR ) );
  return nbsecs - CALC_REF_YEAR_TO_UNIX_REF_YEAR_COMPENSATION_IN_SECONDS;
}

void get_localtime_from_timestamp( const uint32_t timestamp, struct tm *localtime )
{
  uint32_t correctionMonth[4] =
  {
    DAYS_IN_MONTH_CORRECTION_LEAP,
    DAYS_IN_MONTH_CORRECTION_NORM,
    DAYS_IN_MONTH_CORRECTION_NORM,
    DAYS_IN_MONTH_CORRECTION_NORM
  };
  uint32_t weekDays = 1; // Monday 1st January 1968
  uint32_t seconds;
  uint32_t minutes;
  uint32_t days;
  uint32_t divOut;
  uint32_t divReminder;

  CalendarDiv86400( timestamp + CALC_REF_YEAR_TO_UNIX_REF_YEAR_COMPENSATION_IN_SECONDS, &days, &seconds );

  // Calculates seconds
  CalendarDiv60( seconds, &minutes, &divReminder );
  localtime->tm_sec = ( uint8_t )divReminder;

  // Calculates minutes and hours
  CalendarDiv60( minutes, &divOut, &divReminder);
  localtime->tm_min = ( uint8_t )divReminder;
  localtime->tm_hour = ( uint8_t )divOut;

  // Calculates year
  localtime->tm_year = DIV_365_25( days );
  days-= DIVC_BY_4( ( TM_DAYS_IN_YEAR * 3 + TM_DAYS_IN_LEAP_YEAR ) * localtime->tm_year );

  localtime->tm_yday = days;

  // Calculates month
  localtime->tm_mon = CalendarGetMonth( days, localtime->tm_year );

  // calculates weekdays
  weekDays += DIVC_BY_4( ( localtime->tm_year * 5 ) );
  weekDays += days;
  localtime->tm_wday = MODULO_7( weekDays );

  days -= ( DIVC_BY_2( ( localtime->tm_mon ) * ( 30 + 31 ) ) -
        ( ( ( correctionMonth[localtime->tm_year % 4] >> ( ( localtime->tm_mon ) * 2 ) ) & 0x03 ) ) );

  // Convert 0 to 1 indexed.
  localtime->tm_mday = days + 1;

  localtime->tm_year += CALC_REF_YEAR;

  localtime->tm_isdst = -1;
}


uint32_t get_system_timestamp(){
	struct tm localtime;
	get_system_time(&localtime);
	return get_timestamp_from_time(&localtime);
}

void get_localtime_to_buffer(struct tm* localtime, uint8_t *buffer){
	uint8_t i = 0;
	buffer[i++] = localtime->tm_year % 100;
	buffer[i++] = localtime->tm_mon + 1;
	buffer[i++] = localtime->tm_mday;
	buffer[i++] = localtime->tm_hour;
	buffer[i++] = localtime->tm_min;
	buffer[i++] = localtime->tm_sec;
}

void date_time_to_buffer(uint8_t *buffer) {
	struct tm localtime;
	get_system_time(&localtime);
	get_localtime_to_buffer(&localtime, buffer);
}

void timestamp_to_buffer(uint32_t timestamp, uint8_t *buffer){
	struct tm localtime;
	get_localtime_from_timestamp(timestamp, &localtime);
	get_localtime_to_buffer(&localtime, buffer);
}

uint32_t timestamp_from_buffer(uint8_t *buffer){
	uint32_t timestamp = 0;
	struct tm localtime;
	if(date_time_from_buffer(buffer, &localtime)){
		timestamp = get_timestamp_from_time(&localtime);
	}
	return timestamp;
}

uint8_t date_time_from_buffer(uint8_t *buffer, struct tm *localtime) {
	uint8_t result = false;
	if (validate_date(buffer, false) && validate_time(&buffer[3], false)) {
		uint8_t ix = 0;
		result = true;
		memset(localtime, 0x0, sizeof(struct tm));
		localtime->tm_isdst = -1;
		localtime->tm_year = (APP_START_YEAR - RTC_BASE_YEAR) + buffer[ix++];
		localtime->tm_mon = buffer[ix++] - 1;
		localtime->tm_mday = buffer[ix++];
		localtime->tm_hour = buffer[ix++];
		localtime->tm_min = buffer[ix++];
		localtime->tm_sec = buffer[ix++];
	}

	return result;
}

uint8_t set_date_time(uint8_t year, uint8_t month, uint8_t date, uint8_t hour,
		uint8_t min, uint8_t sec) {
	uint8_t result = false;
	RTC_DATE_TIME date_time;
	struct tm localtime;
	uint8_t ix = 0;
	date_time[ix++] = year % 100;
	date_time[ix++] = month;
	date_time[ix++] = date;
	date_time[ix++] = hour;
	date_time[ix++] = min;
	date_time[ix++] = sec;
	if (date_time_from_buffer(date_time, &localtime)) {
		result = true;
		set_system_time(&localtime);
		HAL_Delay(1000);
		memset(&localtime, 0, sizeof(localtime));
		get_system_time(&localtime);
	}
	return result;
}

void set_system_timestamp(uint32_t timestamp){
	struct tm localtime;
	get_localtime_from_timestamp(timestamp, &localtime);
	set_system_time(&localtime);
}

uint32_t getNextCommunicationPeriod(RTC_TIME sync_time, uint32_t sync_interval) {
	struct tm localtime;
	get_system_time(&localtime);
	uint32_t current_timestamp = get_timestamp_from_time(&localtime);

	localtime.tm_hour = sync_time[TIME_HOUR_INDEX];
	localtime.tm_min = sync_time[TIME_MINUTE_INDEX];
	localtime.tm_sec = sync_time[TIME_SECOND_INDEX];
	uint32_t sync_timestamp = get_timestamp_from_time(&localtime);


	if(sync_interval > 0){
		while (sync_timestamp > current_timestamp) {
			sync_timestamp -= sync_interval;
		}
		while (sync_timestamp <= current_timestamp) {
			sync_timestamp += sync_interval;
		}
	}

	uint32_t diffrence_in_timestamp = get_abs_diffrence(sync_timestamp, current_timestamp);
	if(diffrence_in_timestamp >= sync_interval){
		diffrence_in_timestamp = sync_interval;
	}

	return diffrence_in_timestamp; //get_abs_diffrence(sync_timestamp, current_timestamp);
}
