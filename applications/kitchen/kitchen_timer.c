/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 厨房定时器模块实现（参赛功能批次 4）
 *
 * 到点动作通过回调接线（kitchen_ui.c 注入）：
 *   - 播放 /flash/timer.wav 提示音
 *   - screen_saver_force_wakeup() 息屏时强制亮屏
 * 回调在 tick() 的调用线程（UI 线程）内执行，不能做阻塞操作。
 */
#include "kitchen_timer.h"

#include <rtthread.h>

#define DBG_TAG "kitchen.timer"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* 到点回调（由 kitchen_ui.c 注册） */
static void (*s_on_expire)(void) = RT_NULL;

static kitchen_timer_state_t s_state      = KITCHEN_TIMER_IDLE;
static rt_tick_t             s_end_tick   = 0;
static int                   s_total_sec  = 0;

void kitchen_timer_set_expire_cb(void (*cb)(void))
{
    s_on_expire = cb;
}

int kitchen_timer_start(int minutes)
{
    if ((minutes < 1) || (minutes > 99))
    {
        return -RT_EINVAL;
    }

    s_state     = KITCHEN_TIMER_RUNNING;
    s_total_sec = minutes * 60;
    /* 用 32 位 tick 差值判断，tick 回绕也正确；整机不可能跑满 49 天演示 */
    s_end_tick  = rt_tick_get() + (rt_tick_t)(s_total_sec * RT_TICK_PER_SECOND);
    LOG_I("start %d min, end tick %u", minutes, (unsigned)s_end_tick);
    return 0;
}

int kitchen_timer_cancel(void)
{
    if (s_state != KITCHEN_TIMER_IDLE)
    {
        s_state    = KITCHEN_TIMER_IDLE;
        s_end_tick = 0;
        s_total_sec = 0;
        LOG_I("cancel");
    }
    return 0;
}

kitchen_timer_state_t kitchen_timer_state(void)
{
    return s_state;
}

int kitchen_timer_remaining_sec(void)
{
    rt_tick_t now;

    if (s_state != KITCHEN_TIMER_RUNNING)
    {
        return 0;
    }

    now = rt_tick_get();
    if ((rt_int32_t)(s_end_tick - now) <= 0)
    {
        return 0;
    }
    /* 向上取整：剩余 0.2s 显示 1s，避免刚启动就闪 0 */
    return (int)((s_end_tick - now + RT_TICK_PER_SECOND - 1) / RT_TICK_PER_SECOND);
}

void kitchen_timer_tick(void)
{
    if (s_state != KITCHEN_TIMER_RUNNING)
    {
        return;
    }

    if ((rt_int32_t)(s_end_tick - rt_tick_get()) > 0)
    {
        return;   /* 未到点 */
    }

    /* ---- 到点 ---- */
    s_state = KITCHEN_TIMER_DONE;
    LOG_I("expired");
    if (s_on_expire != RT_NULL)
    {
        s_on_expire();
    }
}
