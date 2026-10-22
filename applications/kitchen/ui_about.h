/*
 * 关于弹层（参赛功能批次 5）
 *
 * 模态弹层：全屏半透明遮罩 + 中央白色面板（项目名/版本/技术栈/作者）。
 * 入口在设置页标题右侧 "About" 按钮；点击遮罩或 OK 关闭。
 * 样式与家电页模式选择弹层一致（固定浅色，不做主题切换）。
 */
#ifndef KITCHEN_UI_ABOUT_H
#define KITCHEN_UI_ABOUT_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 创建关于弹层（UI 初始化阶段调用，初始隐藏）。
 * screen：活动屏幕；f_title / f_name / f_small：标题/正文/小字字体。 */
void ui_about_init(lv_obj_t *screen,
                   lv_font_t const *f_title,
                   lv_font_t const *f_name,
                   lv_font_t const *f_small);

/* 打开关于弹层（设置页 About 按钮回调调用） */
void ui_about_open(void);

/* 关闭关于弹层 */
void ui_about_close(void);

#ifdef __cplusplus
}
#endif

#endif /* KITCHEN_UI_ABOUT_H */
