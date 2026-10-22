/*
 * 亮暗主题实现（参赛功能批次 3）
 *
 * 注意：lv_color_hex() 是函数而非宏（lvgl_9.2.0/src/misc/lv_color.c:264），
 * 函数调用不是编译期常量，不能做 static const 结构体初始化，
 * 因此两套主题在 ui_theme_init() 里运行时填充。
 */
#include "ui_theme.h"

/* 亮色主题：与既有 UI 配色一致 */
static ui_theme_t s_theme_light;

/* 暗色主题：深灰蓝背景 + 浅色文字，高亮保持绿色 */
static ui_theme_t s_theme_dark;

const ui_theme_t *g_ui_theme = RT_NULL;
static rt_bool_t s_dark = RT_FALSE;

void ui_theme_init(void)
{
    /* 亮色 */
    s_theme_light.bg          = lv_color_hex(0xf1f5f9);
    s_theme_light.title_text  = lv_color_hex(0x1e293b);
    s_theme_light.status_text = lv_color_hex(0x64748b);
    s_theme_light.divider     = lv_color_hex(0xcbd5e1);
    s_theme_light.nav_bg      = lv_color_hex(0xe2e8f0);
    s_theme_light.nav_tab_bg  = lv_color_hex(0xffffff);
    s_theme_light.nav_text    = lv_color_hex(0x475569);
    s_theme_light.nav_border  = lv_color_hex(0xcbd5e1);
    s_theme_light.nav_hl_bg   = lv_color_hex(0x16a34a);
    s_theme_light.nav_hl_text = lv_color_hex(0xffffff);

    /* 暗色 */
    s_theme_dark.bg           = lv_color_hex(0x0f172a);
    s_theme_dark.title_text   = lv_color_hex(0xf1f5f9);
    s_theme_dark.status_text  = lv_color_hex(0x94a3b8);
    s_theme_dark.divider      = lv_color_hex(0x334155);
    s_theme_dark.nav_bg       = lv_color_hex(0x1e293b);
    s_theme_dark.nav_tab_bg   = lv_color_hex(0x334155);
    s_theme_dark.nav_text     = lv_color_hex(0x94a3b8);
    s_theme_dark.nav_border   = lv_color_hex(0x334155);
    s_theme_dark.nav_hl_bg    = lv_color_hex(0x16a34a);
    s_theme_dark.nav_hl_text  = lv_color_hex(0xffffff);

    g_ui_theme = &s_theme_light;
    s_dark = RT_FALSE;
}

void ui_theme_set_dark(rt_bool_t dark)
{
    s_dark = (dark != RT_FALSE) ? RT_TRUE : RT_FALSE;
    g_ui_theme = s_dark ? &s_theme_dark : &s_theme_light;
}

rt_bool_t ui_theme_is_dark(void)
{
    return s_dark;
}
