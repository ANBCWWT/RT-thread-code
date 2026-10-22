/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 息屏管理：60s 无触摸自动息屏（LCD 背光电源关断），触摸任意点唤醒。
 * 唤醒后保持原背光亮度（仅操作背光电源引脚，不动 PWM 亮度状态）。
 */
#ifndef SCREEN_SAVER_H
#define SCREEN_SAVER_H

#include <rtthread.h>

/* 息屏状态查询：返回 RT_TRUE 表示当前处于息屏状态 */
rt_bool_t screen_saver_is_sleeping(void);

/* 初始化（仅复位状态变量，不碰硬件；须在触摸驱动初始化之后调用） */
int screen_saver_init(void);

/* 状态机推进：由 UI 线程主循环每轮调用。
 * - 亮屏态：检测触摸刷新计时，超时（默认 60s，可设置）无触摸自动息屏；
 * - 息屏态：内部以 500ms 间隔轮询触摸，按下即唤醒（内部阻塞等待）。 */
void screen_saver_loop(void);

/* 设置无触摸息屏超时（ms）。0 = 常亮（不自动息屏）；其余限制 >=10s。默认 60s。 */
void screen_saver_set_timeout(rt_uint32_t timeout_ms);

/* 强制唤醒（定时器到点等场景）：息屏时点亮背光并解除息屏态；
 * 已亮屏时无操作。唤醒不经过触摸路径，不触发触摸误触保护。 */
void screen_saver_force_wakeup(void);

#endif /* SCREEN_SAVER_H */
