/*
 * 亮暗主题（参赛功能批次 3）
 *
 * 主题作用于"框架层"：屏幕背景、顶部标题/时间、底部状态栏、分隔线、右侧导航栏。
 * 各页面内部卡片/曲线维持自身配色（浅色卡片在暗色背景上呈现为白色面板，可接受）。
 * 切换主题后调用 ui_apply_theme()（kitchen_ui 实现）与 ui_page_apply_theme()
 * 重设框架对象；启动时在创建 UI 前调用 ui_theme_set_dark() 可让对象直接用主题色。
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include <lvgl.h>
#include <rtthread.h>

/* 框架层主题配色 */
typedef struct ui_theme_s
{
    lv_color_t bg;          /* 屏幕背景 */
    lv_color_t title_text;  /* 顶部标题/时间文字 */
    lv_color_t status_text; /* 底部状态栏文字 */
    lv_color_t divider;     /* 标题栏分隔线 */
    lv_color_t nav_bg;      /* 右侧导航栏背景 */
    lv_color_t nav_tab_bg;  /* 导航 tab 背景（非当前页） */
    lv_color_t nav_text;    /* 导航文字（非当前页） */
    lv_color_t nav_border;  /* 导航 tab 边框（非当前页） */
    lv_color_t nav_hl_bg;   /* 导航当前页高亮背景 */
    lv_color_t nav_hl_text; /* 导航当前页高亮文字 */
} ui_theme_t;

/* 当前主题（对象创建时读取它取色） */
extern const ui_theme_t *g_ui_theme;

/* 初始化：默认亮色 */
void ui_theme_init(void);

/* 切换亮/暗，并更新 g_ui_theme 指针 */
void ui_theme_set_dark(rt_bool_t dark);

/* 当前是否暗色 */
rt_bool_t ui_theme_is_dark(void);

/* 运行时重设框架对象颜色（screen/标题/时间/状态栏/分隔线）。
 * 由 kitchen_ui.c 实现，切换主题后调用。 */
void ui_apply_theme(void);

#endif /* UI_THEME_H */
