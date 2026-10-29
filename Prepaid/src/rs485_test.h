#ifndef RS485_TEST_H
#define RS485_TEST_H

#include "ms51_device.h"

#define ENERGY_METER_REGISTER_COUNT 50
#define SLAVE_HOLDING_REGISTER_COUNT 50
#define SLAVE_RELAY_COUNT 3
#define SLAVE_RELAY_COMMAND_OFFSET 0

/* Zero-based offsets; 40001 controls all three relays as one command. */
#define SLAVE_PORT1_BAUD_OFFSET      1
#define SLAVE_PORT1_PARITY_OFFSET    2
#define SLAVE_PORT1_ID_OFFSET        3
#define SLAVE_PORT2_BAUD_OFFSET      4
#define SLAVE_PORT2_PARITY_OFFSET    5
#define SLAVE_PORT2_ID_OFFSET        6

/* Relay command is 40001; physical states stay internal, meter data at 40201+. */

/* Company name occupies 40008..40012; balance is at 40013. */
#define COMPANY_NAME_FIRST_OFFSET    7
#define COMPANY_NAME_REGISTER_COUNT 5
/* 40013 is the signed calculated balance; 40044 accepts recharge writes. */
#define BALANCE_OFFSET              12
#define LAST_RECHARGE_OFFSET       42
#define RECHARGE_INPUT_OFFSET      43
#define TOTAL_DEDUCTED_OFFSET      44
#define RTC_SYNC_OFFSET             45
#define MAIN_KWH_RATE_OFFSET        13
#define MAIN_KVAH_RATE_OFFSET       14
#define DG_KWH_RATE_OFFSET          15
#define DG_KVAH_RATE_OFFSET         16
#define SOLAR_KWH_RATE_OFFSET       17
#define SOLAR_KVAH_RATE_OFFSET      18
#define LOW_BALANCE_THRESHOLD_OFFSET 19
#define LOW_BALANCE_ALERT_OFFSET    20
#define BACKLIGHT_TIMEOUT_OFFSET    21
#define MAIN_KWH_CONSUMPTION_OFFSET 22
#define MAIN_KVAH_CONSUMPTION_OFFSET 23
#define DG_KWH_CONSUMPTION_OFFSET   24
#define DG_KVAH_CONSUMPTION_OFFSET  25
#define SOLAR_KWH_CONSUMPTION_OFFSET 26
#define SOLAR_KVAH_CONSUMPTION_OFFSET 27
#define MAIN_CONSUMPTION_OFFSET     MAIN_KWH_CONSUMPTION_OFFSET
#define DG_CONSUMPTION_OFFSET       DG_KWH_CONSUMPTION_OFFSET
#define SOLAR_CONSUMPTION_OFFSET   SOLAR_KWH_CONSUMPTION_OFFSET
#define TAMPER_EVENT_OFFSET         28
#define BILLING_PARAMETER_OFFSET    29
#define RELAY_DISCONNECT_STATUS_OFFSET 30
#define LOAD_LIMIT_OFFSET           31
#define TIME_LIMIT_OFFSET           32
#define DISCONNECT_MODE_OFFSET      33
#define BMS_RELAY_OVERRIDE_OFFSET   34
#define CONSUMPTION_AFTER_DISCONNECT_OFFSET 35
#define RTC_LIVE_YEAR_OFFSET        36
#define RTC_LIVE_MONTH_OFFSET       37
#define RTC_LIVE_DAY_OFFSET         38
#define RTC_LIVE_HOUR_OFFSET        39
#define RTC_LIVE_MINUTE_OFFSET      40
#define RTC_LIVE_SECOND_OFFSET      41
#define RTC_RESERVED_FIRST_OFFSET   42

#define BILLING_MODE_KWH            0
#define BILLING_MODE_KVAH           1
#define BILLING_MODE_PF             2
#define BILLING_MODE_KW             3
#define BILLING_MODE_KVA            4
#define BILLING_MODE_KVARH          5

#define PREPAID_UI_EVENT_COMPANY    0x01
#define PREPAID_UI_EVENT_BALANCE    0x02
#define PREPAID_UI_EVENT_LOADS      0x04
#define PREPAID_UI_EVENT_RATES      0x08
#define PREPAID_UI_EVENT_ACTIVITY   0x10
#define PREPAID_UI_EVENT_TAMPER     0x20

/* Add these globals to Keil Watch 1 while the target is running. */
extern volatile uint8_t energy_meter_connected;
extern volatile uint8_t energy_meter_data_valid;
extern volatile uint16_t energy_meter_successful_responses;
extern volatile uint8_t energy_meter_master_state;
extern volatile uint16_t energy_meter_poll_count;
extern volatile uint16_t energy_meter_request_count;
extern volatile uint8_t energy_meter_last_error;
extern volatile uint16_t energy_meter_timeout_count;
extern volatile uint16_t energy_meter_parity_error_count;
extern volatile uint16_t energy_meter_invalid_frame_count;
extern volatile uint8_t energy_meter_last_exception;
extern volatile uint16_t port1_rx_byte_count;
extern volatile uint16_t port1_parity_error_count;
extern volatile uint16_t port1_valid_request_count;
extern volatile uint16_t port1_response_count;
extern volatile uint16_t port1_bad_crc_count;
extern volatile uint16_t port1_wrong_address_count;
extern volatile uint8_t port1_last_frame_length;
extern volatile uint8_t xdata port1_last_frame[8];
extern volatile uint8_t meter_eeprom_present;
extern volatile uint8_t meter_eeprom_loaded;
extern volatile uint8_t meter_eeprom_last_error;
extern volatile uint16_t meter_eeprom_save_count;
extern volatile uint16_t xdata energy_meter_registers[ENERGY_METER_REGISTER_COUNT];
extern volatile uint16_t xdata s_energy_meter_registers[ENERGY_METER_REGISTER_COUNT];
extern volatile uint16_t xdata slave_holding_registers[SLAVE_HOLDING_REGISTER_COUNT];
extern volatile uint8_t slave_relay_states[SLAVE_RELAY_COUNT];
extern volatile uint8_t prepaid_ui_events;

/* energy_meter_last_error values */
#define ENERGY_METER_ERROR_NONE          0
#define ENERGY_METER_ERROR_TIMEOUT       1
#define ENERGY_METER_ERROR_PARITY        2
#define ENERGY_METER_ERROR_BAD_FRAME     3
#define ENERGY_METER_ERROR_EXCEPTION     4
#define ENERGY_METER_ERROR_BAD_RESPONSE  5

void RS485_TestInit(void);
void RS485_Poll(void);
void RS485_UpdateLiveRtcRegisters(void);
uint16_t RS485_GetBillingParameterValue(uint16_t offset);
uint8_t RS485_SetBillingParameter(uint16_t offset, uint16_t value);
void RS485_GetCommunicationSettings(uint8_t port, uint32_t *baud_rate,
                                    uint8_t *parity, uint8_t *slave_id);

#endif
