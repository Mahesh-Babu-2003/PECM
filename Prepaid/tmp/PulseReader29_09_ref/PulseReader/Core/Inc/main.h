/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "eeprom.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PULSE0_INT_Pin GPIO_PIN_0
#define PULSE0_INT_GPIO_Port GPIOA
#define PULSE0_INT_EXTI_IRQn EXTI0_1_IRQn
#define PULSE1_INT_Pin GPIO_PIN_2
#define PULSE1_INT_GPIO_Port GPIOA
#define PULSE1_INT_EXTI_IRQn EXTI2_3_IRQn
#define DISPLAY_PB_INT_Pin GPIO_PIN_3
#define DISPLAY_PB_INT_GPIO_Port GPIOA
#define DISPLAY_PB_INT_EXTI_IRQn EXTI2_3_IRQn
#define PULSE2_INT_Pin GPIO_PIN_4
#define PULSE2_INT_GPIO_Port GPIOA
#define PULSE2_INT_EXTI_IRQn EXTI4_15_IRQn
#define EXT_PWR_STATUS_INT_Pin GPIO_PIN_5
#define EXT_PWR_STATUS_INT_GPIO_Port GPIOA
#define EXT_PWR_STATUS_INT_EXTI_IRQn EXTI4_15_IRQn
#define BAT_ADC_Pin GPIO_PIN_6
#define BAT_ADC_GPIO_Port GPIOA
#define RS485_EN_Pin GPIO_PIN_7
#define RS485_EN_GPIO_Port GPIOA
#define DIP1_Pin GPIO_PIN_0
#define DIP1_GPIO_Port GPIOB
#define DIP2_Pin GPIO_PIN_1
#define DIP2_GPIO_Port GPIOB
#define DIP3_Pin GPIO_PIN_2
#define DIP3_GPIO_Port GPIOB
#define LED1_Pin GPIO_PIN_8
#define LED1_GPIO_Port GPIOA
#define LED2_Pin GPIO_PIN_6
#define LED2_GPIO_Port GPIOC
#define DIP4_Pin GPIO_PIN_3
#define DIP4_GPIO_Port GPIOB
#define DIP5_Pin GPIO_PIN_4
#define DIP5_GPIO_Port GPIOB
#define DIP6_Pin GPIO_PIN_5
#define DIP6_GPIO_Port GPIOB
#define DIP7_Pin GPIO_PIN_6
#define DIP7_GPIO_Port GPIOB
#define DIP8_Pin GPIO_PIN_7
#define DIP8_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

//#define EEPROM_PULSE0_ADDR                  0x04
//#define EEPROM_PULSE1_ADDR                  0x08
//#define EEPROM_PULSE2_ADDR                  0x0C
//
//#define EEPROM_SLAVE_ID_ADDR                0x10
//#define EEPROM_BAUD_RAET_ADDR               0x14
//#define EEPROM_PARITY_ADDR                  0x18
//#define EEPROM_SCALLING_FACTOR_ADDR         0x1C
//#define EEPROM_CH_1_INIT_VAL_ADDR           0x20
//#define EEPROM_CH_2_INIT_VAL_ADDR           0x24
//#define EEPROM_CH_3_INIT_VAL_ADDR           0x28


/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
