/*
 * OLED.h
 *
 *  Created on: Jan 21, 2026
 *      Author: vishnu
 */

#ifndef INC_OLED_H_
#define INC_OLED_H_


#include "stm32g0xx_hal.h"

#define OLED_I2C_ADDR      (0x3C << 1)   // try 0x3F<<1 if this fails

void OLED_Init(void);
void OLED_Clear(void);
void OLED_PrintString(uint8_t page, uint8_t col, char *str);
void OLED_ClearPage(uint8_t page);


#endif /* INC_OLED_H_ */
