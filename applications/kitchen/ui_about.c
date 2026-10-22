/*
 * 关于弹层实现（参赛功能批次 5）
 */
#include "ui_about.h"

#include <rtthread.h>

/* ---- 布局与配色 ---- */
#define ABOUT_MASK_W       800
#define ABOUT_MASK_H       480
#define ABOUT_PANEL_W      480
#define ABOUT_PANEL_H      344

#define ABOUT_CARD_BG      0xffffff
#define ABOUT_CARD_BORDER  0xcbd5e1
#define ABOUT_TEXT_TITLE   0x1e293b
#define ABOUT_TEXT_NAME    0x475569
#define ABOUT_TEXT_SMALL   0x64748b
#define ABOUT_DIVIDER      0xe2e8f0
#define ABOUT_BTN_BG       0x16a34a
#define ABOUT_BTN_TEXT     0xffffff

static lv_obj_t *s_about_mask  = RT_NULL;
static lv_obj_t *s_about_panel = RT_NULL;

static lv_font_t const *s_f_title = RT_NULL;
static lv_font_t const *s_f_name  = RT_NULL;
static lv_font_t const *s_f_small = RT_NULL;

/* 遮罩点击：关闭 */
static void about_mask_event_cb(lv_event_t *e)
{
    RT_UNUSED(e);
    ui_about_close();
}

/* OK 按钮点击：关闭 */
static void about_ok_event_cb(lv_event_t *e)
{
    RT_UNUSED(e);
    ui_about_close();
}

void ui_about_init(lv_obj_t *screen,
                   lv_font_t const *f_title,
                   lv_font_t const *f_name,
                   lv_font_t const *f_small)
{
    lv_obj_t *lbl;
    lv_obj_t *line;
    lv_obj_t *btn;

    if ((screen == RT_NULL) || (s_about_mask != RT_NULL))
    {
        return;
    }

    s_f_title = f_title;
    s_f_name  = f_name;
    s_f_small = f_small;

    /* 全屏半透明遮罩（可点击，拦截并关闭） */
    s_about_mask = lv_obj_create(screen);
    lv_obj_set_pos(s_about_mask, 0, 0);
    lv_obj_set_size(s_about_mask, ABOUT_MASK_W, ABOUT_MASK_H);
    lv_obj_set_style_bg_color(s_about_mask, lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_about_mask, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_about_mask, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_about_mask, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_about_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_about_mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_about_mask, about_mask_event_cb, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_flag(s_about_mask, LV_OBJ_FLAG_HIDDEN);

    /* 中央面板 */
    s_about_panel = lv_obj_create(s_about_mask);
    lv_obj_set_size(s_about_panel, ABOUT_PANEL_W, ABOUT_PANEL_H);
    lv_obj_align(s_about_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_about_panel, lv_color_hex(ABOUT_CARD_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_about_panel, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_about_panel, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_about_panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_about_panel, lv_color_hex(ABOUT_CARD_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_about_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_about_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题 */
    lbl = lv_label_create(s_about_panel);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ABOUT_TEXT_TITLE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, s_f_title, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "About");
    lv_obj_set_pos(lbl, 20, 14);

    /* 分隔线 */
    line = lv_obj_create(s_about_panel);
    lv_obj_set_pos(line, 20, 54);
    lv_obj_set_size(line, ABOUT_PANEL_W - 40, 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(ABOUT_DIVIDER),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 项目名 */
    lbl = lv_label_create(s_about_panel);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ABOUT_TEXT_TITLE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, s_f_name, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "Smart Kitchen Panel");
    lv_obj_set_pos(lbl, 20, 72);

    /* 版本 + 编译日期 */
    lbl = lv_label_create(s_about_panel);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ABOUT_TEXT_SMALL),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, s_f_small, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_fmt(lbl, "Version 1.0.0   |   Built %s %s", __DATE__, __TIME__);
    lv_obj_set_pos(lbl, 20, 106);

    /* 技术栈清单 */
    lbl = lv_label_create(s_about_panel);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ABOUT_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, s_f_small, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "RT-Thread 5.0.2   |   LVGL 9.2\n"
                           "PSoC Edge E84 (M55)\n"
                           "Xunfei ASR (PTT)   |   SNTP\n"
                           "AHT20   |   WiFi 802.11ax\n"
                           "Open-Meteo Weather");
    lv_obj_set_pos(lbl, 20, 144);

    /* 作者 */
    lbl = lv_label_create(s_about_panel);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ABOUT_TEXT_SMALL),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, s_f_small, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "Author  :  ANBCWWT");
    lv_obj_set_pos(lbl, 20, 262);

    /* OK 按钮 */
    btn = lv_button_create(s_about_panel);
    lv_obj_set_size(btn, 120, 40);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -20, -18);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(ABOUT_BTN_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn, about_ok_event_cb, LV_EVENT_CLICKED, RT_NULL);

    lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ABOUT_BTN_TEXT),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, s_f_name, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "OK");
    lv_obj_center(lbl);
}

void ui_about_open(void)
{
    if (s_about_mask != RT_NULL)
    {
        lv_obj_clear_flag(s_about_mask, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_about_close(void)
{
    if (s_about_mask != RT_NULL)
    {
        lv_obj_add_flag(s_about_mask, LV_OBJ_FLAG_HIDDEN);
    }
}
