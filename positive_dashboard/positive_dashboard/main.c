/**
 * Author: Karbon
 * GitHub: https://github.com/csy-tvgo
 * Date:   2022-Apr-14
 * Doc:    https://verimake.com/d/183
 */

#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <vfs.h>
#include <aos/kernel.h>
#include <aos/yloop.h>
#include <event_device.h>
#include <cli.h>

#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/tcp.h>
#include <lwip/err.h>
#include <http_client.h>
#include <netutils/netutils.h>

#include <bl602_glb.h>
#include <bl602_hbn.h>

#include <bl_uart.h>
#include <bl_chip.h>
#include <bl_wifi.h>
#include <hal_wifi.h>
#include <bl_sec.h>
#include <bl_cks.h>
#include <bl_irq.h>
#include <bl_dma.h>
#include <bl_timer.h>
#include <bl_gpio.h>
#include <bl_gpio_cli.h>
#include <bl_wdt_cli.h>
#include <hosal_uart.h>
#include <hosal_adc.h>
#include <hosal_spi.h>
#include <hosal_flash.h>
#include <hal_sys.h>
#include <hal_gpio.h>
#include <hal_boot2.h>
#include <hal_board.h>
#include <looprt.h>
#include <loopset.h>
#include <sntp.h>
#include <bl_sys_time.h>
#include <bl_sys.h>
#include <bl_sys_ota.h>
#include <bl_romfs.h>
#include <fdt.h>
#include <device/vfs_uart.h>
#include <easyflash.h>
#include <bl60x_fw_api.h>
#include <wifi_mgmr_ext.h>
#include <utils_log.h>
#include <libfdt.h>
#include <blog.h>

#include "wifi_utils.c"
#include "eInk.c"
#include "get_covid19_data.c"

// 声明 CLI 里可用的命令

static void cmd_stack_wifi(char *buf, int len, int argc, char **argv)
{
    /*wifi fw stack and thread stuff*/
    static uint8_t stack_wifi_init = 0;

    if (1 == stack_wifi_init)
    {
        puts("Wi-Fi Stack Started already!!!\r\n");
        return;
    }
    stack_wifi_init = 1;

    printf("Start Wi-Fi fw @%lums\r\n", bl_timer_now_us() / 1000);
    hal_wifi_start_firmware_task();
    /*Trigger to start Wi-Fi*/
    printf("Start Wi-Fi fw is Done @%lums\r\n", bl_timer_now_us() / 1000);
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);
}

static void cmd_memleft(char *buf, int len, int argc, char **argv)
{
    printf("Memory left is %d Bytes\r\n", xPortGetFreeHeapSize());
}

static void cmd_covid_download(char *buf, int len, int argc, char **argv)
{
    covid_download_http();
}

static void cmd_covid_printJSON(char *buf, int len, int argc, char **argv)
{
    covid_printJSON();
}

static void cmd_covid_query_test(char *buf, int len, int argc, char **argv)
{
    if (argc == 3)
    {
        covid_query_test(argv[1], argv[2]);
    }
    else if (argc == 2) // 如果只填一个参数，默认查当前确诊数
    {
        covid_query_test(argv[1], "econNum");
    }
    else
    {
        printf("Input error! Your input has %d arguments, please read the code.", argc);
        return;
    }
}

static void cmd_covid_somecities(char *buf, int len, int argc, char **argv)
{
    covid_somecities();
}

static void cmd_covid_display_somecities(char *buf, int len, int argc, char **argv)
{
    covid_display_somecities();
}

// 下边的结构体 cli_command 与函数 _cli_init() 用来配置 CLI 里的命令
// 这里的 CLI 似乎来自 AliOS Things

const static struct cli_command cmds_user[] STATIC_CLI_CMD_ATTRIBUTE = {
    {"stack_wifi", "Wi-Fi Stack", cmd_stack_wifi},
    {"covid_download", "Download COVID-19 data", cmd_covid_download},
    {"covid_printJSON", "Print COVID-19 raw JSON", cmd_covid_printJSON},
    {"covid_query", "Query COVID-19 data of one city", cmd_covid_query_test},
    {"covid_somecities", "Query data of some JZH cities", cmd_covid_somecities},
    {"covid_display_somecities", "Display data of some JZH cities", cmd_covid_display_somecities},
    {"memleft", "View the mem left.", cmd_memleft},
};

static void _cli_init()
{
    /*Put CLI which needs to be init here*/
}

// 下边是启动后一直运行的任务

static void query_loop(void *pvParameters)
{
    // WiFi 使用方式也可参考: https://lupyuen.github.io/articles/book#wifi-on-bl602
    // 与 LwIP 的文档: https://www.nongnu.org/lwip/2_1_x/group__httpc.html#gabd4ef2259885a93090733235cc0fa8d6

    easyflash_init();
    _cli_init();

    aos_register_event_filter(EV_WIFI, event_cb_wifi_event, NULL);

    cmd_stack_wifi(NULL, 0, 0, NULL);

    static char *ssid = "XXX";     // XXX: 请在这里填上自己 WiFi 的用户名和密码
    static char *password = "XXX"; // XXX: 请在这里填上自己 WiFi 的用户名和密码
    wifi_sta_connect(ssid, password);

    eInk_init();
    vTaskDelay(5000);
    printf("\r\n\r\n inited. \r\n\r\n");

    while (1)
    {
        covid_download_http();
        // covid_somecities();
        covid_display_somecities();

        vTaskDelay(600000);
    }
}

void main(void)
{
    bl_sys_init();

    xTaskCreate(query_loop, "query_loop", 1024, NULL, 15, NULL);

    tcpip_init(NULL, NULL);
}
