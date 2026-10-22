/*
 * 讯飞语音听写（流式版）识别模块接口（语音控制 · 阶段 2）
 *
 * 交互：板载 PTT 按键按住说话，释放后自动上传并打印识别文本。
 * 调试：msh 命令 `asr_once [秒数]` 可免按键自动录一段测试。
 *
 * 密钥：引用 applications/API.h 中的 APIID / APISecret / APIKey 宏，
 *      该文件由用户保管，本模块不读不写。
 */
#ifndef VOICE_ASR_H
#define VOICE_ASR_H

#include <rtthread.h>

/* PTT 按下（开始录音+上传）；重复按下忽略 */
void asr_ptt_pressed(void);

/* PTT 释放（结束录音，发送最后一帧并接收识别结果） */
void asr_ptt_released(void);

/* 当前是否正在录音（供 record.c 的 LED 闪烁线程查询） */
rt_bool_t asr_is_recording(void);

/* asr 模块是否已初始化完成（线程已创建） */
rt_bool_t asr_is_ready(void);

#endif /* VOICE_ASR_H */
