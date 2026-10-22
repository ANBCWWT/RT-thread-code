/*
 * 系统监控页 UI 实现（参赛功能批次 2）
 */
#include "ui_sys_page.h"

#include <rtthread.h>
#include <lwip/netif.h>
#include <lwip/ip4_addr.h>
#include "voice_ctrl/sntp.h"
#include "voice_ctrl/wifi_link.h"

/* 布局（内容区 640x368） */
#define SYS_TITLE_X     20
#define SYS_TITLE_Y     8
#define SYS_COL_X       20
#define SYS_COL_W       340
#define SYS_ROW_H       92
#define SYS_ROW_GAP     12
#define SYS_THREAD_X    380
#define SYS_THREAD_W    240

/* 配色（与主界面一致） */
#define SYS_COLOR_BG        0xffffff
#define SYS_COLOR_TEXT_TITLE 0x1e293b
#define SYS_COLOR_TEXT_NAME  0x475569
#define SYS_COLOR_TEXT_VALUE 0x0284c7
#define SYS_COLOR_BORDER     0xcbd5e1
#define SYS_COLOR_BAR_BG     0xe2e8f0
#define SYS_COLOR_BAR_FILL   0x16a34a

static lv_obj_t *s_cpu_label = RT_NULL;
static lv_obj_t *s_cpu_bar   = RT_NULL;
static lv_obj_t *s_ram_label = RT_NULL;
static lv_obj_t *s_ram_bar   = RT_NULL;
static lv_obj_t *s_up_label  = RT_NULL;
static lv_obj_t *s_ver_label = RT_NULL;
static lv_obj_t *s_net_label = RT_NULL;
static lv_obj_t *s_th_name_lbl = RT_NULL;
static lv_obj_t *s_th_prio_lbl = RT_NULL;
static char s_th_name_cur[512] = "";
static char s_th_prio_cur[512] = "";

/* ---- CPU 空闲率统计（idle hook 数"空闲 tick"，不是数调用次数） ----
 * 注意：RT-Thread 空闲线程是死循环，同一 tick 内会多次进入 idle hook，
 * 必须只在 tick 变化时计一次，否则空闲时计数爆炸、CPU 恒算成 0。
 * 忙的 tick（idle 被抢占）hook 不执行，自然不计入空闲。 */
static volatile rt_uint32_t s_idle_cnt = 0;
static volatile rt_tick_t   s_idle_last_hook = 0;
static rt_uint32_t s_idle_snap = 0;   /* 上次刷新时的空闲 tick 快照 */
static rt_tick_t  s_tick_last  = 0;

static void sys_idle_hook(void)
{
    rt_tick_t now = rt_tick_get();

    if (now != s_idle_last_hook)
    {
        s_idle_cnt++;            /* 每个空闲 tick 最多计一次 */
        s_idle_last_hook = now;
    }
}

static const char *sys_thread_stat_str(rt_uint8_t stat)
{
    /* RT-Thread 5.x：低 3 位是状态，高位带 YIELD(0x08)/SIGNAL(0x10~0x40) 标志，
     * 必须先用 RT_THREAD_STAT_MASK(0x07) 掩码再判断，否则运行中线程会显示 "?" */
    stat &= RT_THREAD_STAT_MASK;

    if (stat == RT_THREAD_INIT)
    {
        return "INIT";
    }
    if (stat == RT_THREAD_READY)
    {
        return "READY";
    }
    if (stat == RT_THREAD_RUNNING)
    {
        return "RUN";
    }
    if (stat == RT_THREAD_CLOSE)
    {
        return "CLOSE";
    }
    /* 挂起态 0x04~0x07 变体（可中断/可杀/不可中断）统一显示 SUSPEND */
    if (stat & RT_THREAD_SUSPEND_MASK)
    {
        return "SUSPEND";
    }
    return "?";
}

/* 创建一行"名称 + 数值"的卡片 */
static void sys_row_card(lv_obj_t *parent, int y, const char *name,
                         lv_obj_t **value_lbl, lv_obj_t **bar)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, SYS_COL_X, y);
    lv_obj_set_size(card, SYS_COL_W, SYS_ROW_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(SYS_COLOR_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(SYS_COLOR_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(card);
    lv_obj_set_style_text_color(t, lv_color_hex(SYS_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(t, name);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    *value_lbl = lv_label_create(card);
    lv_obj_set_style_text_color(*value_lbl, lv_color_hex(SYS_COLOR_TEXT_VALUE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(*value_lbl, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(*value_lbl, "--");
    /* 标题 22px（约 24px 高）下方 4px 起排数值，避免标题与数值重叠 */
    lv_obj_align(*value_lbl, LV_ALIGN_TOP_LEFT, 0, 28);

    if (bar != RT_NULL)
    {
        *bar = lv_bar_create(card);
        lv_obj_set_size(*bar, SYS_COL_W - 40, 8);
        /* 进度条独立一行：标题(0-24) + 数值(28-52) 之后，紧贴卡片底部 */
        lv_obj_align(*bar, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_bar_set_range(*bar, 0, 100);
        lv_bar_set_value(*bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(*bar, lv_color_hex(SYS_COLOR_BAR_BG),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(*bar, lv_color_hex(SYS_COLOR_BAR_FILL),
                                  LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(*bar, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
}

/* 遍历线程对象，生成"名字列"与"Pxx 状态列"两段文本（每行以 \n 结尾）。
 * Montserrat 是比例字体，空格填充在像素上不对齐，独立两列才能保证 P 列对齐 */
static void sys_build_threads_text(char *name_buf, rt_size_t name_len,
                                   char *prio_buf, rt_size_t prio_len)
{
    struct rt_object_information *info;
    rt_list_t *node;
    int shown = 0;
    char *pn = name_buf;
    char *pp = prio_buf;
    rt_size_t ln = name_len;
    rt_size_t lp = prio_len;
    int n;

    *pn = '\0';
    *pp = '\0';

    info = rt_object_get_information(RT_Object_Class_Thread);
    if (info == RT_NULL)
    {
        return;
    }

    for (node = info->object_list.next;
         (node != &(info->object_list)) && (shown < 8);
         node = node->next)
    {
        struct rt_object *obj = rt_list_entry(node, struct rt_object, list);
        rt_thread_t th = (rt_thread_t)obj;

        n = rt_snprintf(pn, ln, "%-8s\n", th->parent.name);
        if (n < 0 || n >= (int)ln)
        {
            break;
        }
        pn += n;
        ln -= (rt_size_t)n;

        n = rt_snprintf(pp, lp, "P%02d %s\n", th->current_priority,
                        sys_thread_stat_str(th->stat));
        if (n < 0 || n >= (int)lp)
        {
            break;
        }
        pp += n;
        lp -= (rt_size_t)n;
        shown++;
    }
}

/* 线程列表有变化才刷新：名字/优先级/状态任一变化即更新 label，无变化不重绘 */
static void sys_refresh_threads(void)
{
    static char new_name[512];
    static char new_prio[512];

    if (s_th_name_lbl == RT_NULL)
    {
        return;
    }

    sys_build_threads_text(new_name, sizeof(new_name), new_prio, sizeof(new_prio));
    if (rt_strcmp(new_name, s_th_name_cur) != 0 ||
        rt_strcmp(new_prio, s_th_prio_cur) != 0)
    {
        rt_strcpy(s_th_name_cur, new_name);
        rt_strcpy(s_th_prio_cur, new_prio);
        lv_label_set_text(s_th_name_lbl, s_th_name_cur);
        lv_label_set_text(s_th_prio_lbl, s_th_prio_cur);
    }
}

/* 创建线程列表：标题 + 左右两列 label（init 时一次），首次填充文本 */
static void sys_build_threads(lv_obj_t *parent, int y)
{
    lv_obj_t *t = lv_label_create(parent);
    lv_obj_set_pos(t, SYS_THREAD_X, y - 24);
    lv_obj_set_style_text_color(t, lv_color_hex(SYS_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(t, "Threads");

    s_th_name_lbl = lv_label_create(parent);
    lv_obj_set_pos(s_th_name_lbl, SYS_THREAD_X, y);
    lv_obj_set_size(s_th_name_lbl, 120, 240);
    lv_obj_set_style_text_color(s_th_name_lbl, lv_color_hex(SYS_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_th_name_lbl, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_th_name_lbl, "");
    lv_obj_set_style_text_line_space(s_th_name_lbl, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_th_prio_lbl = lv_label_create(parent);
    lv_obj_set_pos(s_th_prio_lbl, SYS_THREAD_X + 122, y);
    lv_obj_set_size(s_th_prio_lbl, 140, 240);
    lv_obj_set_style_text_color(s_th_prio_lbl, lv_color_hex(SYS_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_th_prio_lbl, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_th_prio_lbl, "");
    lv_obj_set_style_text_line_space(s_th_prio_lbl, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    sys_refresh_threads();
}

void ui_sys_page_init(lv_obj_t *parent)
{
    lv_obj_t *title;

    title = lv_label_create(parent);
    lv_obj_set_pos(title, SYS_TITLE_X, SYS_TITLE_Y);
    lv_obj_set_style_text_color(title, lv_color_hex(SYS_COLOR_TEXT_TITLE),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(title, "System Monitor");

    sys_row_card(parent, 44,  "CPU",   &s_cpu_label, &s_cpu_bar);
    sys_row_card(parent, 148, "Memory", &s_ram_label, &s_ram_bar);

    /* 运行时长（无卡片，直接文本行） */
    s_up_label = lv_label_create(parent);
    lv_obj_set_pos(s_up_label, SYS_COL_X, 256);
    lv_obj_set_style_text_color(s_up_label, lv_color_hex(SYS_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_up_label, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_up_label, "Up: --");

    s_ver_label = lv_label_create(parent);
    lv_obj_set_pos(s_ver_label, SYS_COL_X, 296);
    lv_obj_set_style_text_color(s_ver_label, lv_color_hex(SYS_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_ver_label, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_fmt(s_ver_label, "RT-Thread %d.%d.%d  %s %s",
                          RT_VERSION_MAJOR, RT_VERSION_MINOR, RT_VERSION_PATCH,
                          __DATE__, __TIME__);

    s_net_label = lv_label_create(parent);
    lv_obj_set_pos(s_net_label, SYS_COL_X, 336);
    lv_obj_set_style_text_color(s_net_label, lv_color_hex(SYS_COLOR_TEXT_NAME),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_net_label, &lv_font_montserrat_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_net_label, "Net: --");

    sys_build_threads(parent, 44 + 24);

    /* 注册空闲钩子统计 CPU（仅注册一次） */
    rt_thread_idle_sethook(sys_idle_hook);
    s_tick_last = rt_tick_get();

    ui_sys_page_refresh();
}

void ui_sys_page_refresh(void)
{
    rt_uint32_t idle_delta;
    rt_tick_t   tick_delta;
    rt_uint32_t cpu_used = 0;
    rt_size_t   mem_total = 0, mem_used = 0, mem_max = 0;
    rt_tick_t   now;
    rt_uint32_t up_sec;
    const char *ip;
    struct netif *nif;

    if (s_cpu_label == RT_NULL)
    {
        return;
    }

    /* ---- CPU 占用率：1s 窗口内 (总 tick - 空闲 tick) / 总 tick ---- */
    now = rt_tick_get();
    idle_delta = s_idle_cnt - s_idle_snap;
    tick_delta = (rt_uint32_t)(now - s_tick_last);
    s_idle_snap = s_idle_cnt;
    s_tick_last = now;
    if (tick_delta > 0)
    {
        rt_uint32_t idle_pct = idle_delta * 100 / tick_delta;
        cpu_used = (idle_pct >= 100) ? 0 : (100 - idle_pct);
    }
    lv_label_set_text_fmt(s_cpu_label, "CPU %u %%", cpu_used);
    lv_bar_set_value(s_cpu_bar, cpu_used, LV_ANIM_OFF);

    /* ---- 内存 ---- */
    rt_memory_info(&mem_total, &mem_used, &mem_max);
    {
        rt_uint32_t used_pct = (mem_total > 0) ? (mem_used * 100 / mem_total) : 0;
        lv_label_set_text_fmt(s_ram_label, "RAM %d.%d / %d.%d MB",
                              (int)(mem_used / 1024 / 1024), (int)(mem_used / 1024 % 1024 / 102),
                              (int)(mem_total / 1024 / 1024), (int)(mem_total / 1024 % 1024 / 102));
        lv_bar_set_value(s_ram_bar, used_pct, LV_ANIM_OFF);
    }

    /* ---- 运行时长 ---- */
    up_sec = rt_tick_get() / RT_TICK_PER_SECOND;
    lv_label_set_text_fmt(s_up_label, "Up: %dd %02dh %02dm %02ds",
                          up_sec / 86400, (up_sec / 3600) % 24,
                          (up_sec / 60) % 60, up_sec % 60);

    /* ---- 网络：IP + SNTP ---- */
    ip = "no link";
    nif = netif_find("w0");
    if (nif != RT_NULL && netif_is_up(nif))
    {
        ip = ip4addr_ntoa(netif_ip4_addr(nif));
    }
    lv_label_set_text_fmt(s_net_label, "Net: %s  SNTP %s",
                          ip, sntp_is_synced() ? "ok" : "pending");

    /* 线程列表：有变化才刷新 */
    sys_refresh_threads();
}
