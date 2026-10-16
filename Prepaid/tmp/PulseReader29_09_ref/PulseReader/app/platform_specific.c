#include "application.h"
#include "modbus.h"

#define DEFAULT_BAUD_RATE	9600
void UpdateUart_Baudrate(uint32_t baudrate, uint8_t reqParity){
	uint32_t Parity = UART_PARITY_NONE;
	uint32_t WordLength = UART_WORDLENGTH_8B;
	if(baudrate == 0){
		baudrate = DEFAULT_BAUD_RATE;
	}

	if(reqParity == 1){
		Parity = UART_PARITY_EVEN;
	}else if(reqParity == 2){
		Parity = UART_PARITY_ODD;
	}else{
		Parity = UART_PARITY_NONE;
	}

	/* On STM32, parity consumes one bit of the configured word length.  Modbus
	 * tools use 8E1/8O1, so parity needs a 9-bit UART word length to retain all
	 * eight data bits. */
	if(Parity != UART_PARITY_NONE){
		WordLength = UART_WORDLENGTH_9B;
	}

	if(MODBUS_UART.Init.BaudRate != baudrate
			|| MODBUS_UART.Init.Parity != Parity
			|| MODBUS_UART.Init.WordLength != WordLength){
		HAL_UART_DeInit(&MODBUS_UART);
		MODBUS_UART.Init.BaudRate = baudrate;
		MODBUS_UART.Init.Parity = Parity;
		MODBUS_UART.Init.WordLength = WordLength;
		HAL_UART_Init(&MODBUS_UART);
		HAL_UARTEx_SetTxFifoThreshold(&MODBUS_UART, UART_TXFIFO_THRESHOLD_1_8);
		HAL_UARTEx_SetRxFifoThreshold(&MODBUS_UART, UART_RXFIFO_THRESHOLD_1_8);
		HAL_UARTEx_DisableFifoMode(&MODBUS_UART);
		HAL_UART_Receive_IT(&MODBUS_UART, &state_obj.uart1_rx_byte, 1);
	}
}

time_t get_system_time_from_rtc(struct tm* localtime){
	volatile time_t t_of_day = 0;
	RTC_TimeTypeDef sTime = { 0 };
	RTC_DateTypeDef DateToUpdate = { 0 };
	RTC_DateTypeDef DateToUpdate2 = { 0 };
	HAL_RTC_GetDate(&hrtc, &DateToUpdate2, RTC_FORMAT_BIN);
	if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK
			&& HAL_RTC_GetDate(&hrtc, &DateToUpdate, RTC_FORMAT_BIN) == HAL_OK) {
		if(DateToUpdate2.Date != DateToUpdate.Date){
			HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
		}
		localtime->tm_year = (APP_START_YEAR - RTC_BASE_YEAR) +  DateToUpdate.Year ;  // Year - 2000
		localtime->tm_mon = DateToUpdate.Month - 1;           // Month, where 0 = jan
		localtime->tm_mday = DateToUpdate.Date;          // Day of the month
		localtime->tm_hour = sTime.Hours;
		localtime->tm_min = sTime.Minutes;
		localtime->tm_sec = sTime.Seconds;
		localtime->tm_isdst = -1;        // Is DST on? 1 = yes, 0 = no, -1 = unknown
		t_of_day = mktime(localtime);
		t_of_day = t_of_day*1000UL + (sTime.SubSeconds*1000UL/256UL);

		//		t_of_day += 19800;
		//		t = *localtime(&t_of_day);
	}else{
		memset(localtime, 0, sizeof(struct tm));
	}
	return t_of_day;
}

time_t get_system_time(struct tm* localtime){
	time_t time_1, time_2;
	do{
		time_1 = get_system_time_from_rtc(localtime);
		time_2 = get_system_time_from_rtc(localtime);
	}while(time_1 != time_2);
	return time_1;
}

void set_system_time(struct tm*  localtime){
	RTC_TimeTypeDef sTime = { 0 };
	RTC_DateTypeDef DateToUpdate = { 0 };
	sTime.Hours = localtime->tm_hour;
	sTime.Minutes = localtime->tm_min;
	sTime.Seconds = localtime->tm_sec;

	DateToUpdate.WeekDay =  localtime->tm_wday; //RTC_WEEKDAY_MONDAY;
	DateToUpdate.Date = localtime->tm_mday;
	DateToUpdate.Month = localtime->tm_mon + 1;
	DateToUpdate.Year = localtime->tm_year - 100;

	HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_SetDate(&hrtc, &DateToUpdate, RTC_FORMAT_BIN);
}

TIME_T get_timestamp_in_msec(){
	struct tm localtime;
	return get_system_time(&localtime);
}

void disable_interrupts(){
	__disable_irq();
}

void enable_interrupts(){
	__enable_irq();
}

uint8_t WriteByteStreamTo_UART(uint8_t* buffer, uint16_t count){
	uint32_t baudrate = MODBUS_UART.Init.BaudRate;
	uint32_t bits_per_frame = (MODBUS_UART.Init.Parity == UART_PARITY_NONE) ? 10U : 11U;
	uint32_t timeout;

	if(baudrate == 0U){
		baudrate = DEFAULT_BAUD_RATE;
	}

	/* Transmission time depends on the selected baud rate.  A timeout equal to
	 * the byte count fails valid Modbus replies at lower baud rates. */
	timeout = ((uint32_t)count * bits_per_frame * 1000U + baudrate - 1U) / baudrate;
	timeout += 5U;

	return HAL_UART_Transmit(&MODBUS_UART, buffer, count, timeout);
}

float Read_ADC_Voltage(void)
{
	uint32_t sum = 0, sample_count = 20, samples = 0;

	for (uint8_t i = 0; i < sample_count; i++)
	{
		HAL_ADC_Start(&BATTERY_ADC);
		if (HAL_ADC_PollForConversion(&BATTERY_ADC, 2) == HAL_OK){
			sum += HAL_ADC_GetValue(&BATTERY_ADC);
			++samples;
		}
		HAL_ADC_Stop(&BATTERY_ADC);
	}
	if(samples > 0){
		sum = (sum / samples);
	}

	float vadc = (sum * VREF) / ADC_MAX;
	float vbat = vadc / DIVIDER_FACTOR;

	return vbat;
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{

	//	if (GPIO_Pin == PULSE0_INT_Pin)
	//	{
	//		CapturePulse(PULSE_CH1,
	//				HAL_GPIO_ReadPin(pulsePins[PULSE_CH1].gpio, pulsePins[PULSE_CH1].pin));
	//
	//	}
	//	else if (GPIO_Pin == PULSE1_INT_Pin)
	//	{
	//		CapturePulse(PULSE_CH2,
	//				HAL_GPIO_ReadPin(pulsePins[PULSE_CH2].gpio, pulsePins[PULSE_CH2].pin));
	//	}
	//	else if (GPIO_Pin == PULSE2_INT_Pin)
	//	{
	//		CapturePulse(PULSE_CH3,
	//				HAL_GPIO_ReadPin(pulsePins[PULSE_CH3].gpio, pulsePins[PULSE_CH3].pin));
	//	}
	//	else
	if (GPIO_Pin == EXT_PWR_STATUS_INT_Pin)
	{
		if(!HAL_GPIO_ReadPin(EXT_PWR_STATUS_INT_GPIO_Port, EXT_PWR_STATUS_INT_Pin)
				&& (get_time_diffrence(state_obj.power_pulse_timestamp, get_timestamp_in_msec()) > PULSE_MINIMUM_TIME)){
			state_obj.externalPowerStatus = true;
		}
		state_obj.lastPowerPinStatus = HAL_GPIO_ReadPin(EXT_PWR_STATUS_INT_GPIO_Port, EXT_PWR_STATUS_INT_Pin);
		state_obj.power_pulse_timestamp = get_timestamp_in_msec();
	}else if(DISPLAY_PB_INT_Pin == GPIO_Pin){
		if(HAL_GPIO_ReadPin(DISPLAY_PB_INT_GPIO_Port, DISPLAY_PB_INT_Pin)
				&& (get_time_diffrence(state_obj.display_pb_timestamp, get_timestamp_in_msec()) > PULSE_MINIMUM_TIME)){
			UpdatePageDisplay();
		}
		state_obj.display_pb_timestamp = get_timestamp_in_msec();
	}
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if(huart->Instance == MODBUS_UART.Instance){
		process_modbus_byte(state_obj.uart1_rx_byte);
		HAL_UART_Receive_IT(&MODBUS_UART, &state_obj.uart1_rx_byte, 1);
	}
}

uint8_t Read_DIP_Address(void)
{
	uint8_t prev_addr = 0, addr = 0;
	do{
		prev_addr = addr;
		addr = 0;

		addr |= (HAL_GPIO_ReadPin(DIP1_GPIO_Port, DIP1_Pin) == GPIO_PIN_RESET) ? (1 << 0) : 0;
		addr |= (HAL_GPIO_ReadPin(DIP2_GPIO_Port, DIP2_Pin) == GPIO_PIN_RESET) ? (1 << 1) : 0;
		addr |= (HAL_GPIO_ReadPin(DIP3_GPIO_Port, DIP3_Pin) == GPIO_PIN_RESET) ? (1 << 2) : 0;
		addr |= (HAL_GPIO_ReadPin(DIP4_GPIO_Port, DIP4_Pin) == GPIO_PIN_RESET) ? (1 << 3) : 0;
		addr |= (HAL_GPIO_ReadPin(DIP5_GPIO_Port, DIP5_Pin) == GPIO_PIN_RESET) ? (1 << 4) : 0;
		addr |= (HAL_GPIO_ReadPin(DIP6_GPIO_Port, DIP6_Pin) == GPIO_PIN_RESET) ? (1 << 5) : 0;
		addr |= (HAL_GPIO_ReadPin(DIP7_GPIO_Port, DIP7_Pin) == GPIO_PIN_RESET) ? (1 << 6) : 0;
		addr |= (HAL_GPIO_ReadPin(DIP8_GPIO_Port, DIP8_Pin) == GPIO_PIN_RESET) ? (1 << 7) : 0;

	}while(addr != prev_addr);

	/* Modbus RTU reserves address 0 for broadcast and allows unit IDs 1-247.
	 * Do not replace a valid configured address with an invalid DIP setting:
	 * with every switch OFF the raw value is 0, which previously stopped the
	 * receiver from accepting any Modbus request. */
	if(addr >= 1U && addr <= 247U){
		state_obj.registers.slave_address = addr;
	}

	return (uint8_t)state_obj.registers.slave_address;
}

void check_uart_receiver_errors(UART_HandleTypeDef *huart, uint8_t* rx_byte){
	if(huart != NULL){
		uint32_t isrflags   = READ_REG(huart->Instance->ISR);
		//| USART_SR_IDLE | USART_SR_CTS | USART_SR_LBD
		uint32_t errorflags = (isrflags & (uint32_t)(USART_ISR_PE | USART_ISR_FE | USART_ISR_ORE | USART_ISR_NE  )); //USART_SR_IDLE_Pos
		if(errorflags != RESET || huart->ErrorCode != HAL_UART_ERROR_NONE){
			HAL_UART_Receive_IT(huart, rx_byte, 1);
		}
	}
}

void SystemClock_Decrease(void)
{
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};

	HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2);

	/* Select HSI as system clock source */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
	if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
	{
		/* Initialization Error */
		Error_Handler();
	}

	/* Modify HSI to HSI DIV8 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV16; //RCC_HSI_DIV32
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_OFF;
	if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		/* Initialization Error */
		Error_Handler();
	}
}
