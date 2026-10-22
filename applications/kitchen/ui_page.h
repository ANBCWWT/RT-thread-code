/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 页面框架（参赛功能批次1）：右侧垂直导航栏 + 5 个页面容器
 * - 导航栏：右侧 100px 宽，5 个 tab（LVGL 内置图标 + 中文字体），当前页高亮
 * - 页面容器：内容区 640x368，切换时显示/隐藏（对象常驻，不反复创建）
 *
 * 页面划分：
 *   主页(0) / 家电(1) / 环境(2) / 系统(3) / 设置(4)
 * 布局（800x480）：
 *   +---------------------------------------------------+
 *   | 顶部栏（标题 + 时间 + WiFi 图标）                    |
 *   +---------------------------------------+-----------+
 *   | 内容区 640x368（页面容器）              | 导航栏    |
 *   |                                       | 100px     |
 *   +---------------------------------------+-----------+
 *   | 底部状态栏                                        |
 *   +---------------------------------------------------+
 */
#ifndef UI_PAGE_H
#define UI_PAGE_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 页面索引 */
#define UI_PAGE_HOME       0
#define UI_PAGE_APPLIANCE  1
#define UI_PAGE_ENV        2
#define UI_PAGE_SYS        3
#define UI_PAGE_SETTINGS   4
#define UI_PAGE_COUNT      5

/* 页面内容区布局常量（右侧导航栏占 x>=680 区域，内容区在左侧） */
#define UI_PAGE_CONTENT_X  20
#define UI_PAGE_CONTENT_Y  64
#define UI_PAGE_CONTENT_W  640
#define UI_PAGE_CONTENT_H  368

/* 初始化页面框架：创建右侧导航栏 + 5 个页面容器。
 * screen：活动屏幕（需已创建并设好背景）；font_text：导航文字字体（中文）；
 * on_switch：页面切换回调（可为 NULL）。返回 0 成功。 */
int ui_page_init(lv_obj_t *screen, lv_font_t const *font_text, void (*on_switch)(int idx));

/* 获取页面容器（kitchen_ui 等模块向其中添加页面内容） */
lv_obj_t *ui_page_get_container(int idx);

/* 切换当前页面（0~4），内部更新导航高亮；返回 0 成功 */
int ui_page_switch(int idx);

/* 当前页面索引 */
int ui_page_current(void);

/* 主题切换后重设导航栏配色与高亮（由设置页调用） */
void ui_page_apply_theme(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_PAGE_H */
