/**
 * Author: Karbon
 * GitHub: https://github.com/csy-tvgo
 * Date:   2022-Apr-14
 * Doc:    https://verimake.com/d/183
 */

#ifndef _EINK_C_
#define _EINK_C_

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <bl_gpio.h>
#include <bl_gpio_cli.h>
#include <hal_gpio.h>
#include <hosal_spi.h>

#include "imgs.c"

extern const char IMG_background[15000];
extern const char ascii[95][14];

////////////////////////////////////////////////////////////////////////////////
// 对外的接口
////////////////////////////////////////////////////////////////////////////////

void eInk_init();
void eInk_resetCanvas();
void eInk_show();
void eInk_drawPixel(int x, int y, uint8_t set_black);
void eInk_drawLine(int ax, int ay, int bx, int by, uint8_t set_black);                   // 只允许横线或者竖线
void eInk_drawRectangle(int ax, int ay, int bx, int by, uint8_t border_black, int fill); // fill: -1 (填白), 0 (不管), 1 (填黑)
void eInk_print(int left_top_x, int left_top_y, char *content);
void eInk_print_withbox(int left_top_x, int left_top_y, char *content);

////////////////////////////////////////////////////////////////////////////////
// 与墨水屏通信
////////////////////////////////////////////////////////////////////////////////

volatile static hosal_spi_dev_t spi;

// 如果用 BL604E_MB 开发板，引脚可配为这一组
volatile static const int PIN_BUSY = 6;
volatile static const int PIN_RES = 4;
volatile static const int PIN_DC = 2;
volatile static const int PIN_CS = 5;
volatile static const int PIN_SCK = 3;
volatile static const int PIN_SOI = 0;

// 如果用 BL-HWC-G1 开发板，引脚可配为这一组
// volatile static const int PIN_BUSY = 11;
// volatile static const int PIN_RES = 14;
// volatile static const int PIN_DC = 17;
// volatile static const int PIN_CS = 2;
// volatile static const int PIN_SCK = 3;
// volatile static const int PIN_SOI = 0;

void spi_master_cb(void *arg)
{
    // nothing
}

void eInk_SPI_init()
{
    uint32_t ret = -1;

    bl_gpio_enable_input(PIN_BUSY, 1, 0); // PULLUP
    bl_gpio_enable_output(PIN_RES, 0, 0);
    bl_gpio_enable_output(PIN_DC, 0, 0);
    bl_gpio_enable_output(PIN_CS, 0, 0);

    spi.port = 0;
    spi.config.mode = HOSAL_SPI_MODE_MASTER;
    spi.config.dma_enable = 0;
    spi.config.polar_phase = 0;
    spi.config.freq = 100000;
    spi.config.pin_clk = PIN_SCK;
    spi.config.pin_mosi = PIN_SOI;
    // spi.config.pin_miso = -1;
    ret = hosal_spi_init(&spi);
    hosal_spi_irq_callback_set(&spi, spi_master_cb, (void *)&spi);
    if (ret != 0)
    {
        printf("[eInk] SPI init error! error code:%d\r\n", ret);
    }
    else
    {
        printf("[eInk] SPI inited.\r\n");
    }
}

void EPD_W21_INIT()
{
    bl_gpio_output_set(PIN_CS, 0);
    bl_gpio_output_set(PIN_RES, 0);
    vTaskDelay(100);
    bl_gpio_output_set(PIN_RES, 1);
    vTaskDelay(100);
    printf("[eInk] EPD_W21_INIT finished.\r\n");
}

void check_busy(const char *temp)
{
    uint8_t send_buf[1] = {0x71};
    uint8_t value = -1;
    printf("[eInk] %s: Waiting...\r\n", temp);
    do
    {
        hosal_spi_send(&spi, (uint8_t *)send_buf, sizeof(send_buf), 1000);
        bl_gpio_input_get(PIN_BUSY, &value);
        // printf("[eInk] PIN_BUSY = %d after %s (0 means busy)\r\n", value, temp);
        vTaskDelay(100);
    } while (value != 1);
    printf("[eInk] %s: Ready.\r\n", temp);
}

uint32_t send_com(uint8_t command)
{
    uint32_t ret = -1;
    uint8_t send_buf[1] = {command};

    bl_gpio_output_set(PIN_DC, 0);

    bl_gpio_output_set(PIN_CS, 1);
    bl_gpio_output_set(PIN_CS, 0);
    ret = hosal_spi_send(&spi, (uint8_t *)send_buf, sizeof(send_buf), 1000);
    bl_gpio_output_set(PIN_CS, 1);
    if (ret != 0)
    {
        printf("[eInk] command send error! error code:%d\r\n", ret);
        return ret;
    }
    else
        return ret;
}

uint32_t send_data(uint8_t data)
{
    uint32_t ret = -1;
    uint8_t send_buf[1] = {data};

    bl_gpio_output_set(PIN_DC, 1);

    bl_gpio_output_set(PIN_CS, 1);
    bl_gpio_output_set(PIN_CS, 0);
    ret = hosal_spi_send(&spi, (uint8_t *)send_buf, sizeof(send_buf), 1000);
    bl_gpio_output_set(PIN_CS, 1);
    if (ret != 0)
    {
        printf("[eInk] data send error! error code:%d\r\n", ret);
        return ret;
    }
    else
        return ret;
}

void Display(char *pixel_new)
{
    volatile uint32_t i;

    // Reset the EPD driver IC
    EPD_W21_INIT();

    // Booster soft start
    send_com(0x06);
    send_data(0x17);
    send_data(0x17);
    send_data(0x17);

    // Power on
    send_com(0x04);

    check_busy("Power On");

    // Panel setting
    send_com(0x00);
    send_data(0x1f);

    // Data tx 1
    send_com(0x10);
    for (i = 0; i < 15000; i++)
    {
        send_data(0x00);
    }
    check_busy("Data TX 1");

    // Data tx 2
    send_com(0x13);
    for (i = 0; i < 15000; i++)
    {
        send_data(0xff - pixel_new[i]);
    }
    check_busy("Data TX 2");

    // Data tx over
    send_com(0x11);

    // Display refresh
    send_com(0x12);

    // Check BUSY
    check_busy("Refresh");

    // Enter LPM
    send_com(0x50);
    send_data(0x17);

    send_com(0x02);

    check_busy("Power off");

    send_com(0x07);
    send_data(0XA5);

    printf("[eInk] A graph has been sent to the eInk.\r\n");
}

////////////////////////////////////////////////////////////////////////////////
// 绘图
////////////////////////////////////////////////////////////////////////////////

volatile static char eInkPixel[15000];

void eInk_init()
{
    eInk_SPI_init();
    vTaskDelay(1000);
    EPD_W21_INIT();

    vTaskDelay(1000);

    eInk_resetCanvas();

    eInk_print_withbox(155, 140, "Loading...");

    eInk_show();
}

void eInk_resetCanvas()
{
    memcpy(eInkPixel, IMG_background, 15000);
}

void eInk_show()
{
    Display(eInkPixel);
}

void eInk_drawPixel(int x, int y, uint8_t set_black)
{
    // y ↓ x →
    if (set_black)
    {
        eInkPixel[y * 50 + (int)(x / 8)] |=
            0x80 >> (x % 8);
    }
    else
    {
        eInkPixel[y * 50 + (int)(x / 8)] &=
            ~(0x80 >> (x % 8));
    }
}

void eInk_drawLine(int ax, int ay, int bx, int by, uint8_t set_black) // 只允许横线或者竖线
{
    if (ax == bx)
    {
        int begin = ay <= by ? ay : by;
        int end = ay <= by ? by : ay;
        for (int y = begin; y <= end; y++)
        {
            eInk_drawPixel(ax, y, set_black);
        }
    }
    if (ay == by)
    {
        int begin = ax <= bx ? ax : bx;
        int end = ax <= bx ? bx : ax;
        for (int x = begin; x <= end; x++)
        {
            eInk_drawPixel(x, ay, set_black);
        }
    }
}

void eInk_drawRectangle(int ax, int ay, int bx, int by, uint8_t border_black, int fill)
{
    int begin_x = ax <= bx ? ax : bx;
    int begin_y = ay <= by ? ay : by;
    int end_x = ax <= bx ? bx : ax;
    int end_y = ay <= by ? by : ay;

    eInk_drawLine(begin_x, begin_y, end_x, begin_y, border_black);
    eInk_drawLine(begin_x, end_y, end_x, end_y, border_black);
    eInk_drawLine(begin_x, begin_y + 1, begin_x, end_y - 1, border_black);
    eInk_drawLine(end_x, begin_y + 1, end_x, end_y - 1, border_black);

    if (fill != 0)
    {
        fill = (fill + 1) / 2;
        for (int y = begin_y + 1; y <= end_y - 1; y++)
        {
            eInk_drawLine(begin_x + 1, y, end_x - 1, y, fill);
        }
    }
}

void eInk_print(int left_top_x, int left_top_y, char *content)
{
    for (int i = 0; i < strlen(content); i++)
    {
        for (int j = 0; j < 14; j++) // 字符大小 8*14
        {
            for (int k = 0; k < 8; k++)
            {
                int x = left_top_x + 8 * i + k;
                int y = left_top_y + j;
                if (ascii[content[i] - ' '][j] & (0x80 >> k))
                {
                    eInk_drawPixel(x, y, 1);
                }
            }
        }
    }
}

void eInk_print_withbox(int left_top_x, int left_top_y, char *content)
{
    char buf[20][50] = {0};
    int lines = 0;
    int longest = 0;

    int temp = 0;
    for (int i = 0; i < strlen(content); i++)
    {
        if (content[i] == '\n')
        {
            if (temp > longest)
            {
                longest = temp;
            }
            lines++;
            temp = 0;
        }
        else
        {
            buf[lines][temp] = content[i];
            temp++;
        }
    }

    lines++;
    if (temp > longest)
    {
        longest = temp;
    }

    eInk_drawRectangle(left_top_x, left_top_y,
                       left_top_x + 5 + 8 * longest + 5,
                       left_top_y + 3 + (14 + 2) * lines - 2 + 3,
                       1, -1);

    for (int i = 0; i < lines; i++)
    {
        eInk_print(left_top_x + 5, left_top_y + 3 + (14 + 2) * i, buf[i]);
    }
}

#endif
