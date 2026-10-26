/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 低功耗阶段1：RT-Thread PM 电源管理框架接入
 *
 * - rt_pm_ops.sleep：浅睡眠 = CPU 执行 WFI（SysTick 保持运行，rt_tick 正常走）；
 * - timer_mask = 0：阶段1 不做 tickless（低功耗定时器 tick 补偿留阶段 2 实测）；
 * - WFI 会被"挂起且使能"的中断唤醒（PRIMASK 只屏蔽中断响应、不屏蔽 WFI 唤醒），
 *   唤醒后由 PM 框架 rt_pm_exit_critical 统一恢复中断上下文；
 * - 因此 kitchen_timer / 息屏轮询 / 天气线程等一切基于 rt_tick 的逻辑零改动、零回归。
 */
#include "drv_pm.h"

#include <rtthread.h>
#include <drivers/pm.h>

#define DBG_TAG "drv.pm"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* CPU 进入 WFI（等待中断）状态：ARMv8-M（Cortex-M55） */
static void cpu_wfi(void)
{
#if defined(__GNUC__)
    __asm volatile ("wfi" ::: "memory");
#else
    __asm volatile ("wfi");
#endif
}

/* PM 睡眠回调：进入浅睡眠 */
static void pm_sleep(struct rt_pm *pm, rt_uint8_t mode)
{
    RT_UNUSED(pm);
    if (mode > PM_SLEEP_MODE_NONE)
    {
        cpu_wfi();
    }
}

/* PM 运行模式回调：阶段1 不做 CPU 变频 */
static void pm_run(struct rt_pm *pm, rt_uint8_t mode)
{
    RT_UNUSED(pm);
    RT_UNUSED(mode);
}

static const struct rt_pm_ops drv_pm_ops =
{
    .sleep          = pm_sleep,
    .run            = pm_run,
    .timer_start    = RT_NULL,
    .timer_stop     = RT_NULL,
    .timer_get_tick = RT_NULL,
};

int drv_pm_init(void)
{
    /* timer_mask = 0：所有睡眠模式均不带低功耗定时器（阶段1 SysTick 保持走时） */
    rt_system_pm_init(&drv_pm_ops, 0, RT_NULL);
    LOG_I("pm framework ready (stage1: WFI light sleep, tickless disabled)");
    return 0;
}
INIT_APP_EXPORT(drv_pm_init);
