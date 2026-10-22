/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL 主界面实现（智能厨房面板）
 *
 * 依赖说明：
 * - LVGL 9.2（工程自带），LV_USE_OS = LV_OS_RTTHREAD，lv_lock/lv_unlock 可用
 * - 字体：LVGL 内置 Montserrat（英文 UI，开源无版权问题；不再依赖中文字库）
 * - 显示/触摸端口：libraries/Common/board/ports/lvgl（lv_port_disp / lv_port_indev）
 */
#include "kitchen_ui.h"

#include <lvgl.h>
#include <board.h>

#include "relay.h"
#include "voice_prompt.h"
#include "voice_ctrl/sntp.h"
#include "voice_ctrl/wifi_link.h"
#include "screen_saver.h"
#include "ui_page.h"
#include "ui_theme.h"
#include "settings_store.h"
#include "ui_settings_page.h"
#include "env_history.h"
#include "ui_env_page.h"
#include "ui_sys_page.h"
#include "weather.h"
#include "ui_home_widgets.h"
#include "kitchen_timer.h"
#include "ui_about.h"
#include "ui_splash.h"

#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include <wavplayer.h>

/* 背光接口：drv_lcd.c 已导出（批次 3 去 static） */
extern rt_err_t lcd_backlight_set(rt_uint8_t percent);

/* ============================================================================
 * 布局常量（800 x 480 横屏）
 * ==========================================================================*/
#define UI_SCREEN_W        800
#define UI_SCREEN_H        480

#define UI_TITLE_BAR_H     56
#define UI_STATUS_BAR_H    40
#define UI_STATUS_BAR_Y    (UI_SCREEN_H - UI_STATUS_BAR_H)

#define UI_MARGIN          20

/* 环境卡片（主页内容区左上角） */
#define UI_CARD_ENV_W      280
#define UI_CARD_ENV_H      220

/* 家电卡片（家电页 3 列网格：3*200 + 2*20 = 640 = 内容区宽） */
#define UI_CARD_APP_W      200
#define UI_CARD_APP_H      160
#define UI_CARD_APP_GAP    20

/* 模式按钮配色 */
#define COLOR_MODE_BTN     0xf1f5f9

/* 字体字号 */
#define FONT_SIZE_TITLE    28
#define FONT_SIZE_NAME     22
#define FONT_SIZE_VALUE    34
#define FONT_SIZE_SMALL    20

/* 继电器卡片点击防抖间隔（ms）：同一卡片两次点击至少间隔 1 秒，防止连触误触发 */
#define UI_CLICK_DEBOUNCE_MS  1000

/* 配色（明亮风格） */
#define COLOR_BG           0xf1f5f9   /* 屏幕背景：浅蓝灰     */
#define COLOR_TITLE_BAR    0xffffff   /* 标题栏：白           */
#define COLOR_CARD_OFF     0xffffff   /* 卡片（关闭）：白      */
#define COLOR_CARD_ON      0x16a34a   /* 卡片（开启）：亮绿    */
#define COLOR_CARD_ENV     0xe0f2fe   /* 环境卡片：浅蓝       */
#define COLOR_CARD_BORDER  0xcbd5e1   /* 卡片描边：浅灰       */
#define COLOR_CARD_SHADOW  0x94a3b8   /* 卡片阴影             */
#define COLOR_TEXT_TITLE   0x1e293b   /* 标题：深蓝灰         */
#define COLOR_TEXT_NAME    0x475569   /* 卡片名：中灰         */
#define COLOR_TEXT_OFF     0x94a3b8   /* 关闭状态：浅灰       */
#define COLOR_TEXT_ON      0xffffff   /* 开启状态：白（绿底） */
#define COLOR_TEXT_VALUE   0x0284c7   /* 温湿度数值：亮蓝     */
#define COLOR_STATUS_TEXT  0x64748b   /* 状态栏文字：灰       */
#define COLOR_DOT_ON       0x22c55e   /* 状态圆点：亮绿       */
#define COLOR_DOT_OFF      0x94a3b8   /* 状态圆点：浅灰       */

/* ============================================================================
 * 类型定义
 * ==========================================================================*/
typedef struct
{
    const char *name;        /* 卡片标题          */
    int relay_idx;           /* 继电器通道，-1=环境卡片 */
    rt_tick_t last_click;    /* 上次点击时刻（防抖）    */
    lv_obj_t *card;          /* 卡片容器          */
    lv_obj_t *name_label;    /* 标题 label        */
    lv_obj_t *state_label;   /* 状态 label（继电器卡片） */
    lv_obj_t *state_dot;     /* 状态圆点（继电器卡片）   */
    lv_obj_t *value_label;   /* 数值 label（环境卡片）  */
    lv_obj_t *extra_label;   /* 附加 label（环境卡片）  */
    const char *const *modes; /* 模式/强度选项（继电器卡片） */
    int mode_count;           /* 选项个数 */
    int mode_idx;             /* 当前档位索引（默认 0） */
    lv_obj_t *mode_btn;       /* 模式按钮（右下角） */
    lv_obj_t *mode_label;     /* 模式按钮内文字 label */
} kitchen_card_t;

/* ============================================================================
 * 静态变量
 * ==========================================================================*/
static lv_obj_t *s_screen          = RT_NULL;
static lv_obj_t *s_title_label     = RT_NULL;
static lv_obj_t *s_time_label      = RT_NULL;
static lv_obj_t *s_divider         = RT_NULL;
static lv_obj_t *s_status_text     = RT_NULL;
static lv_font_t const *s_font_title   = RT_NULL;
static lv_font_t const *s_font_name    = RT_NULL;
static lv_font_t const *s_font_value   = RT_NULL;

/* WiFi 状态图标（右上角）：1 个圆点 + 3 条同心弧（信号强弱），
 * 有网=绿色满格，断线=灰色强弱循环动画 */
static lv_obj_t *s_wifi_dot         = RT_NULL;
static lv_obj_t *s_wifi_arc[3]      = {RT_NULL, RT_NULL, RT_NULL};
static int       s_wifi_level       = -1;   /* 当前档位缓存（0~3） */
static int       s_wifi_color       = -1;   /* 当前颜色缓存（0=灰 1=绿） */

/* 家电卡片模式/强度选项（纯 UI 档位，不映射继电器——继电器只有开/关） */
static const char *const s_modes_lamp[] = { "Low", "Med", "High" };
static const char *const s_modes_hood[] = { "Low", "Med", "High" };
static const char *const s_modes_fan[]  = { "Low", "Med", "High" };
static const char *const s_modes_dish[] = { "Std", "Strong", "Eco", "Fast" };
static const char *const s_modes_oven[] = { "Top", "Bot", "Both", "Hot" };

/* 6 个卡片：0 环境（主页），1~5 继电器（家电页，与 RELAY_IDX_* 一致） */
static kitchen_card_t s_cards[6] =
{
    { "ENV",    -1,                 0, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, 0, 0, RT_NULL, RT_NULL },
    { "Lamp",   RELAY_IDX_LAMP,       0, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, s_modes_lamp, 3, 0, RT_NULL, RT_NULL },
    { "Hood",   RELAY_IDX_RANGE_HOOD, 0, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, s_modes_hood, 3, 0, RT_NULL, RT_NULL },
    { "Fan",    RELAY_IDX_FAN,        0, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, s_modes_fan, 3, 0, RT_NULL, RT_NULL },
    { "Washer", RELAY_IDX_DISHWASHER, 0, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, s_modes_dish, 4, 0, RT_NULL, RT_NULL },
    { "Oven",   RELAY_IDX_OVEN,       0, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, RT_NULL, s_modes_oven, 4, 0, RT_NULL, RT_NULL },
};

/* ============================================================================
 * 内部函数
 * ==========================================================================*/
static void ui_set_card_style(lv_obj_t *card, uint32_t bg_color)
{
    lv_obj_set_style_bg_color(card, lv_color_hex(bg_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_CARD_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(card, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(card, lv_color_hex(COLOR_CARD_SHADOW), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* 创建卡片：0=环境卡片（主页），1~5=继电器卡片（家电页 3 列网格） */
static lv_obj_t *ui_create_card(int index)
{
    lv_obj_t *card;
    lv_obj_t *parent;

    if (index == 0)
    {
        parent = ui_page_get_container(UI_PAGE_HOME);
        card = lv_obj_create(parent);
        lv_obj_set_pos(card, 0, 0);
        lv_obj_set_size(card, UI_CARD_ENV_W, UI_CARD_ENV_H);
    }
    else
    {
        int n   = index - 1;
        int col = n % 3;
        int row = n / 3;
        parent = ui_page_get_container(UI_PAGE_APPLIANCE);
        card = lv_obj_create(parent);
        lv_obj_set_pos(card, col * (UI_CARD_APP_W + UI_CARD_APP_GAP),
                       row * (UI_CARD_APP_H + UI_CARD_APP_GAP));
        lv_obj_set_size(card, UI_CARD_APP_W, UI_CARD_APP_H);
    }

    ui_set_card_style(card, COLOR_CARD_OFF);

    return card;
}

/* 更新单个继电器卡片的外观 */
static void ui_update_relay_card(int index)
{
    kitchen_card_t *card = &s_cards[index];
    rt_bool_t on;

    if (card->relay_idx < 0)
    {
        return;
    }

    on = relay_get(card->relay_idx);

    if (on)
    {
        lv_obj_set_style_bg_color(card->card, lv_color_hex(COLOR_CARD_ON),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(card->card, lv_color_hex(COLOR_CARD_ON),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(card->name_label, lv_color_hex(COLOR_TEXT_ON),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(card->state_label, lv_color_hex(COLOR_TEXT_ON),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(card->state_dot, lv_color_hex(COLOR_DOT_ON),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(card->state_label, "ON");
    }
    else
    {
        lv_obj_set_style_bg_color(card->card, lv_color_hex(COLOR_CARD_OFF),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(card->card, lv_color_hex(COLOR_CARD_BORDER),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(card->name_label, lv_color_hex(COLOR_TEXT_NAME),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(card->state_label, lv_color_hex(COLOR_TEXT_OFF),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(card->state_dot, lv_color_hex(COLOR_DOT_OFF),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(card->state_label, "OFF");
    }
}

/* 继电器卡片点击事件回调（运行在 LVGL 线程内，可直接操作 UI） */
static void relay_card_event_cb(lv_event_t *e)
{
    int idx = (int)(rt_base_t)lv_event_get_user_data(e);
    kitchen_card_t *card;
    rt_tick_t now;
    char prompt[64];

    if ((idx < 0) || (idx >= 6) || (s_cards[idx].relay_idx < 0))
    {
        return;
    }

    card = &s_cards[idx];

    /* ---- 点击防抖：同一卡片 1 秒内的重复点击直接忽略 ----
     * 用 (rt_int32_t) 差值判断可正确处理 tick 回绕；每个卡片独立计时。 */
    now = rt_tick_get();
    if ((rt_int32_t)(now - card->last_click) <
        (rt_int32_t)rt_tick_from_millisecond(UI_CLICK_DEBOUNCE_MS))
    {
        return;
    }
    card->last_click = now;

    /* 切换继电器 */
    relay_toggle(card->relay_idx);

    /* 刷新卡片外观 */
    ui_update_relay_card(idx);

    /* 更新底部状态栏，并播放点击音效（button.wav）。
     * 注：如需恢复"点击后播人声（已打开/已关闭）"，把下面
     * voice_prompt_play_button() 换成 voice_prompt_play_on() /
     * voice_prompt_play_off()（按状态分支），并注释掉本行。 */
    if (relay_get(card->relay_idx))
    {
        rt_snprintf(prompt, sizeof(prompt), "%s ON", card->name);
    }
    else
    {
        rt_snprintf(prompt, sizeof(prompt), "%s OFF", card->name);
    }

    voice_prompt_play_button();
    lv_label_set_text(s_status_text, prompt);
}

/* ============================================================================
 * 模式/强度选择弹层（模态）：全屏半透明遮罩 + 中央面板
 * 档位只存 s_cards[].mode_idx（纯 UI 状态，不驱动继电器——继电器只有开/关）
 * ==========================================================================*/
#define MODE_OPT_MAX        4   /* 最大选项数（洗碗机/烤箱 4 项） */
#define MODE_PANEL_W        340
#define MODE_PANEL_H        320

static lv_obj_t *s_mode_mask       = RT_NULL;
static lv_obj_t *s_mode_title      = RT_NULL;
static lv_obj_t *s_mode_opts[MODE_OPT_MAX];
static lv_obj_t *s_mode_opt_lbl[MODE_OPT_MAX];
static int       s_mode_card_idx   = 0;   /* 当前操作的卡片索引 1~5 */

/* 打开指定卡片（1~5）的模式选择面板 */
static void ui_mode_panel_open(int card_idx)
{
    kitchen_card_t *card = &s_cards[card_idx];
    int i;

    if ((card_idx < 1) || (card_idx >= 6) || (s_mode_mask == RT_NULL))
    {
        return;
    }

    s_mode_card_idx = card_idx;
    lv_label_set_text_fmt(s_mode_title, "%s - Mode", card->name);

    for (i = 0; i < MODE_OPT_MAX; i++)
    {
        if (i < card->mode_count)
        {
            lv_obj_clear_flag(s_mode_opts[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_mode_opt_lbl[i], card->modes[i]);
            if (i == card->mode_idx)
            {
                lv_obj_set_style_bg_color(s_mode_opts[i], lv_color_hex(COLOR_CARD_ON),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_text_color(s_mode_opt_lbl[i], lv_color_hex(COLOR_TEXT_ON),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            else
            {
                lv_obj_set_style_bg_color(s_mode_opts[i], lv_color_hex(COLOR_MODE_BTN),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_text_color(s_mode_opt_lbl[i], lv_color_hex(COLOR_TEXT_TITLE),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
        else
        {
            lv_obj_add_flag(s_mode_opts[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_clear_flag(s_mode_mask, LV_OBJ_FLAG_HIDDEN);
}

/* 选项点击：记录档位、更新按钮文字、播放点击音效、关闭 */
static void mode_opt_event_cb(lv_event_t *e)
{
    int opt = (int)(rt_base_t)lv_event_get_user_data(e);
    kitchen_card_t *card = &s_cards[s_mode_card_idx];

    if (opt >= card->mode_count)
    {
        return;
    }

    card->mode_idx = opt;
    lv_label_set_text_fmt(card->mode_label, "Mode: %s", card->modes[opt]);
    voice_prompt_play_button();
    lv_obj_add_flag(s_mode_mask, LV_OBJ_FLAG_HIDDEN);
}

/* 点击遮罩空白处：取消关闭 */
static void mode_mask_event_cb(lv_event_t *e)
{
    lv_obj_add_flag(s_mode_mask, LV_OBJ_FLAG_HIDDEN);
}

/* 卡片模式按钮点击 */
static void mode_btn_event_cb(lv_event_t *e)
{
    int idx = (int)(rt_base_t)lv_event_get_user_data(e);

    if ((idx >= 1) && (idx < 6))
    {
        ui_mode_panel_open(idx);
    }
}

/* 创建模式选择弹层（UI 初始化阶段调用，初始隐藏） */
static void ui_create_mode_panel(void)
{
    int i;

    /* 全屏半透明遮罩（可点击，拦截点击并关闭） */
    s_mode_mask = lv_obj_create(s_screen);
    lv_obj_set_pos(s_mode_mask, 0, 0);
    lv_obj_set_size(s_mode_mask, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(s_mode_mask, lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_mode_mask, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_mode_mask, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_mode_mask, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_mode_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mode_mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_mode_mask, mode_mask_event_cb, LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_flag(s_mode_mask, LV_OBJ_FLAG_HIDDEN);

    /* 中央面板 */
    {
        lv_obj_t *panel = lv_obj_create(s_mode_mask);

        lv_obj_set_size(panel, MODE_PANEL_W, MODE_PANEL_H);
        lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
        ui_set_card_style(panel, 0xffffff);
        lv_obj_set_style_pad_all(panel, 16, LV_PART_MAIN | LV_STATE_DEFAULT);

        s_mode_title = lv_label_create(panel);
        lv_obj_set_style_text_color(s_mode_title, lv_color_hex(COLOR_TEXT_TITLE),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(s_mode_title, s_font_name,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_mode_title, "Mode");
        lv_obj_align(s_mode_title, LV_ALIGN_TOP_MID, 0, 4);

        for (i = 0; i < MODE_OPT_MAX; i++)
        {
            lv_obj_t *btn = lv_button_create(panel);
            lv_obj_t *lbl;

            lv_obj_set_size(btn, 280, 48);
            lv_obj_set_pos(btn, 14, 48 + i * 58);
            lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_MODE_BTN),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_CARD_BORDER),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_event_cb(btn, mode_opt_event_cb, LV_EVENT_CLICKED,
                                (void *)(rt_base_t)i);
            s_mode_opts[i] = btn;

            lbl = lv_label_create(btn);
            lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT_TITLE),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(lbl, s_font_name,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_center(lbl);
            s_mode_opt_lbl[i] = lbl;
        }
    }
}

/* 页面内容：环境页/系统页由批次 2 实现，设置页待批次 3 */
static void ui_create_placeholder_pages(void)
{
    lv_obj_t *container;

    /* 主页扩展：天气卡 + 厨房定时器卡（批次 4，右上/底部） */
    container = ui_page_get_container(UI_PAGE_HOME);
    ui_home_widgets_init(container, s_font_title, s_font_name, s_font_value);

    /* 环境页：趋势曲线 + 极值（批次 2） */
    container = ui_page_get_container(UI_PAGE_ENV);
    ui_env_page_init(container);

    /* 系统页：CPU / 内存 / 线程 / 网络 / 版本（批次 2） */
    container = ui_page_get_container(UI_PAGE_SYS);
    ui_sys_page_init(container);

    /* 设置页：批次 3 实现（亮度/息屏/音量/主题） */
    container = ui_page_get_container(UI_PAGE_SETTINGS);
    ui_settings_page_init(container);
}

/* 创建 6 个卡片 */
static void ui_objects_init(void)
{
    int i;

    for (i = 0; i < 6; i++)
    {
        kitchen_card_t *card = &s_cards[i];

        card->card = ui_create_card(i);

        /* 名称 label：环境卡为顶部小标题，继电器卡为主体大字（字号/位置在分支中设置） */
        card->name_label = lv_label_create(card->card);
        lv_obj_set_style_text_color(card->name_label, lv_color_hex(COLOR_TEXT_NAME),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(card->name_label, card->name);

        if (card->relay_idx < 0)
        {
            /* ---- 环境卡片（左上角）：两排字（温度 / 湿度），字号一致 ---- */
            lv_obj_set_style_text_font(card->name_label, s_font_name,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(card->name_label, LV_ALIGN_TOP_MID, 0, 16);

            card->value_label = lv_label_create(card->card);
            lv_obj_set_style_text_color(card->value_label, lv_color_hex(COLOR_TEXT_NAME),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(card->value_label, s_font_value,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(card->value_label, "Temp  --.- C");
            lv_obj_align(card->value_label, LV_ALIGN_CENTER, 0, -24);

            card->extra_label = lv_label_create(card->card);
            lv_obj_set_style_text_color(card->extra_label, lv_color_hex(COLOR_TEXT_NAME),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(card->extra_label, s_font_value,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(card->extra_label, "Hum  --.- %");
            lv_obj_align(card->extra_label, LV_ALIGN_CENTER, 0, 30);

            lv_obj_set_style_bg_color(card->card, lv_color_hex(COLOR_CARD_ENV),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            /* ---- 继电器开关卡片：名称主体（大字居中）+ 状态小字 + 模式按钮 ---- */
            lv_obj_set_style_text_font(card->name_label, s_font_value,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(card->name_label, LV_ALIGN_CENTER, 0, -40);

            /* 状态圆点（左上角，开=绿 关=灰） */
            card->state_dot = lv_obj_create(card->card);
            lv_obj_set_size(card->state_dot, 14, 14);
            lv_obj_set_style_bg_color(card->state_dot, lv_color_hex(COLOR_DOT_OFF),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(card->state_dot, LV_OPA_COVER,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(card->state_dot, LV_RADIUS_CIRCLE,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(card->state_dot, 0,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(card->state_dot, LV_ALIGN_TOP_LEFT, 12, 12);

            card->state_label = lv_label_create(card->card);
            lv_obj_set_style_text_color(card->state_label, lv_color_hex(COLOR_TEXT_OFF),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(card->state_label, s_font_name,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(card->state_label, "OFF");
            lv_obj_align(card->state_label, LV_ALIGN_CENTER, 0, 0);

            /* 模式/强度条（卡片底部整宽，文字=当前档位）：点击弹出选择面板。
             * 目标区域大，不易误触；事件不冒泡，点击不会触发卡片开关。 */
            card->mode_btn = lv_button_create(card->card);
            lv_obj_set_size(card->mode_btn, UI_CARD_APP_W - 8, 44);
            lv_obj_set_style_radius(card->mode_btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(card->mode_btn, lv_color_hex(COLOR_MODE_BTN),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(card->mode_btn, LV_OPA_COVER,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(card->mode_btn, 1,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(card->mode_btn, lv_color_hex(COLOR_CARD_BORDER),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(card->mode_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_obj_add_event_cb(card->mode_btn, mode_btn_event_cb, LV_EVENT_CLICKED,
                                (void *)(rt_base_t)i);

            card->mode_label = lv_label_create(card->mode_btn);
            lv_obj_set_style_text_color(card->mode_label, lv_color_hex(COLOR_TEXT_NAME),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(card->mode_label, s_font_name,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_fmt(card->mode_label, "Mode: %s", card->modes[card->mode_idx]);
            lv_obj_center(card->mode_label);

            /* 可点击 + 绑定事件 */
            lv_obj_add_flag(card->card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card->card, relay_card_event_cb, LV_EVENT_CLICKED,
                                (void *)(rt_base_t)i);
        }
    }
}

/* 更新时间显示：未对时隐藏；对时后顶部居中显示 HH:MM（24h），
 * 分钟变化才刷新文本，避免频繁重绘。运行在 UI 线程内，无需加锁。 */
static void ui_update_time(void)
{
    char buf[8];
    static char s_last_time[8] = {0};

    if (s_time_label == RT_NULL)
    {
        return;
    }

    if (sntp_is_synced() && sntp_get_local_hm(buf, sizeof(buf)))
    {
        if (rt_strcmp(buf, s_last_time) != 0)
        {
            rt_strncpy(s_last_time, buf, sizeof(s_last_time) - 1);
            s_last_time[sizeof(s_last_time) - 1] = '\0';
            lv_label_set_text(s_time_label, buf);
            lv_obj_clear_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        s_last_time[0] = '\0';
        lv_obj_add_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------- WiFi 状态图标（右上角） ----------------
 * 结构：1 个圆点（信号源）+ 3 条同心弧（半径递增，开口朝下）。
 * 档位 = 亮几条弧：L0 只点、L1 点+内弧、L2 点+内中弧、L3 满格三弧。
 * 有网：绿色满格静止；断线：灰色按 L0→L1→L2→L3→L2→L1→L0 循环动画。 */

/* 创建图标（s_screen 就绪后调用；运行在 UI 线程初始化阶段） */
static void ui_create_wifi_icon(void)
{
    static const int arc_radius[3] = {6, 10, 14};   /* 内/中/外弧半径（差 4，弧间距匀称） */
    lv_obj_t *box;
    int i;

    /* 透明容器：右上角 34x34 定位，子对象相对其居中。
     * 加高 4px + 弧角度收窄（136°→104°）→ 图标纵向更高、横向更窄，整体修长。 */
    box = lv_obj_create(s_screen);
    lv_obj_set_size(box, 34, 34);
    lv_obj_set_pos(box, UI_SCREEN_W - UI_MARGIN - 34, 14);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    /* 信号源圆点：与弧缺口之间留出匀称间距，让图标上下拉开、更修长。
     * 计算依据（容器 34、弧中心 (17,17)、外弧 r=14、角度 218°→322°）：
     * 弧端屏幕 y = 17 + 14*sin(218°) ≈ 8.4；
     * 圆点 CENTER+(0,+2) → 中心 y=19，8px 圆点顶部 y=15，距外弧端约 6.6px；
     * 三弧半径差 4 → 弧间距均匀；点-弧间距略大于弧间距，信号源层次清晰。
     * 总高约 20px、宽约 22px，纵向>横向，图标修长不扁。 */
    s_wifi_dot = lv_obj_create(box);
    lv_obj_set_size(s_wifi_dot, 8, 8);
    lv_obj_set_style_bg_color(s_wifi_dot, lv_color_hex(COLOR_DOT_OFF),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wifi_dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_wifi_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_wifi_dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(s_wifi_dot, LV_ALIGN_CENTER, 0, 2);

    for (i = 0; i < 3; i++)
    {
        lv_obj_t *arc = lv_arc_create(box);
        lv_obj_set_size(arc, arc_radius[i] * 2 + 3, arc_radius[i] * 2 + 3);
        lv_arc_set_bg_angles(arc, 218, 322);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 100);
        /* 隐藏旋钮与背景轨道，只保留 indicator 弧 */
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_width(arc, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_color(arc, lv_color_hex(COLOR_DOT_OFF),
                                   LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(arc, 1, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        /* 初始全透明（L0） */
        lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
        s_wifi_arc[i] = arc;
    }
}

/* 设置档位（0~3）与颜色：弧按档位亮/灭，圆点与弧同步换色 */
static void ui_set_wifi_level(int level, uint32_t color)
{
    int i;

    for (i = 0; i < 3; i++)
    {
        lv_obj_set_style_opa(s_wifi_arc[i],
                             (i < level) ? LV_OPA_COVER : LV_OPA_TRANSP,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_set_style_bg_color(s_wifi_dot, lv_color_hex(color),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    for (i = 0; i < 3; i++)
    {
        lv_obj_set_style_arc_color(s_wifi_arc[i], lv_color_hex(color),
                                   LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
}

/* 状态刷新：有网→绿色满格；断线→灰色强弱循环（每档 300ms，由主循环驱动）。
 * 带缓存：档位/颜色无变化时不触碰 LVGL，避免无效重绘。 */
static void ui_update_wifi_icon(void)
{
    static const rt_uint8_t seq[7] = {0, 1, 2, 3, 2, 1, 0};   /* 序列 A */
    static int step = 0;
    int level;
    int color;

    if (s_wifi_dot == RT_NULL)
    {
        return;
    }

    if (wifi_link_is_connected())
    {
        level = 3;
        color = 1;   /* 绿 */
    }
    else
    {
        step = (step + 1) % 7;
        level = seq[step];
        color = 0;   /* 灰 */
    }

    if ((level != s_wifi_level) || (color != s_wifi_color))
    {
        s_wifi_level = level;
        s_wifi_color = color;
        ui_set_wifi_level(level, color ? COLOR_DOT_ON : COLOR_DOT_OFF);
    }
}

/* UI 线程入口 */
static void ui_thread_entry(void *args)
{
    uint32_t period_ms;

    RT_UNUSED(args);

    /* 初始化 LVGL（与小智 UI 相同的启动流程） */
    lv_init();
    lv_tick_set_cb(&rt_tick_get_millisecond);
    lv_port_disp_init();
    lv_port_indev_init();

    /* 息屏管理：须在触摸驱动初始化之后调用（只复位状态变量） */
    screen_saver_init();

    /* 加载持久化设置并应用（背光/音量/息屏超时/主题）。
     * 必须在创建 UI 对象前设置主题，让框架对象直接用主题色创建 */
    ui_theme_init();
    settings_init();
    lcd_backlight_set(settings_get()->brightness);
    wavplayer_volume_set(settings_get()->volume);
    screen_saver_set_timeout((rt_uint32_t)settings_get()->sleep_sec * 1000U);
    ui_theme_set_dark(settings_get()->dark ? RT_TRUE : RT_FALSE);

    /* 英文字体：LVGL 内置 Montserrat（开源无版权问题）。
     * UI 已改全英文，不再依赖中文字库（原中文字体完整备份已损坏，见实现方案 md） */
    s_font_title = &lv_font_montserrat_28;
    s_font_name  = &lv_font_montserrat_22;
    s_font_value = &lv_font_montserrat_34;

    /* 屏幕：使用 lv_port_disp_init 中 lv_display_create 自动创建的活动屏幕（LVGL 9）。
     * 注意：不能 lv_obj_create(NULL) 另建屏幕——那是一个孤立对象，不会被 LVGL 渲染，
     * 会导致纯白屏。参考官方 xiaozhi_ui.c 的 lv_screen_active() 用法。 */
    s_screen = lv_screen_active();
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, g_ui_theme->bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 顶部标题栏 */
    s_title_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_title_label, g_ui_theme->title_text,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_title_label, s_font_title,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_title_label, "Smart Kitchen Panel");
    lv_obj_set_pos(s_title_label, UI_MARGIN, 12);

    /* 右上角 WiFi 状态图标（替代原"待机中"占位文字） */
    ui_create_wifi_icon();

    /* 顶部正中间：时间（联网 SNTP 对时成功后显示，HH:MM 24h；未对时隐藏） */
    s_time_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_time_label, g_ui_theme->title_text,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_time_label, s_font_title,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_time_label, "00:00");
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_add_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);

    /* 标题栏与卡片区之间的浅色分隔线 */
    {
        lv_obj_t *line = lv_obj_create(s_screen);
        lv_obj_set_pos(line, 0, UI_TITLE_BAR_H);
        lv_obj_set_size(line, UI_SCREEN_W, 2);
        lv_obj_set_style_bg_color(line, g_ui_theme->divider,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(line, LV_OPA_60, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        s_divider = line;
    }

    /* 底部状态栏 */
    s_status_text = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_status_text, g_ui_theme->status_text,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_status_text, s_font_name,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_status_text, "Welcome to Smart Kitchen Panel");
    lv_obj_set_pos(s_status_text, UI_MARGIN, UI_STATUS_BAR_Y + 8);

    /* 页面框架：右侧导航 + 5 个页面容器（批次1：环境/系统/设置页为占位） */
    ui_page_init(s_screen, s_font_name, RT_NULL);
    ui_create_placeholder_pages();
    ui_create_mode_panel();

    /* 关于弹层（批次 5）：设置页 About 按钮打开，初始隐藏 */
    ui_about_init(s_screen, s_font_title, s_font_name, &lv_font_montserrat_16);

    /* 天气线程（批次 4）：独立低优先级线程，WiFi 连上后拉取 Open-Meteo，
     * 每 30 分钟刷新；请求阻塞只影响天气线程，不卡 UI */
    weather_init();

    /* 创建卡片（0=主页环境卡，1~5=家电页继电器卡）并同步一次继电器状态 */
    ui_objects_init();
    kitchen_ui_update_relay_all();

    /* 开机动画（批次 5）：覆盖全屏上滑淡入 → 停顿 → 淡出，完成后自删。
     * 放在主循环前，动画由 LVGL 任务驱动，期间拦截触摸防误触 */
    ui_splash_play(s_screen, g_ui_theme->bg, s_font_title, s_font_name);

    lv_task_handler();

    /* 主循环：周期调用 lv_task_handler 驱动 LVGL 刷新 */
    {
        rt_tick_t time_check_tick = 0;
        rt_tick_t wifi_anim_tick   = 0;
        rt_tick_t env_page_tick    = 0;
        rt_tick_t timer_tick       = 0;

        while (1)
        {
            if (!screen_saver_is_sleeping())
            {
                period_ms = lv_task_handler();
                if ((period_ms == LV_NO_TIMER_READY) || (period_ms > 100))
                {
                    period_ms = 20;
                }

                /* 每秒检查一次时间：对时后显示 HH:MM，分钟变化才刷新；
                 * 同时刷新系统页（CPU/内存/运行时长/网络）、主页天气/定时器卡 */
                if (rt_tick_get() - time_check_tick >= RT_TICK_PER_SECOND)
                {
                    time_check_tick = rt_tick_get();
                    ui_update_time();
                    ui_sys_page_refresh();
                    ui_home_widgets_tick();
                }

                /* 环境页：每 5s 刷新曲线与极值（历史 60s 才追加一条） */
                if (rt_tick_get() - env_page_tick >= rt_tick_from_millisecond(5000))
                {
                    env_page_tick = rt_tick_get();
                    ui_env_page_refresh();
                }

                /* WiFi 图标：每 300ms 刷新一档（断线时推进强弱循环动画；
                 * 有网时缓存挡住、无实际重绘） */
                if (rt_tick_get() - wifi_anim_tick >= rt_tick_from_millisecond(300))
                {
                    wifi_anim_tick = rt_tick_get();
                    ui_update_wifi_icon();
                }
            }
            else
            {
                /* 息屏态：不刷 LVGL（省 CPU/功耗），仅轮询触摸等唤醒；
                 * period_ms=0 → 主循环不再额外等待，触摸轮询间隔由
                 * screen_saver_loop 内部（500ms）精确控制。
                 * 但厨房定时器必须继续走：纯状态机 tick（不碰 LVGL），
                 * 到点回调会播提示音 + 强制唤醒亮屏（唤醒后自然回亮屏态）。 */
                period_ms = 0;
                if (rt_tick_get() - timer_tick >= RT_TICK_PER_SECOND)
                {
                    timer_tick = rt_tick_get();
                    kitchen_timer_tick();
                }
            }

            /* 息屏管理状态机：
             * - 亮屏态：检测触摸刷新计时，60s 无触摸自动息屏；
             * - 息屏态：内部 500ms 轮询触摸，按下即唤醒（内部阻塞等待） */
            screen_saver_loop();

            rt_thread_mdelay(period_ms);
        }
    }
}

/* ============================================================================
 * 公共接口
 * ==========================================================================*/
/* 运行时切换主题：重设框架对象颜色（screen/标题/时间/分隔线/状态栏）。
 * 由设置页在主题切换后调用；导航栏由 ui_page_apply_theme() 重设 */
void ui_apply_theme(void)
{
    if (s_screen == RT_NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(s_screen, g_ui_theme->bg,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    if (s_title_label != RT_NULL)
    {
        lv_obj_set_style_text_color(s_title_label, g_ui_theme->title_text,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_time_label != RT_NULL)
    {
        lv_obj_set_style_text_color(s_time_label, g_ui_theme->title_text,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_divider != RT_NULL)
    {
        lv_obj_set_style_bg_color(s_divider, g_ui_theme->divider,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_status_text != RT_NULL)
    {
        lv_obj_set_style_text_color(s_status_text, g_ui_theme->status_text,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

int kitchen_ui_init(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("kitchen_ui", ui_thread_entry, RT_NULL,
                           1024 * 12, 25, 10);
    if (tid == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    rt_thread_startup(tid);
    return 0;
}

void kitchen_ui_update_env(float temperature_c, float humidity_pct)
{
    if (s_cards[0].value_label == RT_NULL)
    {
        return;
    }

    lv_lock();
    lv_label_set_text_fmt(s_cards[0].value_label, "Temp  %.1f C", temperature_c);
    lv_label_set_text_fmt(s_cards[0].extra_label, "Hum  %.1f %%", humidity_pct);
    lv_unlock();
}

void kitchen_ui_update_relay_all(void)
{
    int i;

    /* 可能从 asr 线程等外部调用，LVGL 对象操作必须加锁（与 env/status 一致） */
    lv_lock();
    for (i = 0; i < 6; i++)
    {
        if (s_cards[i].relay_idx >= 0)
        {
            ui_update_relay_card(i);
        }
    }
    lv_unlock();
}

void kitchen_ui_show_status(const char *text)
{
    if ((s_status_text == RT_NULL) || (text == RT_NULL))
    {
        return;
    }

    lv_lock();
    lv_label_set_text(s_status_text, text);
    lv_unlock();
}
