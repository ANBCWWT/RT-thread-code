/*
 * SNTP 对时（语音控制 · 阶段 2）
 *
 * 讯飞流式接口签名要求 date 头为 RFC1123 GMT 格式，且与服务器
 * 偏差 < 300 秒，因此必须先对时。本模块用 UDP 123 访问公网 NTP
 * 服务器（默认 ntp.aliyun.com），对时一次后基于 rt_tick 维持。
 */
#ifndef VOICE_SNTP_H
#define VOICE_SNTP_H

#include <rtthread.h>
#include <time.h>

/* 同步一次系统时间（阻塞，约 1~3 秒）。成功返回 RT_EOK */
int sntp_sync(void);

/* 当前是否已成功对时 */
rt_bool_t sntp_is_synced(void);

/* 当前 unix 时间戳（秒），未对时返回 0 */
time_t sntp_now(void);

/*
 * 获取北京时间（UTC+8）的 HH:MM 字符串（24 小时制）。
 * 未对时返回 RT_FALSE；buf 至少 6 字节。
 */
rt_bool_t sntp_get_local_hm(char *buf, int len);

/*
 * 把 unix 时间戳格式化为 RFC1123 GMT 字符串，如：
 *   "Thu, 01 Oct 2026 10:00:00 GMT"
 * out 缓冲区至少 40 字节。
 */
void sntp_format_rfc1123(time_t t, char *out, int out_len);

#endif /* VOICE_SNTP_H */
