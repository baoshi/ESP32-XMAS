#pragma GCC optimize ("O2")
/*
 * LCD driver for ILI9163 160x128 LCD
 * Based on sprite_tm's ESP31 SMSEMU, see below
 */

/*
 * ESPRSSIF MIT License
 *
 * Copyright (c) 2015 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/xtensa_api.h"
#include "esp_common.h"
#include "gpio.h"
#include "spi_register.h"

// #define TIMING_DEBUG
#define FPS_COUNTER

#ifdef TIMING_DEBUG
#define LCD_TEST 23
#endif

#ifdef FPS_COUNTER
#define LCD_FPS 22
#endif

#define LCD_A0  17
#define LCD_RST 18
#define LCD_CS  19
#define LCD_CLK 20
#define LCD_SDI 21

#define SPIDEV 3    // SPI Device 3

static uint8 *lcdFrameBuffer = 0;
static uint8 *lcdFramePtr = 0;

static int lcdDone = 0;

static uint32_t lcdData[16]; //can contain (16*32/9=)56 9-bit data words.

static int lcdXpos = 0;
static int lcdYpos = 0;

static int lcdDataPos = 0;


static void lcdSpiSend(int isCmd)
{
    int x = 0;

    if (lcdDataPos == 0)
        return;

    while (READ_PERI_REG(SPI_CMD(SPIDEV)) & SPI_USR)
        ;

    if (isCmd)
    {
        GPIO_REG_WRITE(GPIO_OUT_W1TC, (1 << LCD_A0));
    }
    else
    {
        GPIO_REG_WRITE(GPIO_OUT_W1TS, (1 << LCD_A0));
    }

    for (x = 0; x < 16; x++)
    {
        WRITE_PERI_REG(SPI_W0(SPIDEV) + (x * 4), lcdData[x]);  // It seems SPI has 16 32-bit buffer named SPI_W0 ~ SPI_W15
        lcdData[x] = 0;
    }
    WRITE_PERI_REG(SPI_USER1(SPIDEV), (((lcdDataPos * 8) - 1) << SPI_USR_MOSI_BITLEN_S));

    WRITE_PERI_REG(SPI_CMD(SPIDEV), SPI_USR);
    lcdDataPos = 0;
}


static void lcdSpiWrite(int data)
{
    // This fill data into the SPI buffer only
    int bytePos = lcdDataPos / 4;
    switch (lcdDataPos & 3)
    {
    case 0:
        lcdData[bytePos] |= data << 24;
        break;
    case 1:
        lcdData[bytePos] |= data << 16;
        break;
    case 2:
        lcdData[bytePos] |= data << 8;
        break;
    case 3:
        lcdData[bytePos] |= data;
        break;
    }
    ++lcdDataPos;
}


static void SPI_WriteCMD(int cmd)
{
    if (lcdDataPos != 0) lcdSpiSend(0); // flush previous non-command data
    lcdSpiWrite(cmd);
    lcdSpiSend(1);
}


static void SPI_WriteDAT(int dat)
{
    lcdSpiWrite(dat & 0xff);
}



// 1000 ticks in-between every 32-bytes SPI data. Is just enough for our LCD controller
#define SENDTICKS 1000

void lcdPumpPixels()
{
#ifdef TIMING_DEBUG
    GPIO_REG_WRITE(GPIO_OUT_W1TS, (1 << LCD_TEST));
#endif
    if (lcdYpos == 160)
    {
        // printf("Disp Done\n");
#ifdef FPS_COUNTER
        GPIO_REG_WRITE(GPIO_OUT_W1TC, (1 << LCD_FPS));
#endif
        // ack int
        xthal_set_ccompare(1, xthal_get_ccount() - 1);
        // disable int
        xt_ints_off(1 << XCHAL_TIMER_INTERRUPT(1));
        lcdDone = 1;
        return;
    }
    do
    {
        SPI_WriteDAT(*lcdFramePtr);
        ++lcdFramePtr;
        SPI_WriteDAT(*lcdFramePtr);
        ++lcdFramePtr;
        ++lcdXpos;
    } while (lcdXpos & 31);

#ifdef TIMING_DEBUG
    GPIO_REG_WRITE(GPIO_OUT_W1TC, (1 << LCD_TEST));
#endif

    if (READ_PERI_REG(SPI_CMD(SPIDEV)) & SPI_USR)
    {
        printf("SPI not done!\n");
        lcdXpos = 0;
        lcdYpos = 160; // End frame, this one is probably borked anyway.
        return;
    }
    lcdSpiSend(0);

    if (lcdXpos >= 128)
    {
        lcdXpos = 0;
        ++lcdYpos;
    }

    xthal_set_ccompare(1, xthal_get_ccount() + SENDTICKS);
    xt_ints_on(1 << XCHAL_TIMER_INTERRUPT(1));

}


void lcdWriteFrame()
{
    int index;
    int col;
    int x, y;
    if (!lcdDone)
    {
        printf("LCD not done yet. Skipping frame.\n");
        return;
    }

    SPI_WriteCMD(0x2a); // Column address set
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x9f); // 0 - 159
    SPI_WriteCMD(0x2b); // Page address set
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x7f); // 0 - 127
    SPI_WriteCMD(0x2c); // Memory write
    lcdFramePtr = lcdFrameBuffer;
    lcdXpos = 0;
    lcdYpos = 0;
    lcdDataPos = 0;
    lcdDone = 0;
#ifdef FPS_COUNTER
    GPIO_REG_WRITE(GPIO_OUT_W1TS, (1 << LCD_FPS));
#endif
    lcdPumpPixels();
}


void lcdInit(uint8* buffer)
{
    GPIO_ConfigTypeDef GConf;

    printf("LCD init\n");

    lcdFrameBuffer = buffer;

    for (int i = 0; i < 40960; ++i)
        lcdFrameBuffer[i] = 0;

    // Config GPIO pins
    GConf.GPIO_Pin = (1 << LCD_RST) | (1 << LCD_A0);
#ifdef TIMING_DEBUG
    GConf.GPIO_Pin |= (1 << LCD_TEST);
#endif
#ifdef FPS_COUNTER
    GConf.GPIO_Pin |= (1 << LCD_FPS);
#endif
    GConf.GPIO_Pin_high = 0;
    GConf.GPIO_Mode = GPIO_Mode_Output;
    GConf.GPIO_Pullup = GPIO_PullUp_DIS;
    GConf.GPIO_Pulldown = GPIO_PullDown_DIS;
    GConf.GPIO_IntrType = GPIO_PIN_INTR_DISABLE;
    gpio_config(&GConf);

    // SPI clk = 13.3MHz (80 / 6)
    WRITE_PERI_REG(SPI_CLOCK(SPIDEV), (0 << SPI_CLKDIV_PRE_S) | (5 << SPI_CLKCNT_N_S) | (3 << SPI_CLKCNT_L_S) | (0 << SPI_CLKCNT_H_S));
    WRITE_PERI_REG(SPI_CTRL(SPIDEV), 0);
    WRITE_PERI_REG(SPI_USER(SPIDEV), SPI_CS_SETUP | SPI_CS_HOLD | SPI_USR_MOSI | SPI_WR_BYTE_ORDER);
    WRITE_PERI_REG(SPI_USER1(SPIDEV), (9 << SPI_USR_MOSI_BITLEN_S));

    // Route SPI to pins
    // LCD_CS  VSPICS0 19
    // LCD_CLK VSPICLK 20
    // LCD_SDI VSPID   21
    WRITE_PERI_REG(GPIO_ENABLE, 0xfffffff);
    SET_PERI_REG_BITS(GPIO_FUNC_OUT_SEL4, GPIO_GPIO_FUNC19_OUT_SEL, VSPICS0_OUT_IDX, GPIO_GPIO_FUNC19_OUT_SEL_S);
    SET_PERI_REG_BITS(PERIPHS_IO_MUX_GPIO19_U, MCU_SEL, 0, MCU_SEL_S);
    SET_PERI_REG_BITS(GPIO_FUNC_OUT_SEL5, GPIO_GPIO_FUNC20_OUT_SEL, VSPICLK_OUT_MUX_IDX, GPIO_GPIO_FUNC20_OUT_SEL_S);
    SET_PERI_REG_BITS(PERIPHS_IO_MUX_GPIO20_U, MCU_SEL, 0, MCU_SEL_S);
    SET_PERI_REG_BITS(GPIO_FUNC_OUT_SEL5, GPIO_GPIO_FUNC21_OUT_SEL, VSPID_OUT_IDX, GPIO_GPIO_FUNC21_OUT_SEL_S);
    SET_PERI_REG_BITS(PERIPHS_IO_MUX_GPIO21_U, MCU_SEL, 0, MCU_SEL_S);

    lcdSpiWrite(0); // dummy
    lcdSpiSend(0);

    // Reset
    vTaskDelay(1);
    GPIO_REG_WRITE(GPIO_OUT_W1TC, (1 << LCD_RST));
    vTaskDelay(15);
    GPIO_REG_WRITE(GPIO_OUT_W1TS, (1 << LCD_RST));
    vTaskDelay(1);

    SPI_WriteCMD(0x11);     // Sleep Out
    vTaskDelay(1);

    SPI_WriteCMD(0x3a);     // Interface Pixel Format
    SPI_WriteDAT(0x05);     // Control Interface 16 bit/pixel

    SPI_WriteCMD(0x26);     // Gamma Set
    SPI_WriteDAT(0x04);     // Gamma Curve 3

    SPI_WriteCMD(0xe0);     // Positive Gamma Correction Setting
    SPI_WriteDAT(0x3f);
    SPI_WriteDAT(0x25);
    SPI_WriteDAT(0x1c);
    SPI_WriteDAT(0x1e);
    SPI_WriteDAT(0x20);
    SPI_WriteDAT(0x12);
    SPI_WriteDAT(0x2a);
    SPI_WriteDAT(0x90);
    SPI_WriteDAT(0x24);
    SPI_WriteDAT(0x11);
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);

    SPI_WriteCMD(0xe1);     // Positive Gamma Correction Setting
    SPI_WriteDAT(0x20);
    SPI_WriteDAT(0x20);
    SPI_WriteDAT(0x20);
    SPI_WriteDAT(0x20);
    SPI_WriteDAT(0x05);
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x15);
    SPI_WriteDAT(0xa7);
    SPI_WriteDAT(0x3d);
    SPI_WriteDAT(0x18);
    SPI_WriteDAT(0x25);
    SPI_WriteDAT(0x2a);
    SPI_WriteDAT(0x2b);
    SPI_WriteDAT(0x2b);
    SPI_WriteDAT(0x3a);

    SPI_WriteCMD(0xb1);    // Frame Rate Control
    SPI_WriteDAT(0x08); 
    SPI_WriteDAT(0x08);

    SPI_WriteCMD(0xb4);    // Display Inversion Control 
    SPI_WriteDAT(0x07);    // NLA, NLB, NLC

    SPI_WriteCMD(0xc0);    // Power Control 1
    SPI_WriteDAT(0x0a);    // GVDD = 4.3
    SPI_WriteDAT(0x02);    // VCI1 = 2.7  

    SPI_WriteCMD(0xc1);    // Power Control 1
    SPI_WriteDAT(0x02);

    SPI_WriteCMD(0xc5);    // VCOM Control 1 
    SPI_WriteDAT(0x4f);
    SPI_WriteDAT(0x5a);

    SPI_WriteCMD(0xc7);    // VCOM Offset Control
    SPI_WriteDAT(0x40);

    SPI_WriteCMD(0x2a);    // Column Address Set 
    SPI_WriteDAT(0x00);    // XStart = 0
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);    // XEnd = 159
    SPI_WriteDAT(0x9f);

    SPI_WriteCMD(0x2b);    // Page Address Set 
    SPI_WriteDAT(0x00);    // YStart = 0
    SPI_WriteDAT(0x00);
    SPI_WriteDAT(0x00);    // YEnd = 127
    SPI_WriteDAT(0x7f);
    
    SPI_WriteCMD(0x36);    // Memory Access Control
    SPI_WriteDAT(0x60);    // MY, MX
    
    SPI_WriteCMD(0xb7);    // Source Driver Direction Control
    SPI_WriteDAT(0x00);    // GM[2:0] = 011, S7 -> S390 
    
    //  SPI_WriteCMD(0xb8);
    //  SPI_WriteDAT(0x01);

    SPI_WriteCMD(0x29);    // Display on

    //SPI_WriteCMD(0x2c);
    //lcdSpiSend(0);

    xt_set_interrupt_handler(XCHAL_TIMER_INTERRUPT(1), lcdPumpPixels, NULL);

    lcdDone = 1;
}

