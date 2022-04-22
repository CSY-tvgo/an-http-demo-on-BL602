/**
 * Author: Karbon
 * GitHub: https://github.com/csy-tvgo
 * Date:   2022-Apr-14
 * Doc:    https://verimake.com/d/183
 */

#ifndef _GET_COVID19_DATA_C_
#define _GET_COVID19_DATA_C_

#include "wifi_utils.c"
#include "eInk.c"

void safe_hosal_flash_raw_erase(uint32_t start_addr, uint32_t length)
{
    int8_t failed = 1;
    while (failed)
    {
        failed = hosal_flash_raw_erase(start_addr, length);
    }
}

void safe_hosal_flash_raw_write(void *buffer, uint32_t address, uint32_t length)
{
    int8_t failed = 1;
    while (failed)
    {
        failed = hosal_flash_raw_write(buffer, address, length);
    }
}

void safe_hosal_flash_raw_read(void *buffer, uint32_t address, uint32_t length)
{
    int8_t failed = 1;
    while (failed)
    {
        failed = hosal_flash_raw_read(buffer, address, length);
        for (volatile int i = 0; i < 16; i++)
        {
        }
    }
}

#define BUFFER_SIZE (12 * 1024)

int covid_download_http()
{
    // 把疫情数据下载下来存入 Flash
    // @return 错误代码，≥ 0 表示运行成功时接收的数据长度

    char *hostname = "interface.sina.cn"; // 网页版：https://news.sina.cn/zt_d/yiqing0121
    char *request = "GET /news/wap/fymap2020_data.d.json HTTP/1.1\r\n"
                    "Host: interface.sina.cn\r\n\r\n";
    int port = 80;

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

    // Start receiving

    int32_t recv_length = 0;
    int32_t ret = 0;
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

        uint32_t flash_addr_this = 0x192000;           // Media 分区的起始地址
        safe_hosal_flash_raw_erase(0x192000, 0x57000); // 擦除了才能写
        printf("Flash 0x192000 to 0x1E8FFF erased.\r\n", time_consumed);

        while (1)
        {
            ret = read(sockfd, recv_buffer, BUFFER_SIZE);
            if ((ret == 0) || (recv_length > 300000) ||
                (strstr((char *)recv_buffer, "otherhistorylist"))) // 读到这个就停止，因为后边的数据用不着
            {
                safe_hosal_flash_raw_write("\0", flash_addr_this + recv_length, 2);
                printf("  #EOF written in flash: %lX\n\r", flash_addr_this + recv_length);
                break;
            }
            else if (ret < 0)
            {
                printf("  Read error: ret = %d, err = %d\n\r", ret, errno);
                break;
            }
            else
            {
                char *tag;
                while ((tag = strstr((char *)recv_buffer, "\r\n1000\r\n")) != NULL)
                {
                    // 去掉所有 <CR><LF>1000<CR><LF>
                    memmove(tag, tag + 6, ret + (long)recv_buffer - (long)tag - 6);
                    ret -= 6;
                }
                while ((tag = strstr((char *)recv_buffer, "\r\n")) != NULL)
                {
                    // 去掉所有 <CR><LF>
                    memmove(tag, tag + 2, ret + (long)recv_buffer - (long)tag - 2);
                    ret -= 2;
                }
                recv_buffer[ret] = '\0';
                safe_hosal_flash_raw_write(recv_buffer, flash_addr_this + recv_length, ret);
                recv_length += ret;
                printf("  ret = %d, recv_length(total) = %d\n\r", ret, recv_length);

                // for (int i = 0; i < ret; i++) // FOR DEBUG
                // {
                // printf("%c", recv_buffer[i]);
                // }
            }
        }

        ticks_1 = xTaskGetTickCount();
        time_consumed = ((uint32_t)(((int32_t)ticks_1) - ((int32_t)ticks_0))) / 1000.0;
        printf("Finish Time: %.4f s, received length = %d.\r\n", time_consumed, recv_length);

        vPortFree(recv_buffer);
    }
    close(sockfd);
    return 0;
}

void covid_printJSON()
{
    uint32_t flash_addr_this = 0x192000;
    char buf = '\n';
    while ((buf != 0) && (buf != 0xff))
    {
        safe_hosal_flash_raw_write(&buf, flash_addr_this, 1);
        printf("%c", buf);
        flash_addr_this++;
    }
    printf("Met #EOF ADDR=%lX", flash_addr_this - 1);
}

uint32_t strstr_from_flash(uint32_t flash_addr_begin, const char *substr)
{
    // Flash 版 strstr()
    // @param[in] flash_addr_begin  开始查找的地址
    // @param[in] substr            要查找的子串
    // @return                      找到的子串在 Flash 里的起始地址

    const uint32_t max_searh_len = 0x57000;      // 最大扫描长度
    uint32_t flash_addr_this = flash_addr_begin; // 当前扫描字符的地址
    uint32_t flash_addr_batch;                   // 当前扫描批次的起始地址
    uint32_t batch_count = 0;                    // 当前扫描字符的地址（用于调试）
    uint32_t RESULT_ADDR = 0;                    // 查找到的子串在 Flash 中的起始地址，0 表示没找到
    static char buf[2048];
    int buf_len = 0;

    while ((buf[buf_len] != 0xff) && (flash_addr_this < flash_addr_begin + max_searh_len))
    {
        memset(buf, 0, sizeof(buf));
        buf_len = 0;
        flash_addr_batch = flash_addr_this;

        while ((buf[buf_len] != 0xff) && (buf[buf_len] != '}')) // 以 '}' 作为分批次的符号
        {
            safe_hosal_flash_raw_read(&buf[buf_len], flash_addr_this, 1);
            buf_len++;
            flash_addr_this++;
        }

        // printf("\n\r  0x%X (0x%X:%c)", flash_addr_this, buf[buf_len], buf[buf_len]); // FOR DEBUG
        // printf("\n\r   Finished batch %d, addr 0x%X to 0x%X (len=%d) ",
        //    batch_count++, flash_addr_batch, flash_addr_this, buf_len); // FOR DEBUG

        char *finded = strstr(buf, substr);
        // printf("\n\r finded=%s  (%d)", finded, (uint32_t)finded); // FOR DEBUG
        if (NULL != finded)
        {
            RESULT_ADDR = flash_addr_batch + (finded - buf);
            // printf("\n\r RESULT_ADDR=0x%X ", RESULT_ADDR); // FOR DEBUG
            break;
        }
    }
    return RESULT_ADDR;
}

char *strncpy_from_flash(char *dest, uint32_t flash_addr_begin, uint32_t length)
{
    // Flash 版 strncpy()
    // @param[in] dest             要存至的字符串的指针
    // @param[in] flash_addr_begin 要复制的字符串在 Flash 中的起始地址
    // @param[in] length           要复制的长度
    // @return                     dest 的起始地址

    for (uint32_t i = 0; i < length; i++)
    {
        safe_hosal_flash_raw_read(&dest[i], flash_addr_begin + i, 1);
    }
    dest[length] = '\0';
    return dest;
}

int32_t covid_query(uint32_t flash_addr_begin, char *citycode, char *key,
                    char *datetime, char *result)
{
    // 查询一些城市的疫情相关的数据
    // @param[in]  flash_addr_begin  在 Flash 中开始查找的地址
    // @param[in]  citycode          要查询的城市的编号
    // @param[in]  key               要查询的数据的 key，如“nowConfirm”
    // @param[out] datetime          返回结果的更新时间
    // @param[out] result            返回的结果
    // @return                       为负表示错误代码；为正表示查找到的数据在 Flash 里的地址

    uint32_t temp_addr;
    memset(result, 0, 32);

    // 处理更新时间
    if (0x192000 == flash_addr_begin) // 从头开始扫的才查时间
    {
        // printf("\r\n search datetime, addr_begin=0x%X, result=", flash_addr_begin); // FOR DEBUG

        uint32_t datetime_L = strstr_from_flash(flash_addr_begin, "mtime");

        if (0 == datetime_L)
        {
            printf("covid_query(): Find datetime error. \r\n");
            return -1;
        }
        strncpy_from_flash(datetime, datetime_L + 8, 19);
        // printf("%s \r\n", datetime);                      // FOR DEBUG
        temp_addr = datetime_L + 1;
    }
    else
    {
        // printf("\r\n skip datetime, addr_begin=0x%X \r\n", flash_addr_begin); // FOR DEBUG
        temp_addr = flash_addr_begin;
    }

    // 查找城市
    uint32_t citycode_L = strstr_from_flash(temp_addr, citycode);
    if (0 == citycode_L)
    {
        printf("covid_query(): Find city error. \r\n");
        return -2;
    }

    // 查找该城市的 key
    uint32_t key_L = strstr_from_flash(citycode_L, key);
    if (0 == key_L)
    {
        printf("covid_query(): Find key error. \r\n");
        return -3;
    }

    // 读取数据
    uint32_t result_L = key_L + strlen(key) + 3;
    uint32_t result_R = strstr_from_flash(result_L, "\"");
    strncpy_from_flash(result, result_L, result_R - result_L);
    result[result_R - result_L] = '\0';

    printf("%s of %s: %s (%s)\r\n", key, citycode, result, datetime);
    // printf("\r\n flash_addr_end=0x%X \r\n", result_L); // FOR DEBUG

    return result_L;
}

void covid_query_test(char *citycode, char *key)
{
    char datetime[32];
    memset(datetime, 0, 32);
    char result[32];
    memset(result, 0, 32);
    int32_t ret = covid_query(0x192000, citycode, key, datetime, result);
}

void covid_somecities()
{
    //
    // 按照下列顺序仅需扫描一遍，节省时间：
    //
    // 杭州市  CN330100000000
    // 宁波市  CN330200000000
    // 嘉兴市  CN330400000000
    // 湖州市  CN330500000000
    //
    // 宣城市  CN341800000000
    //
    // 南京市  CN320100000000
    // 无锡市  CN320200000000
    // 常州市  CN320400000000
    // 苏州市  CN320500000000
    // 南通市  CN320600000000
    // 镇江市  CN321100000000
    // 泰州市  CN321200000000
    //
    // 上海市  shanghai","v   (因为上海和省同级，所以查询方式不太一样)
    //

    volatile char datetime[32];
    volatile char result[32];
    volatile int32_t temp_addr = 0x192000;
    memset(datetime, 0, 32);
    memset(result, 0, 32);

    temp_addr = covid_query((uint32_t)temp_addr, "CN3301", "econNum", datetime, result);
    temp_addr = covid_query((uint32_t)temp_addr, "CN3302", "econNum", datetime, result);
    temp_addr = covid_query((uint32_t)temp_addr, "CN3305", "econNum", datetime, result);
    temp_addr = covid_query((uint32_t)temp_addr, "CN3304", "econNum", datetime, result);

    temp_addr = covid_query((uint32_t)temp_addr, "CN3418", "econNum", datetime, result);

    temp_addr = covid_query((uint32_t)temp_addr, "CN3201", "econNum", datetime, result);
    temp_addr = covid_query((uint32_t)temp_addr, "CN3202", "econNum", datetime, result);
    temp_addr = covid_query((uint32_t)temp_addr, "CN3204", "econNum", datetime, result);
    temp_addr = covid_query((uint32_t)temp_addr, "CN3205", "econNum", datetime, result);
    temp_addr = covid_query((uint32_t)temp_addr, "CN3206", "econNum", datetime, result);
    temp_addr = covid_query((uint32_t)temp_addr, "CN3211", "econNum", datetime, result);
    temp_addr = covid_query((uint32_t)temp_addr, "CN3212", "econNum", datetime, result);

    temp_addr = covid_query((uint32_t)temp_addr, "shanghai\",\"v", "econNum", datetime, result);
}

void covid_display_somecities()
{
    volatile char datetime[32];
    volatile char result[13][32];
    volatile int32_t temp_addr = -1;

    while (temp_addr < 0)
    {
        temp_addr = 0x192000;
        memset(datetime, 0, sizeof(datetime));
        memset(result, 0, sizeof(result));

        temp_addr = covid_query((uint32_t)temp_addr, "CN3301", "econNum", datetime, result[0]); // HZ
        temp_addr = covid_query((uint32_t)temp_addr, "CN3302", "econNum", datetime, result[1]); // NB
        temp_addr = covid_query((uint32_t)temp_addr, "CN3305", "econNum", datetime, result[2]); // Hu
        temp_addr = covid_query((uint32_t)temp_addr, "CN3304", "econNum", datetime, result[3]); // JX

        temp_addr = covid_query((uint32_t)temp_addr, "CN3418", "econNum", datetime, result[4]); // XC

        temp_addr = covid_query((uint32_t)temp_addr, "CN3201", "econNum", datetime, result[5]);  // NJ
        temp_addr = covid_query((uint32_t)temp_addr, "CN3202", "econNum", datetime, result[6]);  // WX
        temp_addr = covid_query((uint32_t)temp_addr, "CN3204", "econNum", datetime, result[7]);  // CZ
        temp_addr = covid_query((uint32_t)temp_addr, "CN3205", "econNum", datetime, result[8]);  // SZ
        temp_addr = covid_query((uint32_t)temp_addr, "CN3206", "econNum", datetime, result[9]);  // NT
        temp_addr = covid_query((uint32_t)temp_addr, "CN3211", "econNum", datetime, result[10]); // ZJ
        temp_addr = covid_query((uint32_t)temp_addr, "CN3212", "econNum", datetime, result[11]); // TZ

        temp_addr = covid_query((uint32_t)temp_addr, "shanghai\",\"v", "econNum", datetime, result[12]); // SH

        if (temp_addr > 0x192000) // 读到了才显示，没读到重来
        {
            eInk_resetCanvas();

            eInk_print_withbox(183, 268, result[0]);
            eInk_print_withbox(304, 278, result[1]);
            eInk_print_withbox(150, 208, result[2]);
            eInk_print_withbox(245, 230, result[3]);
            eInk_print_withbox(56, 160, result[4]);
            eInk_print_withbox(29, 56, result[5]);
            eInk_print_withbox(208, 93, result[6]);
            eInk_print_withbox(121, 82, result[7]);
            eInk_print_withbox(236, 150, result[8]);
            eInk_print_withbox(305, 36, result[9]);
            eInk_print_withbox(105, 42, result[10]);
            eInk_print_withbox(191, 25, result[11]);
            eInk_print_withbox(328, 164, result[12]);

            char temp[64];
            sprintf(temp, "Updated at:\n%s", datetime);
            eInk_print_withbox(3, 233, temp);

            eInk_show();
            break;
        }
        else
        {
            printf("covid_display_somecities(): met error, retrying...\r\n");
            vTaskDelay(1000);
        }
    }
}

#endif

// FIXME: 有的时候会出现诡异的现象，可能是分配内存不太稳定
