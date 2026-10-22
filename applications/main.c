/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-01-01     RT-Thread    First version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <board.h>
#include "kitchen/kitchen_panel.h"

/*****************************************************************************
 * Macro Definitions
 *****************************************************************************/
#define DBG_TAG    "main"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

/* ============================================================================
 * 应用模式选择（智能厨房面板改造）
 *   定义   BSP_USING_KITCHEN_PANEL → 智能厨房面板（当前默认）
 *   注释掉 下面这行               → 恢复原小智语音交互（走 BSP_USING_XiaoZhi 分支）
 * 平台文件（rt-thread / libraries / libs / board 等）一律不改动。
 * ==========================================================================*/
#define BSP_USING_KITCHEN_PANEL

/* LED Pin */
#define LED_PIN_GREEN       GET_PIN(16, 6)

/* UI initialization timeout (ms) */
#define UI_INIT_TIMEOUT_MS  5000

#define LCD_BL_GPIO_NUM GET_PIN(15, 7)
#define BL_PWM_DISP_CTRL GET_PIN(20, 6)

#ifndef BSP_LCD_STARTUP_STABILIZE_MS
#define BSP_LCD_STARTUP_STABILIZE_MS 1500U
#endif

#ifndef BSP_LCD_FIRST_FRAME_DELAY_MS
#define BSP_LCD_FIRST_FRAME_DELAY_MS 300U
#endif

/*****************************************************************************
 * External Function Declarations
 *****************************************************************************/
extern void xiaozhi_ui_init(void);
extern rt_err_t xiaozhi_ui_wait_ready(rt_int32_t timeout);
extern void wifi_manager_init(void);

/*****************************************************************************
 * Main Entry
 *****************************************************************************/

static void m55_lvgl_cpu_cache_enable(void)
{
#if defined(BSP_LVGL_ENABLE_CPU_CACHE) && defined(RT_USING_CACHE)
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    if (!rt_hw_cpu_icache_status())
    {
        rt_hw_cpu_icache_enable();
    }
#endif

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (!rt_hw_cpu_dcache_status())
    {
        rt_hw_cpu_dcache_enable();
    }
#endif

    LOG_I("M55 cache enabled: I=%d D=%d",
          rt_hw_cpu_icache_status(),
          rt_hw_cpu_dcache_status());
#endif
}

/* 仅小智模式需要背光函数（厨房模式下由 kitchen_panel_init 内部开背光） */
#if !defined(BSP_USING_KITCHEN_PANEL)
static void m55_lcd_backlight_enable(void)
{
    rt_pin_mode(LCD_BL_GPIO_NUM, PIN_MODE_OUTPUT);
    rt_pin_mode(BL_PWM_DISP_CTRL, PIN_MODE_OUTPUT);
    rt_pin_write(LCD_BL_GPIO_NUM, PIN_HIGH);
    rt_pin_write(BL_PWM_DISP_CTRL, PIN_HIGH);
}
#endif

int main(void)
{
    LOG_I("Cortex-M55 started");
#ifdef BSP_USING_KITCHEN_PANEL
    /* ============ 智能厨房面板模式 ============
     * kitchen_panel_init() 内部完成：
     *   开 LCD 背光 → 继电器初始化 → AHT20 温湿度 → 提示音音量
     *   → LVGL 主界面线程 → 业务线程（每 2s 采样温湿度） */
    m55_lvgl_cpu_cache_enable();
    rt_thread_mdelay(BSP_LCD_STARTUP_STABILIZE_MS);
    kitchen_panel_init();

#elif defined(BSP_USING_XiaoZhi)
    /* ============ 原小智语音模式（保留，可随时切回） ============ */
    m55_lvgl_cpu_cache_enable();
    rt_thread_mdelay(BSP_LCD_STARTUP_STABILIZE_MS);

    /* Initialize UI subsystem */
    xiaozhi_ui_init();

    /* Wait for UI initialization to complete */
    if (xiaozhi_ui_wait_ready(rt_tick_from_millisecond(UI_INIT_TIMEOUT_MS)) != RT_EOK)
    {
        LOG_W("UI initialization timeout");
    }
    rt_thread_mdelay(BSP_LCD_FIRST_FRAME_DELAY_MS);
    m55_lcd_backlight_enable();

    /* Initialize WiFi manager */
    wifi_manager_init();
#endif
    return 0;
}
