/*
 * 环境历史记录（参赛功能批次 2）
 *
 * 环形缓冲保存温湿度采样历史，持久化到 /flash/history.dat（littlefs）。
 * - 容量：128 条 × 60s ≈ 2 小时窗口；
 * - 文件格式：前 4 字节条数 count，后跟 count 条记录（时间顺序，最旧在前）；
 * - 追加即全量落盘（128×12B ≈ 1.5KB，频率由调用方控制）；
 * - 息屏暂停采样期间不追加（由调用方采样节奏保证）。
 */
#ifndef KITCHEN_ENV_HISTORY_H
#define KITCHEN_ENV_HISTORY_H

#include <rtthread.h>
#include <time.h>

/* 历史记录条数（128 条 × 60s ≈ 2h 窗口） */
#define ENV_HISTORY_MAX   128

/* 单条记录：温度(℃)、湿度(%)、unix 时间戳（未对时为 0） */
typedef struct
{
    float   temp;
    float   hum;
    int32_t epoch;
} env_history_item_t;

/* 极值统计 */
typedef struct
{
    float t_min;
    float t_max;
    float h_min;
    float h_max;
} env_history_minmax_t;

/* 初始化：从 /flash/history.dat 加载历史（文件不存在则从空开始） */
int env_history_init(void);

/* 追加一条记录（调用方控制节奏），并立即全量落盘 */
int env_history_append(float temp, float hum, time_t epoch);

/* 当前有效条数（0 ~ ENV_HISTORY_MAX） */
int env_history_count(void);

/* 读取第 idx 条（0 = 最旧） */
rt_bool_t env_history_get(int idx, env_history_item_t *item);

/* 极值统计 */
void env_history_minmax(env_history_minmax_t *mm);

#endif /* KITCHEN_ENV_HISTORY_H */
