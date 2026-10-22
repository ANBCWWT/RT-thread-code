/*
 * WiFi 连接模块接口（语音控制 · 第一阶段）
 *
 * 职责：开机自动连接写死的默认网络（wifi_config.h 中的宏），
 *       断线后由事件驱动自动重连（3s 退避），并提供 msh 调试命令。
 *
 * 使用约束：
 *   1. 本模块只使用 RT-Thread 标准 wlan API 与 lwIP，不依赖小智代码；
 *   2. 初始化自动执行（INIT_APP_EXPORT），无需修改 main.c；
 *   3. 阶段 3（语音按钮/云端 ASR）通过 wifi_link_is_connected()
 *      查询网络状态，网络不可用时语音入口应降级提示。
 */
#ifndef VOICE_WIFI_LINK_H
#define VOICE_WIFI_LINK_H

#include <rtthread.h>

/* 手动初始化（通常不需要，模块已自动初始化；供需要时调用） */
int wifi_link_init(void);

/* 查询当前是否已连接并完成 DHCP（READY 事件后为 RT_TRUE） */
rt_bool_t wifi_link_is_connected(void);

#endif /* VOICE_WIFI_LINK_H */
