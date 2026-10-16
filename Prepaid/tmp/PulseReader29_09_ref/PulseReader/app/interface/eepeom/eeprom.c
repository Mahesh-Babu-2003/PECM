#include "eeprom.h"
#include "main.h"

extern I2C_HandleTypeDef hi2c2;

/*
 * Write bytes to AT24C04
 */
HAL_StatusTypeDef EEPROM_Write(uint32_t mem_addr, uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Write(&hi2c2, AT24C04_I2C_ADDR, mem_addr,
                               I2C_MEMADD_SIZE_8BIT, data, length, HAL_MAX_DELAY);

    HAL_Delay(20);   // EEPROM write cycle time (IMPORTANT)

    return status;
}

/*
 * Read bytes from AT24C04
 */
HAL_StatusTypeDef EEPROM_Read(uint32_t mem_addr,uint8_t *data,uint16_t length)
{
    return HAL_I2C_Mem_Read(&hi2c2, AT24C04_I2C_ADDR, mem_addr,
                            I2C_MEMADD_SIZE_8BIT, data, length, HAL_MAX_DELAY);
}
