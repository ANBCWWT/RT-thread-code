/*
 * 开机动画实现（参赛功能批次 5）
 */
#include "ui_splash.h"

#include <rtthread.h>

/* ---- 布局与配色（与主界面一致：800x480 / 白卡 / 绿色高亮） ---- */
#define SPLASH_W          800
#define SPLASH_H          480
#define SPLASH_CARD_W     420
#define SPLASH_CARD_H     260

#define SPLASH_CARD_BG     0xffffff
#define SPLASH_CARD_BORDER 0xcbd5e1
#define SPLASH_TEXT_TITLE  0x1e293b
#define SPLASH_TEXT_NAME   0x475569
#define SPLASH_LOGO_GREEN  0x16a34a

static lv_obj_t *s_splash = RT_NULL;

/* ---- 动画执行回调 ---- */
static void splash_opa_exec(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v,
                         LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* 淡出完成：删除启动层 */
static void splash_fade_out_ready(lv_anim_t *a)
{
    RT_UNUSED(a);
    if (s_splash != RT_NULL)
    {
        lv_obj_delete(s_splash);
        s_splash = RT_NULL;
    }
}

/* 上滑完成：停顿后整体淡出 */
static void splash_slide_in_ready(lv_anim_t *a)
{
    lv_anim_t out;

    RT_UNUSED(a);
    if (s_splash == RT_NULL)
    {
        return;
    }

    lv_anim_init(&out);
    lv_anim_set_var(&out, s_splash);
    lv_anim_set_exec_cb(&out, splash_opa_exec);
    lv_anim_set_values(&out, 255, 0);
    lv_anim_set_time(&out, 500);
    lv_anim_set_delay(&out, 800);   /* 停在居中位置 800ms */
    lv_anim_set_path_cb(&out, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&out, splash_fade_out_ready);
    lv_anim_start(&out);
}

void ui_splash_play(lv_obj_t *screen, lv_color_t bg,
                    lv_font_t const *f_title, lv_font_t const *f_name)
{
    lv_obj_t *card;
    lv_obj_t *logo;
    lv_obj_t *lbl;
    lv_anim_t a;

    if ((screen == RT_NULL) || (s_splash != RT_NULL))
    {
        return;
    }

    /* 全屏启动层：创建即不透明（盖住首页，避免"先看到首页再闪动画"），
     * 可点击但无事件回调（拦截触摸防误触） */
    s_splash = lv_obj_create(screen);
    lv_obj_set_pos(s_splash, 0, 0);
    lv_obj_set_size(s_splash, SPLASH_W, SPLASH_H);
    lv_obj_set_style_bg_color(s_splash, bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_splash, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_splash, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_splash, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_splash, LV_OBJ_FLAG_CLICKABLE);

    /* 中央 logo 卡片（初始在屏幕下方，动画上滑到居中） */
    card = lv_obj_create(s_splash);
    lv_obj_set_size(card, SPLASH_CARD_W, SPLASH_CARD_H);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, SPLASH_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(SPLASH_CARD_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(SPLASH_CARD_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* logo 绿圆标（卡片上部居中） */
    logo = lv_obj_create(card);
    lv_obj_set_size(logo, 56, 56);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(logo, lv_color_hex(SPLASH_LOGO_GREEN),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(logo, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(logo, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 项目名 */
    lbl = lv_label_create(card);
    lv_obj_set_style_text_color(lbl, lv_color_hex(SPLASH_TEXT_TITLE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, f_title, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "SMART KITCHEN");
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 108);

    lbl = lv_label_create(card);
    lv_obj_set_style_text_color(lbl, lv_color_hex(SPLASH_TEXT_TITLE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, f_title, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "PANEL");
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 150);

    /* 副标题 */
    lbl = lv_label_create(card);
    lv_obj_set_style_text_color(lbl, lv_color_hex(SPLASH_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, f_name, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "Kitchen Terminal");
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -24);

    /* ---- 动画（启动层创建即不透明，无需淡入，直接滑入卡片） ---- */
    /* 卡片上滑（屏底 y=480 → 居中 y=(480-260)/2=110，800ms），
     * 完成回调负责停顿后淡出 */
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, SPLASH_H, (SPLASH_H - SPLASH_CARD_H) / 2);
    lv_anim_set_time(&a, 800);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, splash_slide_in_ready);
    lv_anim_start(&a);
}
