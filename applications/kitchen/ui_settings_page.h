/*
 * 设置页（参赛功能批次 3）
 *
 * 设置项：背光亮度（滑条）/ 息屏时间（4 档）/ 音量（滑条）/ 亮暗主题（2 档）。
 * 修改即应用 + 持久化（settings_store 落盘 /flash/settings.dat）。
 * 滑条拖动实时生效，松手才写 flash（防频繁擦写）。
 */
#ifndef UI_SETTINGS_PAGE_H
#define UI_SETTINGS_PAGE_H

#include <lvgl.h>

/* 创建设置页内容（parent = 设置页容器） */
void ui_settings_page_init(lv_obj_t *parent);

/* 占位刷新接口（当前所有控件事件自更新，无需周期刷新） */
void ui_settings_page_refresh(void);

#endif /* UI_SETTINGS_PAGE_H */
