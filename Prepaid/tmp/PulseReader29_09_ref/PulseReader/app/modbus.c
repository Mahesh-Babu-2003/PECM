#include 	"modbus.h"

#define RS485_WRITE_EN_OP_ON		HAL_GPIO_WritePin(RS485_EN_GPIO_Port, RS485_EN_Pin, GPIO_PIN_SET)
#define RS485_WRITE_EN_OP_OFF		HAL_GPIO_WritePin(RS485_EN_GPIO_Port, RS485_EN_Pin, GPIO_PIN_RESET)
MB_Packet mbPkt;


void reset_modbus_state(){
	memset(mbPkt.pktBuffer, 0, sizeof(mbPkt.pktBuffer));
	mbPkt.pktSize = 0;
	mbPkt.pktState = MB_SLAVE_ADDRESS;
	mbPkt.crc_byte_received = 0;
	mbPkt.length_bytes = 0;
	mbPkt.function_code = 0;
	mbPkt.expected_byte_count = 0;
	mbPkt.last_byte_received_time = get_timestamp_in_msec();
	mbPkt.isPaketProcessingPending = false;
}

void initialize_modbus(uint8_t address){
	mbPkt.address = address;
	reset_modbus_state();
}

uint8_t isFunction_codeValid(uint8_t function_code){
	return (function_code >= MB_READ_HOLDING_REGISTER && function_code <= MB_WRITE_SINGLE_REGISTER)
			|| (function_code == MB_WRITE_MULTIPLE_REGISTER);
}

void mbSendBytes(uint8_t* buffer, uint8_t count){
	(void)buffer;
	(void)count;

	RS485_WRITE_EN_OP_ON;
	application_delay(1);
	WriteByteStreamTo_UART(buffer, count);
	application_delay(1);
	RS485_WRITE_EN_OP_OFF;
}

void mbSendReadResponse(uint8_t fcode, uint8_t* buffer, uint8_t byteCount){
	uint8_t header_bytes[3] = {mbPkt.address, fcode, byteCount};
	uint16_t crc = ModBus_CRC16(header_bytes, sizeof(header_bytes));
	crc = UpdateModbus_CRC16(crc, buffer, byteCount);

	mbSendBytes(header_bytes, sizeof(header_bytes));
	mbSendBytes(buffer, byteCount);
	mbSendBytes((uint8_t*)&crc, SIZE_OF_CRC);
}

void mbSendWriteResponse(uint8_t fcode, uint16_t* start_address_ptr, uint16_t* reg_qty_or_data_ptr){
	uint16_t start_address; //= SWAP_UINT16(*start_address_ptr);
	uint16_t reg_qty_or_data; //= SWAP_UINT16(*reg_qty_or_data_ptr);

	memcpy_reverse(&start_address, start_address_ptr, sizeof(uint16_t));
	memcpy_reverse(&reg_qty_or_data, reg_qty_or_data_ptr, sizeof(uint16_t));

	uint16_t crc = ModBus_CRC16(&mbPkt.address, 1);
	crc = UpdateModbus_CRC16(crc, &fcode, 1);
	crc = UpdateModbus_CRC16(crc, (uint8_t*)&start_address, sizeof(uint16_t));
	crc = UpdateModbus_CRC16(crc, (uint8_t*)&reg_qty_or_data, sizeof(uint16_t));

	mbSendBytes(&mbPkt.address, 1);
	mbSendBytes(&fcode, 1);
	mbSendBytes((uint8_t*)&start_address, sizeof(uint16_t));
	mbSendBytes((uint8_t*)&reg_qty_or_data, sizeof(uint16_t));
	mbSendBytes((uint8_t*)&crc, SIZE_OF_CRC);
}

void mbSendErrorCode(MB_ERROR_CODES error_code){

	uint8_t error_response_pkt[5], ix = 0;
	error_response_pkt[ix++] = mbPkt.address;
	error_response_pkt[ix++] = mbPkt.function_code | 0x80;
	error_response_pkt[ix++] = error_code;
	calculate_and_fill_modbus_crc(error_response_pkt, ix+SIZE_OF_CRC);
	ix += SIZE_OF_CRC;
	mbSendBytes(error_response_pkt, ix);

}

void process_modbus_byte(uint8_t byte){

	if(mbPkt.isPaketProcessingPending == false && state_obj.registers.slave_address  > 0){
		if(get_abs_diffrence(mbPkt.last_byte_received_time, get_timestamp_in_msec()) >= BYTE_TO_BYTE_MAX_INTERVAL
				|| (mbPkt.pktSize >= MB_PKT_MAX_SIZE )){
			reset_modbus_state();
		}else{
			mbPkt.last_byte_received_time = get_timestamp_in_msec();
		}

		switch (mbPkt.pktState) {
		case MB_SLAVE_ADDRESS:
			if (state_obj.registers.slave_address == byte) { //|| BROADCAST_MODBUS_ADDRESS == byte
				mbPkt.address = state_obj.registers.slave_address;
				reset_modbus_state();
				mbPkt.pktBuffer[mbPkt.pktSize++] = byte;
				mbPkt.pktState = MB_FUNCTION_CODE;
			}
			break;
		case MB_FUNCTION_CODE:
			if (isFunction_codeValid(byte)) {
				mbPkt.function_code = byte;
				mbPkt.pktBuffer[mbPkt.pktSize++] = byte;
				mbPkt.pktState = MB_START_ADDRESS;
			} else {
				reset_modbus_state();
			}
			break;
		case MB_START_ADDRESS:
			mbPkt.pktBuffer[mbPkt.pktSize++] = byte;
			if (mbPkt.pktSize >= MB_MINIMUM_PKT_SIZE) {
				switch (mbPkt.function_code) {
				case MB_READ_HOLDING_REGISTER:
				case MB_READ_INPUT_REGISTER:
				case MB_WRITE_MULTIPLE_REGISTER:
					mbPkt.pktState = MB_REG_QUANTITY_COUNT;
					break;
				case MB_WRITE_SINGLE_REGISTER:
					mbPkt.pktState = MB_WRITE_DATA;
					mbPkt.expected_byte_count = MB_REG_VALUE_BYTE_COUNT;
					break;
				default:
					reset_modbus_state();
					break;
				}
			}
			break;
		case MB_REG_QUANTITY_COUNT:
			mbPkt.pktBuffer[mbPkt.pktSize++] = byte;
			mbPkt.reg_qty_byte_received++;
			if (mbPkt.reg_qty_byte_received >= 2) {
				switch (mbPkt.function_code) {
				case MB_READ_HOLDING_REGISTER:
				case MB_READ_INPUT_REGISTER:
					mbPkt.pktState = MB_CRC_CHECK;
					break;
				case MB_WRITE_MULTIPLE_REGISTER:
					mbPkt.pktState = MB_BYTE_COUNT;
					break;
				default:
					reset_modbus_state();
					break;
				}
			}
			break;
		case MB_BYTE_COUNT:
			mbPkt.pktBuffer[mbPkt.pktSize++] = byte;
			mbPkt.expected_byte_count = byte;
			mbPkt.pktState = MB_WRITE_DATA;
			break;
		case MB_WRITE_DATA:
			if (mbPkt.expected_byte_count > 0) {
				--mbPkt.expected_byte_count;
				mbPkt.pktBuffer[mbPkt.pktSize++] = byte;
			}
			if (mbPkt.expected_byte_count == 0) {
				mbPkt.pktState = MB_CRC_CHECK;
			}
			break;
		case MB_CRC_CHECK:
			mbPkt.pktBuffer[mbPkt.pktSize++] = byte;
			mbPkt.crc_byte_received++;
			if (mbPkt.crc_byte_received >= SIZE_OF_CRC) {
				if (ModBus_CRC16(mbPkt.pktBuffer, mbPkt.pktSize) == 0) {
					//					process_modbus_request();
					mbPkt.isPaketProcessingPending = true;
				}else{
					reset_modbus_state();
				}
			}
			break;
		default:
			reset_modbus_state();
			break;
		}
	}
}

void process_modbus_request(){
	uint16_t start_address;
	uint16_t reg_qty;

	memcpy_reverse(&start_address, &mbPkt.pktBuffer[START_ADDRESS_IX], sizeof(uint16_t));
	memcpy_reverse(&reg_qty, &mbPkt.pktBuffer[REG_QTY_IX], sizeof(uint16_t));

	switch(mbPkt.function_code){
	case MB_READ_HOLDING_REGISTER:
		process_read_holding_register(&start_address, &reg_qty);
		break;
	case MB_READ_INPUT_REGISTER:
		process_read_input_register(&start_address, &reg_qty);
		break;
	case MB_WRITE_MULTIPLE_REGISTER:
		process_write_multiple_register(&start_address, &reg_qty,
				mbPkt.pktBuffer[WRITE_MULTIPLE_DATA_COUNT_IX],
				&mbPkt.pktBuffer[WRITE_MULTIPLE_DATA_IX]);
		break;
	case MB_WRITE_SINGLE_REGISTER:
		process_write_single_register(&start_address,
				&mbPkt.pktBuffer[WRITE_SINGLE_DATA_IX]);
		break;
	default:
		mbSendErrorCode(MB_ILLEGAL_Function);
		break;
	}
}
