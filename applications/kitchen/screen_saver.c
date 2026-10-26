/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 息屏管理实现：
 * - 亮屏态：触摸任意点即刷新"最后触摸时刻"；连续 60s 无触摸 → 息屏；
 * - 息屏：拉低 LCD 背光电源引脚（屏幕全黑），UI 线程停刷 LVGL；
 *   以 500ms 间隔轮询触摸芯片（ST7102_get_single_touch），按下即唤醒；
 * - 唤醒：拉高背光电源引脚，恢复 LVGL 刷新。背光 PWM 亮度状态全程不动，
 *   因此唤醒后保持息屏前的亮度。
 */
#include "screen_saver.h"

#include <board.h>
#include <drivers/pm.h>
#include <rtdevice.h>
#include "drv_touch.h"
#include "lv_port_indev.h"

#define DBG_TAG "kitchen.saver"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* 无触摸息屏超时（ms）——可被设置页修改（screen_saver_set_timeout），0=常亮 */
static rt_uint32_t s_timeout_ms = 60U * 1000U;

/* 息屏态触摸轮询间隔（ms）——用户指定 500ms */
#define SCREEN_SAVER_POLL_MS      500U

/* 亮屏态触摸检测间隔（ms）：只需"有没有触摸"用于刷新计时，100ms 粒度足够 */
#define SCREEN_SAVER_CHECK_MS     100U

/* LCD 背光电源开关（与 board.c / kitchen_panel.c 保持一致，高电平点亮） */
#define LCD_BL_GPIO_NUM           GET_PIN(15, 7)

static rt_bool_t s_sleeping         = RT_FALSE;
static rt_bool_t s_hold             = RT_FALSE;   /* 息屏锁定：到点提醒期间不自动息屏 */
static rt_tick_t s_last_touch_tick  = 0;   /* 最近一次触摸时刻（用于 60s 超时） */
static rt_tick_t s_last_check_tick  = 0;   /* 亮屏态触摸检测节流 */

/* 读取触摸芯片：返回 RT_TRUE 表示当前有手指按下 */
static rt_bool_t touch_pressed(void)
{
    rt_int16_t touch_x = 0;
    rt_int16_t touch_y = 0;

    return (ST7102_get_single_touch(&touch_x, &touch_y) == RT_EOK);
}

/* 息屏态触摸唤醒信号量：ST7102 IRQ(P17_2) 下降沿中断释放，UI 线程 sem_take 等待。
 * 中断回调只释放信号量（中断上下文不做 I2C 读取）；500ms 超时轮询作保底。 */
static struct rt_semaphore s_touch_wake_sem;

static void touch_irq_cb(void *arg)
{
    RT_UNUSED(arg);
    rt_sem_release(&s_touch_wake_sem);
}

/* 关闭触摸中断并清空信号量残留计数（唤醒时调用，防止下次息屏残留计数误唤醒） */
static void touch_wakeup_cleanup(void)
{
    rt_pin_irq_enable(ST7102_IRQ_PIN, PIN_IRQ_DISABLE);
    rt_pin_detach_irq(ST7102_IRQ_PIN);
    while (rt_sem_take(&s_touch_wake_sem, 0) == RT_EOK)
    {
    }
}

/* 挂载触摸中断（息屏进入时调用）：ST7102 触摸事件把 IRQ 拉低，下降沿触发 */
static void touch_irq_arm(void)
{
    /* 先清空残留计数，再挂中断，避免旧事件立即误唤醒 */
    while (rt_sem_take(&s_touch_wake_sem, 0) == RT_EOK)
    {
    }
    if (rt_pin_attach_irq(ST7102_IRQ_PIN, PIN_IRQ_MODE_FALLING, touch_irq_cb, RT_NULL) != RT_EOK)
    {
        LOG_W("touch irq attach failed, fallback to polling");
        return;
    }
    rt_pin_irq_enable(ST7102_IRQ_PIN, PIN_IRQ_ENABLE);
}

static void backlight_off(void)
{
    rt_pin_mode(LCD_BL_GPIO_NUM, PIN_MODE_OUTPUT);
    rt_pin_write(LCD_BL_GPIO_NUM, PIN_LOW);
}

static void backlight_on(void)
{
    rt_pin_mode(LCD_BL_GPIO_NUM, PIN_MODE_OUTPUT);
    rt_pin_write(LCD_BL_GPIO_NUM, PIN_HIGH);
}

rt_bool_t screen_saver_is_sleeping(void)
{
    return s_sleeping;
}

int screen_saver_init(void)
{
    rt_sem_init(&s_touch_wake_sem, "tp_wake", 0, RT_IPC_FLAG_FIFO);
    s_sleeping        = RT_FALSE;
    s_last_touch_tick = rt_tick_get();
    s_last_check_tick = rt_tick_get();
    return 0;
}

void screen_saver_loop(void)
{
    if (!s_sleeping)
    {
        /* ---- 亮屏态：100ms 粒度检测触摸 + 60s 超时息屏 ---- */
        if (rt_tick_get() - s_last_check_tick < rt_tick_from_millisecond(SCREEN_SAVER_CHECK_MS))
        {
            return;
        }
        s_last_check_tick = rt_tick_get();

        if (touch_pressed())
        {
            /* 有触摸（用户在操作）：刷新计时，不熄屏 */
            s_last_touch_tick = rt_tick_get();
            return;
        }

        /* 息屏锁定期间不自动息屏（定时器到点提醒等场景） */
        if (!s_hold &&
            s_timeout_ms > 0 &&
            rt_tick_get() - s_last_touch_tick >= rt_tick_from_millisecond(s_timeout_ms))
        {
            s_sleeping = RT_TRUE;
            backlight_off();
            /* 低功耗：请求浅睡眠（WFI），idle 线程自动进入；唤醒时 release */
            rt_pm_request(PM_SLEEP_MODE_IDLE);
            /* 触摸中断唤醒：挂 P17_2 下降沿中断（替代 500ms I2C 轮询） */
            touch_irq_arm();
            LOG_I("screen off (%ds idle, touch to wake)", (int)(s_timeout_ms / 1000U));
        }
    }
    else
    {
        /* ---- 息屏态：触摸中断唤醒（P17_2 下降沿），500ms 轮询保底 ---- */
        if (rt_sem_take(&s_touch_wake_sem,
                        rt_tick_from_millisecond(SCREEN_SAVER_POLL_MS)) == RT_EOK ||
            touch_pressed())
        {
            touch_wakeup_cleanup();
            s_sleeping        = RT_FALSE;
            s_last_touch_tick = rt_tick_get();
            s_last_check_tick = rt_tick_get();
            backlight_on();
            /* 退出浅睡眠 */
            rt_pm_release(PM_SLEEP_MODE_IDLE);
            /* 唤醒保护：唤醒那一下触摸不上报为点击（lv_port_indev 层拦截），
             * 待手指抬起后自动解除，防止息屏唤醒瞬间误触发卡片 */
            lv_port_indev_guard_enter();
            LOG_I("wakeup by touch");
            return;
        }

        rt_thread_mdelay(SCREEN_SAVER_POLL_MS);
    }
}

void screen_saver_set_timeout(rt_uint32_t timeout_ms)
{
    /* 0 = 常亮；其余限制 >=10s，防止误设过小导致频繁息屏 */
    if ((timeout_ms == 0) || (timeout_ms >= 10U * 1000U))
    {
        s_timeout_ms = timeout_ms;
    }
}

void screen_saver_force_wakeup(void)
{
    if (!s_sleeping)
    {
        return;   /* 已在亮屏态，无需动作 */
    }

    /* 息屏态挂着触摸中断：定时器唤醒前先关闭，防止后续触摸误触发 */
    touch_wakeup_cleanup();
    s_sleeping        = RT_FALSE;
    s_last_touch_tick = rt_tick_get();
    s_last_check_tick = rt_tick_get();
    backlight_on();
    /* 退出浅睡眠（与息屏时的 request 配对） */
    rt_pm_release(PM_SLEEP_MODE_IDLE);
    /* 注意：不走 lv_port_indev_guard_enter()——那是触摸唤醒时拦截"唤醒那一下
     * 的触摸点击"，定时器唤醒没有对应触摸，不应拦截后续正常操作 */
    LOG_I("wakeup by timer");
}

void screen_saver_hold(void)
{
    s_hold = RT_TRUE;
}

void screen_saver_hold_release(void)
{
    s_hold = RT_FALSE;
    /* 解除锁定视为一次"最近操作"：从此刻重新计 60s，
     * 避免 DONE 期间长时间亮屏、释放后因旧触摸计时立刻息屏 */
    s_last_touch_tick = rt_tick_get();
    s_last_check_tick = rt_tick_get();
}
