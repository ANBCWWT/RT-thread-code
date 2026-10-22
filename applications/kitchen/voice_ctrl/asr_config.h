/*
 * 语音控制公共配置（阶段 2：录音 + 讯飞流式识别）
 *
 * 注意：密钥不在这里！三个密钥（APIID / APISecret / APIKey）
 * 在工程根目录 applications/API.h 中定义，由用户保管，本模块
 * 仅通过 #include "API.h" 引用宏。
 */
#ifndef VOICE_ASR_CONFIG_H
#define VOICE_ASR_CONFIG_H

#include <rtthread.h>
#include <rtdevice.h>
#include "board.h"   /* GET_PIN 宏（BSP 定义于 libraries/HAL_Drivers/drv_gpio.h，board.h 第 17 行引入） */

/* ---- 讯飞语音听写（流式版）WebSocket 接口 ---- */
#define ASR_HOST            "iat-api.xfyun.cn"
#define ASR_PORT            80              /* 明文 ws（demo 够用，密钥签名在 header 中） */
#define ASR_PATH            "/v2/iat"

/* ---- 音频参数（讯飞要求 16k / 16bit / 单声道 PCM） ---- */
#define ASR_SAMPLE_RATE     16000
#define ASR_FRAME_BYTES     1280            /* 40ms 一帧：16000*2*0.04 */
#define ASR_BASE64_LEN      ((ASR_FRAME_BYTES / 3) * 4 + 8)

/* ---- 行为参数 ---- */
#define ASR_MAX_RECORD_MS   10000           /* 单次录音最长 10s（讯飞 10s 无数据会断开） */
#define ASR_RECV_TIMEOUT_MS 6000            /* 收结果超时 */
#define ASR_CONNECT_TIMEOUT_MS 8000         /* TCP/握手超时 */

/* 板载 PTT 按键（手册板载按键，与小智 xiaozhi.h 定义一致） */
#define VOICE_PTT_PIN       GET_PIN(8, 3)

/* 板载蓝色 LED（P16.5，官方 demo_key 示例：按键中断翻转此 LED）。
 * 用途：录音时闪烁、平时常亮。若实测极性相反，把 record.c
 * 中 LED 线程的 PIN_HIGH/PIN_LOW 对调即可。 */
#define VOICE_LED_PIN       GET_PIN(16, 5)

/* ---- 调试开关 ---- */
#define ASR_PRINT_RESULT    1               /* 识别文本打印到串口 */

#endif /* VOICE_ASR_CONFIG_H */
