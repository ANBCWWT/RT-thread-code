/*
 * 设置项存储（参赛功能批次 3）
 *
 * 设置持久化到 /flash/settings.dat（littlefs，与 env_history 同机制）。
 * 所有接口可在任意线程调用；修改即写文件（结构体约 16 字节，开销可忽略）。
 */
#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <rtthread.h>

/* 设置项结构 */
typedef struct
{
    rt_uint32_t magic;       /* 魔数校验，防止读到垃圾数据 */
    rt_uint8_t  brightness;  /* 背光亮度 0-100 */
    rt_uint8_t  volume;      /* 音量 0-99 */
    rt_uint16_t sleep_sec;   /* 息屏超时（秒），0 = 常亮 */
    rt_uint8_t  dark;        /* 主题：0 = 亮色，1 = 暗色 */
    rt_uint8_t  reserved[3]; /* 对齐保留 */
} settings_t;

/* 启动加载：有有效文件读文件，否则用默认值并落盘。须在背光/音量/息屏模块可用后调用 */
void settings_init(void);

/* 当前设置（只读指针） */
const settings_t *settings_get(void);

/* 立即写 flash，返回 0 成功 */
int settings_save(void);

/* 修改单个设置项并落盘 */
void settings_set_brightness(rt_uint8_t v);
void settings_set_volume(rt_uint8_t v);
void settings_set_sleep_sec(rt_uint16_t v);
void settings_set_dark(rt_uint8_t v);

#endif /* SETTINGS_STORE_H */
