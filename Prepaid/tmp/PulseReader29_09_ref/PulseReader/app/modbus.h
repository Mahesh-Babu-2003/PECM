#ifndef LIB_MODBUS_H_
#define LIB_MODBUS_H_

#include "application.h"


#define BROADCAST_MODBUS_ADDRESS	255
#define WINDOWS_QT      0
#define MB_STATE_DEBUG  0
#if (WINDOWS_QT == 1)
#include <QDateTime>
#include <QDebug>
#include <QByteArray>
#endif

#define SWAP_UINT16(x) (((x) >> 8) | ((x) << 8))
#define SWAP_UINT32(x) (((x) >> 24) | (((x) & 0x00FF0000) >> 8) | (((x) & 0x0000FF00) << 8) | ((x) << 24))

#define BYTE_TO_BYTE_MAX_INTERVAL			50
#define MB_REG_VALUE_BYTE_COUNT             2
typedef enum{
	MB_READ_HOLDING_REGISTER = 0x03, MB_READ_INPUT_REGISTER = 0x04,
	MB_WRITE_SINGLE_REGISTER = 0x06, MB_WRITE_MULTIPLE_REGISTER = 0x10,
}MB_FUNCION_CODE;

typedef enum{
	MB_ILLEGAL_Function = 0x01, MB_ILLEGAL_DATA_ADDRESS, MB_ILLEGAL_DATA_VALUE,
	MB_SLAVE_DEVICE_FAILURE, MB_ACKNOWLEDGE
}MB_ERROR_CODES;

#define SLAVE_ADDRESS_IX					0
#define FUNCTION_CODE_IX					1
#define START_ADDRESS_IX					2
#define REG_QTY_IX							4
#define WRITE_SINGLE_DATA_IX				4
#define WRITE_MULTIPLE_DATA_COUNT_IX		6
#define WRITE_MULTIPLE_DATA_IX				7

#define MB_MINIMUM_PKT_SIZE			4 //SLAVE_ADDRESS + FC + START _ADDRESS(2)
typedef enum{
	MB_SLAVE_ADDRESS = 0,
	MB_FUNCTION_CODE,
	MB_START_ADDRESS,
	MB_REG_QUANTITY_COUNT, //for read and multiple write
	MB_BYTE_COUNT, //for multiple write
	MB_WRITE_DATA,

	MB_CRC_CHECK
}ModbusPktState;
#define MB_PKT_MAX_SIZE		128
typedef struct{
	uint8_t address;
	uint8_t pktBuffer[MB_PKT_MAX_SIZE];
	uint8_t pktSize;
	ModbusPktState pktState;
	uint64_t last_byte_received_time;
	uint8_t isPaketProcessingPending;

	uint8_t function_code;
	union{
		struct{
			uint8_t reg_qty_byte_received:2;
			uint8_t crc_byte_received:2;
		};
		uint8_t length_bytes;
	};
	uint8_t expected_byte_count;

}MB_Packet;

extern MB_Packet mbPkt;

uint8_t isFunction_codeValid(uint8_t function_code);

void initialize_modbus(uint8_t address);
void reset_modbus_state();

void process_modbus_byte(uint8_t byte);
void process_modbus_request();

void process_write_single_register(uint16_t* start_address, uint8_t* buffer);
void process_write_multiple_register(uint16_t* start_address, uint16_t* reg_qty,
		uint8_t byte_count, uint8_t* buffer );

void process_read_holding_register(uint16_t* start_address, uint16_t* reg_qty);
void process_read_input_register(uint16_t* start_address, uint16_t* reg_qty);

void mbSendErrorCode(MB_ERROR_CODES error_code);

void mbSendWriteResponse(uint8_t fcode, uint16_t* start_address_ptr, uint16_t* reg_qty_or_data_ptr);
void mbSendReadResponse(uint8_t fcode, uint8_t* buffer, uint8_t byteCount);

void mbSendBytes(uint8_t* buffer, uint8_t count);

#endif /* LIB_MODBUS_H_ */
