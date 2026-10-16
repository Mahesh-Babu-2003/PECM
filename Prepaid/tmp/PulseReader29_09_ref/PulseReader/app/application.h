/*
 * application.h
 *
 *  Created on: Jan 25, 2026
 *      Author: aj
 */

#ifndef APPLICATION_H_
#define APPLICATION_H_
#include "common.h"

#include "oled.h"

#define DEFAULT_FONT	Font_7x10
#define SMALL_FONT		Font_6x8
#define MEDIUM_FONT		DEFAULT_FONT


#define EEPROM_MEMORY_SIZE			256
#define EACH_REGISTER_SIZE_WITH_CRC	8

#define EEPROM_BASE_ADDRESS			0
#define EEPROM_BACKUP_ADDRESS		(EEPROM_MEMORY_SIZE/2)
#define EEPROM_WRITE_ATTEMPTS		2

#define APPLICATION_LOOP_DELAY		50

#define PULSE_BLINK_TIME		50
#define PULSE_MINIMUM_TIME		150
#define DEFAULT_SLAVE_ADDR		1
#define DEFAULT_BAUD_RATE		9600
#define DEFAULT_PARITY			0
#define DEFAULT_DISPLAY_TIME	5000 //in msec

#define ADC_MAX     4095.0f
#define VREF        3.3f

//#define DIVIDER_FACTOR  0.6875f          // 2.2M / (1M + 2.2M)
#define DIVIDER_FACTOR  0.6667f          // 2M / (1M + 2M)
#define REVERSE_FACTOR  (1.0f / DIVIDER_FACTOR)

#define BAT_FULL    3.65f      // Tekcell full
#define BAT_EMPTY   2.00f      // Tekcell empty

#define BAT_LOW_PERCENT     	20U

#define PULASE_SAVE_INTERVAL	5 //in minutes

#define DEVICE_ID_LENGTH		12

typedef enum {
    BAT_STATUS_OK = 0,
    BAT_STATUS_LOW,
    BAT_STATUS_EMPTY
} BatteryStatus_t;


typedef enum{
	OLED_OFF = 0, OLED_COMPANY_INFO, OLED_CONFIGURATION_INFO, OLED_PULSE_INFO, MAX_OLED_SCREENS
}OLED_STATE;

typedef enum{
	PULSE_CH1 = 0, PULSE_CH2, PULSE_CH3, MAX_PULSE
}PULSE_CHANNEL;

typedef enum{
	EEPROM_BAUDRATE_ADDR = 0,
	EEPROM_PARITY_ADDR = 8,
	EEPROM_DISPLAY_TIME_ADDR = 16,
	EEPROM_SLAVE_ADDR = 24,
	EEPROM_DEVICE_ID_ADDR = 32,
	EEPROM_PULSE_ADDR = 48,
	EEPROM_FACTOR_ADDR = 72,
	EEPROM_NO_OF_EXT_POWER_ON_OFF = 96,
	EEPROM_NO_OF_AMR_ON_OFF = 104,
}EEPROM_REGISTER;

typedef struct{
	GPIO_TypeDef* gpio;
	uint32_t pin;
}GPIO_Pins;

typedef struct __attribute__((__packed__)){
	uint8_t device_id[DEVICE_ID_LENGTH];
	uint32_t slave_address;
	uint32_t display_time;
	float amr_on_off;
	float ext_power_on_off;
	float bat_status;
	float bat_p;
	float parity;
	float baud_rate;
	float bat_v;
	float factor[MAX_PULSE];
	float reading[MAX_PULSE];
}APP_REGISTERS;

#define REGISTER_SIZE	sizeof(APP_REGISTERS)

typedef struct {
	APP_REGISTERS registers;
	uint32_t pending_pulses[MAX_PULSE];

	uint8_t uart1_rx_byte;
	uint8_t oled_state, oled_prev_state;
	uint8_t oled_channel, oled_prev_channel;
	uint8_t oled_force_update;
	uint8_t last_pulse_state[MAX_PULSE];
	TIME_T last_pulse_timestamp[MAX_PULSE];
	TIME_T last_saved_timestamp;

	uint8_t isConfiguratnoSavePending;

	TIME_T power_pulse_timestamp;
	TIME_T display_pb_timestamp;
	uint8_t externalPowerStatus;
	uint8_t lastPowerPinStatus;
	TIME_T oled_display_update_at;
	TIME_T green_oled_update_at;

	uint32_t displayUpdateTime;

	uint8_t is_low_power_enabled;
}APP_State;

extern APP_State state_obj;
extern GPIO_Pins pulsePins[MAX_PULSE];

void SystemClock_Config(void);

void initialize_application();
void process_application();

void UpdateUart_Baudrate(uint32_t baudrate, uint8_t parity);

void application_delay(uint32_t delay);

void checkCommunicationConfiguration();

void UpdatePageDisplay();
uint32_t getPulseSaveInterval();
void CapturePulse(uint8_t pulse_channel, uint8_t pulse_state);

uint8_t Read_DIP_Address(void);
void ReadBatteryVoltage();
uint8_t Battery_Percent(float vbat);
BatteryStatus_t Get_Battery_Status(float vbat, uint8_t percent);

uint8_t save_eepeom_register(uint32_t register_address, uint8_t* bytes, const uint16_t count);
uint8_t load_eepeom_register(EEPROM_REGISTER register_address, uint8_t* value, const uint16_t count);

void load_configuration();
void save_configuration();
#endif /* APPLICATION_H_ */
