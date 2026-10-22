/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ============================================================
 * WiFi 连接配置（语音控制 · 第一阶段：写死默认网络）
 * ------------------------------------------------------------
 * 【请在这里填写你家 WiFi】
 *   把下面两个宏的值改成你的路由器信息：
 *     WIFI_SSID      —— WiFi 名称（区分大小写；中文 SSID 建议
 *                        改路由器设置成英文名，避免编码问题）
 *     WIFI_PASSWORD  —— WiFi 密码
 *
 *   填写后重新编译烧录即可：开机自动连接、断线自动重连。
 *   调试期不想重新编译时，可用串口命令临时连接：
 *       wifi_join <ssid> <password>
 *   （该命令不保存，重启后仍回落到下面两个宏）
 * ============================================================
 */
#ifndef VOICE_WIFI_CONFIG_H
#define VOICE_WIFI_CONFIG_H

/* ---------- 在这里填 WiFi 名称和密码 ---------- */
#define ANBCWWT
#if  defined(ANBCWWT)
    #define WIFI_SSID       "your_wifi_ssid"
    #define WIFI_PASSWORD   "your_wifi_password"

#elif  defined(HOME)
    #define WIFI_SSID       "your_home_ssid"
    #define WIFI_PASSWORD   "your_home_password"
#endif
/* --------------------------------------------- */

#endif /* VOICE_WIFI_CONFIG_H */
