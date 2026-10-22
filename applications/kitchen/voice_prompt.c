/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 语音提示模块实现
 */
#include "voice_prompt.h"

#include <wavplayer.h>
#include <finsh.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "button_wav.h"
#include "open_wav.h"
#include "close_wav.h"

/* ============================================================================
 * 提示音路径配置
 * ----------------------------------------------------------------------------
 * "已打开 / 已关闭"人声 wav 已编译进固件（open_wav.c / close_wav.c，
 * 16kHz/16bit/单声道，由 gen_wav_c.py 从 applications/*.wav 生成），
 * 开机自动写入板载 littlefs 的 /flash：
 *     /flash/open.wav   内容："已打开"
 *     /flash/close.wav  内容："已关闭"
 * 若写入失败（flash 满等），回退到 /flash/button.wav（已有点击音效），
 * 保证执行后一定有声音。想换人声：替换 applications/open.wav 和
 * close.wav（16kHz/16bit 单声道或 44.1k 立体声均可），重跑
 * applications/kitchen/resample_wav.py + gen_wav_c.py 后重新编译。
 * ==========================================================================*/
#define PROMPT_ON_PATH    "/flash/open.wav"     /* "已打开"（开机自动安装） */
#define PROMPT_OFF_PATH   "/flash/close.wav"    /* "已关闭"（开机自动安装） */
#define PROMPT_FALLBACK   "/flash/button.wav"   /* 回退：点击音效（已有） */

/* 点击音效：/flash/button.wav（应用层自带的 button.wav 写入 littlefs 而来） */
#define BUTTON_WAV_PATH   "/flash/button.wav"

/* 播放音量（0 ~ 99） */
#define PROMPT_VOLUME     80

/* ============================================================================
 * 提示音安装（开机一次性写入 littlefs）
 * ----------------------------------------------------------------------------
 * 把编译进固件的 wav（C 数组）写入板载 littlefs 的 /flash。
 * 只写一次：文件已存在、长度一致且头部有效则跳过，避免每次开机重复
 * 擦写 flash。若文件损坏（头部无效）则强制重写，保证一定能播。
 * /flash 在系统 INIT_ENV 阶段已挂载，早于本函数（main 里）被调用。
 * ==========================================================================*/
/* 校验 wav 文件头（RIFF....WAVE），文件损坏时能同步感知，
 * 避免 wavplayer 异步打开失败后无声无息。 */
static int wav_header_valid(const char *path)
{
    int fd;
    unsigned char hdr[12];

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        return 0;
    }
    if (read(fd, hdr, 12) != 12)
    {
        close(fd);
        return 0;
    }
    close(fd);
    return (rt_memcmp(hdr, "RIFF", 4) == 0 && rt_memcmp(hdr + 8, "WAVE", 4) == 0);
}

static int voice_prompt_install_file(const char *path, const rt_uint8_t *data, unsigned int len)
{
    int fd;
    struct stat st;

    if (stat(path, &st) == 0 && st.st_size == (int)len && wav_header_valid(path))
    {
        rt_kprintf("[kitchen.voice] %s ok, skip\n", path);
        return 0;   /* 已安装且有效，跳过 */
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        rt_kprintf("[kitchen.voice] open %s failed\n", path);
        return -RT_ERROR;
    }

    if (write(fd, data, len) != (int)len)
    {
        close(fd);
        rt_kprintf("[kitchen.voice] write %s failed\n", path);
        return -RT_ERROR;
    }

    close(fd);
    rt_kprintf("[kitchen.voice] %s installed (%u bytes)\n", path, len);
    return 0;
}

static void voice_prompt_install_prompts(void)
{
    voice_prompt_install_file(BUTTON_WAV_PATH, button_wav_data, button_wav_len);
    voice_prompt_install_file(PROMPT_ON_PATH, open_wav_data, open_wav_len);
    voice_prompt_install_file(PROMPT_OFF_PATH, close_wav_data, close_wav_len);
}

int voice_prompt_init(void)
{
    wavplayer_volume_set(PROMPT_VOLUME);
    voice_prompt_install_prompts();
    return 0;
}

int voice_prompt_play(const char *path)
{
    struct stat st;

    if (path == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* wavplayer_play 是异步的：它只把 PLAY 消息投递到 wavplayer 线程就立即
     * 返回，真正的文件 open 发生在该线程里，调用方无法感知播放失败。
     * 所以播放前先确认文件存在且头部有效：不满足直接返回失败，让调用方
     * 走重装/回退；满足时（格式已验证）播放必然成功。 */
    if (stat(path, &st) != 0 || st.st_size == 0 || !wav_header_valid(path))
    {
        return -RT_ENOENT;
    }

    /* 打断上一段未播完的提示音 */
    wavplayer_stop();

    if (wavplayer_play((char *)path) == 0)
    {
        return 0;
    }

    return -RT_ERROR;
}

/* 播放失败（文件缺失/损坏）时：重新安装一次再播，自愈后仍有问题才走回退 */
static int voice_prompt_play_with_reinstall(const char *path,
                                            const rt_uint8_t *data, unsigned int len)
{
    if (voice_prompt_play(path) == 0)
    {
        return 0;
    }
    rt_kprintf("[kitchen.voice] %s not playable, reinstalling\n", path);
    voice_prompt_install_file(path, data, len);
    return voice_prompt_play(path);
}

int voice_prompt_play_on(void)
{
    if (voice_prompt_play_with_reinstall(PROMPT_ON_PATH, open_wav_data, open_wav_len) == 0)
    {
        return 0;
    }
    return voice_prompt_play(PROMPT_FALLBACK);
}

int voice_prompt_play_off(void)
{
    if (voice_prompt_play_with_reinstall(PROMPT_OFF_PATH, close_wav_data, close_wav_len) == 0)
    {
        return 0;
    }
    return voice_prompt_play(PROMPT_FALLBACK);
}

int voice_prompt_play_button(void)
{
    return voice_prompt_play(BUTTON_WAV_PATH);
}

#ifdef RT_USING_MSH
/* msh 调试命令：
 *   voice_play <path> —— 播放指定 wav
 *   voice_on          —— 播放"已打开"提示音
 *   voice_off         —— 播放"已关闭"提示音
 *   voice_button      —— 播放点击音效（button.wav）
 */
static void voice_play_cmd(int argc, char **argv)
{
    if (argc >= 2)
    {
        voice_prompt_play(argv[1]);
    }
    else
    {
        rt_kprintf("usage: voice_play <path>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(voice_play_cmd, voice_play, play a wav file by path);

static void voice_on_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    voice_prompt_play_on();
}
MSH_CMD_EXPORT_ALIAS(voice_on_cmd, voice_on, play 'opened' prompt);

static void voice_off_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    voice_prompt_play_off();
}
MSH_CMD_EXPORT_ALIAS(voice_off_cmd, voice_off, play 'closed' prompt);

static void voice_button_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    voice_prompt_play_button();
}
MSH_CMD_EXPORT_ALIAS(voice_button_cmd, voice_button, play button click sound);
#endif /* RT_USING_MSH */
