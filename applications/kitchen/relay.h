/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 继电器控制模块（智能厨房面板）
 * - 5 路继电器输出，高电平吸合
 * - 引脚：P5_0 ~ P5_4（未被板级外设占用，可直接 GPIO 操作）
 */
#ifndef KITCHEN_RELAY_H
#define KITCHEN_RELAY_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 继电器通道数 */
#define RELAY_CH_NUM        5

/* 通道索引（与 UI 卡片顺序保持一致） */
#define RELAY_IDX_LAMP       0   /* 厨房灯   */
#define RELAY_IDX_RANGE_HOOD 1   /* 油烟机   */
#define RELAY_IDX_FAN        2   /* 风扇     */
#define RELAY_IDX_DISHWASHER 3   /* 洗碗机   */
#define RELAY_IDX_OVEN       4   /* 烤箱     */

/* 初始化所有继电器输出（默认全部断开） */
int relay_init(void);

/* 设置指定通道开关状态：on = RT_TRUE 吸合 / RT_FALSE 断开 */
int relay_set(int idx, rt_bool_t on);

/* 读取指定通道当前状态 */
rt_bool_t relay_get(int idx);

/* 翻转指定通道状态 */
int relay_toggle(int idx);

/* 获取通道名称（用于 UI / 播报） */
const char *relay_name(int idx);

#ifdef __cplusplus
}
#endif

#endif /* KITCHEN_RELAY_H */
