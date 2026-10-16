#include "application.h"
#include "modbus.h"

APP_State state_obj;
const char DEFAULT_DEVICE_ID[DEVICE_ID_LENGTH + 1] = { "PCM3C485V0\0"};
const char PARITY_STRING[3][5] = {
		"None\0", "Even\0", "Odd\0"
};

GPIO_Pins pulsePins[MAX_PULSE] = {
		{PULSE0_INT_GPIO_Port, PULSE0_INT_Pin},
		{PULSE1_INT_GPIO_Port, PULSE1_INT_Pin},
		{PULSE2_INT_GPIO_Port, PULSE2_INT_Pin}
};
#define LED_ON_STATE		GPIO_PIN_RESET
#define LED_OFF_STATE		GPIO_PIN_SET
#define GREEN_LED_STATE(state)		HAL_GPIO_ReadPin(LED2_GPIO_Port, LED2_Pin) == state
#define RED_LED_STATE(state)		HAL_GPIO_ReadPin(LED1_GPIO_Port, LED1_Pin) == state
#define GREEN_LED(state)	HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, state)
#define RED_LED(state)		HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, state)

void application_delay(uint32_t delay){
	TIME_T start_tick = get_timestamp_in_msec();
	while(get_time_diffrence(start_tick, get_timestamp_in_msec()) < delay);
}
uint8_t is_oled_display_timeout(){
	return get_time_diffrence(state_obj.oled_display_update_at, get_timestamp_in_msec()) > state_obj.registers.display_time;
}
uint32_t getPulseSaveInterval(){
	return PULASE_SAVE_INTERVAL * 60UL * 1000UL;
}

void checkCommunicationConfiguration(){

#define SUPPORTED_BAUD_RATES_COUNT	8
	uint32_t supported_baudrates[SUPPORTED_BAUD_RATES_COUNT] = {
			4800, 9600, 14400, 19200, 38400, 56000, 57600, 115200
	};

	uint32_t requested = state_obj.registers.baud_rate;
	uint32_t nearest   = supported_baudrates[0];
	uint32_t min_diff  = abs((int32_t)requested - (int32_t)nearest);

	for (uint32_t i = 1; i < SUPPORTED_BAUD_RATES_COUNT; i++)
	{
		uint32_t diff = abs((int32_t)requested -
				(int32_t)supported_baudrates[i]);

		if (diff < min_diff)
		{
			min_diff = diff;
			nearest  = supported_baudrates[i];
		}
	}

	state_obj.registers.baud_rate = nearest;

	/* The Modbus register contains the protocol values 0=None, 1=Even, 2=Odd.
	 * Do not compare these values with STM32 HAL parity bit masks. */
	if(state_obj.registers.parity != 0.0f
			&& state_obj.registers.parity != 1.0f
			&& state_obj.registers.parity != 2.0f){
		state_obj.registers.parity = DEFAULT_PARITY;
	}

	if(state_obj.registers.slave_address  == 0 || state_obj.registers.slave_address >= 0xFF){
		state_obj.registers.slave_address = DEFAULT_SLAVE_ADDR;
	}

	for(uint8_t ix = 0; ix < MAX_PULSE; ++ix){
		if(state_obj.registers.factor[ix] <= 0.0){
			state_obj.registers.factor[ix] = 1.0;
		}
		if(state_obj.registers.reading[ix] <= 0.0){
			state_obj.registers.reading[ix] = 0.0;
		}
	}

}
void CapturePulse(uint8_t pulse_channel, uint8_t pulse_state){
	TIME_T now = get_timestamp_in_msec();

	if(pulse_state != state_obj.last_pulse_state[pulse_channel]){

		if(pulse_state &&
				(get_time_diffrence(state_obj.last_pulse_timestamp[pulse_channel], now) >= PULSE_MINIMUM_TIME)){
			++state_obj.pending_pulses[pulse_channel];
		}

		state_obj.last_pulse_state[pulse_channel] = pulse_state;
		state_obj.last_pulse_timestamp[pulse_channel] = now;
	}
}

uint8_t save_eepeom_register(uint32_t register_address, uint8_t* bytes, const uint16_t byteCnt){
	volatile uint8_t result = true;
	uint8_t buffer[32] = { 0 };
	uint8_t read_buffer[32] = { 0 };
	uint8_t attempts = EEPROM_WRITE_ATTEMPTS;
	uint32_t address = EEPROM_BASE_ADDRESS + register_address;
	uint32_t backup_address = EEPROM_BACKUP_ADDRESS + register_address;

	memset(buffer, 0, sizeof(buffer));
	memcpy(buffer, bytes, byteCnt);
	calculate_and_fill_modbus_crc(buffer, byteCnt + SIZE_OF_CRC);

	attempts = EEPROM_WRITE_ATTEMPTS;
	do{
		memset(read_buffer, 0, sizeof(read_buffer));
		EEPROM_Write(address, buffer, (byteCnt + SIZE_OF_CRC));
		EEPROM_Read(address, read_buffer, (byteCnt + SIZE_OF_CRC));
		--attempts;
	}while(memcmp(buffer, read_buffer, (byteCnt + SIZE_OF_CRC)) != 0 && attempts > 0);

	if(attempts == 0){
		result = memcmp(buffer, read_buffer, (byteCnt + SIZE_OF_CRC)) == 0;
	}

	attempts = EEPROM_WRITE_ATTEMPTS;
	do{
		memset(read_buffer, 0, sizeof(read_buffer));
		EEPROM_Write(backup_address, buffer, (byteCnt + SIZE_OF_CRC));
		EEPROM_Read(backup_address, read_buffer, (byteCnt + SIZE_OF_CRC));
		--attempts;
	}while(memcmp(buffer, read_buffer, (byteCnt + SIZE_OF_CRC)) != 0 && attempts > 0);

	if(attempts == 0){
		result = memcmp(buffer, read_buffer, (byteCnt + SIZE_OF_CRC)) == 0;
	}
	return result;
}

uint8_t load_eepeom_register(EEPROM_REGISTER register_address, uint8_t* value, const uint16_t byteCnt){
	volatile uint8_t result = false;
	uint8_t buffer[32] = { 0 };
	uint32_t address = EEPROM_BASE_ADDRESS + register_address;
	uint32_t backup_address = EEPROM_BACKUP_ADDRESS + register_address;
	memset(buffer, 0, sizeof(buffer));
	EEPROM_Read(address, buffer, (byteCnt + SIZE_OF_CRC));
	result = ModBus_CRC16(buffer, (byteCnt + SIZE_OF_CRC)) == 0;
	if(!result){
		EEPROM_Read(backup_address, buffer, (byteCnt + SIZE_OF_CRC));
		result = ModBus_CRC16(buffer, (byteCnt + SIZE_OF_CRC)) == 0;
	}
	if(result){
		memcpy(value, buffer, byteCnt);
	}

	return result;
}

void save_configuration(void)
{
	uint32_t value;
	float value_f;
	uint8_t device_id[DEVICE_ID_LENGTH];

	/* Validate before taking the values that are written to EEPROM.  This keeps
	 * an invalid value received over Modbus from returning after a power cycle. */
	checkCommunicationConfiguration();

	if(!load_eepeom_register(EEPROM_BAUDRATE_ADDR, (uint8_t*)&value_f, sizeof(value_f))
			|| value_f != state_obj.registers.baud_rate){
		value_f = state_obj.registers.baud_rate;
		save_eepeom_register(EEPROM_BAUDRATE_ADDR, (uint8_t*)&value_f, sizeof(value_f));
	}

	if(!load_eepeom_register(EEPROM_PARITY_ADDR, (uint8_t*)&value_f, sizeof(value_f))
			|| value_f != state_obj.registers.parity){
		value_f = state_obj.registers.parity;
		save_eepeom_register(EEPROM_PARITY_ADDR, (uint8_t*)&value_f, sizeof(value_f));
	}

	if(!load_eepeom_register(EEPROM_DISPLAY_TIME_ADDR, (uint8_t*)&value, sizeof(value))
			|| value != state_obj.registers.display_time){
		value = state_obj.registers.display_time;
		save_eepeom_register(EEPROM_DISPLAY_TIME_ADDR, (uint8_t*)&value, sizeof(value));
	}

	if(!load_eepeom_register(EEPROM_DEVICE_ID_ADDR, (uint8_t*)device_id, sizeof(device_id))
			|| memcmp(device_id, state_obj.registers.device_id, sizeof(device_id)) != 0){
		save_eepeom_register(EEPROM_DEVICE_ID_ADDR,
				state_obj.registers.device_id, sizeof(device_id));
	}

	if(!load_eepeom_register(EEPROM_SLAVE_ADDR, (uint8_t*)&value, sizeof(value))
			|| value != state_obj.registers.slave_address){
		value = state_obj.registers.slave_address;
		checkCommunicationConfiguration();
		save_eepeom_register(EEPROM_SLAVE_ADDR, (uint8_t*)&value, sizeof(value));
	}

	if(!load_eepeom_register(EEPROM_NO_OF_EXT_POWER_ON_OFF, (uint8_t*)&value_f, sizeof(value_f))
	   || value_f != state_obj.registers.ext_power_on_off){
	    value_f = state_obj.registers.ext_power_on_off;
	    save_eepeom_register(EEPROM_NO_OF_EXT_POWER_ON_OFF, (uint8_t*)&value_f, sizeof(value_f));
	}

	if(!load_eepeom_register(EEPROM_NO_OF_AMR_ON_OFF, (uint8_t*)&value_f, sizeof(value_f))
	   || value_f != state_obj.registers.amr_on_off){
	    value_f = state_obj.registers.amr_on_off;
	    save_eepeom_register(EEPROM_NO_OF_AMR_ON_OFF, (uint8_t*)&value_f, sizeof(value_f));
	}


	for(uint8_t ix = 0; ix < MAX_PULSE; ++ix){
		if(!load_eepeom_register(EEPROM_PULSE_ADDR + (ix * EACH_REGISTER_SIZE_WITH_CRC), (uint8_t*)&value_f, sizeof(value_f))
				|| value_f != state_obj.registers.reading[ix]){
			value_f = state_obj.registers.reading[ix];
			save_eepeom_register(EEPROM_PULSE_ADDR + ix * EACH_REGISTER_SIZE_WITH_CRC, (uint8_t*)&value_f, sizeof(value_f));
		}
	}

	for(uint8_t ix = 0; ix < MAX_PULSE; ++ix){
		if(!load_eepeom_register(EEPROM_FACTOR_ADDR + (ix * EACH_REGISTER_SIZE_WITH_CRC), (uint8_t*)&value_f, sizeof(value_f))
				|| value_f != state_obj.registers.factor[ix]){
			value_f = state_obj.registers.factor[ix];
			save_eepeom_register(EEPROM_FACTOR_ADDR + (ix * EACH_REGISTER_SIZE_WITH_CRC), (uint8_t*)&value_f, sizeof(value_f));
		}
	}

	checkCommunicationConfiguration();
	UpdateUart_Baudrate(state_obj.registers.baud_rate, state_obj.registers.parity);
}

void load_configuration(void)
{
	uint32_t value;
	float value_f;
	uint8_t device_id[DEVICE_ID_LENGTH + 1];
	//	disable_interrupts();

	if(!load_eepeom_register(EEPROM_BAUDRATE_ADDR, (uint8_t*)&value_f, sizeof(value_f))){
		state_obj.registers.baud_rate = DEFAULT_BAUD_RATE;
	}else{
		state_obj.registers.baud_rate = value_f;
	}

	if(!load_eepeom_register(EEPROM_PARITY_ADDR, (uint8_t*)&value_f, sizeof(value_f))){
		state_obj.registers.parity = DEFAULT_PARITY;
	}else{
		state_obj.registers.parity = value_f;
	}
//	state_obj.registers.parity = DEFAULT_PARITY;

	if(!load_eepeom_register(EEPROM_DISPLAY_TIME_ADDR, (uint8_t*)&value, sizeof(value))){
		state_obj.registers.display_time = DEFAULT_DISPLAY_TIME;
	}else{
		state_obj.registers.display_time = value;
	}

	if(!load_eepeom_register(EEPROM_DEVICE_ID_ADDR, (uint8_t*)device_id, DEVICE_ID_LENGTH)
			|| strlen((char*)device_id) <= DEVICE_ID_LENGTH/2){
		memcpy(state_obj.registers.device_id, DEFAULT_DEVICE_ID, DEVICE_ID_LENGTH);
	}else{
		memcpy(state_obj.registers.device_id, device_id, DEVICE_ID_LENGTH);
	}

	if(!load_eepeom_register(EEPROM_SLAVE_ADDR, (uint8_t*)&value, sizeof(value))){
		state_obj.registers.slave_address = DEFAULT_SLAVE_ADDR;
	}else{
		state_obj.registers.slave_address = value;
	}

	if(!load_eepeom_register(EEPROM_NO_OF_EXT_POWER_ON_OFF, (uint8_t*)&value_f, sizeof(value_f))){
	    state_obj.registers.ext_power_on_off = 0;
	}else{
	    state_obj.registers.ext_power_on_off = value_f;
	}

	if(!load_eepeom_register(EEPROM_NO_OF_AMR_ON_OFF, (uint8_t*)&value_f, sizeof(value_f))){
	    state_obj.registers.amr_on_off = 0;
	}else{
	    state_obj.registers.amr_on_off = value_f;
	}

	for(uint8_t ix = 0; ix < MAX_PULSE; ++ix){
		if(!load_eepeom_register(EEPROM_PULSE_ADDR + (ix * EACH_REGISTER_SIZE_WITH_CRC), (uint8_t*)&value_f, sizeof(value_f))){
			state_obj.registers.reading[ix] = 0;
		}else{
			state_obj.registers.reading[ix] = value_f;
		}
		state_obj.last_pulse_state[ix] = HAL_GPIO_ReadPin(pulsePins[ix].gpio, pulsePins[ix].pin);
	}

	for(uint8_t ix = 0; ix < MAX_PULSE; ++ix){
		if(!load_eepeom_register(EEPROM_FACTOR_ADDR + (ix * EACH_REGISTER_SIZE_WITH_CRC), (uint8_t*)&value_f, sizeof(value_f))){
			state_obj.registers.factor[ix] = 1;
		}else{
			state_obj.registers.factor[ix] = value_f;
		}
	}

    state_obj.registers.amr_on_off += 1.0f;
 //   state_obj.isConfiguratnoSavePending = true;

	checkCommunicationConfiguration();
	save_configuration();

	if(state_obj.registers.display_time < 2000){
		state_obj.registers.display_time = DEFAULT_DISPLAY_TIME;
	}

	//	enable_interrupts();
}


uint8_t Battery_Percent(float vbat)
{
	if (vbat >= BAT_FULL) return 100;
	if (vbat <= BAT_EMPTY) return 0;

	return (uint8_t)(((vbat - BAT_EMPTY) / (BAT_FULL - BAT_EMPTY)) * 100.0f);
}


BatteryStatus_t Get_Battery_Status(float vbat, uint8_t percent)
{
	if (vbat <= BAT_EMPTY)
		return BAT_STATUS_EMPTY;

	if (percent <= BAT_LOW_PERCENT)
		return BAT_STATUS_LOW;

	return BAT_STATUS_OK;
}

void ReadBatteryVoltage(){
	state_obj.registers.bat_v = Read_ADC_Voltage();
	state_obj.registers.bat_p = Battery_Percent(state_obj.registers.bat_v);
	state_obj.registers.bat_status = Get_Battery_Status(state_obj.registers.bat_v, state_obj.registers.bat_p);
}

void DisplayOLED_Screen(uint16_t oled_state, PULSE_CHANNEL channel){
	char buffer[20];

	ssd1306_Fill(Black);

	//	if(state_obj.is_low_power_enabled == false)
	{
		ReadBatteryVoltage();
	}

	memset(buffer, 0, sizeof(buffer));

	if(channel >= MAX_PULSE){
		channel = MAX_PULSE;
	}

	checkCommunicationConfiguration();

	switch(oled_state){
	case OLED_COMPANY_INFO:{
		ssd1306_SetCursor(30, 5);
		ssd1306_WriteString("POWERN", DEFAULT_FONT, White);
		ssd1306_SetCursor(20, 25);
		ssd1306_WriteString("Solutions", DEFAULT_FONT, White);
		ssd1306_SetCursor(15, 45);
		memcpy(buffer, state_obj.registers.device_id, sizeof(state_obj.registers.device_id));
		ssd1306_WriteString(buffer, DEFAULT_FONT, White);
	}
	break;
	case OLED_CONFIGURATION_INFO:{
		sprintf(buffer, "%03d", state_obj.registers.slave_address);
		ssd1306_Fill(Black);
		ssd1306_SetCursor(0, 10);
		ssd1306_WriteString("Slave ID", SMALL_FONT, White);
		ssd1306_SetCursor(70, 5);
		ssd1306_WriteString(buffer, DEFAULT_FONT, White);

		ssd1306_SetCursor(0, 30);
		sprintf(buffer, "%d", (int)state_obj.registers.baud_rate);
		ssd1306_WriteString("Baud rate", SMALL_FONT, White);
		ssd1306_SetCursor(70, 25);

		ssd1306_WriteString(buffer, DEFAULT_FONT, White);
		ssd1306_SetCursor(0, 50);
		sprintf(buffer, "%s", PARITY_STRING[(int)state_obj.registers.parity]);
		ssd1306_WriteString("Parity", SMALL_FONT, White);
		ssd1306_SetCursor(70, 45);
		ssd1306_WriteString(buffer, DEFAULT_FONT, White);
	}
	break;
	//	case OLED_BATTERY_INFO:{
	//		if (state_obj.registers.bat_status == BAT_STATUS_EMPTY)
	//		{
	//			ssd1306_SetCursor(10, 20);
	//			ssd1306_WriteString("NO BATTERY", DEFAULT_FONT, White);
	//		}else{
	//			if (state_obj.registers.bat_status == BAT_STATUS_LOW)
	//			{
	//				ssd1306_SetCursor(10, 10);
	//				ssd1306_WriteString("LOW BATTERY", DEFAULT_FONT, White);
	//			}
	//			else
	//			{
	//				sprintf(buffer, "BAT: %.2fV", state_obj.registers.bat_v);
	//				ssd1306_SetCursor(10, 10);
	//				ssd1306_WriteString(buffer, DEFAULT_FONT, White);
	//			}
	//
	//			sprintf(buffer, "%d %%", state_obj.registers.bat_p);
	//			ssd1306_SetCursor(10, 30);
	//			ssd1306_WriteString(buffer, DEFAULT_FONT, White);
	//		}
	//	}
	//	break;
	case OLED_PULSE_INFO:{

		sprintf(buffer, "CHANNEL %2d", (channel+1));
		ssd1306_SetCursor(10, 5);
		ssd1306_WriteString(buffer, DEFAULT_FONT, White);

		float display_value = state_obj.registers.reading[channel] *
		                      state_obj.registers.factor[channel];
		sprintf(buffer, "%9.3f", display_value);
		ssd1306_SetCursor(10, 25);
		ssd1306_WriteString(buffer, DEFAULT_FONT, White);

		ssd1306_SetCursor(10, 45);
		if (state_obj.registers.bat_status == BAT_STATUS_EMPTY)
		{
			ssd1306_WriteString("NO BATTERY", DEFAULT_FONT, White);
		}else{
			if (state_obj.registers.bat_status == BAT_STATUS_LOW)
			{
				ssd1306_WriteString("LOW BATTERY", DEFAULT_FONT, White);
			}
			else
			{
				sprintf(buffer, "BAT: %.2fV", state_obj.registers.bat_v);
				ssd1306_WriteString(buffer, DEFAULT_FONT, White);
			}
		}
	}
	break;
	default:
		break;

	}
	ssd1306_UpdateScreen();
	state_obj.oled_prev_state = oled_state;
	state_obj.oled_prev_channel = channel;
	state_obj.oled_display_update_at = get_timestamp_in_msec();
}

void UpdatePageDisplay(){
	if(state_obj.oled_state == OLED_PULSE_INFO){
		++state_obj.oled_channel;
		if(state_obj.oled_channel >= MAX_PULSE){
			state_obj.oled_state = (state_obj.oled_state + 1) % MAX_OLED_SCREENS;
			state_obj.oled_channel = PULSE_CH1;
		}
	}else{
		state_obj.oled_state = (state_obj.oled_state + 1) % MAX_OLED_SCREENS;
		state_obj.oled_channel = PULSE_CH1;
	}
	if(state_obj.oled_state == OLED_OFF || state_obj.oled_state >= MAX_OLED_SCREENS){
		state_obj.oled_state = OLED_COMPANY_INFO;
		state_obj.oled_channel = PULSE_CH1;
	}
	state_obj.oled_display_update_at = get_timestamp_in_msec();
	state_obj.oled_force_update = true;
}

void initialize_application(){
	GREEN_LED(LED_OFF_STATE);
	RED_LED(LED_ON_STATE);
	application_delay(APPLICATION_LOOP_DELAY);

	load_configuration();
	checkCommunicationConfiguration();
	/* A valid DIP setting takes precedence over the saved address.  Read it
	 * before enabling RX so the first Modbus frame is matched against the
	 * address shown by the DIP switches. */
//	Read_DIP_Address();                   //disabled for reading salveid from dipswitch
	/* USART1 is generated with the default 9600 8N1 configuration.  Apply the
	 * values restored from EEPROM before reception is enabled. */
	UpdateUart_Baudrate(state_obj.registers.baud_rate, state_obj.registers.parity);
	HAL_ADCEx_Calibration_Start(&BATTERY_ADC);

	if(ssd1306_IsAvailable()){
		ssd1306_Init();
#if(1)
		state_obj.oled_state = OLED_COMPANY_INFO;
		state_obj.oled_channel = PULSE_CH1;
		DisplayOLED_Screen(state_obj.oled_state, state_obj.oled_channel);
#else
		for(uint8_t ix = OLED_COMPANY_INFO; ix <= OLED_PULSE_INFO; ++ix){
			state_obj.oled_channel = PULSE_CH1;
			do{
				DisplayOLED_Screen(ix, state_obj.oled_channel);
				application_delay(1000);
				++state_obj.oled_channel;
			}while(ix == OLED_PULSE_INFO && state_obj.oled_channel <= PULSE_CH3);
		}
#endif
	}
	HAL_UART_Receive_IT(&MODBUS_UART, &state_obj.uart1_rx_byte, 1);
	initialize_modbus(state_obj.registers.slave_address);
	//	SystemClock_Decrease();
}

void oled_display_off(){
	if(ssd1306_State().DisplayOn){
		ssd1306_Fill(Black);
		ssd1306_UpdateScreen();
		ssd1306_SetDisplayOn(false);
	}
}

void process_external_power_status(){

	if(state_obj.lastPowerPinStatus != HAL_GPIO_ReadPin(PULSE2_INT_GPIO_Port, EXT_PWR_STATUS_INT_Pin)){
		state_obj.power_pulse_timestamp = get_timestamp_in_msec();
		state_obj.lastPowerPinStatus = HAL_GPIO_ReadPin(PULSE2_INT_GPIO_Port, EXT_PWR_STATUS_INT_Pin);
	}
#if(1)
	if((get_time_diffrence(state_obj.power_pulse_timestamp, get_timestamp_in_msec()) > PULSE_MINIMUM_TIME)){
		if(state_obj.lastPowerPinStatus){
			state_obj.externalPowerStatus = true;
		}else{
			if(state_obj.externalPowerStatus){
				state_obj.registers.ext_power_on_off += 1.0f;
				state_obj.isConfiguratnoSavePending = true;
			}
			state_obj.externalPowerStatus = false;
		}
	}
#else
	state_obj.externalPowerStatus = true;
#endif

	if(state_obj.externalPowerStatus == false){

		if(!state_obj.is_low_power_enabled
				&& is_oled_display_timeout()){
			/* Reduce the System clock to below 2 MHz */
			RED_LED(LED_OFF_STATE);
			oled_display_off();

			HAL_UART_MspDeInit(&MODBUS_UART);
			//			HAL_I2C_MspDeInit(&EEPROM_I2C);
			//			HAL_I2C_MspDeInit(&OLED_I2C);

			SystemClock_Decrease();
			//			MX_I2C1_Init();
			//			MX_I2C2_Init();

			HAL_PWREx_EnableLowPowerRunMode();
			state_obj.is_low_power_enabled= true;
		}
	}else{
		if(state_obj.is_low_power_enabled){

			HAL_PWREx_DisableLowPowerRunMode();

			SystemClock_Config();

			reset_modbus_state();
			HAL_UART_MspInit(&MODBUS_UART);
			HAL_UART_Receive_IT(&MODBUS_UART, &state_obj.uart1_rx_byte, 1);
			RED_LED(LED_ON_STATE);

			state_obj.is_low_power_enabled = false;
		}
	}
}

void process_application(){

//		application_delay(100);
//		mbSendBytes("Start AJ\r\n", 10);



	for(uint8_t ix = PULSE_CH1; ix < MAX_PULSE; ++ix){
		CapturePulse(ix,
				HAL_GPIO_ReadPin(pulsePins[ix].gpio, pulsePins[ix].pin) == GPIO_PIN_SET);

		if(state_obj.pending_pulses[ix] > 0){
			if((state_obj.pending_pulses[ix] * state_obj.registers.factor[ix])
					>= state_obj.registers.factor[ix]){
				state_obj.registers.reading[ix] += state_obj.pending_pulses[ix];
				state_obj.pending_pulses[ix] = 0;

				GREEN_LED(LED_ON_STATE);
				state_obj.green_oled_update_at = get_timestamp_in_msec();
			}
		}

	}

	process_external_power_status();

	if(GREEN_LED_STATE(LED_ON_STATE) &&
			get_time_diffrence(state_obj.green_oled_update_at, get_timestamp_in_msec()) > PULSE_BLINK_TIME){
		GREEN_LED(LED_OFF_STATE);
	}

	if( state_obj.isConfiguratnoSavePending == true
			|| get_time_diffrence(state_obj.last_saved_timestamp, get_timestamp_in_msec()) >= getPulseSaveInterval() ){
		save_configuration();
		state_obj.isConfiguratnoSavePending = false;
		state_obj.last_saved_timestamp = get_timestamp_in_msec();
	}


	//	if(!state_obj.is_low_power_enabled)
	{
//		Read_DIP_Address();                   //disabled for reading slaveid from dipswitch
		if(state_obj.oled_force_update || state_obj.oled_state != state_obj.oled_prev_state
				|| state_obj.oled_channel != state_obj.oled_prev_channel){
			state_obj.oled_force_update = false;
			if( ssd1306_IsAvailable()){
				uint32_t start = HAL_GetTick();

				if(!ssd1306_State().DisplayOn){
					ssd1306_Init();
				}
				DisplayOLED_Screen(state_obj.oled_state, state_obj.oled_channel);

				state_obj.displayUpdateTime = HAL_GetTick() - start;
			}
		}
	}


	if(state_obj.is_low_power_enabled){

		if(is_oled_display_timeout() && ssd1306_State().DisplayOn){
			oled_display_off();
		}

		if(!ssd1306_State().DisplayOn){
			//for Sleep mode
			//				HAL_SuspendTick();              // Stop SysTick (IMPORTANT)
			//				HAL_PWR_EnterSLEEPMode( PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
			//				HAL_ResumeTick();

			//for stop mode
			//				HAL_SuspendTick();
			//				HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);
			//				/* MCU resumes here after wakeup */
			//				SystemClock_Decrease();   //REQUIRED
			//				HAL_ResumeTick();
		}
	}else{
		check_uart_receiver_errors(&MODBUS_UART, &state_obj.uart1_rx_byte);

		if(mbPkt.isPaketProcessingPending){
			process_modbus_request();
			reset_modbus_state();
		}

		if(is_oled_display_timeout() && ssd1306_IsAvailable()){
			UpdatePageDisplay();
		}

//		mbSendBytes("1234\r\n", 6);
//		HAL_Delay(100);
	}

	if(!ssd1306_IsAvailable() && ssd1306_State().DisplayOn){
		oled_display_off();
	}


}
