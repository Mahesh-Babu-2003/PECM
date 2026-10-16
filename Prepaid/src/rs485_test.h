#ifndef RS485_TEST_H
#define RS485_TEST_H

#include "ms51_device.h"

#define ENERGY_METER_REGISTER_COUNT 4
#define SLAVE_HOLDING_REGISTER_COUNT 50
#define SLAVE_RELAY_COUNT 3

/* Zero-based offsets in the 40001..40050 slave holding-register table. */
#define SLAVE_PORT1_BAUD_HIGH_OFFSET 7
#define SLAVE_PORT1_BAUD_LOW_OFFSET  8
#define SLAVE_PORT1_PARITY_OFFSET    9
#define SLAVE_PORT1_ID_OFFSET       10
#define SLAVE_PORT2_BAUD_HIGH_OFFSET 11
#define SLAVE_PORT2_BAUD_LOW_OFFSET  12
#define SLAVE_PORT2_PARITY_OFFSET    13
#define SLAVE_PORT2_ID_OFFSET       14

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

/* energy_meter_last_error values */
#define ENERGY_METER_ERROR_NONE          0
#define ENERGY_METER_ERROR_TIMEOUT       1
#define ENERGY_METER_ERROR_PARITY        2
#define ENERGY_METER_ERROR_BAD_FRAME     3
#define ENERGY_METER_ERROR_EXCEPTION     4
#define ENERGY_METER_ERROR_BAD_RESPONSE  5

void RS485_TestInit(void);
void RS485_Poll(void);

#endif
