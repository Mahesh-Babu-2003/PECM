#include "ms51_device.h"
#include "sys.h"
#include "board_io.h"
#include "i2c_probe.h"
#include "lcd_ui.h"
#include "rs485_test.h"

#define RTC_EDIT_DIGIT_COUNT 14
#define CUSTOMER_SCREEN_TICKS 3000
#define CUSTOMER_VALUE_REFRESH_TICKS 1000
#define LOW_BALANCE_BEEP_REPEAT_HALF_SECOND_UNITS 60
#define LOW_BALANCE_BEEP_BURST_HALF_SECOND_UNITS 5

#define LOW_BALANCE_BUZZER_IDLE       0
#define LOW_BALANCE_BUZZER_ON         1
#define LOW_BALANCE_BUZZER_OFF        2
#define LOW_BALANCE_BUZZER_PAUSE      3

static uint16_t rtc_poll_timer;
static uint8_t backlight_applied;
static uint16_t backlight_idle_seconds;
static uint16_t backlight_idle_ms;
static uint8_t low_balance_buzzer_state;
static uint8_t low_balance_beep_count;
static uint16_t low_balance_buzzer_timer;
static uint8_t rtc_edit_active;
static uint8_t rtc_edit_digit;
static uint8_t customer_display_screen;
static uint16_t customer_display_timer;
static uint16_t customer_value_refresh_timer;
/* Main-loop ticks per second; change this to adjust all buzzer timings. */
static uint16_t buzzer_second_setting_ticks = 1000;

static void UpdateLowBalanceBuzzer(void)
{
    if (slave_holding_registers[LOW_BALANCE_ALERT_OFFSET] == 0)
    {
        low_balance_buzzer_state = LOW_BALANCE_BUZZER_IDLE;
        low_balance_beep_count = 0;
        low_balance_buzzer_timer = 0;
        Board_SetBuzzer(0);
        return;
    }

    if (low_balance_buzzer_state == LOW_BALANCE_BUZZER_IDLE)
    {
        low_balance_buzzer_state = LOW_BALANCE_BUZZER_ON;
        low_balance_beep_count = 0;
        low_balance_buzzer_timer =
            (uint16_t)(buzzer_second_setting_ticks / 2);
        Board_SetBuzzer(1);
        return;
    }

    Board_SetBuzzer((uint8_t)(low_balance_buzzer_state ==
        LOW_BALANCE_BUZZER_ON));
    if (low_balance_buzzer_timer != 0)
    {
        low_balance_buzzer_timer--;
        if (low_balance_buzzer_timer != 0)
        {
            return;
        }
    }

    if (low_balance_buzzer_state == LOW_BALANCE_BUZZER_ON)
    {
        low_balance_beep_count++;
        Board_SetBuzzer(0);
        if (low_balance_beep_count >= 3)
        {
            low_balance_buzzer_state = LOW_BALANCE_BUZZER_PAUSE;
            low_balance_buzzer_timer = (uint16_t)(
                (buzzer_second_setting_ticks / 2) *
                (LOW_BALANCE_BEEP_REPEAT_HALF_SECOND_UNITS -
                    LOW_BALANCE_BEEP_BURST_HALF_SECOND_UNITS));
        }
        else
        {
            low_balance_buzzer_state = LOW_BALANCE_BUZZER_OFF;
            low_balance_buzzer_timer =
                (uint16_t)(buzzer_second_setting_ticks / 2);
        }
    }
    else if (low_balance_buzzer_state == LOW_BALANCE_BUZZER_OFF)
    {
        low_balance_buzzer_state = LOW_BALANCE_BUZZER_ON;
        low_balance_buzzer_timer =
            (uint16_t)(buzzer_second_setting_ticks / 2);
        Board_SetBuzzer(1);
    }
    else
    {
        low_balance_buzzer_state = LOW_BALANCE_BUZZER_ON;
        low_balance_beep_count = 0;
        low_balance_buzzer_timer =
            (uint16_t)(buzzer_second_setting_ticks / 2);
        Board_SetBuzzer(1);
    }
}

static uint8_t RTC_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t count;

    if (month < 1 || month > 12) return 31;
    count = days[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) ||
        year % 400 == 0)) count = 29;
    return count;
}

static uint8_t RTC_DateTimeIsValid(const RTC_DateTime *value)
{
    return (uint8_t)(value->year >= 2000 && value->year <= 2199 &&
        value->month >= 1 && value->month <= 12 && value->day >= 1 &&
        value->day <= RTC_DaysInMonth(value->year, value->month) &&
        value->hours <= 23 && value->minutes <= 59 && value->seconds <= 59);
}

static uint8_t RTC_CalculateWeekday(uint16_t year, uint8_t month, uint8_t day)
{
    uint16_t century_year;
    uint16_t century;
    uint16_t weekday;

    if (month < 3) { month += 12; year--; }
    century_year = (uint16_t)(year % 100);
    century = (uint16_t)(year / 100);
    weekday = (uint16_t)((day + (13 * (month + 1)) / 5 + century_year +
        (century_year / 4) + (century / 4) + (5 * century)) % 7);
    return (uint8_t)((weekday + 6) % 7);
}

static uint16_t RTC_AdjustDigit(uint16_t value, uint16_t place,
    uint8_t increase)
{
    uint8_t digit;
    uint8_t changed;

    digit = (uint8_t)((value / place) % 10);
    changed = increase ? (uint8_t)((digit + 1) % 10) :
        (uint8_t)(digit == 0 ? 9 : digit - 1);
    return (uint16_t)(value - ((uint16_t)digit * place) +
        ((uint16_t)changed * place));
}

static void RTC_AdjustSelectedDigit(uint8_t increase)
{
    RTC_DateTime candidate;
    uint8_t digit;

    candidate.year = rtc_datetime.year;
    candidate.month = rtc_datetime.month;
    candidate.day = rtc_datetime.day;
    candidate.weekday = rtc_datetime.weekday;
    candidate.hours = rtc_datetime.hours;
    candidate.minutes = rtc_datetime.minutes;
    candidate.seconds = rtc_datetime.seconds;
    digit = rtc_edit_digit;
    if (digit < 2)
    {
        candidate.day = (uint8_t)RTC_AdjustDigit(candidate.day,
            (digit == 0) ? 10 : 1, increase);
    }
    else if (digit < 4)
    {
        candidate.month = (uint8_t)RTC_AdjustDigit(candidate.month,
            (digit == 2) ? 10 : 1, increase);
    }
    else if (digit < 8)
    {
        candidate.year = RTC_AdjustDigit(candidate.year,
            (digit == 4) ? 1000 : ((digit == 5) ? 100 :
            ((digit == 6) ? 10 : 1)), increase);
    }
    else if (digit < 10)
    {
        candidate.hours = (uint8_t)RTC_AdjustDigit(candidate.hours,
            (digit == 8) ? 10 : 1, increase);
    }
    else if (digit < 12)
    {
        candidate.minutes = (uint8_t)RTC_AdjustDigit(candidate.minutes,
            (digit == 10) ? 10 : 1, increase);
    }
    else
    {
        candidate.seconds = (uint8_t)RTC_AdjustDigit(candidate.seconds,
            (digit == 12) ? 10 : 1, increase);
    }
    if (RTC_DateTimeIsValid(&candidate))
    {
        candidate.weekday = RTC_CalculateWeekday(candidate.year,
            candidate.month, candidate.day);
        if (!RTC_WriteDateTime(&candidate)) return;
        rtc_datetime.year = candidate.year;
        rtc_datetime.month = candidate.month;
        rtc_datetime.day = candidate.day;
        rtc_datetime.weekday = candidate.weekday;
        rtc_datetime.hours = candidate.hours;
        rtc_datetime.minutes = candidate.minutes;
        rtc_datetime.seconds = candidate.seconds;
        rtc_datetime_valid = 1;
        RS485_UpdateLiveRtcRegisters();
    }
}

static uint8_t RTC_RestoreEepromSnapshot(void)
{
    RTC_DateTime saved_datetime;

    if (!RTC_LoadDateTimeFromEEPROM(&saved_datetime) ||
        !RTC_WriteDateTime(&saved_datetime))
    {
        return 0;
    }
    rtc_datetime.year = saved_datetime.year;
    rtc_datetime.month = saved_datetime.month;
    rtc_datetime.day = saved_datetime.day;
    rtc_datetime.weekday = saved_datetime.weekday;
    rtc_datetime.hours = saved_datetime.hours;
    rtc_datetime.minutes = saved_datetime.minutes;
    rtc_datetime.seconds = saved_datetime.seconds;
    rtc_datetime_valid = 1;
    return 1;
}

void main(void)
{
    RTC_DateTime rtc_save_datetime;
    uint8_t screen_changed;
    uint8_t prepaid_events;
    uint8_t backlight_activity;
    uint16_t backlight_timeout;
    uint8_t switch_events;
    uint8_t switch_press_events;

    MODIFY_HIRC(HIRC_24);
    SFRS = 0;
    Board_Init();
    I2C_Init();
    RS485_TestInit();
    RTC_ReadDateTime(&rtc_datetime);
    if (!rtc_datetime_valid)
    {
        RTC_RestoreEepromSnapshot();
    }
    RS485_UpdateLiveRtcRegisters();
    LCD_UI_Init();
    backlight_applied = (back_light_on == BACK_LIGHT_ON) ? 1 : 0;
    customer_display_screen = LCD_UI_SCREEN_1_CUSTOMER_ID;
    customer_display_timer = 0;
    LCD_UI_ShowCompanyName();

    while (1)
    {
        screen_changed = 0;
        backlight_activity = 0;
        switch_events = 0;
        RS485_Poll();
        prepaid_events = prepaid_ui_events;
        prepaid_ui_events = 0;
        if (prepaid_events & PREPAID_UI_EVENT_ACTIVITY)
        {
            backlight_activity = 1;
        }
        if (prepaid_events & PREPAID_UI_EVENT_TAMPER) Board_Beep(100);
        if (rtc_poll_timer >= 999)
        {
            rtc_poll_timer = 0;
            if (!rtc_edit_active)
            {
                RTC_ReadDateTime(&rtc_datetime);
                RS485_UpdateLiveRtcRegisters();
            }
        }
        else
        {
            rtc_poll_timer++;
        }

        switch_events = Board_ReadSwitchPresses();
        if (switch_events != 0)
        {
            backlight_activity = 1;
        }
        switch_press_events = (uint8_t)(switch_events &
            (BOARD_SWITCH_1 | BOARD_SWITCH_2 | BOARD_SWITCH_3));
        if (switch_press_events != 0)
        {
            Board_Beep(35);
        }

        if (switch_events & BOARD_SWITCH_1_LONG)
        {
            if (!rtc_edit_active)
            {
                if (!rtc_datetime_valid) RTC_RestoreEepromSnapshot();
                if (!rtc_datetime_valid)
                {
                    rtc_save_datetime.year = 2000;
                    rtc_save_datetime.month = 1;
                    rtc_save_datetime.day = 1;
                    rtc_save_datetime.weekday = 6;
                    rtc_save_datetime.hours = 0;
                    rtc_save_datetime.minutes = 0;
                    rtc_save_datetime.seconds = 0;
                    if (RTC_WriteDateTime(&rtc_save_datetime))
                    {
                        rtc_datetime.year = rtc_save_datetime.year;
                        rtc_datetime.month = rtc_save_datetime.month;
                        rtc_datetime.day = rtc_save_datetime.day;
                        rtc_datetime.weekday = rtc_save_datetime.weekday;
                        rtc_datetime.hours = rtc_save_datetime.hours;
                        rtc_datetime.minutes = rtc_save_datetime.minutes;
                        rtc_datetime.seconds = rtc_save_datetime.seconds;
                        rtc_datetime_valid = 1;
                    }
                }
                rtc_edit_active = 1;
                rtc_edit_digit = 0;
            }
            else
            {
                rtc_save_datetime.year = rtc_datetime.year;
                rtc_save_datetime.month = rtc_datetime.month;
                rtc_save_datetime.day = rtc_datetime.day;
                rtc_save_datetime.weekday = rtc_datetime.weekday;
                rtc_save_datetime.hours = rtc_datetime.hours;
                rtc_save_datetime.minutes = rtc_datetime.minutes;
                rtc_save_datetime.seconds = rtc_datetime.seconds;
                if (rtc_datetime_valid &&
                    RTC_SaveDateTimeToEEPROM(&rtc_save_datetime))
                {
                    Board_Beep(50);
                }
                else
                {
                    Board_Beep(150);
                }
                rtc_edit_active = 0;
            }
            screen_changed = 1;
        }

        if (rtc_edit_active && !(switch_events & BOARD_SWITCH_1_LONG))
        {
            if (switch_press_events & BOARD_SWITCH_1)
            {
                rtc_edit_digit = (uint8_t)((rtc_edit_digit + 1) %
                    RTC_EDIT_DIGIT_COUNT);
                screen_changed = 1;
            }
            else if (switch_press_events & BOARD_SWITCH_2)
            {
                RTC_AdjustSelectedDigit(1);
                screen_changed = 1;
            }
            else if (switch_press_events & BOARD_SWITCH_3)
            {
                RTC_AdjustSelectedDigit(0);
                screen_changed = 1;
            }
        }

        if (!rtc_edit_active &&
            (switch_press_events & BOARD_SWITCH_1) &&
            !(switch_events & BOARD_SWITCH_1_LONG))
        {
            customer_display_screen = LCD_UI_NextCustomerScreen(
                customer_display_screen);
            customer_display_timer = 0;
            customer_value_refresh_timer = 0;
            LCD_UI_ShowCustomerScreen(customer_display_screen);
        }

        backlight_timeout = slave_holding_registers[BACKLIGHT_TIMEOUT_OFFSET];
        if (backlight_timeout == 0)
        {
            backlight_idle_seconds = 0;
            backlight_idle_ms = 0;
            back_light_on = BACK_LIGHT_ON;
        }
        else if (backlight_activity)
        {
            backlight_idle_seconds = 0;
            backlight_idle_ms = 0;
            back_light_on = BACK_LIGHT_ON;
        }
        else if (back_light_on == BACK_LIGHT_ON)
        {
            backlight_idle_ms++;
            if (backlight_idle_ms >= 1000)
            {
                backlight_idle_ms = 0;
                if (backlight_idle_seconds < backlight_timeout)
                {
                    backlight_idle_seconds++;
                }
                if (backlight_idle_seconds >= backlight_timeout)
                {
                    back_light_on = BACK_LIGHT_OFF;
                }
            }
        }

        if (((back_light_on == BACK_LIGHT_ON) ? 1 : 0) != backlight_applied)
        {
            LCD_UI_ApplyBacklight();
            backlight_applied = (back_light_on == BACK_LIGHT_ON) ? 1 : 0;
        }

        if (rtc_edit_active)
        {
            if (screen_changed) LCD_UI_ShowRtcEdit(rtc_edit_digit);
        }
        else
        {
            if (switch_events & BOARD_SWITCH_1_LONG)
            {
                LCD_UI_ShowCustomerScreen(customer_display_screen);
            }
            if ((prepaid_events & PREPAID_UI_EVENT_COMPANY) &&
                customer_display_screen == LCD_UI_SCREEN_1_CUSTOMER_ID)
            {
                LCD_UI_ShowCompanyName();
            }
            if (customer_display_timer >= (CUSTOMER_SCREEN_TICKS - 1))
            {
                customer_display_timer = 0;
                customer_display_screen = LCD_UI_NextCustomerScreen(
                    customer_display_screen);
                LCD_UI_ShowCustomerScreen(customer_display_screen);
            }
            else
            {
                customer_display_timer++;
            }
            if (customer_value_refresh_timer >=
                (CUSTOMER_VALUE_REFRESH_TICKS - 1))
            {
                customer_value_refresh_timer = 0;
                if (customer_display_screen == LCD_UI_SCREEN_6_LOW_BALANCE_ALERT &&
                    slave_holding_registers[LOW_BALANCE_ALERT_OFFSET] != 1)
                {
                    customer_display_screen = LCD_UI_NextCustomerScreen(
                        customer_display_screen);
                    customer_display_timer = 0;
                }
                if (customer_display_screen != LCD_UI_SCREEN_1_CUSTOMER_ID)
                {
                    LCD_UI_ShowCustomerScreen(customer_display_screen);
                }
            }
            else
            {
                customer_value_refresh_timer++;
            }
        }
        UpdateLowBalanceBuzzer();
        Board_DelayMs(1);
    }
}
