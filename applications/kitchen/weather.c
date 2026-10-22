/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 天气模块实现（参赛功能批次 4）
 *
 * 实测数据（2026-10-22 石家庄，电脑 curl 验证）：
 *   GET https://api.open-meteo.com/v1/forecast?latitude=38.04&longitude=114.51
 *       &current=temperature_2m,relative_humidity_2m,weather_code
 *       &timezone=Asia%2FShanghai
 *   返回（约 240 字节）：
 *   {"latitude":38.07,"longitude":114.47,...,"current":
 *    {"time":"2026-10-22T14:45","interval":900,
 *     "temperature_2m":13.5,"relative_humidity_2m":59,"weather_code":3}}
 *   wttr.in 国内不可达（SSL exit 35），故采用 Open-Meteo。
 */
#include "weather.h"

#include "cJSON.h"
#include "webclient.h"
#include "voice_ctrl/wifi_link.h"

#include <rtthread.h>

#define DBG_TAG "kitchen.weather"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* 石家庄（比赛 / 演示现场定位） */
#define WEATHER_LAT    "38.04"
#define WEATHER_LON    "114.51"

/* 刷新周期：30 分钟（演示时可用 msh 命令 weather_now 手动刷新） */
#define WEATHER_FETCH_MS   (30U * 60U * 1000U)

/* 天气线程：栈 4KB、优先级 26（低于 UI 线程 25，请求期间不抢 UI） */
#define WEATHER_THREAD_PRIORITY  26
#define WEATHER_THREAD_STACK     4096

static rt_thread_t s_weather_thread = RT_NULL;
static weather_t   s_weather;

const char *weather_city(void)
{
    return "Shijiazhuang";
}

void weather_get(weather_t *out)
{
    if (out != RT_NULL)
    {
        *out = s_weather;
    }
}

/* WMO 天气码 -> 英文描述（Montserrat 字体可渲染，无生僻字符） */
const char *weather_wmo_desc(int code)
{
    switch (code)
    {
    case 0:                       return "Clear";
    case 1:                       return "Mostly Clear";
    case 2:                       return "Partly Cloudy";
    case 3:                       return "Overcast";
    case 45: case 48:             return "Fog";
    case 51: case 53: case 55:    return "Drizzle";
    case 56: case 57:             return "Freezing Drizzle";
    case 61: case 63: case 65:    return "Rain";
    case 66: case 67:             return "Freezing Rain";
    case 71: case 73: case 75:    return "Snow";
    case 77:                      return "Snow Grains";
    case 80: case 81: case 82:    return "Showers";
    case 85: case 86:             return "Snow Showers";
    case 95:                      return "Thunderstorm";
    case 96: case 99:             return "Thunderstorm Hail";
    default:                      return "--";
    }
}

/* 拉取并解析 Open-Meteo 当前天气。
 * 成功：更新 s_weather（valid=1, offline=0）；
 * 失败：仅标记 offline=1，保留上次数据（UI 显示 "--" 或旧值+Offline 标记）。 */
static void weather_fetch_once(void)
{
    /* 用 HTTP（非 HTTPS）：板载 mbedTLS 无根证书，https 握手校验失败
     * （[E/mbedtls.clnt] verify peer certificate fail），Open-Meteo 实测
     * 同时支持 http，取天气数据不受影响 */
    const char *url =
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=" WEATHER_LAT
        "&longitude=" WEATHER_LON
        "&current=temperature_2m,relative_humidity_2m,weather_code"
        "&timezone=Asia%2FShanghai";
    void *resp = RT_NULL;
    size_t resp_len = 0;
    int rc;

    /* webclient_request：同步 GET。返回值语义（webclient.c:1870）：
     * >0 = 响应体字节数（内部已保证 HTTP 200）；<0 = 错误码。
     * 响应体（含 JSON）由内部申请，用完 web_free 释放。 */
    rc = webclient_request(url, RT_NULL, RT_NULL, 0, &resp, &resp_len);
    if (rc <= 0 || resp == RT_NULL || resp_len == 0)
    {
        LOG_W("fetch failed rc=%d len=%d", rc, (int)resp_len);
        s_weather.offline = RT_TRUE;
        return;
    }

    {
        cJSON *root = cJSON_Parse((const char *)resp);
        cJSON *cur  = RT_NULL;
        cJSON *item;

        if (root == RT_NULL)
        {
            LOG_E("JSON parse failed");
            s_weather.offline = RT_TRUE;
        }
        else
        {
            cur = cJSON_GetObjectItemCaseSensitive(root, "current");
            if (cur != RT_NULL)
            {
                item = cJSON_GetObjectItemCaseSensitive(cur, "temperature_2m");
                if (cJSON_IsNumber(item))
                {
                    s_weather.temp = (float)item->valuedouble;
                }
                item = cJSON_GetObjectItemCaseSensitive(cur, "relative_humidity_2m");
                if (cJSON_IsNumber(item))
                {
                    s_weather.hum = (int)item->valueint;
                }
                item = cJSON_GetObjectItemCaseSensitive(cur, "weather_code");
                if (cJSON_IsNumber(item))
                {
                    s_weather.code = item->valueint;
                }
            }

            s_weather.valid   = RT_TRUE;
            s_weather.offline = RT_FALSE;
            s_weather.ts      = rt_tick_get();
            LOG_I("updated: %.1fC %d%% code=%d (%s)",
                  s_weather.temp, s_weather.hum, s_weather.code,
                  weather_wmo_desc(s_weather.code));
        }

        cJSON_Delete(root);
    }

    web_free(resp);
}

/* 天气线程：等 WiFi -> 拉取 -> 睡 30min 循环。
 * 断网期间请求失败自然标记 offline，不重试风暴（30min 才一次）；
 * 每次拉取前都确认 WiFi 在位，避免请求卡在无网状态过久。 */
static void weather_thread_entry(void *param)
{
    RT_UNUSED(param);

    /* 等待 WiFi 连上（首次连接通常 5~15s，最多等 60s，之后每轮循环也会检查） */
    {
        int waited = 0;
        while (!wifi_link_is_connected() && waited < 60)
        {
            rt_thread_mdelay(1000);
            waited++;
        }
    }

    while (1)
    {
        if (wifi_link_is_connected())
        {
            weather_fetch_once();
        }
        rt_thread_mdelay(WEATHER_FETCH_MS);
    }
}

int weather_init(void)
{
    if (s_weather_thread != RT_NULL)
    {
        return 0;
    }

    s_weather.code    = -1;
    s_weather.valid   = RT_FALSE;
    s_weather.offline = RT_FALSE;

    s_weather_thread = rt_thread_create("weather",
                                        weather_thread_entry, RT_NULL,
                                        WEATHER_THREAD_STACK,
                                        WEATHER_THREAD_PRIORITY, 10);
    if (s_weather_thread == RT_NULL)
    {
        LOG_E("thread create failed");
        return -RT_ENOMEM;
    }
    rt_thread_startup(s_weather_thread);
    LOG_I("thread started");
    return 0;
}
