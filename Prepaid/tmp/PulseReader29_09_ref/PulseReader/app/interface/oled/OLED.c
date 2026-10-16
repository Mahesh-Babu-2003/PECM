/*
 * OLED.c
 *
 *  Created on: Jan 21, 2026
 *      Author: vishnu
 */
#include "main.h"

#include "OLED.h"

extern I2C_HandleTypeDef hi2c1;

static void OLED_WriteCmd(uint8_t cmd)
{
    uint8_t data[2];
    data[0] = 0x00;   // Control byte for command
    data[1] = cmd;

    HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, data, 2, HAL_MAX_DELAY);
}

static void OLED_WriteData(uint8_t dataByte)
{
    uint8_t data[2];
    data[0] = 0x40;   // Control byte for data
    data[1] = dataByte;

    HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, data, 2, HAL_MAX_DELAY);
}

/* ================= OLED Initialization ================= */

void OLED_Init(void)
{
    HAL_Delay(50);

    OLED_WriteCmd(0xAE);   // Display OFF
    OLED_WriteCmd(0xA2);   // Bias
    OLED_WriteCmd(0xA1);   // ADC select
    OLED_WriteCmd(0xC8);   // COM scan direction
    OLED_WriteCmd(0x22);   // Voltage resistor ratio
    OLED_WriteCmd(0x2F);   // Power control
    OLED_WriteCmd(0x40);   // Start line
    OLED_WriteCmd(0xAF);   // Display ON
}

/* ================= Clear Display ================= */

void OLED_Clear(void)
{
    for(uint8_t page = 0; page < 8; page++)
    {
        OLED_WriteCmd(0xB0 | page);  // Page address
        OLED_WriteCmd(0x10);         // Column MSB
        OLED_WriteCmd(0x00);         // Column LSB

        for(uint8_t col = 0; col < 128; col++)
        {
            OLED_WriteData(0x00);
        }
    }
}

/* ================= Minimal 5x7 Font =================
 * Only characters required for "GME12864"
 */

//static const uint8_t Font5x7[][5] =
//{
//    {0x00,0x00,0x00,0x00,0x00}, // space
//    {0x3E,0x41,0x49,0x49,0x2E}, // G
//    {0x7F,0x02,0x04,0x02,0x7F}, // M
//    {0x7F,0x49,0x49,0x49,0x41}, // E
//    {0x36,0x49,0x49,0x49,0x36}, // 8
//    {0x3E,0x49,0x49,0x49,0x30}, // 6
//    {0x08,0x14,0x22,0x7F,0x20}, // 4
//    {0x00,0x42,0x7F,0x40,0x00}, // 1
//    {0x62,0x51,0x49,0x49,0x46}  // 2
//};

static const uint8_t Font5x7[][5] =
{
    /* 0 space */ {0x00,0x00,0x00,0x00,0x00},

    /* 1 P */ {0x7E,0x09,0x09,0x09,0x06},
    /* 2 o */ {0x38,0x44,0x44,0x44,0x38},
    /* 3 w */ {0x7C,0x20,0x18,0x20,0x7C},
    /* 4 e */ {0x38,0x54,0x54,0x54,0x18},
    /* 5 r */ {0x7C,0x08,0x04,0x04,0x08},
    /* 6 n */ {0x7C,0x08,0x04,0x04,0x78},

    /* 7 S */ {0x48,0x54,0x54,0x54,0x20},

    /* 8 l */ {0x00,0x44,0x7C,0x40,0x00},
    /* 9 u */ {0x3C,0x40,0x40,0x20,0x7C},
    /*10 t */ {0x04,0x3F,0x44,0x40,0x20},
    /*11 i */ {0x00,0x44,0x7D,0x40,0x00},
    /*12 s */ {0x20,0x54,0x54,0x54,0x08},
};

/* ================= Cursor Control ================= */

static void OLED_SetCursor(uint8_t page, uint8_t col)
{
    OLED_WriteCmd(0xB0 | page);             // Page (0–7)
    OLED_WriteCmd(0x10 | (col >> 4));       // Column high nibble
    OLED_WriteCmd(col & 0x0F);              // Column low nibble
}

/* ================= Font Index ================= */

//static uint8_t OLED_GetFontIndex(char c)
//{
//    switch(c)
//    {
//        case 'G': return 1;
//        case 'M': return 2;
//        case 'E': return 3;
//        case '8': return 4;
//        case '6': return 5;
//        case '4': return 6;
//        case '1': return 7;
//        case '2': return 8;
//        default:  return 0;
//    }
//}

static uint8_t OLED_GetFontIndex(char c)
{
    switch(c)
    {
        case ' ': return 0;
        case 'P': return 1;
        case 'o': return 2;
        case 'w': return 3;
        case 'e': return 4;
        case 'r': return 5;
        case 'n': return 6;
        case 'S': return 7;
        case 'l': return 8;
        case 'u': return 9;
        case 't': return 10;
        case 'i': return 11;
        case 's': return 7;
        case '0': return 1;
        case '1': return 2;
        case '3': return 3;
        case '4': return 4;


        default:  return 0;
    }
}

/* ================= Print String ================= */

void OLED_PrintString(uint8_t page, uint8_t col, char *str)
{
    while(*str)
    {
        uint8_t idx = OLED_GetFontIndex(*str++);

        OLED_SetCursor(page, col);

        for(uint8_t i = 0; i < 5; i++)
        {
            OLED_WriteData(Font5x7[idx][i]);
        }

        OLED_WriteData(0x00);  // space between chars
        col += 6;
    }
}


void OLED_ClearPage(uint8_t page)
{
    OLED_WriteCmd(0xB0 | page);  // Page address
    OLED_WriteCmd(0x10);         // Column high
    OLED_WriteCmd(0x00);         // Column low

    for (uint8_t i = 0; i < 128; i++)
    {
        OLED_WriteData(0x00);
    }
}

