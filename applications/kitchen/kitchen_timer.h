/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 厨房定时器模块（参赛功能批次 4）
 *
 * 纯状态机 + 倒计时，不依赖 LVGL：
 * - 预设 3 / 5 / 10 分钟，自定义 1~99 分钟（UI 侧 ±1 步进）
 * - 基于 rt_tick 倒计时，息屏时由 UI 主循环继续 tick，照常到点
 * - 到点：置 DONE 状态，回调触发提示音 + 强制唤醒亮屏
 */
#ifndef KITCHEN_TIMER_H
#define KITCHEN_TIMER_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    KITCHEN_TIMER_IDLE = 0,   /* 未启动 */
    KITCHEN_TIMER_RUNNING,    /* 倒计时中 */
    KITCHEN_TIMER_DONE,       /* 已到点（等待取消/重新开始，UI 持续提示） */
} kitchen_timer_state_t;

/* 预设档位：3 / 5 / 10 分钟 */
#define KITCHEN_TIMER_PRESET_COUNT   3

/* 启动/重启定时器（minutes: 1~99），返回 0 成功 */
int kitchen_timer_start(int minutes);

/* 取消定时器（回到 IDLE），返回 0 成功 */
int kitchen_timer_cancel(void);

/* 注册到点回调（kitchen_ui 初始化时调用，只支持一个） */
void kitchen_timer_set_expire_cb(void (*cb)(void));

/* 当前状态 */
kitchen_timer_state_t kitchen_timer_state(void);

/* 剩余秒数（RUNNING 时有效；其他状态返回 0） */
int kitchen_timer_remaining_sec(void);

/* 每秒节拍（UI 主循环调用）：
 * RUNNING 到点 -> DONE，并通过回调通知（播提示音 + 强制唤醒） */
void kitchen_timer_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* KITCHEN_TIMER_H */
