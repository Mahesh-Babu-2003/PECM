/*
 * time_interface.h
 *
 *  Created on: 20-Feb-2023
 *      Author: amarnath_jakkam
 */

#ifndef INTERFACE_TIME_INTERFACE_H_
#define INTERFACE_TIME_INTERFACE_H_
#include "main.h"
#include "common.h"
/*!
* @brief Days, Hours, Minutes and seconds of systime.h
*/
#define TM_DAYS_IN_LEAP_YEAR                        ( ( uint32_t )  366U )
#define TM_DAYS_IN_YEAR                             ( ( uint32_t )  365U )
#define TM_SECONDS_IN_1DAY                          ( ( uint32_t )86400U )
#define TM_SECONDS_IN_1HOUR                         ( ( uint32_t ) 3600U )
#define TM_SECONDS_IN_1MINUTE                       ( ( uint32_t )   60U )
#define TM_MINUTES_IN_1HOUR                         ( ( uint32_t )   60U )
#define TM_HOURS_IN_1DAY                            ( ( uint32_t )   24U )

/*!
* @brief Months of systime.h
*/
#define TM_MONTH_JANUARY                            ( ( uint8_t ) 0U )
#define TM_MONTH_FEBRUARY                           ( ( uint8_t ) 1U )
#define TM_MONTH_MARCH                              ( ( uint8_t ) 2U )
#define TM_MONTH_APRIL                              ( ( uint8_t ) 3U )
#define TM_MONTH_MAY                                ( ( uint8_t ) 4U )
#define TM_MONTH_JUNE                               ( ( uint8_t ) 5U )
#define TM_MONTH_JULY                               ( ( uint8_t ) 6U )
#define TM_MONTH_AUGUST                             ( ( uint8_t ) 7U )
#define TM_MONTH_SEPTEMBER                          ( ( uint8_t ) 8U )
#define TM_MONTH_OCTOBER                            ( ( uint8_t ) 9U )
#define TM_MONTH_NOVEMBER                           ( ( uint8_t )10U )
#define TM_MONTH_DECEMBER                           ( ( uint8_t )11U )

/*!
* @brief Week days of systime.h
*/
#define TM_WEEKDAY_SUNDAY                           ( ( uint8_t )0U )
#define TM_WEEKDAY_MONDAY                           ( ( uint8_t )1U )
#define TM_WEEKDAY_TUESDAY                          ( ( uint8_t )2U )
#define TM_WEEKDAY_WEDNESDAY                        ( ( uint8_t )3U )
#define TM_WEEKDAY_THURSDAY                         ( ( uint8_t )4U )
#define TM_WEEKDAY_FRIDAY                           ( ( uint8_t )5U )
#define TM_WEEKDAY_SATURDAY                         ( ( uint8_t )6U )

void get_localtime_from_timestamp( const uint32_t timestamp, struct tm *localtime );
uint32_t getNextCommunicationPeriod(RTC_TIME sync_time, uint32_t sync_interval);
void set_system_timestamp(uint32_t timestamp);
uint8_t set_date_time(uint8_t year, uint8_t month, uint8_t date, uint8_t hour, uint8_t min, uint8_t sec);
uint8_t date_time_from_buffer(uint8_t *buffer, struct tm *localtime) ;
uint32_t timestamp_from_buffer(uint8_t *buffer);
void timestamp_to_buffer(uint32_t timestamp, uint8_t *buffer);
void date_time_to_buffer(uint8_t *buffer);
uint32_t get_system_timestamp();
void get_localtime_to_buffer(struct tm* localtime, uint8_t *buffer);

#endif /* INTERFACE_TIME_INTERFACE_H_ */
