/*
 * common.h
 *
 *  Created on: Jan 25, 2026
 *      Author: aj
 */

#ifndef COMMON_H_
#define COMMON_H_
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include "stdlib.h"
#include "math.h"

#include "main.h"
#include "usart.h"
#include "i2c.h"
#include "rtc.h"
#include "adc.h"

#define TIME_T	uint64_t
#define RTC_DRIFT_ALLOWED_TIME		60UL //drift allowed time
#define APP_START_YEAR				2000
#define RTC_BASE_YEAR				1900

extern ADC_HandleTypeDef hadc1;

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

#define RTC_DATE_TIME_LENGTH            6      //YY/MM/DD HH:MM:SS
#define RTC_DATE_LENGTH                 3      //YY/MM/DD
#define RTC_TIME_LENGTH                 3      //HH:MM:SS

typedef uint8_t  RTC_DATE_TIME[RTC_DATE_TIME_LENGTH];
typedef uint8_t  RTC_DATE[RTC_DATE_LENGTH];
typedef uint8_t  RTC_TIME[RTC_TIME_LENGTH];

typedef enum{

  DATE_TIME_YEAR_INDEX = 0, DATE_TIME_MONTH_INDEX = 1, DATE_TIME_DATE_INDEX = 2,
  DATE_TIME_HOUR_INDEX = 3, DATE_TIME_MINUTE_INDEX = 4, DATE_TIME_SECOND_INDEX = 5,

  DATE_YEAR_INDEX = 0, DATE_MONTH_INDEX = 1, DATE_DATE_INDEX = 2, DATE_DAY_INDEX = 3,

  TIME_HOUR_INDEX = 0, TIME_MINUTE_INDEX = 1, TIME_SECOND_INDEX = 2, TIME_MILLI_SECOND_INDEX = 3,
}DATE_TIME_INDEX;

#define EEPROM_I2C					hi2c2
#define OLED_I2C					hi2c1
#define MODBUS_UART					huart1
#define BATTERY_ADC					hadc1

#define SIZE_OF_CRC					2


time_t get_system_time(struct tm* localtime);
void set_system_time(struct tm*  localtime);

TIME_T get_timestamp_in_msec();
void enable_interrupts();
void disable_interrupts();
uint8_t WriteByteStreamTo_UART(uint8_t* buffer, uint16_t count);
void SystemClock_Decrease(void);
void check_uart_receiver_errors(UART_HandleTypeDef *huart, uint8_t* rx_byte);
float Read_ADC_Voltage(void);

uint32_t getTimestamp(int year, int month, int date, int hour, int min, int sec);
bool validate_date_time(uint8_t* buffer, bool is_reverse);
bool validate_date(uint8_t* buffer, bool is_reverse);
bool validate_time(uint8_t* buffer, bool is_reverse);

void memcpy_reverse(void* dest, const void* src, uint16_t length);
void fill_crc(uint8_t *data, size_t length);
uint16_t ModBus_CRC16( uint8_t *aucData, uint16_t usInputLength);
uint16_t UpdateModbus_CRC16(uint16_t  usCRC16,  uint8_t *aucData, uint16_t usInputLength);
void calculate_and_fill_modbus_crc(unsigned char * Buffer, unsigned short Length);
uint16_t crc16_ccitt(const uint8_t *data, size_t length);
uint32_t get_time_diffrence(TIME_T min_val, TIME_T max_val);
uint32_t get_abs_diffrence(uint32_t min_val, uint32_t max_val);

#endif /* COMMON_H_ */
