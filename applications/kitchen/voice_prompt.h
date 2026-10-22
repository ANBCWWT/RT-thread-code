/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 语音提示模块（智能厨房面板）
 *
 * - 基于 wavplayer 播放本地 wav 文件，不依赖任何云端 API
 * - 音频输出走 ES8388 编解码器（sound0 设备，已在基础工程中驱动）
 * - wav 文件来源：全部编译进固件、开机自动写入 /flash：
 *   1) button.wav —— 屏幕点击音效
 *   2) open.wav / close.wav —— "已打开 / 已关闭"人声提示（TTS 生成）
 *   缺失/写入失败时回退 button.wav，保证执行后一定有声音
 */
#ifndef KITCHEN_VOICE_PROMPT_H
#define KITCHEN_VOICE_PROMPT_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化提示音模块（设置播放音量），可在系统启动时调用一次 */
int voice_prompt_init(void);

/* 播放指定路径的 wav（会先打断上一段播放） */
int voice_prompt_play(const char *path);

/* 播放"已打开"提示音（文件缺失时回退到 button.wav） */
int voice_prompt_play_on(void);

/* 播放"已关闭"提示音（文件缺失时回退到 button.wav） */
int voice_prompt_play_off(void);

/* 播放屏幕点击音效（/flash/button.wav，应用层自动安装） */
int voice_prompt_play_button(void);

#ifdef __cplusplus
}
#endif

#endif /* KITCHEN_VOICE_PROMPT_H */
