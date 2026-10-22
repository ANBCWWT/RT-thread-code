/*
 * 开机动画（参赛功能批次 5）
 *
 * 全屏启动层 + 中央 logo 卡片（绿圆标 + 项目名），时序：
 *   上滑淡入（700ms）→ 停顿（400ms）→ 整体淡出（400ms）→ 删除启动层
 * 动画期间启动层可点击但无操作（拦截触摸，避免误触下层）。
 * 在 UI 全部创建完成后、进入主循环前调用一次。
 */
#ifndef KITCHEN_UI_SPLASH_H
#define KITCHEN_UI_SPLASH_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 播放开机动画（UI 线程初始化阶段调用，仅一次）。
 * screen：活动屏幕；bg：启动层背景色（取主题背景）；
 * f_title / f_name：标题与正文 Montserrat 字体。 */
void ui_splash_play(lv_obj_t *screen, lv_color_t bg,
                    lv_font_t const *f_title, lv_font_t const *f_name);

#ifdef __cplusplus
}
#endif

#endif /* KITCHEN_UI_SPLASH_H */
