/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 继电器控制模块实现（智能厨房面板）
 *
 * 说明：
 * - P5_0 ~ P5_4 在 design.modus / cycfg_pins.h 中均未被占用，
 *   且 RT-Thread 的 drv_gpio 支持直接操作任意 GET_PIN 引脚，
 *   因此无需修改 device-configurator 配置。
 * - 输出电平：高电平吸合（适配常见的低电平触发继电器模块时，
 *   请把 RELAY_ON_LEVEL / RELAY_OFF_LEVEL 对调）。
 */
#include "relay.h"

#include <stdlib.h>
#include <board.h>
#include <finsh.h>

/* 继电器输出引脚定义（P5 端口，位 0~4） */
#define RELAY_PIN_LAMP       GET_PIN(5, 0)
#define RELAY_PIN_RANGE_HOOD GET_PIN(5, 1)
#define RELAY_PIN_FAN        GET_PIN(5, 2)
#define RELAY_PIN_DISHWASHER GET_PIN(5, 3)
#define RELAY_PIN_OVEN       GET_PIN(5, 4)

/* 吸合 / 断开电平 */
#define RELAY_ON_LEVEL       PIN_HIGH
#define RELAY_OFF_LEVEL      PIN_LOW

/* 通道 -> 引脚映射表（顺序与 RELAY_IDX_* 一致） */
static const rt_base_t s_relay_pins[RELAY_CH_NUM] =
{
    RELAY_PIN_LAMP,
    RELAY_PIN_RANGE_HOOD,
    RELAY_PIN_FAN,
    RELAY_PIN_DISHWASHER,
    RELAY_PIN_OVEN,
};

/* 通道 -> 名称映射表 */
static const char *const s_relay_names[RELAY_CH_NUM] =
{
    "厨房灯",
    "油烟机",
    "风扇",
    "洗碗机",
    "烤箱",
};

/* 当前状态缓存 */
static rt_uint8_t s_relay_state[RELAY_CH_NUM] = {0};

int relay_init(void)
{
    int i;

    for (i = 0; i < RELAY_CH_NUM; i++)
    {
        rt_pin_mode(s_relay_pins[i], PIN_MODE_OUTPUT);
        rt_pin_write(s_relay_pins[i], RELAY_OFF_LEVEL);
        s_relay_state[i] = 0;
    }

    return 0;
}

int relay_set(int idx, rt_bool_t on)
{
    if ((idx < 0) || (idx >= RELAY_CH_NUM))
    {
        return -RT_EINVAL;
    }

    rt_pin_write(s_relay_pins[idx], on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
    s_relay_state[idx] = on ? 1 : 0;

    return 0;
}

rt_bool_t relay_get(int idx)
{
    if ((idx < 0) || (idx >= RELAY_CH_NUM))
    {
        return RT_FALSE;
    }

    return s_relay_state[idx] ? RT_TRUE : RT_FALSE;
}

int relay_toggle(int idx)
{
    if ((idx < 0) || (idx >= RELAY_CH_NUM))
    {
        return -RT_EINVAL;
    }

    return relay_set(idx, s_relay_state[idx] ? RT_FALSE : RT_TRUE);
}

const char *relay_name(int idx)
{
    if ((idx < 0) || (idx >= RELAY_CH_NUM))
    {
        return "?";
    }

    return s_relay_names[idx];
}

#ifdef RT_USING_MSH
/* msh 调试命令：
 *   relay            —— 打印全部通道状态
 *   relay <idx>      —— 翻转指定通道
 *   relay <idx> <0|1>—— 强制设置指定通道
 */
static void relay_cmd(int argc, char **argv)
{
    int idx;

    if (argc < 2)
    {
        for (idx = 0; idx < RELAY_CH_NUM; idx++)
        {
            rt_kprintf("[%d] %-6s = %s\n",
                       idx, relay_name(idx), relay_get(idx) ? "ON" : "OFF");
        }
        return;
    }

    idx = atoi(argv[1]);
    if (argc >= 3)
    {
        relay_set(idx, atoi(argv[2]) ? RT_TRUE : RT_FALSE);
    }
    else
    {
        relay_toggle(idx);
    }

    rt_kprintf("[%d] %-6s = %s\n",
               idx, relay_name(idx), relay_get(idx) ? "ON" : "OFF");
}
MSH_CMD_EXPORT_ALIAS(relay_cmd, relay, kitchen relay: relay / relay <idx> / relay <idx> <0|1>);
#endif /* RT_USING_MSH */
