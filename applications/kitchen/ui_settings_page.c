/*
 * 设置页实现（参赛功能批次 3）
 */
#include "ui_settings_page.h"

#include "ui_theme.h"
#include "ui_page.h"
#include "settings_store.h"
#include "screen_saver.h"
#include "ui_about.h"

#include <wavplayer.h>
#include <rtthread.h>

/* 背光接口：drv_lcd.c 已导出（去 static） */
extern rt_err_t lcd_backlight_set(rt_uint8_t percent);

/* ---- 布局（内容区 640x368） ---- */
#define SET_COL_X      20
#define SET_COL_W      480
#define SET_ROW_H      64
#define SET_ROW_Y0     44
#define SET_ROW_GAP    12

/* ---- 配色（浅色卡片，与系统页一致） ---- */
#define SET_COLOR_CARD_BG   0xffffff
#define SET_COLOR_BORDER    0xcbd5e1
#define SET_COLOR_TEXT_NAME 0x475569
#define SET_COLOR_TEXT_VALUE 0x0284c7
#define SET_COLOR_BTN_BG    0xf1f5f9
#define SET_COLOR_BTN_ON    0x16a34a
#define SET_COLOR_BTN_TEXT  0x475569
#define SET_COLOR_BTN_TEXT_ON 0xffffff

/* 息屏档位：30s / 1min / 5min / 常亮(0) */
static const rt_uint16_t s_sleep_opts[4]  = {30U, 60U, 300U, 0U};
static const char *const s_sleep_names[4] = {"30s", "1m", "5m", "Off"};
static const char *const s_theme_names[2] = {"Light", "Dark"};

static lv_obj_t *s_bright_slider = RT_NULL;
static lv_obj_t *s_bright_val    = RT_NULL;
static lv_obj_t *s_vol_slider    = RT_NULL;
static lv_obj_t *s_vol_val       = RT_NULL;
static lv_obj_t *s_sleep_btn[4];
static lv_obj_t *s_theme_btn[2];

/* 创建一行设置卡片（标题在左上） */
static lv_obj_t *settings_row_card(lv_obj_t *parent, int y, const char *name)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *t;

    lv_obj_set_pos(card, SET_COL_X, y);
    lv_obj_set_size(card, SET_COL_W, SET_ROW_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(SET_COLOR_CARD_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(SET_COLOR_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    t = lv_label_create(card);
    lv_obj_set_style_text_color(t, lv_color_hex(SET_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(t, name);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 4);

    return card;
}

/* 滑条事件：拖动实时生效，松手写 flash */
static void settings_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int v = (int)lv_slider_get_value(slider);

    if (slider == s_bright_slider)
    {
        lv_label_set_text_fmt(s_bright_val, "%d %%", v);
        lcd_backlight_set((rt_uint8_t)v);
        if (code == LV_EVENT_RELEASED)
        {
            settings_set_brightness((rt_uint8_t)v);
        }
    }
    else if (slider == s_vol_slider)
    {
        lv_label_set_text_fmt(s_vol_val, "%d", v);
        wavplayer_volume_set(v);
        if (code == LV_EVENT_RELEASED)
        {
            settings_set_volume((rt_uint8_t)v);
        }
    }
}

/* 息屏档位按钮高亮 */
static void settings_sleep_highlight(int sel)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        if (i == sel)
        {
            lv_obj_set_style_bg_color(s_sleep_btn[i], lv_color_hex(SET_COLOR_BTN_ON),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(s_sleep_btn[i], lv_color_hex(SET_COLOR_BTN_ON),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lv_obj_get_child(s_sleep_btn[i], 0),
                                        lv_color_hex(SET_COLOR_BTN_TEXT_ON),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            lv_obj_set_style_bg_color(s_sleep_btn[i], lv_color_hex(SET_COLOR_BTN_BG),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(s_sleep_btn[i], lv_color_hex(SET_COLOR_BORDER),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lv_obj_get_child(s_sleep_btn[i], 0),
                                        lv_color_hex(SET_COLOR_BTN_TEXT),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

/* 息屏档位点击 */
static void settings_sleep_btn_cb(lv_event_t *e)
{
    int idx = (int)(rt_base_t)lv_event_get_user_data(e);

    settings_set_sleep_sec(s_sleep_opts[idx]);
    screen_saver_set_timeout((rt_uint32_t)s_sleep_opts[idx] * 1000U);
    settings_sleep_highlight(idx);
}

/* 主题按钮高亮 */
static void settings_theme_highlight(int sel)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (i == sel)
        {
            lv_obj_set_style_bg_color(s_theme_btn[i], lv_color_hex(SET_COLOR_BTN_ON),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(s_theme_btn[i], lv_color_hex(SET_COLOR_BTN_ON),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lv_obj_get_child(s_theme_btn[i], 0),
                                        lv_color_hex(SET_COLOR_BTN_TEXT_ON),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            lv_obj_set_style_bg_color(s_theme_btn[i], lv_color_hex(SET_COLOR_BTN_BG),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(s_theme_btn[i], lv_color_hex(SET_COLOR_BORDER),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lv_obj_get_child(s_theme_btn[i], 0),
                                        lv_color_hex(SET_COLOR_BTN_TEXT),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

/* 主题切换：改主题指针 + 重设框架层 + 持久化 */
static void settings_theme_btn_cb(lv_event_t *e)
{
    int idx = (int)(rt_base_t)lv_event_get_user_data(e);

    ui_theme_set_dark(idx ? RT_TRUE : RT_FALSE);
    settings_set_dark((rt_uint8_t)idx);
    ui_apply_theme();
    ui_page_apply_theme();
    settings_theme_highlight(idx);
}

/* About 入口（批次 5）：弹出关于层 */
static void settings_about_cb(lv_event_t *e)
{
    RT_UNUSED(e);
    ui_about_open();
}

void ui_settings_page_init(lv_obj_t *parent)
{
    const settings_t *st = settings_get();
    lv_obj_t *card;
    lv_obj_t *lbl;
    lv_obj_t *slider;
    lv_obj_t *btn;
    int i;
    int sleep_sel = 1;   /* 默认 1min */

    /* 页标题 */
    lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, SET_COL_X, 8);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1e293b),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "Settings");

    /* About 入口按钮（批次 5）：标题行右侧，点击弹出关于层 */
    btn = lv_button_create(parent);
    lv_obj_set_pos(btn, 500, 6);
    lv_obj_set_size(btn, 92, 36);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(SET_COLOR_BTN_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(SET_COLOR_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn, settings_about_cb, LV_EVENT_CLICKED, RT_NULL);

    lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, lv_color_hex(SET_COLOR_BTN_TEXT),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "About");
    lv_obj_center(lbl);

    /* ---- 背光亮度 ---- */
    card = settings_row_card(parent, SET_ROW_Y0, "Brightness");

    slider = lv_slider_create(card);
    lv_obj_set_pos(slider, 150, 16);
    lv_obj_set_size(slider, 220, 14);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, st->brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, settings_slider_event_cb,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);
    lv_obj_add_event_cb(slider, settings_slider_event_cb,
                        LV_EVENT_RELEASED, RT_NULL);
    s_bright_slider = slider;

    s_bright_val = lv_label_create(card);
    lv_obj_set_pos(s_bright_val, 380, 6);
    lv_obj_set_size(s_bright_val, 90, 30);
    lv_obj_set_style_text_color(s_bright_val, lv_color_hex(SET_COLOR_TEXT_VALUE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_bright_val, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_fmt(s_bright_val, "%d %%", st->brightness);

    /* ---- 息屏时间 ---- */
    card = settings_row_card(parent, SET_ROW_Y0 + SET_ROW_H + SET_ROW_GAP, "Sleep");

    for (i = 0; i < 4; i++)
    {
        btn = lv_obj_create(card);
        lv_obj_set_pos(btn, 150 + i * (68 + 8), 8);
        lv_obj_set_size(btn, 68, 32);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, settings_sleep_btn_cb, LV_EVENT_CLICKED,
                            (void *)(rt_base_t)i);
        s_sleep_btn[i] = btn;

        lbl = lv_label_create(btn);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl, s_sleep_names[i]);

        if (s_sleep_opts[i] == st->sleep_sec)
        {
            sleep_sel = i;
        }
    }
    settings_sleep_highlight(sleep_sel);

    /* ---- 音量 ---- */
    card = settings_row_card(parent, SET_ROW_Y0 + 2 * (SET_ROW_H + SET_ROW_GAP), "Volume");

    slider = lv_slider_create(card);
    lv_obj_set_pos(slider, 150, 16);
    lv_obj_set_size(slider, 220, 14);
    lv_slider_set_range(slider, 0, 99);
    lv_slider_set_value(slider, st->volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, settings_slider_event_cb,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);
    lv_obj_add_event_cb(slider, settings_slider_event_cb,
                        LV_EVENT_RELEASED, RT_NULL);
    s_vol_slider = slider;

    s_vol_val = lv_label_create(card);
    lv_obj_set_pos(s_vol_val, 380, 6);
    lv_obj_set_size(s_vol_val, 90, 30);
    lv_obj_set_style_text_color(s_vol_val, lv_color_hex(SET_COLOR_TEXT_VALUE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_vol_val, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_fmt(s_vol_val, "%d", st->volume);

    /* ---- 亮暗主题 ---- */
    card = settings_row_card(parent, SET_ROW_Y0 + 3 * (SET_ROW_H + SET_ROW_GAP), "Theme");

    for (i = 0; i < 2; i++)
    {
        btn = lv_obj_create(card);
        lv_obj_set_pos(btn, 150 + i * (110 + 12), 8);
        lv_obj_set_size(btn, 110, 32);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, settings_theme_btn_cb, LV_EVENT_CLICKED,
                            (void *)(rt_base_t)i);
        s_theme_btn[i] = btn;

        lbl = lv_label_create(btn);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(lbl, s_theme_names[i]);
    }
    settings_theme_highlight(st->dark ? 1 : 0);
}

void ui_settings_page_refresh(void)
{
    /* 所有控件由事件回调自更新，无需周期刷新 */
}
