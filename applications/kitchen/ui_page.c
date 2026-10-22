/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 页面框架实现（参赛功能批次1）
 * - 右侧垂直导航栏：5 个 tab（图标 + 中文），当前页绿色高亮
 * - 5 个页面容器：常驻内存，切换时显示/隐藏
 */
#include "ui_page.h"
#include "ui_theme.h"

#include <rtthread.h>

/* 导航栏布局（800x480，右侧） */
#define NAV_W           100
#define NAV_X           680
#define NAV_Y           64
#define NAV_H           368
#define NAV_TAB_W       80
#define NAV_TAB_H       60
#define NAV_TAB_GAP     8
#define NAV_PAD         10

static lv_obj_t  *s_nav_bar      = RT_NULL;
static lv_obj_t  *s_tab[UI_PAGE_COUNT];
static lv_obj_t  *s_tab_icon[UI_PAGE_COUNT];
static lv_obj_t  *s_tab_text[UI_PAGE_COUNT];
static lv_obj_t  *s_container[UI_PAGE_COUNT];
static lv_font_t const *s_font_text = RT_NULL;
static void (*s_on_switch)(int idx) = RT_NULL;
static int        s_current      = UI_PAGE_HOME;

static const char *const s_tab_icon_txt[UI_PAGE_COUNT] =
{
    LV_SYMBOL_HOME, LV_SYMBOL_POWER, LV_SYMBOL_BARS, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS
};
static const char *const s_tab_name[UI_PAGE_COUNT] =
{
    "Home", "Device", "Env", "Sys", "Set"
};

/* 更新全部 tab 高亮（当前页绿底白字，其余主题色底/字） */
static void ui_page_refresh_tab(void)
{
    int i;

    for (i = 0; i < UI_PAGE_COUNT; i++)
    {
        if (i == s_current)
        {
            lv_obj_set_style_bg_color(s_tab[i], g_ui_theme->nav_hl_bg,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(s_tab[i], LV_OPA_COVER,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(s_tab[i], g_ui_theme->nav_hl_bg,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(s_tab_icon[i], g_ui_theme->nav_hl_text,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(s_tab_text[i], g_ui_theme->nav_hl_text,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            lv_obj_set_style_bg_color(s_tab[i], g_ui_theme->nav_tab_bg,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(s_tab[i], LV_OPA_COVER,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(s_tab[i], g_ui_theme->nav_border,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(s_tab_icon[i], g_ui_theme->nav_text,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(s_tab_text[i], g_ui_theme->nav_text,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

/* 主题切换后重设导航栏（由设置页调用） */
void ui_page_apply_theme(void)
{
    if (s_nav_bar == RT_NULL)
    {
        return;
    }
    lv_obj_set_style_bg_color(s_nav_bar, g_ui_theme->nav_bg,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_page_refresh_tab();
}

/* tab 点击 */
static void ui_page_tab_event_cb(lv_event_t *e)
{
    int idx = (int)(rt_base_t)lv_event_get_user_data(e);

    if (idx != s_current)
    {
        ui_page_switch(idx);
    }
}

int ui_page_switch(int idx)
{
    int old;

    if ((idx < 0) || (idx >= UI_PAGE_COUNT) || (s_container[idx] == RT_NULL))
    {
        return -1;
    }

    old = s_current;
    if (old != idx)
    {
        lv_obj_add_flag(s_container[old], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_container[idx], LV_OBJ_FLAG_HIDDEN);
        s_current = idx;
        ui_page_refresh_tab();
        if (s_on_switch != RT_NULL)
        {
            s_on_switch(idx);
        }
    }
    return 0;
}

int ui_page_current(void)
{
    return s_current;
}

lv_obj_t *ui_page_get_container(int idx)
{
    if ((idx < 0) || (idx >= UI_PAGE_COUNT))
    {
        return RT_NULL;
    }
    return s_container[idx];
}

int ui_page_init(lv_obj_t *screen, lv_font_t const *font_text, void (*on_switch)(int idx))
{
    int i;

    if ((screen == RT_NULL) || (font_text == RT_NULL))
    {
        return -1;
    }

    s_font_text = font_text;
    s_on_switch = on_switch;

    /* 右侧导航栏容器 */
    s_nav_bar = lv_obj_create(screen);
    lv_obj_set_pos(s_nav_bar, NAV_X, NAV_Y);
    lv_obj_set_size(s_nav_bar, NAV_W, NAV_H);
    lv_obj_set_style_bg_color(s_nav_bar, g_ui_theme->nav_bg,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_nav_bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_nav_bar, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_nav_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_nav_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_nav_bar, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0; i < UI_PAGE_COUNT; i++)
    {
        lv_obj_t *tab = lv_obj_create(s_nav_bar);

        /* tab：可点击容器 */
        lv_obj_set_size(tab, NAV_TAB_W, NAV_TAB_H);
        lv_obj_set_pos(tab, NAV_PAD, NAV_PAD + i * (NAV_TAB_H + NAV_TAB_GAP));
        lv_obj_set_style_radius(tab, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(tab, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tab, ui_page_tab_event_cb, LV_EVENT_CLICKED,
                            (void *)(rt_base_t)i);
        s_tab[i] = tab;

        /* 图标（LVGL 内置 symbol 字体，默认字体渲染） */
        s_tab_icon[i] = lv_label_create(tab);
        lv_obj_set_style_text_color(s_tab_icon[i], g_ui_theme->nav_text,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_tab_icon[i], s_tab_icon_txt[i]);
        lv_obj_align(s_tab_icon[i], LV_ALIGN_TOP_MID, 0, 6);

        /* 文字（中文子集字体） */
        s_tab_text[i] = lv_label_create(tab);
        lv_obj_set_style_text_color(s_tab_text[i], g_ui_theme->nav_text,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(s_tab_text[i], s_font_text,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_tab_text[i], s_tab_name[i]);
        lv_obj_align(s_tab_text[i], LV_ALIGN_BOTTOM_MID, 0, -4);

        /* 页面容器：透明、无边框、不可滚动；初始仅主页显示 */
        s_container[i] = lv_obj_create(screen);
        lv_obj_set_pos(s_container[i], UI_PAGE_CONTENT_X, UI_PAGE_CONTENT_Y);
        lv_obj_set_size(s_container[i], UI_PAGE_CONTENT_W, UI_PAGE_CONTENT_H);
        lv_obj_set_style_bg_opa(s_container[i], LV_OPA_TRANSP,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(s_container[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(s_container[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(s_container[i], LV_OBJ_FLAG_SCROLLABLE);
        if (i != UI_PAGE_HOME)
        {
            lv_obj_add_flag(s_container[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    s_current = UI_PAGE_HOME;
    ui_page_refresh_tab();
    return 0;
}
