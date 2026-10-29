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
#define LCD_UI_SCREEN_4_MAIN_KWH_RATE       4
#define LCD_UI_SCREEN_5_MAIN_KVAH_RATE      5
#define LCD_UI_SCREEN_6_DG_KWH_RATE         6
#define LCD_UI_SCREEN_7_DG_KVAH_RATE        7
#define LCD_UI_SCREEN_8_SOLAR_KWH_RATE      8
#define LCD_UI_SCREEN_9_SOLAR_KVAH_RATE     9
#define LCD_UI_SCREEN_10_LOW_BALANCE_ALERT 10
#define LCD_UI_SCREEN_11_LOW_BALANCE_LIMIT 11
#define LCD_UI_SCREEN_12_DISCONNECT_LIMIT  12
#define LCD_UI_SCREEN_13_TAMPER_EVENT      13
#define LCD_UI_SCREEN_14_BMS_OVERRIDE      14
#define LCD_UI_SCREEN_15_BILLING_PARAMETER 15
#define LCD_UI_SCREEN_16_LAST_RECHARGE     16
#define LCD_UI_SCREEN_17_TOTAL_DEDUCTED    17
#define LCD_UI_SCREEN_COUNT                17
#define LCD_UI_SCREEN_RTC_EDIT             0

void LCD_UI_Init(void);
void LCD_UI_ApplyBacklight(void);
void LCD_UI_ShowCustomerScreen(uint8_t screen_number);
void LCD_UI_ShowRtcEdit(uint8_t digit_index);
void LCD_UI_ShowCompanyName(void);
uint8_t LCD_UI_NextCustomerScreen(uint8_t current_screen);

#endif
