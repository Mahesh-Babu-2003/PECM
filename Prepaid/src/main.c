#include "ms51_device.h"
#include "sys.h"
#include "board_io.h"
#include "i2c_probe.h"
#include "lcd_595.h"
#include "rs485_test.h"

#define RS485_INFO_PAGE_TICKS 3000
#define LCD_PAGE_COMPANY 0
#define LCD_PAGE_BALANCE 1
#define LCD_PAGE_LOADS   2
#define LCD_PAGE_RATES   3
#define LCD_PAGE_METER   4
#define LCD_PAGE_RTC     5
#define LCD_PAGE_TAMPER  6
#define LCD_PAGE_BILLING 7
#define LCD_PAGE_COUNT   8
#define BILLING_PARAMETER_COUNT 16
#define RTC_SWITCH_TRIPLE_TAP_WINDOW_TICKS 2500
#define LOW_BALANCE_BEEP_REPEAT_HALF_SECOND_UNITS 60
#define LOW_BALANCE_BEEP_BURST_HALF_SECOND_UNITS 5

#define LOW_BALANCE_BUZZER_IDLE       0
#define LOW_BALANCE_BUZZER_ON         1
#define LOW_BALANCE_BUZZER_OFF        2
#define LOW_BALANCE_BUZZER_PAUSE      3

static uint16_t rtc_poll_timer;
static uint16_t screen_switch_timer;
static uint8_t lcd_values_initialized;
static uint8_t lcd_page;
static uint8_t backlight_applied;
static uint16_t backlight_idle_seconds;
static uint16_t backlight_idle_ms;
static uint16_t lcd_last_float_registers[4];
static uint8_t rs485_info_active;
static uint8_t rs485_info_port;
static uint16_t rs485_info_page_ticks;
static uint8_t low_balance_buzzer_state;
static uint8_t low_balance_beep_count;
static uint16_t low_balance_buzzer_timer;
static uint8_t billing_edit_active;
static uint8_t billing_parameter_index;
static uint16_t billing_recharge_entry;
static uint8_t billing_recharge_error;
static uint8_t rtc_view_locked;
static uint8_t rtc_view_tap_count;
static uint16_t rtc_view_tap_timer;
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

static uint16_t BillingParameterOffset(uint8_t index)
{
    switch (index)
    {
        case 0: return BALANCE_OFFSET;
        case 1: return MAIN_KWH_RATE_OFFSET;
        case 2: return MAIN_KVAH_RATE_OFFSET;
        case 3: return DG_KWH_RATE_OFFSET;
        case 4: return DG_KVAH_RATE_OFFSET;
        case 5: return SOLAR_KWH_RATE_OFFSET;
        case 6: return SOLAR_KVAH_RATE_OFFSET;
        case 7: return LOW_BALANCE_THRESHOLD_OFFSET;
        case 8: return BACKLIGHT_TIMEOUT_OFFSET;
        case 9: return MAIN_CONSUMPTION_OFFSET;
        case 10: return DG_CONSUMPTION_OFFSET;
        case 11: return SOLAR_CONSUMPTION_OFFSET;
        case 12: return BILLING_PARAMETER_OFFSET;
        case 13: return LOAD_LIMIT_OFFSET;
        case 14: return TIME_LIMIT_OFFSET;
        default: return DISCONNECT_MODE_OFFSET;
    }
}

static void Billing_AdjustSelectedParameter(uint8_t increase)
{
    uint16_t offset;
    uint16_t value;

    if (billing_parameter_index == 0)
    {
        if (increase)
        {
            if (billing_recharge_entry < 0xFFFF)
            {
                billing_recharge_entry++;
            }
        }
        else if (billing_recharge_entry != 0)
        {
            billing_recharge_entry--;
        }
        billing_recharge_error = 0;
        return;
    }

    offset = BillingParameterOffset(billing_parameter_index);
    value = RS485_GetBillingParameterValue(offset);
    if (offset == BILLING_PARAMETER_OFFSET)
    {
        value = increase ? (uint16_t)((value + 1) % 6) :
            (uint16_t)((value == 0) ? 5 : (value - 1));
        RS485_SetBillingParameter(offset, value);
        return;
    }
    if (offset == DISCONNECT_MODE_OFFSET)
    {
        value = (uint16_t)((value == 0) ? 1 : 0);
        RS485_SetBillingParameter(offset, value);
        return;
    }
    if (increase)
    {
        if (value < 0xFFFF)
        {
            value++;
        }
    }
    else if (value != 0)
    {
        value--;
    }
    RS485_SetBillingParameter(offset, value);
}

static uint8_t Billing_ApplyPendingRecharge(void)
{
    if (billing_recharge_entry == 0)
    {
        billing_recharge_error = 0;
        return 1;
    }
    if (RS485_SetBillingParameter(BALANCE_OFFSET, billing_recharge_entry))
    {
        billing_recharge_entry = 0;
        billing_recharge_error = 0;
        return 1;
    }
    billing_recharge_error = 1;
    return 0;
}

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
        registers[index] = s_energy_meter_registers[index];
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
    LCD_SetCursor(0, 0);
    if (!rtc_datetime_valid)
    {
        LCD_WriteText("RTC NOT SYNCED  ");
        LCD_SetCursor(1, 0);
        LCD_WriteText("SYNC FROM BMS   ");
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
    LCD_Command(0x0C);
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
        slave_holding_registers[MAIN_KWH_RATE_OFFSET]));
    LCD_WriteText(" DG:");
    used_columns = (uint8_t)(used_columns + 4 + LCD_WriteUnsignedLong(
        slave_holding_registers[DG_KWH_RATE_OFFSET]));
    LCD_PadLine(0, used_columns);
    LCD_SetCursor(1, 0);
    LCD_WriteText("SOLAR:");
    used_columns = (uint8_t)(6 + LCD_WriteUnsignedLong(
        slave_holding_registers[SOLAR_KWH_RATE_OFFSET]));
    LCD_PadLine(1, used_columns);
}

static void LCD_ShowTamperEvent(void)
{
    uint8_t used_columns;

    LCD_Command(0x0C);
    LCD_SetCursor(0, 0);
    LCD_WriteText("TAMPER COUNT");
    LCD_PadLine(0, 12);
    LCD_SetCursor(1, 0);
    LCD_WriteText("40028: ");
    used_columns = (uint8_t)(7 + LCD_WriteUnsignedLong(
        slave_holding_registers[TAMPER_EVENT_OFFSET]));
    LCD_PadLine(1, used_columns);
}

static void LCD_ShowBillingParameter(void)
{
    uint16_t offset;
    uint8_t used_columns;

    offset = BillingParameterOffset(billing_parameter_index);
    LCD_Command(0x0C);
    LCD_SetCursor(0, 0);
    LCD_WriteChar(billing_edit_active ? '*' : ' ');
    used_columns = 1;
    switch (billing_parameter_index)
    {
        case 0:
            if (billing_edit_active)
            {
                LCD_WriteText("ADD CREDIT");
                used_columns += 10;
            }
            else
            {
                LCD_WriteText("BALANCE");
                used_columns += 7;
            }
            break;
        case 1:
            LCD_WriteText("MAIN kWh RATE");
            used_columns += 13;
            break;
        case 2:
            LCD_WriteText("MAIN kVAh RATE");
            used_columns += 14;
            break;
        case 3:
            LCD_WriteText("DG kWh RATE");
            used_columns += 12;
            break;
        case 4:
            LCD_WriteText("DG kVAh RATE");
            used_columns += 13;
            break;
        case 5:
            LCD_WriteText("SOLAR kWh RATE");
            used_columns += 15;
            break;
        case 6:
            LCD_WriteText("SOLAR kVAh RATE");
            used_columns += 15;
            break;
        case 7:
            LCD_WriteText("LOW LIMIT");
            used_columns += 9;
            break;
        case 8:
            LCD_WriteText("BACKLIGHT SEC");
            used_columns += 13;
            break;
        case 9:
            LCD_WriteText("MAIN USE");
            used_columns += 8;
            break;
        case 10:
            LCD_WriteText("DG USE");
            used_columns += 6;
            break;
        case 11:
            LCD_WriteText("SOLAR USE");
            used_columns += 9;
            break;
        case 12:
        {
            uint16_t billing_code;

            billing_code = slave_holding_registers[BILLING_PARAMETER_OFFSET];
            if (billing_code > 5) billing_code = 0;
            switch (billing_code)
            {
                case 0: LCD_WriteText("BILL kWh"); break;
                case 1: LCD_WriteText("BILL kVAh"); break;
                case 2: LCD_WriteText("BILL PF"); break;
                case 3: LCD_WriteText("BILL kW"); break;
                case 4: LCD_WriteText("BILL kVA"); break;
                default: LCD_WriteText("BILL kVArh"); break;
            }
            used_columns = 12;
            break;
        }
        case 13:
            LCD_WriteText("LOAD LIMIT");
            used_columns += 10;
            break;
        case 14:
            LCD_WriteText("TIME LIMIT");
            used_columns += 10;
            break;
        default:
            LCD_WriteText("LIMIT MODE");
            used_columns += 10;
            break;
    }
    LCD_PadLine(0, used_columns);

    LCD_SetCursor(1, 0);
    if (billing_recharge_error)
    {
        LCD_WriteText("TOPUP OVER LIMIT");
        used_columns = 16;
    }
    else if (billing_edit_active && billing_parameter_index == 0)
    {
        LCD_WriteText("ADD=");
        used_columns = (uint8_t)(4 + LCD_WriteUnsignedLong(
            billing_recharge_entry));
    }
    else
    {
        LCD_WriteText("REG ");
        used_columns = (uint8_t)(4 + LCD_WriteUnsignedLong(40001UL + offset));
        LCD_WriteChar('=');
        used_columns++;
        if (billing_parameter_index == 0)
        {
            used_columns = (uint8_t)(used_columns + LCD_WriteSignedInteger(
                (int16_t)slave_holding_registers[offset]));
        }
        else
        {
            used_columns = (uint8_t)(used_columns + LCD_WriteUnsignedLong(
                RS485_GetBillingParameterValue(offset)));
        }
    }
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
        case LCD_PAGE_TAMPER:
            LCD_ShowTamperEvent();
            break;
        case LCD_PAGE_BILLING:
            LCD_ShowBillingParameter();
            break;
        default:
            LCD_ShowRtc();
            break;
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
    uint8_t rtc_refresh_due;
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
    rtc_view_locked = 0;
    rtc_view_tap_count = 0;
    rtc_view_tap_timer = 0;
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
        if (prepaid_events & PREPAID_UI_EVENT_TAMPER)
        {
            if (billing_edit_active)
            {
                Billing_ApplyPendingRecharge();
            }
            billing_edit_active = 0;
            lcd_page = LCD_PAGE_TAMPER;
            screen_changed = 1;
            Board_Beep(100);
        }
        else if (prepaid_events & PREPAID_UI_EVENT_BALANCE)
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
        if (billing_edit_active)
        {
            lcd_page = LCD_PAGE_BILLING;
        }
        if (screen_changed)
        {
            screen_switch_timer = 0;
        }
        if (rtc_poll_timer >= 999)
        {
            rtc_poll_timer = 0;
            RTC_ReadDateTime(&rtc_datetime);
            RS485_UpdateLiveRtcRegisters();
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

        if (rtc_view_tap_count != 0)
        {
            if (rtc_view_tap_timer < RTC_SWITCH_TRIPLE_TAP_WINDOW_TICKS)
            {
                rtc_view_tap_timer++;
            }
            if (rtc_view_tap_timer >= RTC_SWITCH_TRIPLE_TAP_WINDOW_TICKS)
            {
                rtc_view_tap_count = 0;
                rtc_view_tap_timer = 0;
            }
        }
        if (!rs485_info_active && !billing_edit_active)
        {
            if (!rtc_view_locked && lcd_page != LCD_PAGE_BILLING &&
                (switch_press_events & BOARD_SWITCH_1))
            {
                if (rtc_view_tap_count < 3)
                {
                    rtc_view_tap_count++;
                }
                rtc_view_tap_timer = 0;
                screen_switch_timer = 0;
                if (rtc_view_tap_count >= 3)
                {
                    rtc_view_locked = 1;
                    rtc_view_tap_count = 0;
                    rtc_view_tap_timer = 0;
                    lcd_page = LCD_PAGE_RTC;
                    screen_switch_timer = 0;
                    screen_changed = 1;
                }
            }
            else if (rtc_view_locked &&
                (switch_press_events & BOARD_SWITCH_2))
            {
                if (rtc_view_tap_count < 3)
                {
                    rtc_view_tap_count++;
                }
                rtc_view_tap_timer = 0;
                if (rtc_view_tap_count >= 3)
                {
                    rtc_view_locked = 0;
                    rtc_view_tap_count = 0;
                    rtc_view_tap_timer = 0;
                    screen_switch_timer = 0;
                    screen_changed = 1;
                }
            }
            else if (switch_press_events != 0)
            {
                rtc_view_tap_count = 0;
                rtc_view_tap_timer = 0;
            }
        }

        if (switch_events & BOARD_SWITCH_2_3_LONG)
        {
            if (billing_edit_active)
            {
                if (Billing_ApplyPendingRecharge())
                {
                    billing_edit_active = 0;
                    lcd_page = LCD_PAGE_BILLING;
                    screen_switch_timer = 0;
                }
                screen_changed = 1;
            }
            else
            {
                rs485_info_active = 1;
                rs485_info_port = 1;
                rs485_info_page_ticks = 0;
                screen_switch_timer = 0;
                screen_changed = 1;
            }
        }

        if (!rs485_info_active && billing_edit_active)
        {
            if (switch_events & BOARD_SWITCH_1_LONG)
            {
                if (Billing_ApplyPendingRecharge())
                {
                    billing_edit_active = 0;
                    lcd_page = LCD_PAGE_BILLING;
                    screen_switch_timer = 0;
                }
                screen_changed = 1;
            }
            else if (switch_press_events & BOARD_SWITCH_1)
            {
                billing_parameter_index++;
                if (billing_parameter_index >= BILLING_PARAMETER_COUNT)
                {
                    billing_parameter_index = 0;
                }
                screen_changed = 1;
            }
            else if (switch_press_events & BOARD_SWITCH_2)
            {
                Billing_AdjustSelectedParameter(0);
                screen_changed = 1;
            }
            else if (switch_press_events & BOARD_SWITCH_3)
            {
                Billing_AdjustSelectedParameter(1);
                screen_changed = 1;
            }
        }
        else if (!rs485_info_active)
        {
            if (switch_press_events & BOARD_SWITCH_1)
            {
                if (lcd_page == LCD_PAGE_BILLING)
                {
                    billing_edit_active = 1;
                    billing_parameter_index = 0;
                    lcd_page = LCD_PAGE_BILLING;
                    screen_switch_timer = 0;
                    screen_changed = 1;
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

        if (!rs485_info_active && !billing_edit_active && !rtc_view_locked &&
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
        else if (!rs485_info_active && !billing_edit_active &&
            !rtc_view_locked)
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
            (lcd_page == LCD_PAGE_RTC && rtc_refresh_due))
        {
            LCD_ShowPage(lcd_page);
        }
        else if (lcd_page == LCD_PAGE_METER)
        {
            LCD_ShowSlaveRegisters();
        }
        UpdateLowBalanceBuzzer();
        Board_DelayMs(1);
    }
}
