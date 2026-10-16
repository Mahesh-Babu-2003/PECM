#include "application.h"
#include "modbus.h"

#define MB_REGISTER_START_ADDRESS    00
#define NUMBER_OF_REGISTERS         (REGISTER_SIZE/MB_REG_VALUE_BYTE_COUNT)

uint8_t isValidRegisterRange(uint16_t address, uint16_t reg_qty){
	return (address >= MB_REGISTER_START_ADDRESS) && ( reg_qty > 0 && reg_qty <= NUMBER_OF_REGISTERS );
}


void process_write_single_register(uint16_t* start_address, uint8_t* buffer ){
	(void)*start_address;
	(void)*buffer;
	if(isValidRegisterRange(*start_address, 1)){

		uint32_t register_index = (*start_address - MB_REGISTER_START_ADDRESS) * MB_REG_VALUE_BYTE_COUNT;
		uint8_t register_buffer[256] = { 0 };

		memcpy_reverse(register_buffer, &state_obj.registers, sizeof(APP_REGISTERS));
		memcpy(&register_buffer[register_index], buffer, MB_REG_VALUE_BYTE_COUNT);
		memcpy_reverse( &state_obj.registers, register_buffer, sizeof(APP_REGISTERS));

		checkCommunicationConfiguration();
		mbSendWriteResponse(MB_WRITE_SINGLE_REGISTER, start_address, (uint16_t*)buffer);
		save_configuration();
	}else{
		mbSendErrorCode(MB_ILLEGAL_DATA_ADDRESS);
	}
}

void process_write_multiple_register(uint16_t* start_address, uint16_t* reg_qty,
		uint8_t byte_count, uint8_t* buffer ){
	(void)*start_address;
	(void)byte_count;
	(void)*reg_qty;
	(void)*buffer;

	if(isValidRegisterRange(*start_address, *reg_qty)){
		uint32_t register_index = (*start_address - MB_REGISTER_START_ADDRESS) * MB_REG_VALUE_BYTE_COUNT;
		uint8_t register_buffer[256] = { 0 };

		memcpy_reverse(register_buffer, &state_obj.registers, sizeof(APP_REGISTERS));
		memcpy(&register_buffer[register_index], buffer, (*reg_qty * MB_REG_VALUE_BYTE_COUNT));
		memcpy_reverse( &state_obj.registers, register_buffer, sizeof(APP_REGISTERS));


		checkCommunicationConfiguration();
		mbSendWriteResponse(MB_WRITE_MULTIPLE_REGISTER, start_address, reg_qty);
		save_configuration();

	}else{
		mbSendErrorCode(MB_ILLEGAL_DATA_ADDRESS);
	}
}

void process_read_registers(uint8_t fcode, uint16_t* start_address, uint16_t* reg_qty){
	(void)*start_address;
	(void)*reg_qty;
	if(isValidRegisterRange(*start_address, *reg_qty)){
		volatile uint32_t register_index = (*start_address - MB_REGISTER_START_ADDRESS) * MB_REG_VALUE_BYTE_COUNT;
		uint8_t register_buffer[256] = { 0 };
		ReadBatteryVoltage();
		memcpy_reverse(register_buffer, (uint8_t*)&state_obj.registers, sizeof(APP_REGISTERS));
		mbSendReadResponse(fcode, (uint8_t*)&register_buffer[register_index], (*reg_qty * MB_REG_VALUE_BYTE_COUNT));
	}else{
		mbSendErrorCode(MB_ILLEGAL_DATA_ADDRESS);
	}
}

void process_read_input_register(uint16_t* start_address, uint16_t* reg_qty){
	process_read_registers(MB_READ_INPUT_REGISTER, start_address, reg_qty);
}

void process_read_holding_register(uint16_t* start_address, uint16_t* reg_qty){
	process_read_registers(MB_READ_HOLDING_REGISTER, start_address, reg_qty);
}
