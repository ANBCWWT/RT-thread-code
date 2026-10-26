/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 主页扩展卡片实现（参赛功能批次 4）
 *
 * 配色沿用现有卡片体系（明亮风格，与 ui_theme 主题解耦）：
 * 背景白 / 浅蓝天气卡 / 深灰文字 / 亮蓝数值 / 绿色高亮按钮。
 */
#include "ui_home_widgets.h"

#include "weather.h"
#include "kitchen_timer.h"
#include "voice_prompt.h"
#include "screen_saver.h"

#include <rtthread.h>

/* 卡片配色（与 kitchen_ui.c 明亮风格一致） */
#define HW_CARD_BG        0xffffff   /* 定时器卡：白       */
#define HW_WEATHER_BG     0xe0f2fe   /* 天气卡：浅蓝       */
#define HW_BORDER         0xcbd5e1   /* 卡片描边           */
#define HW_TEXT_TITLE     0x1e293b   /* 标题深灰           */
#define HW_TEXT_NAME      0x475569   /* 次级文字           */
#define HW_TEXT_VALUE     0x0284c7   /* 数值亮蓝           */
#define HW_BTN_BG         0xf1f5f9   /* 按钮底             */
#define HW_BTN_HL         0x16a34a   /* 高亮绿             */
#define HW_BTN_HL_TEXT    0xffffff   /* 高亮按钮文字白     */
#define HW_WARN           0xdc2626   /* 警示红（到点/离线） */

/* ---- 天气卡 ---- */
#define WEATHER_CARD_X    300
#define WEATHER_CARD_Y    0
#define WEATHER_CARD_W    340
#define WEATHER_CARD_H    220

/* ---- 定时器卡 ---- */
#define TIMER_CARD_X      0
#define TIMER_CARD_Y      232
#define TIMER_CARD_W      640
#define TIMER_CARD_H      132

/* 预设档位（分钟） */
#define TIMER_PRESETS_COUNT  3
#define TIMER_PRESETS        { 3, 5, 10 }

/* 到点循环响铃：间隔（ms）。到点播第一声，此后每隔该间隔再播一次，直到按 OK 清除。
 * 节流放在 ui_home_widgets_tick()（UI 线程每秒执行）里，不做独立软定时器——
 * 软定时器线程栈仅 512B，voice_prompt_play 内部有文件系统 stat 调用，栈不够。 */
#define TIMER_RING_INTERVAL_MS  3000

static lv_font_t const *s_font_name  = RT_NULL;
static lv_font_t const *s_font_value = RT_NULL;

/* 天气卡对象 */
static lv_obj_t *s_weather_card   = RT_NULL;
static lv_obj_t *s_weather_temp   = RT_NULL;
static lv_obj_t *s_weather_desc   = RT_NULL;
static lv_obj_t *s_weather_hum    = RT_NULL;
static lv_obj_t *s_weather_offl   = RT_NULL;

/* 定时器卡对象 */
static lv_obj_t *s_timer_card     = RT_NULL;
static lv_obj_t *s_timer_count    = RT_NULL;   /* mm:ss 大字 */
static lv_obj_t *s_timer_status   = RT_NULL;   /* 底部状态行 */
static lv_obj_t *s_timer_main     = RT_NULL;   /* 合一按钮：Start/Cancel/OK */
static lv_obj_t *s_timer_custom   = RT_NULL;   /* "xx min" 文字 */

static int s_custom_min = 15;   /* 自定义分钟数（±1 步进，1~99） */

/* DONE 闪烁：边框在红/白间翻转 */
static int  s_done_blink    = 0;
static char s_last_count[8] = {0};   /* 倒计时文本缓存，避免每秒无效重绘 */

/* 循环响铃节流：上次响铃时刻（到点第一声也算），每 TIMER_RING_INTERVAL_MS 一声 */
static rt_tick_t s_last_ring_tick = 0;

/* ---- 通用小工具 ---- */
static void card_style(lv_obj_t *card, uint32_t bg)
{
    lv_obj_set_style_bg_color(card, lv_color_hex(bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(HW_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(card, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x94a3b8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* 文字 label：字体 / 颜色 */
static lv_obj_t *make_label(lv_obj_t *parent, lv_font_t const *font, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);

    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    return lbl;
}

/* 按钮：底 / 文字（返回按钮对象，文字由调用方设） */
static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                          lv_event_cb_t cb, void *ud)
{
    lv_obj_t *btn = lv_button_create(parent);

    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(HW_BTN_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(HW_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (cb != RT_NULL)
    {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    }
    return btn;
}

/* 按钮文字（居中），返回文字 label 供后续改内容 */
static lv_obj_t *btn_label_center(lv_obj_t *btn, lv_font_t const *font, const char *text,
                                  uint32_t color)
{
    lv_obj_t *lbl = make_label(btn, font, color);

    lv_label_set_text(lbl, (text != RT_NULL) ? text : "");
    lv_obj_center(lbl);
    return lbl;
}

/* ---- 天气卡 ---- */
static void ui_create_weather_card(lv_obj_t *home)
{
    lv_obj_t *card = lv_obj_create(home);

    lv_obj_set_pos(card, WEATHER_CARD_X, WEATHER_CARD_Y);
    lv_obj_set_size(card, WEATHER_CARD_W, WEATHER_CARD_H);
    card_style(card, HW_WEATHER_BG);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_weather_card = card;

    /* 标题 + 城市（左上两行） */
    {
        lv_obj_t *lbl = make_label(card, s_font_name, HW_TEXT_TITLE);
        lv_label_set_text(lbl, "WEATHER");
        lv_obj_set_pos(lbl, 16, 12);

        lbl = make_label(card, s_font_name, HW_TEXT_NAME);
        lv_label_set_text_fmt(lbl, "%s", weather_city());
        lv_obj_set_pos(lbl, 16, 42);
    }

    /* 温度大字（居中，2026-10-22 用户反馈太靠上，整体下移：-30 → -10） */
    s_weather_temp = make_label(card, s_font_value, HW_TEXT_VALUE);
    lv_label_set_text(s_weather_temp, "--");
    lv_obj_align(s_weather_temp, LV_ALIGN_CENTER, 0, -10);

    /* 天气描述（温度下方，+22 → +38） */
    s_weather_desc = make_label(card, s_font_name, HW_TEXT_NAME);
    lv_label_set_text(s_weather_desc, "--");
    lv_obj_align(s_weather_desc, LV_ALIGN_CENTER, 0, 38);

    /* 湿度（左下） */
    s_weather_hum = make_label(card, s_font_name, HW_TEXT_NAME);
    lv_label_set_text(s_weather_hum, "Hum --");
    lv_obj_set_pos(s_weather_hum, 16, 184);

    /* Offline 标记（右上，默认隐藏） */
    s_weather_offl = make_label(card, s_font_name, HW_WARN);
    lv_label_set_text(s_weather_offl, "Offline");
    lv_obj_align(s_weather_offl, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_add_flag(s_weather_offl, LV_OBJ_FLAG_HIDDEN);
}

/* 天气刷新：数据变化才触碰 LVGL */
static void ui_refresh_weather(void)
{
    weather_t w;
    static char s_last_line[64] = {0};
    char line[64];

    weather_get(&w);

    if (!w.valid)
    {
        /* 从未成功：占位 "--"，隐藏 Offline */
        if (s_weather_offl != RT_NULL)
        {
            lv_obj_add_flag(s_weather_offl, LV_OBJ_FLAG_HIDDEN);
        }
        line[0] = '\0';
        if (rt_strcmp(line, s_last_line) != 0)
        {
            rt_strncpy(s_last_line, line, sizeof(s_last_line) - 1);
            lv_label_set_text(s_weather_temp, "--");
            lv_label_set_text(s_weather_desc, "--");
            lv_label_set_text(s_weather_hum, "Hum --");
        }
        return;
    }

    /* 有效数据：温度/描述/湿度 + Offline 按需显示 */
    rt_snprintf(line, sizeof(line), "%.1fC|%s|%d",
                w.temp, weather_wmo_desc(w.code), w.hum);
    if (rt_strcmp(line, s_last_line) != 0)
    {
        rt_strncpy(s_last_line, line, sizeof(s_last_line) - 1);
        lv_label_set_text_fmt(s_weather_temp, "%.1f C", w.temp);
        lv_label_set_text(s_weather_desc, weather_wmo_desc(w.code));
        lv_label_set_text_fmt(s_weather_hum, "Hum %d%%", w.hum);
    }

    if (w.offline)
    {
        lv_obj_clear_flag(s_weather_offl, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_weather_offl, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- 定时器卡 ---- */

/* 预设按钮点击：启动对应分钟 */
static void timer_preset_event_cb(lv_event_t *e)
{
    int min = (int)(rt_base_t)lv_event_get_user_data(e);

    voice_prompt_play_button();
    kitchen_timer_start(min);
}

/* 定时器到点回调（kitchen_ui 主循环每秒 tick 触发）：
 * 立即播第一声提示音 + 息屏时强制亮屏，然后锁定息屏——
 * 到点状态持续亮屏提醒；后续每 TIMER_RING_INTERVAL_MS 一声由
 * ui_home_widgets_tick() 节流触发，直到用户按 OK 清除（timer_main_cb）。 */
static void timer_on_expire(void)
{
    voice_prompt_play_button();
    screen_saver_force_wakeup();
    s_last_ring_tick = rt_tick_get();
    screen_saver_hold();
}

/* 自定义 -1：只调整分钟并刷新显示，不启动（用户拍板：加减完按 Start 才开始） */
static void timer_custom_minus_cb(lv_event_t *e)
{
    RT_UNUSED(e);
    voice_prompt_play_button();
    if (s_custom_min > 1)
    {
        s_custom_min--;
    }
    lv_label_set_text_fmt(s_timer_custom, "%d min", s_custom_min);
}

static void timer_custom_plus_cb(lv_event_t *e)
{
    RT_UNUSED(e);
    voice_prompt_play_button();
    if (s_custom_min < 99)
    {
        s_custom_min++;
    }
    lv_label_set_text_fmt(s_timer_custom, "%d min", s_custom_min);
}

/* 合一按钮（右半区，省位）：IDLE=Start（当前自定义分钟）/ RUNNING=Cancel /
 * DONE=OK（清除）。文本与配色在 refresh 里切，回调按状态分派。 */
static void timer_main_cb(lv_event_t *e)
{
    kitchen_timer_state_t st = kitchen_timer_state();

    RT_UNUSED(e);
    voice_prompt_play_button();
    if (st == KITCHEN_TIMER_IDLE)
    {
        kitchen_timer_start(s_custom_min);
    }
    else
    {
        kitchen_timer_cancel();
        /* 用户按 OK/Cancel 清除：解除息屏锁定（循环响铃节流随之失效） */
        screen_saver_hold_release();
    }
}

static void ui_create_timer_card(lv_obj_t *home)
{
    static const int presets[TIMER_PRESETS_COUNT] = TIMER_PRESETS;
    lv_obj_t *card = lv_obj_create(home);
    lv_obj_t *lbl;
    lv_obj_t *btn;
    int i;

    lv_obj_set_pos(card, TIMER_CARD_X, TIMER_CARD_Y);
    lv_obj_set_size(card, TIMER_CARD_W, TIMER_CARD_H);
    card_style(card, HW_CARD_BG);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_timer_card = card;

    /* 标题 */
    lbl = make_label(card, s_font_name, HW_TEXT_TITLE);
    lv_label_set_text(lbl, "KITCHEN TIMER");
    lv_obj_set_pos(lbl, 16, 10);

    /* ---- 左半（y=44）：预设 3 按钮，点击即启动（快捷） ---- */
    for (i = 0; i < TIMER_PRESETS_COUNT; i++)
    {
        lv_obj_t *txt;

        btn = make_btn(card, 16 + i * (76 + 12), 44, 76, 44,
                       timer_preset_event_cb, (void *)(rt_base_t)presets[i]);
        txt = btn_label_center(btn, s_font_name, NULL, HW_TEXT_TITLE);
        lv_label_set_text_fmt(txt, "%dmin", presets[i]);
    }

    /* ---- 中间：倒计时大字 mm:ss（34px，居中于卡中心） ---- */
    s_timer_count = make_label(card, s_font_value, HW_TEXT_VALUE);
    lv_label_set_text(s_timer_count, "--:--");
    lv_obj_set_pos(s_timer_count, 275, 44);

    /* ---- 右半（x=390 起）：自定义区（行1）+ 合一按钮（行2）
     * 2026-10-22 用户反馈：右半上方空余多，整组上移放大。 ---- */
    btn = make_btn(card, 390, 32, 44, 50, timer_custom_minus_cb, RT_NULL);
    btn_label_center(btn, s_font_name, "-", HW_TEXT_TITLE);

    s_timer_custom = make_label(card, s_font_name, HW_TEXT_TITLE);
    lv_label_set_text_fmt(s_timer_custom, "%d min", s_custom_min);
    lv_obj_set_pos(s_timer_custom, 442, 45);

    btn = make_btn(card, 528, 32, 44, 50, timer_custom_plus_cb, RT_NULL);
    btn_label_center(btn, s_font_name, "+", HW_TEXT_TITLE);

    /* 合一按钮：210 宽盛得下 "Cancel"，三态共用一位 */
    s_timer_main = make_btn(card, 390, 88, 210, 40, timer_main_cb, RT_NULL);
    btn_label_center(s_timer_main, s_font_name, "Start 15 min", HW_BTN_HL_TEXT);
    lv_obj_set_style_bg_color(s_timer_main, lv_color_hex(HW_BTN_HL),
                              LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ---- 状态行（左半预设按钮下方） ---- */
    s_timer_status = make_label(card, s_font_name, HW_TEXT_NAME);
    lv_label_set_text(s_timer_status, "Set minutes, then Start");
    lv_obj_set_pos(s_timer_status, 16, 94);
}

/* 刷新定时器卡（每秒）：
 * - 倒计时三态：IDLE "--:--"灰 / RUNNING mm:ss 蓝 / DONE "00:00"红
 * - 合一按钮三态：IDLE=Start xx min（绿底白字，随自定义值变）/ RUNNING=Cancel
 *   （红字白底）/ DONE=OK（绿底白字）；始终显示，不占两个按钮位
 * - DONE 红字 + 边框红白交替闪烁 */
static void ui_refresh_timer(void)
{
    kitchen_timer_state_t st = kitchen_timer_state();
    lv_obj_t *main_lbl;
    char buf[8];
    const char *status = RT_NULL;

    /* ---- 倒计时文本与颜色 ---- */
    if (st == KITCHEN_TIMER_RUNNING)
    {
        int sec = kitchen_timer_remaining_sec();

        rt_snprintf(buf, sizeof(buf), "%02d:%02d", sec / 60, sec % 60);
        if (rt_strcmp(buf, s_last_count) != 0)
        {
            rt_strncpy(s_last_count, buf, sizeof(s_last_count) - 1);
            s_last_count[sizeof(s_last_count) - 1] = '\0';
            lv_label_set_text(s_timer_count, buf);
        }
        lv_obj_set_style_text_color(s_timer_count, lv_color_hex(HW_TEXT_VALUE),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else
    {
        s_last_count[0] = '\0';
        if (st == KITCHEN_TIMER_DONE)
        {
            lv_label_set_text(s_timer_count, "00:00");
            lv_obj_set_style_text_color(s_timer_count, lv_color_hex(HW_WARN),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else   /* IDLE */
        {
            lv_label_set_text(s_timer_count, "--:--");
            lv_obj_set_style_text_color(s_timer_count, lv_color_hex(HW_TEXT_NAME),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    /* ---- 合一按钮三态 ---- */
    main_lbl = lv_obj_get_child(s_timer_main, 0);
    if (st == KITCHEN_TIMER_IDLE)
    {
        lv_label_set_text_fmt(main_lbl, "Start %d min", s_custom_min);
        lv_obj_set_style_bg_color(s_timer_main, lv_color_hex(HW_BTN_HL),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(main_lbl, lv_color_hex(HW_BTN_HL_TEXT),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else if (st == KITCHEN_TIMER_RUNNING)
    {
        lv_label_set_text(main_lbl, "Cancel");
        lv_obj_set_style_bg_color(s_timer_main, lv_color_hex(HW_BTN_BG),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(main_lbl, lv_color_hex(HW_WARN),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else   /* DONE */
    {
        lv_label_set_text(main_lbl, "OK");
        lv_obj_set_style_bg_color(s_timer_main, lv_color_hex(HW_BTN_HL),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(main_lbl, lv_color_hex(HW_BTN_HL_TEXT),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    /* ---- 状态文字 + 边框闪烁 ---- */
    if (st == KITCHEN_TIMER_IDLE)
    {
        status = "Set minutes, then Start";
        lv_obj_set_style_border_color(s_timer_card, lv_color_hex(HW_BORDER),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_timer_status, lv_color_hex(HW_TEXT_NAME),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        s_done_blink = 0;
    }
    else if (st == KITCHEN_TIMER_RUNNING)
    {
        status = "Running...";
        lv_obj_set_style_border_color(s_timer_card, lv_color_hex(HW_BORDER),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_timer_status, lv_color_hex(HW_TEXT_NAME),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        s_done_blink = 0;
    }
    else   /* DONE */
    {
        status = "Time's up!";
        s_done_blink ^= 1;
        lv_obj_set_style_border_color(s_timer_card,
                                      lv_color_hex(s_done_blink ? HW_WARN : HW_BORDER),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_timer_status, lv_color_hex(HW_WARN),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_label_set_text(s_timer_status, status);
}

/* ---- 对外接口 ---- */
void ui_home_widgets_init(lv_obj_t *home_container,
                          lv_font_t const *font_title,
                          lv_font_t const *font_name,
                          lv_font_t const *font_value)
{
    if (home_container == RT_NULL)
    {
        return;
    }

    RT_UNUSED(font_title);
    s_font_name  = font_name;
    s_font_value = font_value;

    ui_create_weather_card(home_container);
    ui_create_timer_card(home_container);

    /* 定时器到点回调：播提示音 + 息屏强制唤醒（在 tick 的 UI 线程内执行） */
    kitchen_timer_set_expire_cb(timer_on_expire);

    ui_refresh_weather();
    ui_refresh_timer();
}

void ui_home_widgets_tick(void)
{
    /* 定时器状态机推进（到点回调播提示音 + 强制唤醒） */
    kitchen_timer_tick();

    /* 循环响铃：DONE 状态下每 TIMER_RING_INTERVAL_MS 再播一声，直到按 OK。
     * 放在 UI 线程每秒 tick（亮屏态，任何页面都执行），不依赖软定时器。 */
    if (kitchen_timer_state() == KITCHEN_TIMER_DONE)
    {
        if (rt_tick_get() - s_last_ring_tick >= rt_tick_from_millisecond(TIMER_RING_INTERVAL_MS))
        {
            s_last_ring_tick = rt_tick_get();
            voice_prompt_play_button();
        }
    }

    ui_refresh_weather();
    ui_refresh_timer();
}
