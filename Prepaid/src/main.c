#include "ms51_device.h"
#include "sys.h"
#include "board_io.h"
#include "i2c_probe.h"
#include "lcd_595.h"
#include "rs485_test.h"

#define RTC_EDIT_TIMEOUT_TICKS 30000
#define RTC_EDIT_DIGIT_COUNT   14
#define RTC_SAVE_MESSAGE_TICKS 2000
#define RTC_KEY_COMBO_DELAY_TICKS 100
#define RS485_INFO_PAGE_TICKS 3000
#define LCD_PAGE_COMPANY 0
#define LCD_PAGE_BALANCE 1
#define LCD_PAGE_LOADS   2
#define LCD_PAGE_RATES   3
#define LCD_PAGE_METER   4
#define LCD_PAGE_RTC     5
#define LCD_PAGE_COUNT   6

static uint16_t rtc_poll_timer;
static uint16_t screen_switch_timer;
static uint16_t rtc_edit_idle_timer;
static uint8_t lcd_values_initialized;
static uint8_t lcd_page;
static uint8_t rtc_edit_active;
static uint8_t rtc_selected_digit;
static uint8_t backlight_applied;
static uint16_t backlight_idle_seconds;
static uint16_t backlight_idle_ms;
static uint8_t rtc_save_message;
static uint16_t rtc_save_message_ticks;
static uint16_t lcd_last_float_registers[4];
static uint8_t pending_rtc_switch_events;
static uint16_t pending_rtc_switch_ticks;
static uint8_t rs485_info_active;
static uint8_t rs485_info_port;
static uint16_t rs485_info_page_ticks;

static uint8_t LCD_WriteUnsignedLong(uint32_t value)
{
    uint8_t digits[10];
    uint8_t digit_count;
    uint8_t index;

    digit_count = 0;
    do
    {
        digits[digit_count++] = (uint8_t)('0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0 && digit_count < sizeof(digits));

    for (index = 0; index < digit_count; index++)
    {
        LCD_WriteChar((char)digits[digit_count - index - 1]);
    }
    return digit_count;
}

static uint8_t LCD_WriteSignedInteger(int16_t value)
{
    uint8_t output_length;

    if (value < 0)
    {
        LCD_WriteChar('-');
        output_length = (uint8_t)(1 + LCD_WriteUnsignedLong(
            (uint32_t)(-(int32_t)value)));
        return output_length;
    }
    return LCD_WriteUnsignedLong((uint32_t)value);
}

static void LCD_ShowRs485Settings(uint8_t port)
{
    uint32_t baud_rate;
    uint8_t parity;
    uint8_t slave_id;
    uint8_t line_length;
    uint8_t index;

    RS485_GetCommunicationSettings(port, &baud_rate, &parity, &slave_id);
    LCD_Command(0x0C);
    LCD_SetCursor(0, 0);
    if (port == 1)
    {
        LCD_WriteText("P1 ");
    }
    else
    {
        LCD_WriteText("P2 ");
    }
    line_length = (uint8_t)(3 + LCD_WriteUnsignedLong(baud_rate));
    LCD_WriteChar(' ');
    line_length++;
    if (parity == 0)
    {
        LCD_WriteText("8N1");
    }
    else if (parity == 1)
    {
        LCD_WriteText("8E1");
    }
    else
    {
        LCD_WriteText("8O1");
    }
    line_length += 3;
    for (index = line_length; index < 16; index++) LCD_WriteChar(' ');

    LCD_SetCursor(1, 0);
    LCD_WriteText("SLAVE ID: ");
    line_length = (uint8_t)(10 + LCD_WriteUnsignedLong(slave_id));
    for (index = line_length; index < 16; index++) LCD_WriteChar(' ');
}

static void LCD_WriteFloatField(unsigned long bits, uint8_t field_width)
{
    union
    {
        unsigned long bits;
        float value;
    } decoded_float;
    uint8_t digits[7];
    uint8_t digit_count;
    uint8_t output_length;
    uint8_t index;
    uint8_t negative;
    uint8_t fraction;
    unsigned long integer_part;
    unsigned long scaled_value;
    float magnitude;

    decoded_float.bits = bits;
    if ((bits & 0x7F800000UL) == 0x7F800000UL)
    {
        if ((bits & 0x007FFFFFUL) != 0)
        {
            output_length = 3;
            LCD_WriteText("NAN");
        }
        else if (bits & 0x80000000UL)
        {
            output_length = 4;
            LCD_WriteText("-INF");
        }
        else
        {
            output_length = 3;
            LCD_WriteText("INF");
        }
        while (output_length++ < field_width) LCD_WriteChar(' ');
        return;
    }

    negative = (bits & 0x80000000UL) ? 1 : 0;
    magnitude = negative ? -decoded_float.value : decoded_float.value;
    if (magnitude > 999999.99f)
    {
        if (negative)
        {
            output_length = 9;
            LCD_WriteText("-OVERFLOW");
        }
        else
        {
            output_length = 8;
            LCD_WriteText("OVERFLOW");
        }
        while (output_length++ < field_width) LCD_WriteChar(' ');
        return;
    }

    scaled_value = (unsigned long)(magnitude * 100.0f + 0.5f);
    integer_part = scaled_value / 100UL;
    fraction = (uint8_t)(scaled_value % 100UL);
    digit_count = 0;
    do
    {
        digits[digit_count++] = (uint8_t)('0' + (integer_part % 10UL));
        integer_part /= 10UL;
    } while (integer_part != 0 && digit_count < sizeof(digits));

    output_length = (uint8_t)(digit_count + 3 + negative);
    for (index = output_length; index < field_width; index++)
    {
        LCD_WriteChar(' ');
    }
    if (negative) LCD_WriteChar('-');
    while (digit_count != 0)
    {
        LCD_WriteChar((char)digits[--digit_count]);
    }
    LCD_WriteChar('.');
    LCD_WriteChar((char)('0' + (fraction / 10)));
    LCD_WriteChar((char)('0' + (fraction % 10)));
}

static void LCD_ShowSlaveRegisters(void)
{
    uint16_t registers[4];
    unsigned long float_bits_1;
    unsigned long float_bits_2;
    uint8_t index;

    for (index = 0; index < 4; index++)
    {
        registers[index] = slave_holding_registers[index];
    }
    if (lcd_values_initialized)
    {
        for (index = 0; index < 4; index++)
        {
            if (registers[index] != lcd_last_float_registers[index]) break;
        }
        if (index == 4) return;
    }

    /* Word-swapped Modbus pairs: the second register supplies the high word. */
    float_bits_1 = ((unsigned long)registers[1] << 16) | registers[0];
    float_bits_2 = ((unsigned long)registers[3] << 16) | registers[2];

    LCD_Command(0x0C);
    LCD_SetCursor(0, 0);
    LCD_WriteText("F1:");
    LCD_WriteFloatField(float_bits_1, 13);

    LCD_SetCursor(1, 0);
    LCD_WriteText("F2:");
    LCD_WriteFloatField(float_bits_2, 13);

    for (index = 0; index < 4; index++)
    {
        lcd_last_float_registers[index] = registers[index];
    }
    lcd_values_initialized = 1;
}

static void LCD_WriteTwoDigits(uint8_t value)
{
    LCD_WriteChar((char)('0' + (value / 10)));
    LCD_WriteChar((char)('0' + (value % 10)));
}

static void LCD_WriteFourDigits(uint16_t value)
{
    LCD_WriteChar((char)('0' + ((value / 1000) % 10)));
    LCD_WriteChar((char)('0' + ((value / 100) % 10)));
    LCD_WriteChar((char)('0' + ((value / 10) % 10)));
    LCD_WriteChar((char)('0' + (value % 10)));
}

static void LCD_ShowRtc(void)
{
    uint8_t cursor_row;
    uint8_t cursor_column;

    LCD_SetCursor(0, 0);
    if (rtc_save_message != 0)
    {
        if (rtc_save_message == 1)
        {
            LCD_WriteText("RTC SAVE SUCCESS");
            LCD_SetCursor(1, 0);
            LCD_WriteText("EEPROM BACKUP   ");
        }
        else
        {
            LCD_WriteText("RTC SAVE FAILED ");
            LCD_SetCursor(1, 0);
            LCD_WriteText("CHECK EEPROM    ");
        }
        LCD_Command(0x0C);
        return;
    }

    if (!rtc_datetime_valid && !rtc_edit_active)
    {
        LCD_WriteText("RTC DATA INVALID");
        LCD_SetCursor(1, 0);
        LCD_WriteText("CHECK RTC SETUP ");
        LCD_Command(0x0C);
        return;
    }

    LCD_WriteText("DATE: ");
    LCD_WriteTwoDigits(rtc_datetime.day);
    LCD_WriteChar(':');
    LCD_WriteTwoDigits(rtc_datetime.month);
    LCD_WriteChar(':');
    LCD_WriteFourDigits(rtc_datetime.year);

    LCD_SetCursor(1, 0);
    LCD_WriteText("TIME: ");
    LCD_WriteTwoDigits(rtc_datetime.hours);
    LCD_WriteChar(':');
    LCD_WriteTwoDigits(rtc_datetime.minutes);
    LCD_WriteChar(':');
    LCD_WriteTwoDigits(rtc_datetime.seconds);
    LCD_WriteText("  ");

    if (rtc_edit_active)
    {
        if (rtc_selected_digit < 8)
        {
            cursor_row = 0;
            if (rtc_selected_digit < 2)
            {
                cursor_column = (uint8_t)(6 + rtc_selected_digit);
            }
            else if (rtc_selected_digit < 4)
            {
                cursor_column = (uint8_t)(7 + rtc_selected_digit);
            }
            else
            {
                cursor_column = (uint8_t)(8 + rtc_selected_digit);
            }
        }
        else
        {
            uint8_t time_digit;

            cursor_row = 1;
            time_digit = (uint8_t)(rtc_selected_digit - 8);
            if (time_digit < 2)
            {
                cursor_column = (uint8_t)(6 + time_digit);
            }
            else if (time_digit < 4)
            {
                cursor_column = (uint8_t)(7 + time_digit);
            }
            else
            {
                cursor_column = (uint8_t)(8 + time_digit);
            }
        }
        LCD_Command(0x0F);
        LCD_SetCursor(cursor_row, cursor_column);
    }
    else
    {
        LCD_Command(0x0C);
    }
}

static void LCD_PadLine(uint8_t row, uint8_t used_columns)
{
    if (used_columns >= 16)
    {
        return;
    }
    LCD_SetCursor(row, used_columns);
    while (used_columns < 16)
    {
        LCD_WriteChar(' ');
        used_columns++;
    }
}

static void LCD_ShowCompanyName(void)
{
    uint8_t index;
    uint8_t character;
    uint16_t word;

    LCD_Command(0x0C);
    for (index = 0; index < 32; index++)
    {
        if (index >= (COMPANY_NAME_REGISTER_COUNT * 2))
        {
            character = ' ';
        }
        else if ((index & 1) == 0)
        {
            word = slave_holding_registers[COMPANY_NAME_FIRST_OFFSET + index / 2];
            character = (uint8_t)(word >> 8);
        }
        else
        {
            character = (uint8_t)word;
        }
        LCD_SetCursor((uint8_t)(index / 16), (uint8_t)(index % 16));
        LCD_WriteChar((character >= 32 && character <= 126) ?
            (char)character : ' ');
    }
}

static void LCD_ShowBalance(void)
{
    int16_t balance;
    uint8_t used_columns;
    uint8_t alert;

    balance = (int16_t)slave_holding_registers[BALANCE_OFFSET];
    alert = (uint8_t)slave_holding_registers[LOW_BALANCE_ALERT_OFFSET];
    LCD_Command(0x0C);
    LCD_SetCursor(0, 0);
    LCD_WriteText("BAL:");
    used_columns = (uint8_t)(4 + LCD_WriteSignedInteger(balance));
    LCD_PadLine(0, used_columns);
    LCD_SetCursor(1, 0);
    LCD_WriteText("LOW BAL ALERT:");
    LCD_WriteChar(alert ? '1' : '0');
    LCD_WriteChar(' ');
}

static void LCD_ShowLoads(void)
{
    LCD_Command(0x0C);
    LCD_SetCursor(0, 0);
    LCD_WriteText("MAIN:");
    LCD_WriteChar(slave_relay_states[0] ? '1' : '0');
    LCD_WriteText(" DG:");
    LCD_WriteChar(slave_relay_states[1] ? '1' : '0');
    LCD_PadLine(0, 11);
    LCD_SetCursor(1, 0);
    LCD_WriteText("SOLAR:");
    LCD_WriteChar(slave_relay_states[2] ? '1' : '0');
    LCD_PadLine(1, 7);
}

static void LCD_ShowRates(void)
{
    uint8_t used_columns;

    LCD_Command(0x0C);
    LCD_SetCursor(0, 0);
    LCD_WriteText("M:");
    used_columns = (uint8_t)(2 + LCD_WriteUnsignedLong(
        slave_holding_registers[MAIN_RATE_OFFSET]));
    LCD_WriteText(" DG:");
    used_columns = (uint8_t)(used_columns + 4 + LCD_WriteUnsignedLong(
        slave_holding_registers[DG_RATE_OFFSET]));
    LCD_PadLine(0, used_columns);
    LCD_SetCursor(1, 0);
    LCD_WriteText("SOLAR:");
    used_columns = (uint8_t)(6 + LCD_WriteUnsignedLong(
        slave_holding_registers[SOLAR_RATE_OFFSET]));
    LCD_PadLine(1, used_columns);
}

static void LCD_ShowPage(uint8_t page)
{
    switch (page)
    {
        case LCD_PAGE_COMPANY:
            LCD_ShowCompanyName();
            break;
        case LCD_PAGE_BALANCE:
            LCD_ShowBalance();
            break;
        case LCD_PAGE_LOADS:
            LCD_ShowLoads();
            break;
        case LCD_PAGE_RATES:
            LCD_ShowRates();
            break;
        case LCD_PAGE_METER:
            lcd_values_initialized = 0;
            LCD_ShowSlaveRegisters();
            break;
        default:
            LCD_ShowRtc();
            break;
    }
}

static uint8_t RTC_IsLeapYear(uint16_t year)
{
    return (uint8_t)(((year % 4) == 0) &&
        (((year % 100) != 0) || ((year % 400) == 0)));
}

static uint8_t RTC_DaysInMonth(uint16_t year, uint8_t month)
{
    switch (month)
    {
        case 2:
            return RTC_IsLeapYear(year) ? 29 : 28;
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        default:
            return 31;
    }
}

static uint8_t RTC_DateTimeIsValid(const RTC_DateTime *datetime)
{
    return (uint8_t)(datetime->year >= 2000 && datetime->year <= 2199 &&
        datetime->month >= 1 && datetime->month <= 12 && datetime->day >= 1 &&
        datetime->day <= RTC_DaysInMonth(datetime->year, datetime->month) &&
        datetime->hours <= 23 && datetime->minutes <= 59 &&
        datetime->seconds <= 59);
}

static uint8_t RTC_CalculateWeekday(uint16_t year, uint8_t month, uint8_t day)
{
    uint16_t century_year;
    uint16_t century;
    uint16_t weekday;

    if (month < 3)
    {
        month += 12;
        year--;
    }
    century_year = (uint16_t)(year % 100);
    century = (uint16_t)(year / 100);
    weekday = (uint16_t)((day + (13 * (month + 1)) / 5 + century_year +
        (century_year / 4) + (century / 4) + (5 * century)) % 7);
    return (uint8_t)((weekday + 6) % 7);
}

static uint16_t RTC_AdjustDigit(uint16_t value, uint16_t place,
                                int8_t direction)
{
    uint8_t digit;
    uint8_t changed_digit;

    digit = (uint8_t)((value / place) % 10);
    if (direction < 0)
    {
        changed_digit = (digit == 0) ? 9 : (uint8_t)(digit - 1);
    }
    else
    {
        changed_digit = (digit == 9) ? 0 : (uint8_t)(digit + 1);
    }
    return (uint16_t)(value - ((uint16_t)digit * place) +
                      ((uint16_t)changed_digit * place));
}

static uint8_t RTC_AdjustSelectedDigit(int8_t direction)
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

    digit = rtc_selected_digit;
    if (digit < 2)
    {
        candidate.day = (uint8_t)RTC_AdjustDigit(candidate.day,
            (digit == 0) ? 10 : 1, direction);
    }
    else if (digit < 4)
    {
        candidate.month = (uint8_t)RTC_AdjustDigit(candidate.month,
            (digit == 2) ? 10 : 1, direction);
    }
    else if (digit < 8)
    {
        candidate.year = RTC_AdjustDigit(candidate.year,
            (digit == 4) ? 1000 : ((digit == 5) ? 100 :
            ((digit == 6) ? 10 : 1)), direction);
    }
    else if (digit < 10)
    {
        candidate.hours = (uint8_t)RTC_AdjustDigit(candidate.hours,
            (digit == 8) ? 10 : 1, direction);
    }
    else if (digit < 12)
    {
        candidate.minutes = (uint8_t)RTC_AdjustDigit(candidate.minutes,
            (digit == 10) ? 10 : 1, direction);
    }
    else
    {
        candidate.seconds = (uint8_t)RTC_AdjustDigit(candidate.seconds,
            (digit == 12) ? 10 : 1, direction);
    }

    if (!RTC_DateTimeIsValid(&candidate))
    {
        return 0;
    }
    candidate.weekday = RTC_CalculateWeekday(candidate.year,
        candidate.month, candidate.day);
    if (!RTC_WriteDateTime(&candidate))
    {
        return 0;
    }

    rtc_datetime.year = candidate.year;
    rtc_datetime.month = candidate.month;
    rtc_datetime.day = candidate.day;
    rtc_datetime.weekday = candidate.weekday;
    rtc_datetime.hours = candidate.hours;
    rtc_datetime.minutes = candidate.minutes;
    rtc_datetime.seconds = candidate.seconds;
    rtc_datetime_valid = 1;
    return 1;
}

static uint8_t RTC_HandleSwitchPresses(uint8_t switch_events)
{
    if (switch_events == 0)
    {
        return 0;
    }

    lcd_page = LCD_PAGE_RTC;
    screen_switch_timer = 0;
    rtc_edit_idle_timer = 0;

    if (switch_events & BOARD_SWITCH_1)
    {
        if (rtc_edit_active)
        {
            rtc_selected_digit++;
            if (rtc_selected_digit >= RTC_EDIT_DIGIT_COUNT)
            {
                rtc_selected_digit = 0;
            }
        }
        else
        {
            rtc_selected_digit = 0;
        }
        rtc_edit_active = 1;
    }
    else if (switch_events & BOARD_SWITCH_2)
    {
        rtc_edit_active = 1;
        RTC_AdjustSelectedDigit(-1);
    }
    else if (switch_events & BOARD_SWITCH_3)
    {
        rtc_edit_active = 1;
        RTC_AdjustSelectedDigit(1);
    }
    return 1;
}

static void RTC_SetDefaultEditValue(void)
{
    if (!rtc_datetime_valid)
    {
        rtc_datetime.year = 2000;
        rtc_datetime.month = 1;
        rtc_datetime.day = 1;
        rtc_datetime.weekday = 6;
        rtc_datetime.hours = 0;
        rtc_datetime.minutes = 0;
        rtc_datetime.seconds = 0;
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

static void RTC_SaveCurrentTimeToEeprom(void)
{
    RTC_DateTime snapshot;

    snapshot.year = rtc_datetime.year;
    snapshot.month = rtc_datetime.month;
    snapshot.day = rtc_datetime.day;
    snapshot.weekday = rtc_datetime.weekday;
    snapshot.hours = rtc_datetime.hours;
    snapshot.minutes = rtc_datetime.minutes;
    snapshot.seconds = rtc_datetime.seconds;

    lcd_page = LCD_PAGE_RTC;
    screen_switch_timer = 0;
    rtc_save_message_ticks = RTC_SAVE_MESSAGE_TICKS;
    if (rtc_datetime_valid && RTC_SaveDateTimeToEEPROM(&snapshot))
    {
        rtc_save_message = 1;
        Board_Beep(50);
        Board_DelayMs(70);
        Board_Beep(50);
    }
    else
    {
        rtc_save_message = 2;
        Board_Beep(150);
    }
}

void main(void)
{
    uint8_t rtc_refresh_due;
    uint8_t screen_changed;
    uint8_t prepaid_events;
    uint8_t backlight_activity;
    uint16_t backlight_timeout;
    uint8_t switch_events;
    uint8_t switch_press_events;
    uint8_t stable_switches;
    uint8_t deferred_switches;

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
    RTC_SetDefaultEditValue();
    LCD_Init();
    backlight_applied = (back_light_on == BACK_LIGHT_ON) ? 1 : 0;
    LCD_ShowPage(lcd_page);

    while (1)
    {
        rtc_refresh_due = 0;
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
        if (prepaid_events & PREPAID_UI_EVENT_BALANCE)
        {
            lcd_page = LCD_PAGE_BALANCE;
            screen_changed = 1;
        }
        else if (prepaid_events & PREPAID_UI_EVENT_COMPANY)
        {
            lcd_page = LCD_PAGE_COMPANY;
            screen_changed = 1;
        }
        else if (prepaid_events & PREPAID_UI_EVENT_LOADS)
        {
            lcd_page = LCD_PAGE_LOADS;
            screen_changed = 1;
        }
        else if (prepaid_events & PREPAID_UI_EVENT_RATES)
        {
            lcd_page = LCD_PAGE_RATES;
            screen_changed = 1;
        }
        if (screen_changed)
        {
            screen_switch_timer = 0;
            rtc_edit_active = 0;
            rtc_save_message = 0;
        }
        if (rtc_poll_timer >= 999)
        {
            rtc_poll_timer = 0;
            RTC_ReadDateTime(&rtc_datetime);
            rtc_refresh_due = 1;
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

        if (switch_events & BOARD_SWITCH_2_3_LONG)
        {
            pending_rtc_switch_events = 0;
            pending_rtc_switch_ticks = 0;
            rs485_info_active = 1;
            rs485_info_port = 1;
            rs485_info_page_ticks = 0;
            screen_switch_timer = 0;
            rtc_edit_active = 0;
            rtc_save_message = 0;
            screen_changed = 1;
        }

        if (rs485_info_active)
        {
            pending_rtc_switch_events = 0;
            pending_rtc_switch_ticks = 0;
        }
        else
        {
            if (switch_press_events & BOARD_SWITCH_1)
            {
                if (RTC_HandleSwitchPresses(BOARD_SWITCH_1))
                {
                    screen_changed = 1;
                }
            }

            pending_rtc_switch_events |= (uint8_t)(switch_press_events &
                (BOARD_SWITCH_2 | BOARD_SWITCH_3));
            stable_switches = Board_GetStableSwitches();
            if ((stable_switches & (BOARD_SWITCH_2 | BOARD_SWITCH_3)) ==
                (BOARD_SWITCH_2 | BOARD_SWITCH_3))
            {
                pending_rtc_switch_ticks = 0;
            }
            else if (pending_rtc_switch_events != 0)
            {
                if (pending_rtc_switch_ticks < RTC_KEY_COMBO_DELAY_TICKS)
                {
                    pending_rtc_switch_ticks++;
                }
                if (pending_rtc_switch_ticks >= RTC_KEY_COMBO_DELAY_TICKS)
                {
                    deferred_switches = pending_rtc_switch_events;
                    pending_rtc_switch_events = 0;
                    pending_rtc_switch_ticks = 0;
                    if (RTC_HandleSwitchPresses(deferred_switches))
                    {
                        screen_changed = 1;
                    }
                }
            }
            else
            {
                pending_rtc_switch_ticks = 0;
            }

            if (switch_events & BOARD_SWITCH_1_LONG)
            {
                RTC_SaveCurrentTimeToEeprom();
                screen_changed = 1;
            }

            if (rtc_edit_active)
            {
                if (switch_events == 0 &&
                    rtc_edit_idle_timer < RTC_EDIT_TIMEOUT_TICKS)
                {
                    rtc_edit_idle_timer++;
                }
                if (rtc_edit_idle_timer >= RTC_EDIT_TIMEOUT_TICKS)
                {
                    rtc_edit_active = 0;
                    screen_changed = 1;
                }
                if (rtc_edit_active)
                {
                    lcd_page = LCD_PAGE_RTC;
                    screen_switch_timer = 0;
                }
            }
        }

        if (rs485_info_active)
        {
            if (rs485_info_page_ticks < RS485_INFO_PAGE_TICKS)
            {
                rs485_info_page_ticks++;
            }
            if (rs485_info_page_ticks >= RS485_INFO_PAGE_TICKS)
            {
                rs485_info_page_ticks = 0;
                if (rs485_info_port == 1)
                {
                    rs485_info_port = 2;
                    screen_changed = 1;
                }
                else
                {
                    rs485_info_active = 0;
                    screen_changed = 1;
                    if (lcd_page == LCD_PAGE_METER)
                    {
                        lcd_values_initialized = 0;
                    }
                }
            }
        }

        if (rtc_save_message_ticks != 0)
        {
            rtc_save_message_ticks--;
            if (rtc_save_message_ticks == 0)
            {
                rtc_save_message = 0;
                screen_changed = 1;
            }
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
            LCD_ApplyBacklight();
            backlight_applied = (back_light_on == BACK_LIGHT_ON) ? 1 : 0;
        }

        if (!rs485_info_active && !rtc_edit_active &&
            screen_switch_timer >= 2999)
        {
            screen_switch_timer = 0;
            lcd_page++;
            if (lcd_page >= LCD_PAGE_COUNT)
            {
                lcd_page = LCD_PAGE_COMPANY;
            }
            screen_changed = 1;
        }
        else if (!rs485_info_active && !rtc_edit_active)
        {
            screen_switch_timer++;
        }

        if (rs485_info_active)
        {
            if (screen_changed)
            {
                LCD_ShowRs485Settings(rs485_info_port);
            }
        }
        else if (screen_changed ||
            (lcd_page == LCD_PAGE_RTC && (rtc_refresh_due || switch_events)))
        {
            LCD_ShowPage(lcd_page);
        }
        else if (lcd_page == LCD_PAGE_METER)
        {
            LCD_ShowSlaveRegisters();
        }
        Board_DelayMs(1);
    }
}
