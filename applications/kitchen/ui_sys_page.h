/*
 * 系统监控页 UI（参赛功能批次 2）
 *
 * 内容区（640x368）内的系统页：
 * - CPU 占用率（idle hook 统计，1s 窗口）；
 * - 内存用量（rt_memory_info）；
 * - 线程列表（对象容器遍历，创建时生成一次）；
 * - 网络（lwip 取 IP）+ SNTP 状态；
 * - 版本 / 构建时间 / 运行时长。
 *
 * 调用约定：ui_sys_page_init 在 UI 线程创建控件（内部注册 idle hook）；
 * ui_sys_page_refresh 由 UI 主循环每秒调用。
 */
#ifndef KITCHEN_UI_SYS_PAGE_H
#define KITCHEN_UI_SYS_PAGE_H

#include <lvgl.h>

/* 创建系统页内容（parent = 系统页容器） */
void ui_sys_page_init(lv_obj_t *parent);

/* 每秒刷新：CPU / 内存 / 运行时长 / 网络（UI 线程内调用） */
void ui_sys_page_refresh(void);

#endif /* KITCHEN_UI_SYS_PAGE_H */
