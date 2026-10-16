/*
 * eeprom.h
 *
 *  Created on: Jan 23, 2026
 *      Author: vishnu
 */

#ifndef INC_EEPROM_H_
#define INC_EEPROM_H_

#include "stm32g0xx_hal.h"
#include "main.h"

#define AT24C04_I2C_ADDR   (0x50 << 1)



HAL_StatusTypeDef EEPROM_Write(uint32_t mem_addr,
                               uint8_t *data,
                               uint16_t length);

HAL_StatusTypeDef EEPROM_Read(uint32_t mem_addr,
                              uint8_t *data,
                              uint16_t length);

#endif /* INC_EEPROM_H_ */
