/*
 * 语音指令执行模块（阶段 3：识别文本 → 厨房设备控制）
 *
 * 入口：asr.c 识别出文本后调用 asr_action_handle()；
 * 也可用 msh 命令 `asr_action 打开厨房灯` 免录音直接测试。
 */
#ifndef VOICE_ASR_ACTION_H
#define VOICE_ASR_ACTION_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 处理一条识别文本：
 * - 归一化（去标点/口头语/全角符号）
 * - 匹配设备对象（厨房灯/油烟机/风扇/洗碗机/烤箱，支持"全部"）
 * - 匹配动作（开/关/翻转）并执行继电器
 * - 同步刷新界面 + 播放"已打开/已关闭"提示音
 */
void asr_action_handle(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_ASR_ACTION_H */
