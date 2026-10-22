/*
 * 设置项存储实现（参赛功能批次 3）
 */
#include "settings_store.h"

#include <dfs_posix.h>

#define DBG_TAG "set.store"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define SETTINGS_FILE    "/flash/settings.dat"
#define SETTINGS_MAGIC   0x53455454U   /* "SETT" */

/* 默认值 = 当前行为：亮度 80 / 音量 80 / 息屏 60s / 亮色 */
#define DEFAULT_BRIGHTNESS   80U
#define DEFAULT_VOLUME       80U
#define DEFAULT_SLEEP_SEC    60U
#define DEFAULT_DARK         0U

static settings_t s_settings;

static void settings_defaults(void)
{
    rt_memset(&s_settings, 0, sizeof(s_settings));
    s_settings.magic      = SETTINGS_MAGIC;
    s_settings.brightness = DEFAULT_BRIGHTNESS;
    s_settings.volume     = DEFAULT_VOLUME;
    s_settings.sleep_sec  = DEFAULT_SLEEP_SEC;
    s_settings.dark       = DEFAULT_DARK;
}

void settings_init(void)
{
    int fd;

    settings_defaults();

    fd = open(SETTINGS_FILE, O_RDONLY, 0);
    if (fd >= 0)
    {
        if ((read(fd, &s_settings, sizeof(s_settings)) == (int)sizeof(s_settings)) &&
            (s_settings.magic == SETTINGS_MAGIC))
        {
            /* 文件有效，使用已存设置 */
            LOG_I("settings loaded (bright=%d vol=%d sleep=%ds dark=%d)",
                  s_settings.brightness, s_settings.volume,
                  s_settings.sleep_sec, s_settings.dark);
        }
        else
        {
            LOG_W("settings file corrupt, use defaults");
            settings_defaults();
            settings_save();
        }
        close(fd);
    }
    else
    {
        /* 首次启动：落盘默认值 */
        settings_save();
    }
}

const settings_t *settings_get(void)
{
    return &s_settings;
}

int settings_save(void)
{
    int fd;

    fd = open(SETTINGS_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        LOG_E("open %s failed (%d)", SETTINGS_FILE, fd);
        return -RT_ERROR;
    }

    if (write(fd, &s_settings, sizeof(s_settings)) != (int)sizeof(s_settings))
    {
        LOG_E("write settings failed");
        close(fd);
        return -RT_ERROR;
    }

    close(fd);
    return RT_EOK;
}

void settings_set_brightness(rt_uint8_t v)
{
    if (v > 100)
    {
        v = 100;
    }
    s_settings.brightness = v;
    settings_save();
}

void settings_set_volume(rt_uint8_t v)
{
    if (v > 99)
    {
        v = 99;
    }
    s_settings.volume = v;
    settings_save();
}

void settings_set_sleep_sec(rt_uint16_t v)
{
    s_settings.sleep_sec = v;
    settings_save();
}

void settings_set_dark(rt_uint8_t v)
{
    s_settings.dark = (v != 0) ? 1U : 0U;
    settings_save();
}
