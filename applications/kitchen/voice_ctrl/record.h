/*
 * 板载 PTT 按键 + mic0 麦克风接口（语音控制 · 阶段 2）
 *
 *  - PTT：板载按键 P8_3（VOICE_PTT_PIN），按下=开始说话，释放=结束。
 *    轮询线程 20ms 消抖，边沿触发调用 asr_ptt_pressed()/asr_ptt_released()。
 *  - mic0：RT-Thread audio 设备（PDM 麦克风），提供打开/读取封装。
 *
 * 按键极性默认"按下=低电平"（板载按键接地）。若实际极性相反，
 * 把 ptt_thread_entry 中的 PIN_LOW / PIN_HIGH 对调即可。
 */
#ifndef VOICE_RECORD_H
#define VOICE_RECORD_H

#include <rtthread.h>

/* 创建 PTT 轮询线程（自动初始化，无需手动调用） */
int record_ptt_init(void);

/* ---- mic0 设备接口 ---- */
int  mic_device_open(void);
void mic_device_close(void);
/* 读取一帧音频，返回实际字节数（<=len，0 或负值表示无数据） */
int  mic_read(uint8_t *buf, int len);

#endif /* VOICE_RECORD_H */
