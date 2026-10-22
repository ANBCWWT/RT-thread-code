/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AHT20 温湿度传感器驱动（智能厨房面板）
 *
 * - 板载 AHT20 挂在软 I2C1 总线（i2c1）上
 *   SCL = BSP_SOFT_I2C1_SCL_PIN(105)、SDA = BSP_SOFT_I2C1_SDA_PIN(106)
 * - 不依赖 aht10/aht20 软件包，直接使用 RT-Thread I2C 设备接口，
 *   因此无需在 menuconfig 中开启任何新选项、无需修改 rtconfig.h。
 */
#ifndef KITCHEN_AHT20_H
#define KITCHEN_AHT20_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 AHT20（发送 0xBE 初始化命令），可在系统启动时调用一次 */
int aht20_init(void);

/* 触发一次测量并读取温湿度（内部会等待测量完成）
 * temperature_c  : 温度，单位 ℃
 * humidity_pct   : 相对湿度，单位 %
 * 任一指针可为 RT_NULL，表示不需要该值
 */
int aht20_read(float *temperature_c, float *humidity_pct);

#ifdef __cplusplus
}
#endif

#endif /* KITCHEN_AHT20_H */
