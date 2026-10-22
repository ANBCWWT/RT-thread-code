/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 主页扩展卡片（参赛功能批次 4）：
 * - 天气卡：城市 / 当前温度 / 天气描述 / 湿度，无网或失败显示 "--" + Offline
 * - 厨房定时器卡：预设 3/5/10 分钟 + 自定义 ±1 分钟，倒计时 mm:ss，
 *   到点闪烁提示 + 提示音 + 息屏强制唤醒
 *
 * 布局（主页内容区 640x368）：
 *   +------------------------------------------------------+
 *   | 环境卡 280x220 (0,0)  | 天气卡 340x220 (300,0)        |
 *   +------------------------------------------------------+
 *   | 定时器卡 640x132 (0,232)                               |
 *   +------------------------------------------------------+
 */
#ifndef UI_HOME_WIDGETS_H
#define UI_HOME_WIDGETS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化主页两张卡（挂到主页容器，字体由 kitchen_ui 传入） */
void ui_home_widgets_init(lv_obj_t *home_container,
                          lv_font_t const *font_title,
                          lv_font_t const *font_name,
                          lv_font_t const *font_value);

/* 每秒节拍（UI 主循环调用）：
 * - 天气文本变化才刷新（含 offline 标记）
 * - 定时器剩余 mm:ss 每秒刷新；状态机 tick + DONE 闪烁 */
void ui_home_widgets_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_HOME_WIDGETS_H */
