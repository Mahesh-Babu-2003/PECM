#ifndef LCD_UI_H
#define LCD_UI_H

#include "ms51_device.h"

#ifndef BACK_LIGHT_OFF
#define BACK_LIGHT_OFF 0
#endif
#ifndef BACK_LIGHT_ON
#define BACK_LIGHT_ON 1
#endif

extern volatile uint8_t back_light_on;

/* Customer display screen numbers, matching their position in the cycle. */
#define LCD_UI_SCREEN_1_CUSTOMER_ID       1
#define LCD_UI_SCREEN_2_BALANCE           2
#define LCD_UI_SCREEN_3_LOAD_STATUS       3
#define LCD_UI_SCREEN_4_MAIN_UNITS        4
#define LCD_UI_SCREEN_5_DG_SOLAR_UNITS    5
#define LCD_UI_SCREEN_6_LOW_BALANCE_ALERT 6
#define LCD_UI_SCREEN_7_LOW_BALANCE_LIMIT 7
#define LCD_UI_SCREEN_8_DISCONNECT_LIMIT  8
#define LCD_UI_SCREEN_9_TAMPER_EVENT       9
#define LCD_UI_SCREEN_10_BMS_OVERRIDE    10
#define LCD_UI_SCREEN_11_CONSUMPTION_FLAG 11
#define LCD_UI_SCREEN_COUNT               11
#define LCD_UI_SCREEN_RTC_EDIT             0

void LCD_UI_Init(void);
void LCD_UI_ApplyBacklight(void);
void LCD_UI_ShowCustomerScreen(uint8_t screen_number);
void LCD_UI_ShowRtcEdit(uint8_t digit_index);
void LCD_UI_ShowCompanyName(void);
uint8_t LCD_UI_NextCustomerScreen(uint8_t current_screen);

#endif
