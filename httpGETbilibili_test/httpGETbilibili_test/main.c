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

#define mainHELLO_TASK_PRIORITY (20)
#define UART_ID_2 (2)
#define WIFI_AP_PSM_INFO_SSID "conf_ap_ssid"
#define WIFI_AP_PSM_INFO_PASSWORD "conf_ap_psk"
#define WIFI_AP_PSM_INFO_PMK "conf_ap_pmk"
#define WIFI_AP_PSM_INFO_BSSID "conf_ap_bssid"
#define WIFI_AP_PSM_INFO_CHANNEL "conf_ap_channel"
#define WIFI_AP_PSM_INFO_IP "conf_ap_ip"
#define WIFI_AP_PSM_INFO_MASK "conf_ap_mask"
#define WIFI_AP_PSM_INFO_GW "conf_ap_gw"
#define WIFI_AP_PSM_INFO_DNS1 "conf_ap_dns1"
#define WIFI_AP_PSM_INFO_DNS2 "conf_ap_dns2"
#define WIFI_AP_PSM_INFO_IP_LEASE_TIME "conf_ap_ip_lease_time"
#define WIFI_AP_PSM_INFO_GW_MAC "conf_ap_gw_mac"
#define CLI_CMD_AUTOSTART1 "cmd_auto1"
#define CLI_CMD_AUTOSTART2 "cmd_auto2"

static wifi_conf_t conf = {
    .country_code = "CN",
};

static wifi_interface_t wifi_interface;

static unsigned char char_to_hex(char asccode)
{
    unsigned char ret;

    if ('0' <= asccode && asccode <= '9')
        ret = asccode - '0';
    else if ('a' <= asccode && asccode <= 'f')
        ret = asccode - 'a' + 10;
    else if ('A' <= asccode && asccode <= 'F')
        ret = asccode - 'A' + 10;
    else
        ret = 0;

    return ret;
}

static void _chan_str_to_hex(uint8_t *chan_band, uint16_t *chan_freq, char *chan)
{
    int i, freq_len, base = 1;
    uint8_t band;
    uint16_t freq = 0;
    char *p, *q;

    /*should have the following format
     * 2412|0
     * */
    p = strchr(chan, '|') + 1;
    if (NULL == p)
    {
        return;
    }
    band = char_to_hex(p[0]);
    (*chan_band) = band;

    freq_len = strlen(chan) - strlen(p) - 1;
    q = chan;
    q[freq_len] = '\0';
    for (i = 0; i < freq_len; i++)
    {
        freq = freq + char_to_hex(q[freq_len - 1 - i]) * base;
        base = base * 10;
    }
    (*chan_freq) = freq;
}

static void bssid_str_to_mac(uint8_t *hex, char *bssid, int len)
{
    unsigned char l4, h4;
    int i, lenstr;
    lenstr = len;

    if (lenstr % 2)
    {
        lenstr -= (lenstr % 2);
    }

    if (lenstr == 0)
    {
        return;
    }

    for (i = 0; i < lenstr; i += 2)
    {
        h4 = char_to_hex(bssid[i]);
        l4 = char_to_hex(bssid[i + 1]);
        hex[i / 2] = (h4 << 4) + l4;
    }
}

int check_dts_config(char ssid[33], char password[64])
{
    bl_wifi_ap_info_t sta_info;

    if (bl_wifi_sta_info_get(&sta_info))
    {
        /*no valid sta info is got*/
        return -1;
    }

    strncpy(ssid, (const char *)sta_info.ssid, 32);
    ssid[31] = '\0';
    strncpy(password, (const char *)sta_info.psk, 64);
    password[63] = '\0';

    return 0;
}

static void _connect_wifi()
{
    /*XXX caution for BIG STACK*/
    char pmk[66], bssid[32], chan[10];
    char ssid[33], password[66];
    char val_buf[66];
    char val_len = sizeof(val_buf) - 1;
    uint8_t mac[6];
    uint8_t band = 0;
    uint16_t freq = 0;

    wifi_interface = wifi_mgmr_sta_enable();
    printf("[APP] [WIFI] [T] %lld\r\n"
           "[APP]   Get STA %p from Wi-Fi Mgmr, pmk ptr %p, ssid ptr %p, password %p\r\n",
           aos_now_ms(),
           wifi_interface,
           pmk,
           ssid,
           password);
    memset(pmk, 0, sizeof(pmk));
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    memset(bssid, 0, sizeof(bssid));
    memset(mac, 0, sizeof(mac));
    memset(chan, 0, sizeof(chan));

    memset(val_buf, 0, sizeof(val_buf));
    ef_get_env_blob((const char *)WIFI_AP_PSM_INFO_SSID, val_buf, val_len, NULL);
    if (val_buf[0])
    {
        /*We believe that when ssid is set, wifi_confi is OK*/
        strncpy(ssid, val_buf, sizeof(ssid) - 1);

        /*setup password ans PMK stuff from ENV*/
        memset(val_buf, 0, sizeof(val_buf));
        ef_get_env_blob((const char *)WIFI_AP_PSM_INFO_PASSWORD, val_buf, val_len, NULL);
        if (val_buf[0])
        {
            strncpy(password, val_buf, sizeof(password) - 1);
        }

        memset(val_buf, 0, sizeof(val_buf));
        ef_get_env_blob((const char *)WIFI_AP_PSM_INFO_PMK, val_buf, val_len, NULL);
        if (val_buf[0])
        {
            strncpy(pmk, val_buf, sizeof(pmk) - 1);
        }
        if (0 == pmk[0])
        {
            printf("[APP] [WIFI] [T] %lld\r\n",
                   aos_now_ms());
            puts("[APP]    Re-cal pmk\r\n");
            /*At lease pmk is not illegal, we re-cal now*/
            //XXX time consuming API, so consider lower-prirotiy for cal PSK to avoid sound glitch
            wifi_mgmr_psk_cal(
                password,
                ssid,
                strlen(ssid),
                pmk);
            ef_set_env(WIFI_AP_PSM_INFO_PMK, pmk);
            ef_save_env();
        }
        memset(val_buf, 0, sizeof(val_buf));
        ef_get_env_blob((const char *)WIFI_AP_PSM_INFO_CHANNEL, val_buf, val_len, NULL);
        if (val_buf[0])
        {
            strncpy(chan, val_buf, sizeof(chan) - 1);
            printf("connect wifi channel = %s\r\n", chan);
            _chan_str_to_hex(&band, &freq, chan);
        }
        memset(val_buf, 0, sizeof(val_buf));
        ef_get_env_blob((const char *)WIFI_AP_PSM_INFO_BSSID, val_buf, val_len, NULL);
        if (val_buf[0])
        {
            strncpy(bssid, val_buf, sizeof(bssid) - 1);
            printf("connect wifi bssid = %s\r\n", bssid);
            bssid_str_to_mac(mac, bssid, strlen(bssid));
            printf("mac = %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   mac[0],
                   mac[1],
                   mac[2],
                   mac[3],
                   mac[4],
                   mac[5]);
        }
    }
    else if (0 == check_dts_config(ssid, password))
    {
        /*nothing here*/
    }
    else
    {
        /*Won't connect, since ssid config is empty*/
        puts("[APP]    Empty Config\r\n");
        puts("[APP]    Try to set the following ENV with psm_set command, then reboot\r\n");
        puts("[APP]    NOTE: " WIFI_AP_PSM_INFO_PMK " MUST be psm_unset when conf is changed\r\n");
        puts("[APP]    env: " WIFI_AP_PSM_INFO_SSID "\r\n");
        puts("[APP]    env: " WIFI_AP_PSM_INFO_PASSWORD "\r\n");
        puts("[APP]    env(optinal): " WIFI_AP_PSM_INFO_PMK "\r\n");
        return;
    }

    printf("[APP] [WIFI] [T] %lld\r\n"
           "[APP]    SSID %s\r\n"
           "[APP]    SSID len %d\r\n"
           "[APP]    password %s\r\n"
           "[APP]    password len %d\r\n"
           "[APP]    pmk %s\r\n"
           "[APP]    bssid %s\r\n"
           "[APP]    channel band %d\r\n"
           "[APP]    channel freq %d\r\n",
           aos_now_ms(),
           ssid,
           strlen(ssid),
           password,
           strlen(password),
           pmk,
           bssid,
           band,
           freq);
    //wifi_mgmr_sta_connect(wifi_interface, ssid, pmk, NULL);
    wifi_mgmr_sta_connect(wifi_interface, ssid, password, pmk, mac, band, freq);
}

static void wifi_sta_connect(char *ssid, char *password)
{
    wifi_interface_t wifi_interface;

    wifi_interface = wifi_mgmr_sta_enable();
    wifi_mgmr_sta_connect(wifi_interface, ssid, password, NULL, NULL, 0, 0);
}

static void event_cb_wifi_event(input_event_t *event, void *private_data)
{
    static char *ssid;
    static char *password;

    switch (event->code)
    {
    case CODE_WIFI_ON_INIT_DONE:
    {
        printf("[APP] [EVT] INIT DONE %lld\r\n", aos_now_ms());
        wifi_mgmr_start_background(&conf);
    }
    break;
    case CODE_WIFI_ON_MGMR_DONE:
    {
        printf("[APP] [EVT] MGMR DONE %lld, now %lums\r\n", aos_now_ms(), bl_timer_now_us() / 1000);
        _connect_wifi();
    }
    break;
    case CODE_WIFI_ON_MGMR_DENOISE:
    {
        printf("[APP] [EVT] Microwave Denoise is ON %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_SCAN_DONE:
    {
        printf("[APP] [EVT] SCAN Done %lld\r\n", aos_now_ms());
        wifi_mgmr_cli_scanlist();
    }
    break;
    case CODE_WIFI_ON_SCAN_DONE_ONJOIN:
    {
        printf("[APP] [EVT] SCAN On Join %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_DISCONNECT:
    {
        printf("[APP] [EVT] disconnect %lld, Reason: %s\r\n",
               aos_now_ms(),
               wifi_mgmr_status_code_str(event->value));
    }
    break;
    case CODE_WIFI_ON_CONNECTING:
    {
        printf("[APP] [EVT] Connecting %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_CMD_RECONNECT:
    {
        printf("[APP] [EVT] Reconnect %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_CONNECTED:
    {
        printf("[APP] [EVT] connected %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_PRE_GOT_IP:
    {
        printf("[APP] [EVT] connected %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_GOT_IP:
    {
        printf("[APP] [EVT] GOT IP %lld\r\n", aos_now_ms());
        printf("[SYS] Memory left is %d Bytes\r\n", xPortGetFreeHeapSize());
    }
    break;
    case CODE_WIFI_ON_EMERGENCY_MAC:
    {
        printf("[APP] [EVT] EMERGENCY MAC %lld\r\n", aos_now_ms());
        hal_reboot(); //one way of handling emergency is reboot. Maybe we should also consider solutions
    }
    break;
    case CODE_WIFI_ON_PROV_SSID:
    {
        printf("[APP] [EVT] [PROV] [SSID] %lld: %s\r\n",
               aos_now_ms(),
               event->value ? (const char *)event->value : "UNKNOWN");
        if (ssid)
        {
            vPortFree(ssid);
            ssid = NULL;
        }
        ssid = (char *)event->value;
    }
    break;
    case CODE_WIFI_ON_PROV_BSSID:
    {
        printf("[APP] [EVT] [PROV] [BSSID] %lld: %s\r\n",
               aos_now_ms(),
               event->value ? (const char *)event->value : "UNKNOWN");
        if (event->value)
        {
            vPortFree((void *)event->value);
        }
    }
    break;
    case CODE_WIFI_ON_PROV_PASSWD:
    {
        printf("[APP] [EVT] [PROV] [PASSWD] %lld: %s\r\n", aos_now_ms(),
               event->value ? (const char *)event->value : "UNKNOWN");
        if (password)
        {
            vPortFree(password);
            password = NULL;
        }
        password = (char *)event->value;
    }
    break;
    case CODE_WIFI_ON_PROV_CONNECT:
    {
        printf("[APP] [EVT] [PROV] [CONNECT] %lld\r\n", aos_now_ms());
        printf("connecting to %s:%s...\r\n", ssid, password);
        wifi_sta_connect(ssid, password);
    }
    break;
    case CODE_WIFI_ON_PROV_DISCONNECT:
    {
        printf("[APP] [EVT] [PROV] [DISCONNECT] %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_AP_STA_ADD:
    {
        printf("[APP] [EVT] [AP] [ADD] %lld, sta idx is %lu\r\n", aos_now_ms(), (uint32_t)event->value);
    }
    break;
    case CODE_WIFI_ON_AP_STA_DEL:
    {
        printf("[APP] [EVT] [AP] [DEL] %lld, sta idx is %lu\r\n", aos_now_ms(), (uint32_t)event->value);
    }
    break;
    default:
    {
        printf("[APP] [EVT] Unknown code %u, %lld\r\n", event->code, aos_now_ms());
        /*nothing*/
    }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// 上边的代码是原封不动地从 bl602_demo_wifi 里复制来的
//
// 下边的代码是本示例里写的或者修改的代码
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define BUFFER_SIZE (12 * 1024)

static int http_request(const char *hostname, const uint16_t port, const char *request, char *response)
{
    // 发出 HTTP 请求，将返回结果存入 response
    // @param[in]  hostname 主机名
    // @param[in]  request  请求的报文
    // @param[out] response 返回的报文
    // @return 错误代码，为 0 表示运行成功

    /* Get host address from the input name */
    struct hostent *hostinfo = gethostbyname(hostname);
    if (!hostinfo)
    {
        printf("gethostbyname Failed\r\n");
        return -1;
    }

    /* Create a socket */
    /*---Open socket for streaming---*/
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("Error in socket\r\n");
        return -1;
    }

    /*---Initialize server address/port struct---*/
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr = *((struct in_addr *)hostinfo->h_addr);

    uint32_t address = dest.sin_addr.s_addr;
    char *ip = inet_ntoa(address);

    printf("Server ip Address : %s\r\n", ip);

    /*---Connect to server---*/
    if (connect(sockfd,
                (struct sockaddr *)&dest,
                sizeof(dest)) != 0)
    {
        printf("Error in connect\r\n");
        return -1;
    }

    // Start transceiving

    write(sockfd, request, strlen(request));

    int ret = 0;
    uint32_t ticks_0, ticks_1;
    double time_consumed;

    uint8_t *recv_buffer;
    recv_buffer = pvPortMalloc(BUFFER_SIZE);
    memset(recv_buffer, 0, BUFFER_SIZE);

    ticks_0 = xTaskGetTickCount();

    if (NULL == recv_buffer)
    {
        close(sockfd);
        return -1;
    }
    else
    {
        ticks_1 = xTaskGetTickCount();
        time_consumed = ((uint32_t)(((int32_t)ticks_1) - ((int32_t)ticks_0))) / 1000.0;
        printf("Start Time: %.4f s.\r\n", time_consumed);

        ret = read(sockfd, recv_buffer, BUFFER_SIZE);

        ticks_1 = xTaskGetTickCount();
        time_consumed = ((uint32_t)(((int32_t)ticks_1) - ((int32_t)ticks_0))) / 1000.0;
        printf("Finish Time: %.4f s, received length = %d.\r\n", time_consumed, ret);

        if (ret >= 0)
        {
            memcpy(response, recv_buffer, ret);
            printf("================Response================\r\n");
            printf("%s", response);
            printf("\r\n========================================\r\n");
        }
        else if (ret < 0)
        {
            printf("ret = %d, err = %d\n\r", ret, errno);
        }
        vPortFree(recv_buffer);
    }
    close(sockfd);
    return 0;
}

long http_bili_GET(char *uid, char *datetime)
{
    // 读取某位 bilibili 用户的粉丝数量
    // @param[in]  uid      用户的 uid
    // @param[out] datetime 返回结果的日期时间
    // @return 粉丝数量，-1 表示读取错误

    int error_code = 0;

    char *hostname_b = "api.bilibili.com"; // 哔哩哔哩的 API
    char request_b[128];
    sprintf(request_b,
            "GET /x/relation/stat?vmid=%s HTTP/1.1\r\n"
            "Host: api.bilibili.com\r\n\r\n",
            uid);
    char *response_b;
    response_b = pvPortMalloc(BUFFER_SIZE);
    memset(response_b, 0, BUFFER_SIZE);

    char *hostname_d = "apps.game.qq.com"; // 这个 API 用来获取北京时间
    char *request_d = "GET /CommArticle/app/reg/gdate.php HTTP/1.1\r\n"
                      "Host: apps.game.qq.com\r\n\r\n";
    char *response_d;
    response_d = pvPortMalloc(BUFFER_SIZE);
    memset(response_d, 0, BUFFER_SIZE);

    printf(">>>>>>>>>>>>Request bilibili<<<<<<<<<<<<\r\n");
    error_code += http_request(hostname_b, 80, request_b, response_b);
    printf("\r\n>>>>>>>>>>>>Request datetime<<<<<<<<<<<<\r\n");
    error_code += http_request(hostname_d, 80, request_d, response_d);

    if (error_code)
    {
        vPortFree(response_b);
        vPortFree(response_d);
        return -1;
    }
    else
    {
        long follower_num;

        // 处理日期时间
        char *datetime_L = strchr(response_d, '\'');
        if (NULL == datetime_L)
        {
            return -3;
        }
        strncpy(datetime, datetime_L + 1, 19);
        vPortFree(response_d);

        // 处理粉丝数
        char *follower_L = strstr(response_b, "follower");
        char *follower_R = strstr(response_b, "}}");
        if (NULL == follower_L)
        {
            return -2;
        }
        *follower_R = '\0';
        follower_num = atol(follower_L + 10);
        vPortFree(response_b);

        return follower_num;
    }
}

void bili_get_test(char *uid)
{
    char datetime[32];
    memset(datetime, 0, 32);
    long follower_num;
    follower_num = http_bili_GET(uid, datetime);
    strcat(datetime, " UTC+8");
    if (follower_num >= 0)
    {
        printf("uid=%s has %ld followers at %s\r\n", uid, follower_num, datetime);
    }
    else
    {
        printf("Read uid=%s error! (code:%d) Time:%s\r\n", uid, follower_num, datetime);
    }
}

// 下边是 CLI 里可用命令的实现

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

static void cmd_bili_get(char *buf, int len, int argc, char **argv)
{
    if (argc != 2) // 如果参数为空，则报错返回
    {
        printf("uid input error! Please input ONE uid, but your input has %d arguments.", argc);
        return;
    }
    else
    {
        bili_get_test(argv[1]);
    }
}

// 下边的结构体 cli_command 与函数 _cli_init() 用来配置 CLI 里的命令
// 这里的 CLI 似乎来自 AliOS Things

const static struct cli_command cmds_user[] STATIC_CLI_CMD_ATTRIBUTE = {
    {"stack_wifi", "Wi-Fi Stack", cmd_stack_wifi},
    {"bili_get", "get follower number of a bilibili account", cmd_bili_get},
};

static void _cli_init()
{
    /*Put CLI which needs to be init here*/
}

// 下边是两个启动后运行的示例任务

static void wifi_test(void *pvParameters) // 这个任务每隔 10 秒获取某个 b 站用户的粉丝数量
{
    // WiFi 使用方式也可参考: https://lupyuen.github.io/articles/book#wifi-on-bl602
    // 与 LwIP 的文档: https://www.nongnu.org/lwip/2_1_x/group__httpc.html#gabd4ef2259885a93090733235cc0fa8d6

    easyflash_init();
    _cli_init();

    aos_register_event_filter(EV_WIFI, event_cb_wifi_event, NULL);

    cmd_stack_wifi(NULL, 0, 0, NULL);

    static char *ssid = "这里填名字";     // TODO: 请在这里填上自己 WiFi 的名字和密码
    static char *password = "这里填密码"; // TODO: 请在这里填上自己 WiFi 的名字和密码
    wifi_sta_connect(ssid, password);

    while (1)
    {
        bili_get_test("356383684");
        vTaskDelay(10000);
    }
}

void blink_test(void *param) // 这个任务用来测试 GPIO
{
    const uint8_t GPIO_LED_PIN = 2;
    const uint8_t GPIO_SOMEINPUT = 3;
    uint8_t value_out = 1;
    uint8_t value_in;
    bl_gpio_enable_output(GPIO_LED_PIN, 0, 0);
    bl_gpio_enable_input(GPIO_SOMEINPUT, 0, 0);

    while (1)
    {
        value_out = !value_out;
        bl_gpio_output_set(GPIO_LED_PIN, value_out);

        bl_gpio_input_get(GPIO_SOMEINPUT, &value_in);
        if (value_in) // 如果给 GPIO3 接高电平，就会在命令行里看到每秒一条下边的提示
        {
            printf("  [LED] State switched to %d.\r\n", value_out);
        }
        vTaskDelay(1000);
    }
}

void main(void)
{
    bl_sys_init();

    xTaskCreate(blink_test, "blink_test", 1024, NULL, 15, NULL);
    xTaskCreate(wifi_test, "wifi_test", 1024, NULL, 15, NULL);

    tcpip_init(NULL, NULL);
}
