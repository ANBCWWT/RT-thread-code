/*
 * 环境趋势页 UI 实现（参赛功能批次 2）
 */
#include "ui_env_page.h"
#include "env_history.h"

/* 布局（内容区 640x368） */
#define ENV_PAGE_TITLE_X    20
#define ENV_PAGE_TITLE_Y    8
#define ENV_CHART_X         20
#define ENV_CHART_Y         44
#define ENV_CHART_W         380
#define ENV_CHART_H         290
#define ENV_INFO_X          420
#define ENV_INFO_Y          44
#define ENV_INFO_W          200

/* 配色（与主界面一致） */
#define ENV_COLOR_BG        0xffffff
#define ENV_COLOR_TEXT_TITLE 0x1e293b
#define ENV_COLOR_TEXT_NAME  0x475569
#define ENV_COLOR_TEXT_VALUE 0x0284c7
#define ENV_COLOR_BORDER     0xcbd5e1
#define ENV_COLOR_TEMP       0xef4444   /* 温度：红 */
#define ENV_COLOR_HUM        0x0284c7   /* 湿度：蓝 */

/* chart 值域：温度 0~50℃（放大 10 倍存储），湿度 0~100% */
#define ENV_CHART_TEMP_MAX   500
#define ENV_CHART_HUM_MAX    100

/* 图表只显示最近 64 点（2026-10-22 用户拍板方案A：128 点太密，点距翻倍）。
 * 存储仍保留 128 条（2h），仅显示窗口收窄到最近 64min。 */
#define ENV_DISPLAY_MAX      64

static lv_obj_t        *s_chart       = RT_NULL;
static lv_chart_series_t *s_ser_temp  = RT_NULL;
static lv_chart_series_t *s_ser_hum   = RT_NULL;
static int              s_last_count  = -1;

static lv_obj_t *s_now_lbl    = RT_NULL;
static lv_obj_t *s_mm_temp_lbl = RT_NULL;
static lv_obj_t *s_mm_hum_lbl  = RT_NULL;

static void ui_env_info_card(lv_obj_t *parent, int x, int y, int w, int h,
                             const char *title, lv_obj_t **lbl)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(ENV_COLOR_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(ENV_COLOR_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(card);
    lv_obj_set_style_text_color(t, lv_color_hex(ENV_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    *lbl = lv_label_create(card);
    lv_obj_set_style_text_color(*lbl, lv_color_hex(ENV_COLOR_TEXT_VALUE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(*lbl, &lv_font_montserrat_16,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(*lbl, w - 20);
    lv_label_set_long_mode(*lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(*lbl, "--");
    /* 标题 22px（约 24px 高）下方 6px 起排数据，避免标题与两行数据重叠 */
    lv_obj_align(*lbl, LV_ALIGN_TOP_LEFT, 0, 30);
}

void ui_env_page_init(lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *card;
    lv_obj_t *note;

    /* 标题 */
    title = lv_label_create(parent);
    lv_obj_set_pos(title, ENV_PAGE_TITLE_X, ENV_PAGE_TITLE_Y);
    lv_obj_set_style_text_color(title, lv_color_hex(ENV_COLOR_TEXT_TITLE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(title, "Environment Trend");

    /* 图表卡片 */
    card = lv_obj_create(parent);
    lv_obj_set_pos(card, ENV_CHART_X, ENV_CHART_Y);
    lv_obj_set_size(card, ENV_CHART_W, ENV_CHART_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(ENV_COLOR_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(ENV_COLOR_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_chart = lv_chart_create(card);
    lv_obj_set_size(s_chart, ENV_CHART_W - 20, ENV_CHART_H - 20);
    lv_obj_center(s_chart);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, ENV_HISTORY_MAX);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, ENV_CHART_TEMP_MAX);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_SECONDARY_Y, 0, ENV_CHART_HUM_MAX);
    lv_chart_set_div_line_count(s_chart, 4, 4);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS | LV_STATE_DEFAULT);

    s_ser_temp = lv_chart_add_series(s_chart, lv_color_hex(ENV_COLOR_TEMP),
                                     LV_CHART_AXIS_PRIMARY_Y);
    s_ser_hum  = lv_chart_add_series(s_chart, lv_color_hex(ENV_COLOR_HUM),
                                     LV_CHART_AXIS_SECONDARY_Y);

    /* 右侧信息：实时值 + 极值 */
    ui_env_info_card(parent, ENV_INFO_X, ENV_INFO_Y, ENV_INFO_W, 88,
                     "Now", &s_now_lbl);
    ui_env_info_card(parent, ENV_INFO_X, ENV_INFO_Y + 100, ENV_INFO_W, 88,
                     "Max / Min", &s_mm_temp_lbl);
    ui_env_info_card(parent, ENV_INFO_X, ENV_INFO_Y + 200, ENV_INFO_W, 76,
                     "Hum Max / Min", &s_mm_hum_lbl);

    note = lv_label_create(parent);
    lv_obj_set_pos(note, ENV_INFO_X, ENV_INFO_Y + 286);
    lv_obj_set_style_text_color(note, lv_color_hex(ENV_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(note, "1 point / 60 s  |  shows last 64 min");

    /* 首个刷新 */
    ui_env_page_refresh();
}

void ui_env_page_refresh(void)
{
    int count;
    int n;
    int start;
    int i;
    env_history_item_t it;
    env_history_minmax_t mm;

    if (s_chart == RT_NULL)
    {
        return;
    }

    count = env_history_count();

    /* 只画最近 ENV_DISPLAY_MAX 点：数据满 128 时从倒数 64 开始画，
     * 数据不足 64 则全画。点位变化才重建（避免重复分配）。 */
    if (count > ENV_DISPLAY_MAX)
    {
        start = count - ENV_DISPLAY_MAX;
        n = ENV_DISPLAY_MAX;
    }
    else
    {
        start = 0;
        n = count;
    }

    if (n != s_last_count)
    {
        if (n > 0)
        {
            lv_chart_set_point_count(s_chart, n);
        }
        s_last_count = n;
    }

    for (i = 0; i < n; i++)
    {
        if (!env_history_get(start + i, &it))
        {
            break;
        }
        lv_chart_set_value_by_id(s_chart, s_ser_temp, i, (lv_coord_t)(it.temp * 10));
        lv_chart_set_value_by_id(s_chart, s_ser_hum,  i, (lv_coord_t)(it.hum));
    }
    lv_chart_refresh(s_chart);

    /* 右侧信息（两行显示，避免长文本溢出卡片） */
    env_history_minmax(&mm);
    if (count > 0)
    {
        env_history_get(count - 1, &it);
        lv_label_set_text_fmt(s_now_lbl, "T %.1f C\nH %.1f %%", it.temp, it.hum);
        lv_label_set_text_fmt(s_mm_temp_lbl, "Max %.1f C\nMin %.1f C", mm.t_max, mm.t_min);
        lv_label_set_text_fmt(s_mm_hum_lbl, "Max %.1f %%\nMin %.1f %%", mm.h_max, mm.h_min);
    }
    else
    {
        lv_label_set_text(s_now_lbl, "no data");
        lv_label_set_text(s_mm_temp_lbl, "--\n--");
        lv_label_set_text(s_mm_hum_lbl, "--\n--");
    }
}
