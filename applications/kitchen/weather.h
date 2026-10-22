/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 天气模块（参赛功能批次 4）
 * - 数据源：Open-Meteo（免费、无需 API Key、国内可达，2026-10-22 实测）
 * - 独立低优先级线程：WiFi 连上后拉取一次，之后每 30 分钟刷新
 * - 拉取走 webclient(HTTPS/mbedTLS)，同步阻塞，但不影响 UI 线程
 * - 断网 / 请求失败：保留上次数据并标记 offline，UI 侧降级显示 "--"
 */
#ifndef KITCHEN_WEATHER_H
#define KITCHEN_WEATHER_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 天气数据（UI 只读，不直接跨线程改） */
typedef struct
{
    float      temp;    /* 当前温度 ℃          */
    int        hum;     /* 相对湿度 %           */
    int        code;    /* WMO 天气码           */
    rt_bool_t  valid;   /* 最近一次请求成功     */
    rt_bool_t  offline; /* 最近一次请求失败（保留旧数据） */
    rt_tick_t  ts;      /* 最近成功时刻         */
} weather_t;

/* 城市名（显示在天气卡上，演示现场固定石家庄） */
const char *weather_city(void);

/* 拷贝当前天气快照（UI 线程调用，结构体整体拷贝，无需锁） */
void weather_get(weather_t *out);

/* WMO 天气码 -> 英文描述（Montserrat 字体可渲染） */
const char *weather_wmo_desc(int code);

/* 启动天气线程（可重复调用，幂等）；返回 0 成功 */
int weather_init(void);

#ifdef __cplusplus
}
#endif

#endif /* KITCHEN_WEATHER_H */
