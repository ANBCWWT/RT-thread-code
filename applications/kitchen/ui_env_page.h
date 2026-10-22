/*
 * 环境趋势页 UI（参赛功能批次 2）
 *
 * 内容区（640x368）内的环境页：
 * - 左侧：LVGL chart 双线（温度主 Y 轴 / 湿度次 Y 轴）；
 * - 右侧：实时值 + 最高/最低极值 + 采样说明。
 *
 * 调用约定：ui_env_page_init 在 UI 线程创建控件；
 * ui_env_page_refresh 由 UI 主循环周期调用（数据变化才重绘）。
 */
#ifndef KITCHEN_UI_ENV_PAGE_H
#define KITCHEN_UI_ENV_PAGE_H

#include <lvgl.h>

/* 创建环境页内容（parent = 环境页容器） */
void ui_env_page_init(lv_obj_t *parent);

/* 刷新：从 env_history 取数更新曲线与极值（UI 线程内调用） */
void ui_env_page_refresh(void);

#endif /* KITCHEN_UI_ENV_PAGE_H */
