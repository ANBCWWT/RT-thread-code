/*
 * 环境历史记录实现（参赛功能批次 2）
 *
 * 环形缓冲 + littlefs 持久化。所有接口只操作内存数据 + 启动/追加时访问文件，
 * 可在任何线程调用（追加频率 60s 一次，写 1.5KB，不会阻塞业务）。
 */
#include "env_history.h"

#include <dfs_posix.h>

#define DBG_TAG "env.hist"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define HISTORY_FILE   "/flash/history.dat"
#define ITEM_BYTES     ((int)sizeof(env_history_item_t))

/* 环形缓冲：s_count < MAX 时顺序追加（s_head 保持 0）；满后覆盖 s_head 并前移 */
static env_history_item_t s_items[ENV_HISTORY_MAX];
static int s_count = 0;
static int s_head  = 0;

/* 全量落盘：写 [count][记录...]，记录按时间顺序（最旧在前） */
static int env_history_save(void)
{
    int fd;
    int i;
    rt_int32_t count = s_count;

    fd = open(HISTORY_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        LOG_E("open %s failed (%d)", HISTORY_FILE, fd);
        return -RT_ERROR;
    }

    if (write(fd, &count, sizeof(count)) != sizeof(count))
    {
        LOG_E("write count failed");
        close(fd);
        return -RT_ERROR;
    }

    for (i = 0; i < s_count; i++)
    {
        int pos = (s_head + i) % ENV_HISTORY_MAX;
        if (write(fd, &s_items[pos], ITEM_BYTES) != ITEM_BYTES)
        {
            LOG_E("write item[%d] failed", i);
            close(fd);
            return -RT_ERROR;
        }
    }

    close(fd);
    return RT_EOK;
}

int env_history_init(void)
{
    int fd;
    rt_int32_t count = 0;
    int i;

    fd = open(HISTORY_FILE, O_RDONLY, 0);
    if (fd < 0)
    {
        LOG_D("no history file, start empty");
        return RT_EOK;
    }

    if (read(fd, &count, sizeof(count)) != sizeof(count))
    {
        LOG_E("read count failed");
        close(fd);
        return -RT_ERROR;
    }

    if ((count < 0) || (count > ENV_HISTORY_MAX))
    {
        LOG_W("bad history count %d, ignore file", (int)count);
        close(fd);
        return -RT_ERROR;
    }

    s_count = 0;
    s_head  = 0;
    for (i = 0; i < count; i++)
    {
        if (read(fd, &s_items[i], ITEM_BYTES) != ITEM_BYTES)
        {
            LOG_W("short read at %d/%d", i, (int)count);
            break;
        }
        s_count++;
    }

    close(fd);
    LOG_I("loaded %d history records from %s", s_count, HISTORY_FILE);
    return RT_EOK;
}

int env_history_append(float temp, float hum, time_t epoch)
{
    if (s_count < ENV_HISTORY_MAX)
    {
        s_items[s_count].temp  = temp;
        s_items[s_count].hum   = hum;
        s_items[s_count].epoch = (int32_t)epoch;
        s_count++;
    }
    else
    {
        s_items[s_head].temp  = temp;
        s_items[s_head].hum   = hum;
        s_items[s_head].epoch = (int32_t)epoch;
        s_head = (s_head + 1) % ENV_HISTORY_MAX;
    }

    return env_history_save();
}

int env_history_count(void)
{
    return s_count;
}

rt_bool_t env_history_get(int idx, env_history_item_t *item)
{
    int pos;

    if ((idx < 0) || (idx >= s_count) || (item == RT_NULL))
    {
        return RT_FALSE;
    }

    pos = (s_head + idx) % ENV_HISTORY_MAX;
    *item = s_items[pos];
    return RT_TRUE;
}

void env_history_minmax(env_history_minmax_t *mm)
{
    int i;
    env_history_item_t it;

    if (mm == RT_NULL)
    {
        return;
    }

    mm->t_min = 999.0f;
    mm->t_max = -999.0f;
    mm->h_min = 999.0f;
    mm->h_max = -999.0f;

    for (i = 0; i < s_count; i++)
    {
        if (!env_history_get(i, &it))
        {
            break;
        }
        if (it.temp < mm->t_min) mm->t_min = it.temp;
        if (it.temp > mm->t_max) mm->t_max = it.temp;
        if (it.hum  < mm->h_min) mm->h_min = it.hum;
        if (it.hum  > mm->h_max) mm->h_max = it.hum;
    }

    if (s_count == 0)
    {
        mm->t_min = mm->t_max = mm->h_min = mm->h_max = 0.0f;
    }
}
