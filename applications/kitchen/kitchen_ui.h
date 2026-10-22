/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL 主界面（智能厨房面板）
 *
 * 布局：800x480 横屏
 * +--------------------------------------------------------------+
 * | 顶部标题栏（标题 + 运行状态）                                   |
 * +--------------------------------------------------------------+
 * | [环境温湿度]  [厨房灯]  [油烟机]                                |  <- 第 1 行
 * | [风扇]      [插座1]   [插座2]                                 |  <- 第 2 行
 * +--------------------------------------------------------------+
 * | 底部状态栏（最近动作播报）                                      |
 * +--------------------------------------------------------------+
 *
 * - 6 个卡片：左上角为环境卡片（AHT20 温湿度），其余 5 个为继电器开关卡片
 * - 触屏点击开关卡片即可切换继电器，并自动播放"已打开/已关闭"提示音
 */
#ifndef KITCHEN_UI_H
#define KITCHEN_UI_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 创建并启动 UI 线程（内部完成 lv_init / 显示 / 触摸初始化） */
int kitchen_ui_init(void);

/* 更新环境温湿度显示（业务线程调用，内部加锁，线程安全） */
void kitchen_ui_update_env(float temperature_c, float humidity_pct);

/* 刷新全部继电器卡片状态（系统启动时同步一次） */
void kitchen_ui_update_relay_all(void);

/* 更新底部状态栏文本（业务线程可调用，线程安全） */
void kitchen_ui_show_status(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* KITCHEN_UI_H */
