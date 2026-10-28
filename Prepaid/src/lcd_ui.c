#include "lcd_ui.h"
#include "lcd_595.h"
#include "board_io.h"
#include "i2c_probe.h"
#include "rs485_test.h"

static uint8_t LCD_UI_WriteUnsignedLong(uint32_t value)
{
    uint8_t digits[10];
    uint8_t count = 0;
    uint8_t index;
    do
    {
        digits[count++] = (uint8_t)('0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0 && count < sizeof(digits));
    for (index = count; index != 0; index--) LCD_WriteChar((char)digits[index - 1]);
    return count;
}

static uint8_t LCD_UI_WriteSignedInteger(int16_t value)
{
    if (value < 0)
    {
        LCD_WriteChar('-');
        return (uint8_t)(1 + LCD_UI_WriteUnsignedLong((uint32_t)(-(int32_t)value)));
    }
    return LCD_UI_WriteUnsignedLong((uint32_t)value);
}

static void LCD_UI_PadLine(uint8_t row, uint8_t used)
{
    if (used >= 16) return;
    LCD_SetCursor(row, used);
    while (used++ < 16) LCD_WriteChar(' ');
}

void LCD_UI_Init(void) { LCD_Init(); }
void LCD_UI_ApplyBacklight(void) { LCD_ApplyBacklight(); }

void LCD_UI_ShowCompanyName(void)
{
    uint8_t index, character, length, start;
    uint16_t word;
    LCD_Command(0x0C);
    for (index = 0; index < 32; index++)
    {
        LCD_SetCursor((uint8_t)(index / 16), (uint8_t)(index % 16));
        LCD_WriteChar(' ');
    }
    length = (uint8_t)(COMPANY_NAME_REGISTER_COUNT * 2);
    while (length != 0)
    {
        index = (uint8_t)(length - 1);
        word = slave_holding_registers[COMPANY_NAME_FIRST_OFFSET + index / 2];
        character = (index & 1) ? (uint8_t)word : (uint8_t)(word >> 8);
        if (character != 0 && character != ' ' && character >= 32 && character <= 126) break;
        length--;
    }
    start = (uint8_t)((16 - length) / 2);
    for (index = 0; index < length; index++)
    {
        word = slave_holding_registers[COMPANY_NAME_FIRST_OFFSET + index / 2];
        character = (index & 1) ? (uint8_t)word : (uint8_t)(word >> 8);
        LCD_SetCursor(0, (uint8_t)(start + index));
        LCD_WriteChar((character >= 32 && character <= 126) ? (char)character : ' ');
    }
}

static void LCD_UI_ShowBalance(void)
{
    int16_t balance = (int16_t)slave_holding_registers[BALANCE_OFFSET];
    uint8_t used;
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Balance Amount");
    LCD_UI_PadLine(0, 14); LCD_SetCursor(1, 6);
    used = (uint8_t)(6 + LCD_UI_WriteSignedInteger(balance));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowLoadStatus(void)
{
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Load Status");
    LCD_UI_PadLine(0, 11); LCD_SetCursor(1, 0);
    if (slave_holding_registers[SLAVE_RELAY_COMMAND_OFFSET] != 0)
    { LCD_WriteText("Disconnected"); LCD_UI_PadLine(1, 12); }
    else { LCD_WriteText("Connected"); LCD_UI_PadLine(1, 9); }
}

static void LCD_UI_ShowRateScreen(uint8_t screen_number)
{
    uint16_t rate;
    uint8_t used;
    LCD_Command(0x0C);
    LCD_SetCursor(0, 0);
    switch (screen_number)
    {
        case LCD_UI_SCREEN_4_MAIN_KWH_RATE:
            LCD_WriteText("Mains kWh Rate");
            rate = slave_holding_registers[MAIN_KWH_RATE_OFFSET];
            break;
        case LCD_UI_SCREEN_5_MAIN_KVAH_RATE:
            LCD_WriteText("Mains kVAh Rate");
            rate = slave_holding_registers[MAIN_KVAH_RATE_OFFSET];
            break;
        case LCD_UI_SCREEN_6_DG_KWH_RATE:
            LCD_WriteText("DG kWh Rate");
            rate = slave_holding_registers[DG_KWH_RATE_OFFSET];
            break;
        case LCD_UI_SCREEN_7_DG_KVAH_RATE:
            LCD_WriteText("DG kVAh Rate");
            rate = slave_holding_registers[DG_KVAH_RATE_OFFSET];
            break;
        case LCD_UI_SCREEN_8_SOLAR_KWH_RATE:
            LCD_WriteText("Solar kWh Rate");
            rate = slave_holding_registers[SOLAR_KWH_RATE_OFFSET];
            break;
        default:
            LCD_WriteText("Solar kVAh Rate");
            rate = slave_holding_registers[SOLAR_KVAH_RATE_OFFSET];
            break;
    }
    LCD_UI_PadLine(0, 16); LCD_SetCursor(1, 0); LCD_WriteText("Rate: ");
    used = (uint8_t)(6 + LCD_UI_WriteUnsignedLong(rate));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowBillingParameter(void)
{
    uint16_t parameter;
    parameter = slave_holding_registers[BILLING_PARAMETER_OFFSET];
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Billing Param");
    LCD_UI_PadLine(0, 13); LCD_SetCursor(1, 0);
    switch (parameter)
    {
        case BILLING_MODE_KWH:  LCD_WriteText("0: kWh"); break;
        case BILLING_MODE_KVAH: LCD_WriteText("1: kVAh"); break;
        case BILLING_MODE_PF:   LCD_WriteText("2: PF"); break;
        case BILLING_MODE_KW:   LCD_WriteText("3: kW"); break;
        case BILLING_MODE_KVA:  LCD_WriteText("4: kVA"); break;
        case BILLING_MODE_KVARH: LCD_WriteText("5: kVArh"); break;
        default: LCD_WriteText("Invalid value"); break;
    }
    LCD_UI_PadLine(1, 13);
}

static void LCD_UI_ShowLowBalanceAlert(void)
{
    uint8_t used;
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Low Balance");
    LCD_UI_PadLine(0, 11); LCD_SetCursor(1, 0); LCD_WriteText("Alert: ");
    used = (uint8_t)(7 + LCD_UI_WriteUnsignedLong(
        slave_holding_registers[LOW_BALANCE_ALERT_OFFSET]));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowLowBalanceThreshold(void)
{
    uint8_t used;
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Low Balance");
    LCD_UI_PadLine(0, 11); LCD_SetCursor(1, 0); LCD_WriteText("Threshold:");
    used = (uint8_t)(10 + LCD_UI_WriteUnsignedLong(slave_holding_registers[LOW_BALANCE_THRESHOLD_OFFSET]));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowDisconnectLimit(void)
{
    uint16_t value;
    uint8_t used;
    if (slave_holding_registers[DISCONNECT_MODE_OFFSET] == 1)
    { value = slave_holding_registers[LOAD_LIMIT_OFFSET]; }
    else { value = slave_holding_registers[TIME_LIMIT_OFFSET]; }
    LCD_Command(0x0C); LCD_SetCursor(0, 0);
    LCD_WriteText(slave_holding_registers[DISCONNECT_MODE_OFFSET] == 1 ? "Max Load" : "Max Time");
    LCD_UI_PadLine(0, 8); LCD_SetCursor(1, 0); LCD_WriteText("Value: ");
    used = (uint8_t)(7 + LCD_UI_WriteUnsignedLong(value));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowTamperEvent(void)
{
    uint8_t used;
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Tamper Event");
    LCD_UI_PadLine(0, 12); LCD_SetCursor(1, 0); LCD_WriteText("Value: ");
    used = (uint8_t)(7 + LCD_UI_WriteUnsignedLong(slave_holding_registers[TAMPER_EVENT_OFFSET]));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowBmsOverride(void)
{
    uint16_t value = slave_holding_registers[BMS_RELAY_OVERRIDE_OFFSET];
    uint8_t used;
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("BMS Relay Ovr");
    LCD_UI_PadLine(0, 13); LCD_SetCursor(1, 0); LCD_WriteText("Value: ");
    used = (uint8_t)(7 + LCD_UI_WriteUnsignedLong(value));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowConsumptionFlag(void)
{
    uint16_t value =
        slave_holding_registers[CONSUMPTION_AFTER_DISCONNECT_OFFSET];
    uint8_t used;
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Consumption Flag");
    LCD_UI_PadLine(0, 16); LCD_SetCursor(1, 0); LCD_WriteText("Value: ");
    used = (uint8_t)(7 + LCD_UI_WriteUnsignedLong(value));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowLastRecharge(void)
{
    uint8_t used;
    int16_t amount;
    amount = (int16_t)slave_holding_registers[LAST_RECHARGE_OFFSET];
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Last Recharge");
    LCD_UI_PadLine(0, 13); LCD_SetCursor(1, 0); LCD_WriteText("Amount: ");
    used = (uint8_t)(8 + LCD_UI_WriteSignedInteger(amount));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowTotalDeducted(void)
{
    uint8_t used;
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Total Deducted");
    LCD_UI_PadLine(0, 14); LCD_SetCursor(1, 0); LCD_WriteText("Amount: ");
    used = (uint8_t)(8 + LCD_UI_WriteUnsignedLong(
        slave_holding_registers[TOTAL_DEDUCTED_OFFSET]));
    LCD_UI_PadLine(1, used);
}

static void LCD_UI_ShowTotalAmount(void)
{
    uint8_t used;
    int16_t amount;
    amount = (int16_t)slave_holding_registers[TOTAL_AMOUNT_OFFSET];
    LCD_Command(0x0C); LCD_SetCursor(0, 0); LCD_WriteText("Total Amount");
    LCD_UI_PadLine(0, 12); LCD_SetCursor(1, 0); LCD_WriteText("Amount: ");
    used = (uint8_t)(8 + LCD_UI_WriteSignedInteger(amount));
    LCD_UI_PadLine(1, used);
}

void LCD_UI_ShowCustomerScreen(uint8_t screen_number)
{
    switch (screen_number)
    {
        case LCD_UI_SCREEN_1_CUSTOMER_ID: LCD_UI_ShowCompanyName(); break;
        case LCD_UI_SCREEN_2_BALANCE: LCD_UI_ShowBalance(); break;
        case LCD_UI_SCREEN_3_LOAD_STATUS: LCD_UI_ShowLoadStatus(); break;
        case LCD_UI_SCREEN_4_MAIN_KWH_RATE:
        case LCD_UI_SCREEN_5_MAIN_KVAH_RATE:
        case LCD_UI_SCREEN_6_DG_KWH_RATE:
        case LCD_UI_SCREEN_7_DG_KVAH_RATE:
        case LCD_UI_SCREEN_8_SOLAR_KWH_RATE:
        case LCD_UI_SCREEN_9_SOLAR_KVAH_RATE:
            LCD_UI_ShowRateScreen(screen_number); break;
        case LCD_UI_SCREEN_10_LOW_BALANCE_ALERT:
            LCD_UI_ShowLowBalanceAlert(); break;
        case LCD_UI_SCREEN_11_LOW_BALANCE_LIMIT:
            LCD_UI_ShowLowBalanceThreshold(); break;
        case LCD_UI_SCREEN_12_DISCONNECT_LIMIT:
            LCD_UI_ShowDisconnectLimit(); break;
        case LCD_UI_SCREEN_13_TAMPER_EVENT:
            LCD_UI_ShowTamperEvent(); break;
        case LCD_UI_SCREEN_14_BMS_OVERRIDE:
            LCD_UI_ShowBmsOverride(); break;
        case LCD_UI_SCREEN_15_BILLING_PARAMETER:
            LCD_UI_ShowBillingParameter(); break;
        case LCD_UI_SCREEN_16_LAST_RECHARGE:
            LCD_UI_ShowLastRecharge(); break;
        case LCD_UI_SCREEN_17_TOTAL_AMOUNT:
            LCD_UI_ShowTotalAmount(); break;
        case LCD_UI_SCREEN_18_TOTAL_DEDUCTED:
            LCD_UI_ShowTotalDeducted(); break;
        default:
            LCD_UI_ShowConsumptionFlag(); break;
    }
}

uint8_t LCD_UI_NextCustomerScreen(uint8_t current_screen)
{
    uint8_t next = current_screen;
    do
    {
        next++;
        if (next > LCD_UI_SCREEN_COUNT) next = LCD_UI_SCREEN_1_CUSTOMER_ID;
    } while (next == LCD_UI_SCREEN_10_LOW_BALANCE_ALERT &&
        slave_holding_registers[LOW_BALANCE_ALERT_OFFSET] != 1);
    return next;
}

void LCD_UI_ShowRtcEdit(uint8_t digit_index)
{
    uint8_t row, column, time_digit;
    LCD_Command(0x0C);
    LCD_SetCursor(0, 0);
    if (!rtc_datetime_valid)
    {
        LCD_WriteText("RTC READ FAILED ");
        LCD_SetCursor(1, 0); LCD_WriteText("CHECK RTC/I2C   ");
        return;
    }
    LCD_WriteText("DATE: ");
    LCD_WriteChar((char)('0' + (rtc_datetime.day / 10)));
    LCD_WriteChar((char)('0' + (rtc_datetime.day % 10))); LCD_WriteChar(':');
    LCD_WriteChar((char)('0' + (rtc_datetime.month / 10)));
    LCD_WriteChar((char)('0' + (rtc_datetime.month % 10))); LCD_WriteChar(':');
    LCD_WriteChar((char)('0' + ((rtc_datetime.year / 1000) % 10)));
    LCD_WriteChar((char)('0' + ((rtc_datetime.year / 100) % 10)));
    LCD_WriteChar((char)('0' + ((rtc_datetime.year / 10) % 10)));
    LCD_WriteChar((char)('0' + (rtc_datetime.year % 10)));
    LCD_SetCursor(1, 0); LCD_WriteText("TIME: ");
    LCD_WriteChar((char)('0' + (rtc_datetime.hours / 10)));
    LCD_WriteChar((char)('0' + (rtc_datetime.hours % 10))); LCD_WriteChar(':');
    LCD_WriteChar((char)('0' + (rtc_datetime.minutes / 10)));
    LCD_WriteChar((char)('0' + (rtc_datetime.minutes % 10))); LCD_WriteChar(':');
    LCD_WriteChar((char)('0' + (rtc_datetime.seconds / 10)));
    LCD_WriteChar((char)('0' + (rtc_datetime.seconds % 10)));
    if (digit_index < 8)
    {
        row = 0;
        if (digit_index < 2) column = (uint8_t)(6 + digit_index);
        else if (digit_index < 4) column = (uint8_t)(7 + digit_index);
        else column = (uint8_t)(8 + digit_index);
    }
    else
    {
        row = 1; time_digit = (uint8_t)(digit_index - 8);
        if (time_digit < 2) column = (uint8_t)(6 + time_digit);
        else if (time_digit < 4) column = (uint8_t)(7 + time_digit);
        else column = (uint8_t)(8 + time_digit);
    }
    LCD_Command(0x0F); LCD_SetCursor(row, column);
}
