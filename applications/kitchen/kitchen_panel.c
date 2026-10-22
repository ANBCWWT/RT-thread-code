/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 智能厨房面板主入口实现
 */
#include "kitchen_panel.h"

#include <board.h>

#include "relay.h"
#include "aht20.h"
#include "voice_prompt.h"
#include "kitchen_ui.h"
#include "screen_saver.h"
#include "voice_ctrl/sntp.h"
#include "voice_ctrl/wifi_link.h"
#include "env_history.h"

#define DBG_TAG "kitchen.panel"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* 业务线程参数 */
#define KITCHEN_THREAD_STACK    4096
#define KITCHEN_THREAD_PRIORITY 20
#define KITCHEN_THREAD_TICK     10

/* 温湿度采样周期（ms） */
#define KITCHEN_ENV_SAMPLE_MS   2000

/* LCD 背光控制引脚（与 board.c / main.c 保持一致） */
#define LCD_BL_GPIO_NUM         GET_PIN(15, 7)   /* LCD 背光电源开关 */
#define BL_PWM_DISP_CTRL        GET_PIN(20, 6)   /* LCD PWM 亮度调节 */

/* ============================================================================
 * 内部函数
 * ==========================================================================*/
static void kitchen_backlight_enable(void)
{
    /* 背光电源开关：P15_7（高=背光电源通）
     * 注意：P20_6（BL_PWM_DISP_CTRL）不再按 GPIO 拉高——它是背光 PWM 输入
     * （TCPWM0 LINE265），由 drv_lcd 的 lcd_backlight_set() 接管（时钟+HSIOM
     * 在 drv_lcd.c lcd_bl_pwm_hw_init 补齐）。此处若再配 GPIO 会把引脚抢回，
     * 导致亮度固定不可调。 */
    rt_pin_mode(LCD_BL_GPIO_NUM, PIN_MODE_OUTPUT);
    rt_pin_write(LCD_BL_GPIO_NUM, PIN_HIGH);
}

/* 业务线程：周期采集 AHT20 温湿度并刷新 UI；联网后自动 SNTP 对时 */
static void kitchen_thread_entry(void *param)
{
    float temperature = 0.0f;
    float humidity    = 0.0f;
    rt_bool_t wifi_prev = RT_FALSE;
    rt_bool_t env_sleeping = RT_FALSE;   /* 息屏期间温湿度采集暂停标志 */
    rt_tick_t sync_at   = 0;   /* 连接边沿后延时对时的时刻（0=空闲） */
    rt_tick_t retry_at  = 0;   /* 对时失败后的重试时刻 */
    rt_tick_t hist_at   = 0;   /* 上次追加环境历史的时刻（60s 一次） */

    RT_UNUSED(param);

    while (1)
    {
        rt_bool_t wifi_now = wifi_link_is_connected();

        /* 边沿：刚连上（含断线重连）→ 1s 后网络稳定再强制重新对时（修正漂移） */
        if (wifi_now && !wifi_prev)
        {
            sync_at  = rt_tick_get() + RT_TICK_PER_SECOND;
            retry_at = 0;   /* 重置重试门：让边沿对时立即可放行 */
            wifi_prev = RT_TRUE;
        }
        else if (!wifi_now)
        {
            wifi_prev = RT_FALSE;
        }

        /* 对时：边沿到点执行一次；若仍未同步则每 10s 兜底重试
         * （用 (rt_int32_t) 差值比较，正确处理 tick 回绕） */
        if (wifi_now && (sync_at != 0 || !sntp_is_synced()))
        {
            rt_tick_t now = rt_tick_get();
            if ((rt_int32_t)(now - sync_at) >= 0 && (rt_int32_t)(now - retry_at) >= 0)
            {
                sync_at  = 0;
                retry_at = rt_tick_get() + RT_TICK_PER_SECOND * 10;
                if (sntp_sync() == RT_EOK)
                {
                    LOG_I("sntp synced (epoch=%d)", (int)sntp_now());
                }
            }
        }

        /* 温湿度采集：息屏期间暂停（省 I2C 唤醒），唤醒后立即补采一次刷新 UI */
        if (screen_saver_is_sleeping())
        {
            if (!env_sleeping)
            {
                env_sleeping = RT_TRUE;
                LOG_D("env sampling paused (screen off)");
            }
        }
        else
        {
            if (env_sleeping)
            {
                env_sleeping = RT_FALSE;   /* 唤醒边沿：立即补采 */
            }
            if (aht20_read(&temperature, &humidity) == 0)
            {
                kitchen_ui_update_env(temperature, humidity);

                /* 每 60s 追加一条环境历史（息屏暂停采样期间自然不追加） */
                if ((rt_int32_t)(rt_tick_get() - hist_at) >= RT_TICK_PER_SECOND * 60)
                {
                    hist_at = rt_tick_get();
                    env_history_append(temperature, humidity, sntp_now());
                }
            }
        }

        rt_thread_mdelay(KITCHEN_ENV_SAMPLE_MS);
    }
}

/* ============================================================================
 * 公共接口
 * ==========================================================================*/
int kitchen_panel_init(void)
{
    rt_thread_t tid;

    /* 1. 打开 LCD 背光（UI 初始化前点亮，避免黑屏等待） */
    kitchen_backlight_enable();

    /* 2. 初始化继电器：全部输出默认断开 */
    relay_init();

    /* 3. 初始化 AHT20（板载，i2c1 总线） */
    aht20_init();

    /* 3.5 加载环境历史（/flash/history.dat，无则从空开始） */
    env_history_init();

    /* 4. 初始化提示音（设置音量） */
    voice_prompt_init();

    /* 5. 启动 LVGL 主界面线程 */
    kitchen_ui_init();

    /* 6. 启动业务线程：周期采样温湿度 */
    tid = rt_thread_create("kitchen", kitchen_thread_entry, RT_NULL,
                           KITCHEN_THREAD_STACK, KITCHEN_THREAD_PRIORITY,
                           KITCHEN_THREAD_TICK);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }

    return 0;
}
