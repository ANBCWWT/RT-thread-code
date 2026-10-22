/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 智能厨房面板主入口（基于 Edgi-Talk M55 小智工程改造）
 *
 * 功能：
 * - 5 路继电器控制（厨房灯 / 油烟机 / 风扇 / 插座1 / 插座2）
 * - 板载 AHT20 温湿度采集显示
 * - LVGL 触屏主界面（2x3 卡片：环境 + 5 个开关）
 * - 点击开关自动播放"已打开 / 已关闭"提示音（本地 wav，无需 API）
 *
 * 启动方式：由 applications/main.c 中的 BSP_USING_KITCHEN_PANEL 分支调用，
 * 不通过 INIT_APP_EXPORT 自动导出，避免与原有小智逻辑同时启动。
 */
#ifndef KITCHEN_PANEL_H
#define KITCHEN_PANEL_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 智能厨房面板主入口：初始化外设、UI、业务线程 */
int kitchen_panel_init(void);

#ifdef __cplusplus
}
#endif

#endif /* KITCHEN_PANEL_H */
