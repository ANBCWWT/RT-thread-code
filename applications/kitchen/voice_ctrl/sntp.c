/*
 * SNTP 对时实现（语音控制 · 阶段 2）
 *
 * 实现：标准 BSD socket（RT-Thread SAL）UDP 123 端口发送 NTP 请求
 * （LI=0 VN=3 MODE=3），接收 48 字节响应，取 transmit timestamp。
 * 对时成功后保存基准 epoch 与基准 tick，之后 sntp_now() 用 tick
 * 差值估算当前时间（家用场景漂移可忽略）。
 */
#include "sntp.h"
#include "asr_config.h"

#include <rtthread.h>
#include <rtdevice.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>

#define DBG_TAG "voice.sntp"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* NTP 服务器（默认 ntp.aliyun.com） */
#ifndef SNTP_SERVER_NAME
#define SNTP_SERVER_NAME   "ntp.aliyun.com"
#endif
#ifndef SNTP_SERVER_PORT
#define SNTP_SERVER_PORT   123
#endif

#define NTP_PACKET_SIZE     48
#define NTP_TIMEOUT_MS      3000
/* NTP 纪元(1900) 到 Unix 纪元(1970) 的秒偏移 */
#define NTP_UNIX_OFFSET     2208988800UL

static time_t      s_ntp_epoch     = 0;   /* 对时时刻的 unix 秒 */
static rt_tick_t   s_ntp_sync_tick = 0;   /* 对时时刻的 tick */

/* 把 4 字节网络序转 uint32 */
static rt_uint32_t ntp_read_u32(const rt_uint8_t *p)
{
    return ((rt_uint32_t)p[0] << 24) | ((rt_uint32_t)p[1] << 16) |
           ((rt_uint32_t)p[2] << 8)  |  (rt_uint32_t)p[3];
}

int sntp_sync(void)
{
    int fd = -1;
    struct sockaddr_in server;
    struct hostent *host;
    rt_uint8_t pkt[NTP_PACKET_SIZE] = {0};
    rt_uint8_t rsp[NTP_PACKET_SIZE];
    struct timeval tv;
    int ret = -RT_ERROR;

    host = gethostbyname(SNTP_SERVER_NAME);
    if (host == RT_NULL)
    {
        LOG_E("dns resolve %s failed", SNTP_SERVER_NAME);
        return -RT_ERROR;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        LOG_E("socket create failed");
        return -RT_ERROR;
    }

    /* 请求包：LI=0 VN=3 MODE=3 → 0x1B */
    pkt[0] = 0x1B;

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(SNTP_SERVER_PORT);
    memcpy(&server.sin_addr, host->h_addr, host->h_length);

    if (sendto(fd, pkt, NTP_PACKET_SIZE, 0,
               (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        LOG_E("sendto failed");
        goto exit;
    }

    /* 接收超时 */
    tv.tv_sec = NTP_TIMEOUT_MS / 1000;
    tv.tv_usec = (NTP_TIMEOUT_MS % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    if (recvfrom(fd, rsp, NTP_PACKET_SIZE, 0, RT_NULL, RT_NULL) <= 0)
    {
        LOG_E("recvfrom timeout/no data");
        goto exit;
    }

    /* transmit timestamp：rsp[40..43]（秒，1900 纪元） */
    {
        rt_uint32_t ntp_sec = ntp_read_u32(&rsp[40]);
        if (ntp_sec == 0 || ntp_sec < NTP_UNIX_OFFSET)
        {
            LOG_E("invalid ntp timestamp");
            goto exit;
        }
        s_ntp_epoch = (time_t)(ntp_sec - NTP_UNIX_OFFSET);
        s_ntp_sync_tick = rt_tick_get();
        LOG_I("time synced: %d", (int)s_ntp_epoch);
        ret = RT_EOK;
    }

exit:
    if (fd >= 0)
    {
        closesocket(fd);
    }
    return ret;
}

rt_bool_t sntp_is_synced(void)
{
    return s_ntp_epoch > 0 ? RT_TRUE : RT_FALSE;
}

time_t sntp_now(void)
{
    if (s_ntp_epoch <= 0)
    {
        return 0;
    }
    return s_ntp_epoch + (time_t)((rt_tick_get() - s_ntp_sync_tick) / RT_TICK_PER_SECOND);
}

rt_bool_t sntp_get_local_hm(char *buf, int len)
{
    time_t t;

    if ((s_ntp_epoch <= 0) || (buf == RT_NULL) || (len < 6))
    {
        return RT_FALSE;
    }

    /* 北京时间 = UTC + 8h */
    t = sntp_now() + 8 * 3600;
    rt_snprintf(buf, len, "%02d:%02d",
                (int)((t % 86400) / 3600),
                (int)((t % 3600) / 60));
    return RT_TRUE;
}

/*
 * 公历转换（Howard Hinnant civil_from_days 算法，全部非负运算）
 * 输入：自 1970-01-01 起的天数；输出：年/月/日
 */
static void civil_from_days(int days, int *y, int *m, int *d)
{
    int z = days + 719468;
    int era = z / 146097;
    int doe = z - era * 146097;              /* [0, 146096] */
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int day = doy - (153 * mp + 2) / 5 + 1;
    int month = mp + (mp < 10 ? 3 : -9);

    year += (month <= 2);
    *y = year;
    *m = month;
    *d = day;
}

void sntp_format_rfc1123(time_t t, char *out, int out_len)
{
    static const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int days = (int)(t / 86400);
    int rem  = (int)(t % 86400);
    int hour = rem / 3600;
    int min  = (rem % 3600) / 60;
    int sec  = rem % 60;
    int year, month, day;

    civil_from_days(days, &year, &month, &day);
    rt_snprintf(out, out_len, "%s, %02d %s %04d %02d:%02d:%02d GMT",
                weekdays[(days + 4) % 7], day, months[month - 1],
                year, hour, min, sec);
}
