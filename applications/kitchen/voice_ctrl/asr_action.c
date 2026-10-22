/*
 * 语音指令执行模块实现（阶段 3）
 *
 * 流程：识别文本 → 归一化 → 对象匹配（设备）→ 动作匹配（开/关/翻转）
 *       → 执行继电器 → 界面刷新 → 播报提示音
 *
 * 规则：
 * - 对象关键词表可扩展（s_objs）
 * - 动作词：开=打开/开启/开，关=关闭/关掉/关上/关；只说设备名则翻转
 * - "全部/所有" + 开/关 → 五路全控（无动作词默认全开）
 * - 只支持单对象（"打开厨房灯和油烟机"暂不支持，先命中第一个设备）
 */
#include "asr_action.h"
#include "asr_config.h"

#include "../relay.h"
#include "../kitchen_ui.h"
#include "../voice_prompt.h"

#include <rtthread.h>
#include <finsh.h>

#include <string.h>
#include <stdio.h>

#define DBG_TAG "voice.act"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* 对象 -> 继电器通道映射表（顺序即匹配优先级，越靠前越先命中） */
typedef struct
{
    const char *kw;
    int relay_idx;
} action_obj_t;

static const action_obj_t s_objs[] =
{
    { "厨房灯", RELAY_IDX_LAMP },
    { "油烟机", RELAY_IDX_RANGE_HOOD },
    { "烟机",   RELAY_IDX_RANGE_HOOD },
    { "风扇",   RELAY_IDX_FAN },
    { "洗碗机", RELAY_IDX_DISHWASHER },
    { "烤箱",   RELAY_IDX_OVEN },
    { "灯",     RELAY_IDX_LAMP },
};
#define ACTION_OBJ_NUM  (sizeof(s_objs) / sizeof(s_objs[0]))

/* ---------------- 文本归一化 ---------------- */

/* 判断是否是可跳过的符号/标点（ASCII + 常见 UTF-8 全角/中文标点）。
 * 返回跳过字节数（0=保留该字符）。 */
static int norm_skip(const unsigned char *p)
{
    if (p[0] < 0x80)
    {
        unsigned char c = p[0];

        if (c <= ' ' || strchr("!?.,;:'\"()[]{}<>-_/\\|`~@#$%^&*+=", c) != RT_NULL)
        {
            return 1;
        }
        return 0;
    }

    /* U+3000~U+303F 中文标点（，。！？；：""''（）《》…） */
    if (p[0] == 0xE3 && p[1] == 0x80)
    {
        return 3;
    }
    /* U+FF01~U+FF5E 全角符号/字母数字（EF BC 81 ~ EF BC BE） */
    if (p[0] == 0xEF && p[1] == 0xBC && p[2] >= 0x81 && p[2] <= 0xBE)
    {
        return 3;
    }
    /* U+2014 破折号 / U+2026 省略号 等（E2 80 94~A6） */
    if (p[0] == 0xE2 && p[1] == 0x80)
    {
        return 3;
    }
    return 0;
}

/* 去掉标点/符号/口头语，输出纯内容文本（保留汉字、字母、数字） */
static void norm_text(const char *in, char *out, int cap)
{
    int o = 0;
    const unsigned char *p = (const unsigned char *)in;

    while (*p != '\0' && o < cap - 1)
    {
        int skip = norm_skip(p);

        if (skip > 0)
        {
            p += skip;
            continue;
        }
        if (*p < 0x80)
        {
            out[o++] = (char)*p++;
        }
        else if (p[0] >= 0xC0)
        {
            /* UTF-8 多字节字符：按首字节确定宽度（3 或 4），校验续字节后整体拷贝 */
            int n = (p[0] >= 0xF0) ? 4 : 3;
            int k;

            if (o + n >= cap)
            {
                break;
            }
            for (k = 1; k < n; k++)
            {
                if (p[k] == '\0' || (p[k] & 0xC0) != 0x80)
                {
                    break;   /* 续字节无效（输入截断），退化处理 */
                }
            }
            if (k == n)
            {
                for (k = 0; k < n; k++)
                {
                    out[o++] = (char)p[k];
                }
                p += n;
            }
            else
            {
                out[o++] = (char)p[0];   /* 无法解析：单字节保留，避免乱序 */
                p += 1;
            }
        }
        else
        {
            break;
        }
    }
    out[o] = '\0';
}

/* 去掉常见口头语：帮我/请/把/给/一下/现在/马上（任意位置出现即删除） */
static void norm_phrase(char *s)
{
    static const char *const drops[] =
    {
        "帮我", "请", "把", "给", "一下", "现在", "马上", "去",
    };
    char buf[128];
    int i;

    for (i = 0; i < (int)(sizeof(drops) / sizeof(drops[0])); i++)
    {
        char *hit;

        while ((hit = strstr(s, drops[i])) != RT_NULL)
        {
            rt_strncpy(buf, hit + rt_strlen(drops[i]), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            *hit = '\0';
            strncat(s, buf, sizeof(buf) - 1 - rt_strlen(s));
        }
    }
}

/* ---------------- 动作匹配 ---------------- */

static rt_bool_t is_close_word(const char *s)
{
    return (strstr(s, "关闭") != RT_NULL ||
            strstr(s, "关掉") != RT_NULL ||
            strstr(s, "关上") != RT_NULL ||
            strstr(s, "关") != RT_NULL);
}

static rt_bool_t is_open_word(const char *s)
{
    return (strstr(s, "打开") != RT_NULL ||
            strstr(s, "开启") != RT_NULL ||
            strstr(s, "开") != RT_NULL);
}

/* ---------------- 执行 ---------------- */

/* 执行单路：act 0=关 1=开 2=翻转 */
static void do_relay(int relay_idx, int act)
{
    rt_bool_t on;
    char status[64];

    if (act == 2)
    {
        on = relay_get(relay_idx) ? RT_FALSE : RT_TRUE;
    }
    else
    {
        on = (act == 1) ? RT_TRUE : RT_FALSE;
    }

    relay_set(relay_idx, on);
    kitchen_ui_update_relay_all();

    rt_snprintf(status, sizeof(status), "%s %s",
                relay_name(relay_idx), on ? "已打开" : "已关闭");
    kitchen_ui_show_status(status);
    LOG_I("voice action: %s -> %s", relay_name(relay_idx), on ? "ON" : "OFF");

    if (on)
    {
        voice_prompt_play_on();
    }
    else
    {
        voice_prompt_play_off();
    }
}

/* 全控五路 */
static void do_all(rt_bool_t on)
{
    int i;

    for (i = 0; i < RELAY_CH_NUM; i++)
    {
        relay_set(i, on);
    }
    kitchen_ui_update_relay_all();

    kitchen_ui_show_status(on ? "全部设备 已打开" : "全部设备 已关闭");
    LOG_I("voice action: all -> %s", on ? "ON" : "OFF");

    if (on)
    {
        voice_prompt_play_on();
    }
    else
    {
        voice_prompt_play_off();
    }
}

/* ---------------- 入口 ---------------- */

void asr_action_handle(const char *text)
{
    char norm[128];
    int i;
    int obj_idx = -1;
    int act;

    if ((text == RT_NULL) || (text[0] == '\0'))
    {
        return;
    }

    /* 1. 归一化 */
    norm_text(text, norm, sizeof(norm));
    norm_phrase(norm);
    if (norm[0] == '\0')
    {
        LOG_W("voice action: empty after normalize (\"%s\")", text);
        return;
    }
    LOG_I("voice cmd: %s", norm);

    /* 2. 全控指令（优先） */
    if (strstr(norm, "全部") != RT_NULL || strstr(norm, "所有") != RT_NULL)
    {
        do_all(is_close_word(norm) ? RT_FALSE : RT_TRUE);
        return;
    }

    /* 3. 对象匹配（先命中第一个设备） */
    for (i = 0; i < (int)ACTION_OBJ_NUM; i++)
    {
        if (strstr(norm, s_objs[i].kw) != RT_NULL)
        {
            obj_idx = i;
            break;
        }
    }
    if (obj_idx < 0)
    {
        kitchen_ui_show_status("未识别指令");
        LOG_W("voice action: no device match (\"%s\")", norm);
        return;
    }

    /* 4. 动作匹配：关 > 开 > 翻转 */
    if (is_close_word(norm))
    {
        act = 0;
    }
    else if (is_open_word(norm))
    {
        act = 1;
    }
    else
    {
        act = 2;   /* 只说设备名 → 翻转 */
    }

    do_relay(s_objs[obj_idx].relay_idx, act);
}

/* ---------------- msh 调试命令 ---------------- */
#ifdef RT_USING_MSH

/* asr_action <文本>：免录音直接测试指令匹配 */
static void asr_action_cmd(int argc, char **argv)
{
    char buf[160] = "";
    int i;

    if (argc < 2)
    {
        rt_kprintf("usage: asr_action <text>\n");
        rt_kprintf("e.g.  asr_action 打开厨房灯\n");
        return;
    }

    for (i = 1; i < argc; i++)
    {
        strncat(buf, argv[i], sizeof(buf) - 1 - rt_strlen(buf));
        if (i < argc - 1)
        {
            strncat(buf, " ", sizeof(buf) - 1 - rt_strlen(buf));
        }
    }
    asr_action_handle(buf);
}
MSH_CMD_EXPORT_ALIAS(asr_action_cmd, asr_action, test voice action matching without recording);

#endif /* RT_USING_MSH */
