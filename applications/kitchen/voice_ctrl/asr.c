/*
 * 讯飞语音听写（流式版）识别实现（语音控制 · 阶段 2）
 *
 * 流程：PTT 按下 → 检查网络 → SNTP 对时 → 构造 HMAC-SHA256 签名
 *       → WebSocket 握手 → 打开 mic0 → 边录边传（1280B/40ms，status 0→1）
 *       → 释放后发尾帧(status=2) → 接收识别 JSON（拼接 text）→ 打印
 *
 * 依赖：wifi_link（网络）、sntp（对时）、ws_client（自写 WS）、
 *       record（PTT+mic0）、mbedtls（HMAC/base64）、API.h（密钥宏）
 */
#include "asr.h"
#include "asr_config.h"
#include "asr_action.h"
#include "record.h"
#include "wifi_link.h"
#include "sntp.h"
#include "ws_client.h"
#include "../voice_prompt.h"   /* 录音开始提示音 */

#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <mbedtls/md.h>
#include <mbedtls/base64.h>

/* 密钥宏：用户自行维护 applications/API.h，本模块只引用。
 * 用相对源文件路径 include（voice_ctrl → applications），
 * 不依赖构建系统的头文件搜索路径，确保一定能编过。 */
#include "../../API.h"

#define DBG_TAG "voice.asr"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ws_connect 握手需约 3.7KB 局部缓冲（request/response），栈给足 */
#define ASR_THREAD_STACK    8192
#define ASR_THREAD_PRIORITY 10

/* ---- 录音边界（用户确认：按住 PTT 说话、说完松开） ----
 * 松开 PTT 后再续录 ASR_TAIL_FRAMES 帧（500ms）尾音，
 * 防止"灯"等末字尾音被截断。开头无需 pre-roll：用户按下后
 * 停顿再开口，recording 开始时尚未说话，开头天然完整。 */
#define ASR_TAIL_MS          500
#define ASR_TAIL_FRAMES      (ASR_TAIL_MS / 40)

/* ---- 静态缓冲（放 BSS，避免占用线程栈） ---- */
static uint8_t  s_audio_frame[ASR_FRAME_BYTES];       /* 1280B 一帧（16k 40ms） */
static uint8_t  s_mic_raw[ASR_FRAME_BYTES * 2];       /* 2560B = 40ms@16k×2ch 交织原始数据 */
static char     s_b64_buf[ASR_BASE64_LEN + 4];        /* base64 音频 */
static char     s_json_buf[ASR_BASE64_LEN + 256];     /* WS 帧 JSON */
static uint8_t  s_recv_buf[2048 + 1];                   /* WS 接收帧（多 1B 存 '\0'） */
static char     s_result[512];                        /* 识别结果 */
static char     s_path[1024];                         /* 带签名的请求路径 */

/* ---- 状态 ---- */
static rt_sem_t s_ptt_sem = RT_NULL;
static volatile rt_bool_t s_ptt_active   = RT_FALSE;  /* 按键占用中 */
static volatile rt_bool_t s_recording    = RT_FALSE;  /* 录音中 */
static volatile int       s_asr_once_secs = 0;        /* msh asr_once 命令时长 */
static volatile rt_bool_t s_asr_dbg      = RT_FALSE;  /* msh asr_dbg 调试打印开关 */
static volatile int       s_peak_max     = 0;         /* 本次识别全程音频峰值 */
static volatile rt_tick_t s_ptt_press_tick = 0;       /* PTT 按下时刻（诊断录音起点延迟） */

/* 峰值时间分布：8 桶 × 400ms，看提示音/语音/静音分别落在哪段 */
#define PEAK_BUCKET_NUM  8
#define PEAK_BUCKET_FRAMES 10   /* 10 帧 = 400ms */
static int s_frame_cnt   = 0;
static int s_peak_buckets[PEAK_BUCKET_NUM];

/* ---------------- 工具 ---------------- */

/* URL 编码（签名参数含 + / = : 空格等，必须编码） */
static void url_encode(const char *in, char *out, int out_cap)
{
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;

    for (; *in != '\0' && o < out_cap - 4; in++)
    {
        unsigned char c = (unsigned char)*in;

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out[o++] = (char)c;
        }
        else
        {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
}

/* base64 编码封装 */
static int b64_encode(char *dst, int dst_cap, const uint8_t *src, int src_len)
{
    size_t olen = 0;

    if (mbedtls_base64_encode((unsigned char *)dst, (size_t)dst_cap,
                              &olen, src, (size_t)src_len) != 0)
    {
        return -1;
    }
    return (int)olen;
}

/* ---------------- mic0 音频读取（16k×2ch 交织 → 16k 单声道） ----------------
 * 采样率真相（官方代码铁证，别再按"64k"算）：
 *  - xiaozhi.h: XZ_MIC_FRAME_LEN = 320*6*2 = 3840B = 1920 样本 = 60ms×16k×2ch，
 *    opus_encoder_create(16000, 2) 直接编码且官方固件工作正常 → PDM 每通道 16k；
 *  - rtconfig.h: #define ENABLE_STEREO_INPUT_FEED → drv_pdm.c 中断里
 *    L/R 通道交织写入 ping-pong 缓冲；
 *  - drv_pdm.c: "10msec data is 320 samples in STEREO mode" = 32k 样本/s 交织
 *    = 每通道 16k（此前把 PDM 时钟算成 64k 是错的，clkDiv 公式不对）。
 *
 * 正确做法：每帧读 2560B（40ms@16k×2ch 交织），解交织取 L 通道
 * → 640 样本@16k 单声道 = 1280B，直接上传，宣告 ASR_SAMPLE_RATE=16000。
 * 历史教训（识别乱码/空/兹拉兹拉的根因）：
 *  1) 按"64k 单声道"读 5120B（=80ms 交织数据）+ 4:1 平均 → 时间压缩 2 倍
 *     + 双 mic 混叠 → 讯飞播放加速 → 识别乱/空；
 *  2) 各种 FIR 抗混叠滤波都救不了采样率错乱，只会更糊。
 * 本条修复后：速率正确 + 单通道波形保真。 */
static int mic_read_downsampled(uint8_t *out, int out_cap)
{
    int got = 0;
    int reads = 0;
    int rmin = 1 << 30;
    int rmax = 0;
    rt_tick_t t0 = rt_tick_get();

    RT_UNUSED(out_cap);

    while (got < (int)sizeof(s_mic_raw))
    {
        int n = mic_read(s_mic_raw + got, (int)sizeof(s_mic_raw) - got);

        if (n <= 0)
        {
            rt_thread_mdelay(5);
            continue;
        }
        if (n < rmin)
        {
            rmin = n;
        }
        if (n > rmax)
        {
            rmax = n;
        }
        reads++;
        got += n;
    }

    /* 解交织：PDM 输出 16k×2ch 交织（L0 R0 L1 R1 ...），取 L 通道 → 16k 单声道。
     * 不降采样、不平均、不整流——波形保真，速率正好。 */
    {
        const int16_t *src = (const int16_t *)s_mic_raw;  /* 1280 个交织样本 */
        int16_t *dst = (int16_t *)out;
        int n = (int)got / 2;   /* 交织样本数（2560B = 1280 样本 = 40ms@16k×2ch） */
        int i, o = 0;
        int32_t peak = 0;
        int64_t dc_sum = 0;     /* 直流偏置（原始符号累加） */
        int zc = 0;             /* 过零次数（语音波形应频繁过零） */

        for (i = 0; i + 1 < n; i += 2)
        {
            int16_t s16 = src[i];   /* L 通道（偶数索引；若换 R 改 src[i+1]） */

            dc_sum += s16;
            if (o > 0 && ((s16 >= 0 && dst[o - 1] < 0) || (s16 < 0 && dst[o - 1] >= 0)))
            {
                zc++;
            }
            {
                int32_t v = s16;

                if (v < 0)
                {
                    v = -v;
                }
                if (v > peak)
                {
                    peak = v;
                }
            }
            dst[o++] = s16;
        }

        if (s_asr_dbg)
        {
            /* 健康语音：dc≈0（±几百）、zc 约 5%~30%。
             * dc 很大或 zc 异常 → 数据不是语音（相位/时钟/通道问题）。 */
            LOG_I("mic dc=%d zc=%d%%", (int)(dc_sum / (o ? o : 1)),
                  o ? (zc * 100 / o) : 0);
        }

        if (peak > s_peak_max)
        {
            s_peak_max = peak;   /* 每次识别全程的最大峰值（自动诊断，无需开关） */
        }
        /* 峰值时间分布桶（400ms/桶），用于判断语音/提示音/静音分布 */
        {
            int b = (s_frame_cnt / PEAK_BUCKET_FRAMES) % PEAK_BUCKET_NUM;

            if (peak > s_peak_buckets[b])
            {
                s_peak_buckets[b] = peak;
            }
            s_frame_cnt++;
        }
        if (s_asr_dbg)
        {
            LOG_I("audio: peak=%d (0..32767)", (int)peak);
        }

        return o * 2;   /* 1280B = 40ms@16k */
    }
}

/* 从识别结果 JSON 中提取词并追加到 out。
 * 注意：讯飞 iat 返回的字段是 data.result.ws[].cw[].w（"w":"..."），
 * 不是 "text"，逐词拼接即可得到完整识别文本。 */
static void asr_append_text(const char *json, char *out, int out_cap)
{
    const char *p = json;
    int len = (int)rt_strlen(out);

    while ((p = strstr(p, "\"w\":\"")) != RT_NULL && len < out_cap - 1)
    {
        p += 5;   /* 跳过 "w":" */

        while (*p != '\0' && *p != '"' && len < out_cap - 1)
        {
            if (*p == '\\' && *(p + 1) == '"')
            {
                out[len++] = '"';
                p += 2;
            }
            else if (*p == '\\' && *(p + 1) == '\\')
            {
                out[len++] = '\\';
                p += 2;
            }
            else
            {
                out[len++] = *p++;
            }
        }
    }
    out[len] = '\0';
}

/* ---------------- 讯飞签名 ---------------- */

static int asr_build_signed_path(char *path, int path_cap)
{
    char date_str[40];
    char sign_origin[256];
    char auth_origin[320];
    unsigned char hmac_out[32];
    char signature_b64[64];
    char auth_b64[256];     /* authorization 原文约 162B，base64 后约 217B */
    char auth_enc[768];     /* base64 字符 URL 编码后最多 3 倍 */
    char date_enc[128];
    time_t now = sntp_now();

    if (now <= 0)
    {
        LOG_E("time not synced");
        return -1;
    }

    sntp_format_rfc1123(now, date_str, sizeof(date_str));

    /* signature 原文：host/date/request-line */
    rt_snprintf(sign_origin, sizeof(sign_origin),
                "host: %s\ndate: %s\nGET %s HTTP/1.1",
                ASR_HOST, date_str, ASR_PATH);

    /* HMAC-SHA256(sign_origin, APISecret) → base64 */
    {
        const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

        mbedtls_md_hmac(md,
                        (const unsigned char *)APISecret, rt_strlen(APISecret),
                        (const unsigned char *)sign_origin, rt_strlen(sign_origin),
                        hmac_out);
    }
    if (b64_encode(signature_b64, sizeof(signature_b64), hmac_out, 32) < 0)
    {
        return -1;
    }

    /* authorization 原文 → base64 */
    rt_snprintf(auth_origin, sizeof(auth_origin),
                "api_key=\"%s\", algorithm=\"hmac-sha256\", "
                "headers=\"host date request-line\", signature=\"%s\"",
                APIKey, signature_b64);
    if (b64_encode(auth_b64, sizeof(auth_b64),
                   (const uint8_t *)auth_origin, (int)rt_strlen(auth_origin)) < 0)
    {
        return -1;
    }

    /* URL 编码后拼查询串 */
    url_encode(auth_b64, auth_enc, sizeof(auth_enc));
    url_encode(date_str, date_enc, sizeof(date_enc));
    rt_snprintf(path, path_cap, "%s?authorization=%s&date=%s&host=%s",
                ASR_PATH, auth_enc, date_enc, ASR_HOST);

    LOG_D("signed path len=%d", (int)rt_strlen(path));
    return 0;
}

/* ---------------- 帧发送 ---------------- */

/* 发送一帧 JSON（自动 base64 音频 + status） */
static int asr_send_frame(int fd, int status, const char *audio_b64, int audio_len)
{
    int n;
    uint8_t opcode = 0x1;   /* text 帧 */

    if (status == 0)
    {
        rt_snprintf(s_json_buf, sizeof(s_json_buf),
                    "{\"common\":{\"app_id\":\"%s\"},"
                    "\"business\":{\"language\":\"zh_cn\",\"domain\":\"iat\"},"
                    "\"data\":{\"status\":0,\"format\":\"audio/L16;rate=%d\","
                    "\"encoding\":\"raw\",\"audio\":\"%.*s\"}}",
                    APIID, ASR_SAMPLE_RATE, audio_len, audio_b64);
    }
    else if (status == 1)
    {
        rt_snprintf(s_json_buf, sizeof(s_json_buf),
                    "{\"data\":{\"status\":1,\"audio\":\"%.*s\"}}",
                    audio_len, audio_b64);
    }
    else
    {
        rt_snprintf(s_json_buf, sizeof(s_json_buf),
                    "{\"data\":{\"status\":2}}");
    }

    n = ws_send(fd, opcode, (uint8_t *)s_json_buf, (int)rt_strlen(s_json_buf));
    if (n < 0)
    {
        LOG_E("ws send fail, status=%d", status);
        return -1;
    }
    return 0;
}

/* ---------------- 完整识别流程（阻塞执行一次） ---------------- */

static int asr_run_once(int max_record_ms)
{
    char err[128];
    int fd = -1;
    int rc = -1;
    rt_tick_t t0;
    int audio_len;
    int b64_len;
    int bytes_total = 0;

    s_result[0] = '\0';
    s_peak_max = 0;   /* 本次识别峰值统计清零 */
    s_frame_cnt = 0;
    memset(s_peak_buckets, 0, sizeof(s_peak_buckets));

    /* 1. 网络检查 */
    if (!wifi_link_is_connected())
    {
        LOG_E("no network, voice disabled");
        return -1;
    }

    /* 2. 对时（签名必需） */
    if (!sntp_is_synced())
    {
        if (sntp_sync() != RT_EOK)
        {
            LOG_E("sntp sync failed, voice aborted");
            return -1;
        }
    }

    /* 3. 签名路径 */
    if (asr_build_signed_path(s_path, sizeof(s_path)) != 0)
    {
        return -1;
    }

    /* 4. WebSocket 握手 */
    fd = ws_connect(ASR_HOST, ASR_PORT, s_path, RT_NULL, err, sizeof(err));
    if (fd < 0)
    {
        LOG_E("ws connect fail: %s", err);
        return -1;
    }
    LOG_I("ws ok (%dms after PTT)",
          (int)((rt_tick_get() - s_ptt_press_tick) * 1000) / RT_TICK_PER_SECOND);

    /* 5. 打开麦克风 */
    if (mic_device_open() != RT_EOK)
    {
        LOG_E("mic open fail");
        goto exit;
    }

    /* 录音模式（用户确认：按住说话，说完松开）：用户按下后会停顿再开口，
     * recording 开始时尚未说话，开头天然完整，无需 pre-roll。
     * 丢 1 帧（40ms）等 PDM 数据稳定后读首帧（status=0 带格式声明）。 */
    mic_read_downsampled(s_audio_frame, sizeof(s_audio_frame));
    audio_len = mic_read_downsampled(s_audio_frame, sizeof(s_audio_frame));
    {
        int retry = 0;

        while (audio_len <= 0 && retry < 5)
        {
            rt_thread_mdelay(20);
            audio_len = mic_read_downsampled(s_audio_frame, sizeof(s_audio_frame));
            retry++;
        }
    }
    if (audio_len <= 0)
    {
        LOG_E("mic read fail at first frame");
        goto exit;
    }
    b64_len = b64_encode(s_b64_buf, sizeof(s_b64_buf), s_audio_frame, audio_len);
    if (b64_len < 0 || asr_send_frame(fd, 0, s_b64_buf, b64_len) != 0)
    {
        goto exit;
    }
    bytes_total += audio_len;
    LOG_I("recording started... (%dms after PTT)",
          (int)((rt_tick_get() - s_ptt_press_tick) * 1000) / RT_TICK_PER_SECOND);

    /* 7. 持续录音上传：按住期间持续录；松开后再续录 500ms 尾音
     * （防止"灯"等尾音被截断），超时兜底 */
    t0 = rt_tick_get();
    {
        int tail = ASR_TAIL_FRAMES;

        while (s_recording || tail > 0)
        {
            if ((rt_tick_get() - t0) > rt_tick_from_millisecond(max_record_ms))
            {
                LOG_I("record timeout (%dms)", max_record_ms);
                break;
            }
            audio_len = mic_read_downsampled(s_audio_frame, sizeof(s_audio_frame));
            if (audio_len <= 0)
            {
                rt_thread_mdelay(10);
                continue;
            }
            b64_len = b64_encode(s_b64_buf, sizeof(s_b64_buf), s_audio_frame, audio_len);
            if (b64_len < 0 || asr_send_frame(fd, 1, s_b64_buf, b64_len) != 0)
            {
                goto exit;
            }
            bytes_total += audio_len;
            if (!s_recording && tail > 0)
            {
                tail--;
            }
        }
    }

    /* 8. 音频电平诊断（每次识别自动输出，定位识别不准用） */
    LOG_I("mic profile (peak per 400ms): %d | %d | %d | %d | %d | %d | %d | %d",
          s_peak_buckets[0], s_peak_buckets[1], s_peak_buckets[2], s_peak_buckets[3],
          s_peak_buckets[4], s_peak_buckets[5], s_peak_buckets[6], s_peak_buckets[7]);
    if (s_peak_max > 15000)
    {
        LOG_W("mic level: peak=%d -> CLIPPING (gain too high)", (int)s_peak_max);
    }
    else if (s_peak_max > 11000)
    {
        LOG_W("mic level: peak=%d -> HIGH (near clipping)", (int)s_peak_max);
    }
    else if (s_peak_max < 300)
    {
        LOG_W("mic level: peak=%d -> TOO LOW (no voice picked up?)", (int)s_peak_max);
    }
    else
    {
        LOG_I("mic level: peak=%d -> ok", (int)s_peak_max);
    }

    /* 9. 尾帧 */
    if (asr_send_frame(fd, 2, RT_NULL, 0) != 0)
    {
        goto exit;
    }

    /* 10. 接收识别结果 */
    LOG_I("upload done (%d bytes), waiting result...", bytes_total);
    {
        rt_tick_t deadline = rt_tick_get() + rt_tick_from_millisecond(ASR_RECV_TIMEOUT_MS);

        while (rt_tick_get() < deadline)
        {
            int opcode = 0;
            int n = ws_recv_frame(fd, s_recv_buf, (int)sizeof(s_recv_buf), 0, &opcode);

            if (n < 0)
            {
                LOG_W("recv end (%d)", n);
                break;
            }
            if (opcode == 0x1 || opcode == 0x2 || opcode == 0x0)
            {
                s_recv_buf[n] = '\0';
                /* 打印服务器原始帧（调试期必开：能看到错误码，如 11200/10160） */
                LOG_W("srv: %s", s_recv_buf);
                asr_append_text((const char *)s_recv_buf, s_result, sizeof(s_result));
                if (strstr((const char *)s_recv_buf, "\"status\":2") != RT_NULL)
                {
                    break;   /* 识别结束帧 */
                }
            }
        }
    }

#if ASR_PRINT_RESULT
    if (s_result[0] != '\0')
    {
        LOG_I("ASR result: %s", s_result);
        asr_action_handle(s_result);   /* 阶段 3：识别文本 → 厨房设备控制 */
    }
    else
    {
        LOG_W("ASR result empty (check mic format / network)");
    }
#endif

    rc = 0;

exit:
    mic_device_close();
    ws_close(fd);
    return rc;
}

/* ---------------- PTT 回调（record.c 线程调用） ---------------- */

void asr_ptt_pressed(void)
{
    if (s_ptt_sem == RT_NULL)
    {
        return;   /* asr 线程未就绪 */
    }
    if (s_ptt_active)
    {
        return;   /* 正在处理，忽略重复按下 */
    }
    s_ptt_press_tick = rt_tick_get();   /* 记录按下时刻，用于统计录音起点延迟 */
    s_ptt_active = RT_TRUE;
    rt_sem_release(s_ptt_sem);
}

void asr_ptt_released(void)
{
    s_recording = RT_FALSE;   /* 仅影响正在进行的录音循环 */
}

/* ---------------- asr 线程 ---------------- */

static void asr_thread_entry(void *param)
{
    RT_UNUSED(param);

    while (1)
    {
        /* 等待 PTT 按下 或 msh asr_once 唤醒 */
        rt_sem_take(s_ptt_sem, RT_WAITING_FOREVER);
        s_recording = RT_TRUE;

        if (s_asr_once_secs > 0)
        {
            /* msh 调试模式：免按键，录固定时长 */
            LOG_I("asr_once %ds...", s_asr_once_secs);
            asr_run_once(s_asr_once_secs * 1000);
            s_asr_once_secs = 0;
        }
        else
        {
            /* PTT 模式：按住说话，释放结束 */
            LOG_I("PTT pressed, listening...");
            asr_run_once(ASR_MAX_RECORD_MS);
        }

        s_ptt_active = RT_FALSE;
        s_recording = RT_FALSE;
    }
}

/* 供 record.c 的 LED 线程查询录音状态 */
rt_bool_t asr_is_recording(void)
{
    return s_recording;
}

rt_bool_t asr_is_ready(void)
{
    return (s_ptt_sem != RT_NULL) ? RT_TRUE : RT_FALSE;
}

int asr_init(void)
{
    rt_thread_t tid;

    if (s_ptt_sem != RT_NULL)
    {
        return 0;
    }

    s_ptt_sem = rt_sem_create("asr_ptt", 0, RT_IPC_FLAG_PRIO);
    if (s_ptt_sem == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    tid = rt_thread_create("asr", asr_thread_entry, RT_NULL,
                           ASR_THREAD_STACK, ASR_THREAD_PRIORITY, 10);
    if (tid == RT_NULL)
    {
        return -RT_ENOMEM;
    }
    rt_thread_startup(tid);

    LOG_I("asr module ready");
    return RT_EOK;
}

INIT_APP_EXPORT(asr_init);

/* ---------------- msh 调试命令 ---------------- */
#ifdef RT_USING_MSH

/* asr_once [秒数]：免按键自动录音测试。
 * 注意：不在 msh(tshell) 线程里执行完整流程（其栈仅 4KB，ws_connect
 * 需约 3.7KB，会栈溢出），而是唤醒 asr 线程（8KB 栈）执行。 */
static void asr_once_cmd(int argc, char **argv)
{
    int secs = 3;

    if (argc >= 2)
    {
        secs = atoi(argv[1]);
    }
    if (secs < 1 || secs > 9)
    {
        rt_kprintf("usage: asr_once [1..9] seconds\n");
        return;
    }
    if (s_ptt_sem == RT_NULL)
    {
        rt_kprintf("asr module not ready\n");
        return;
    }
    if (s_ptt_active)
    {
        rt_kprintf("asr busy\n");
        return;
    }

    s_asr_once_secs = secs;
    s_ptt_active = RT_TRUE;
    rt_sem_release(s_ptt_sem);   /* 交给 asr 线程执行，msh 立即返回 */
    rt_kprintf("asr_once %ds dispatched to asr thread\n", secs);
}
MSH_CMD_EXPORT_ALIAS(asr_once_cmd, asr_once, record N seconds and recognize via iFlytek (asr_once [1..9]));

/* asr_dbg [0|1]：打开/关闭音频诊断打印（每帧读取节奏 + 峰值） */
static void asr_dbg_cmd(int argc, char **argv)
{
    if (argc >= 2)
    {
        s_asr_dbg = atoi(argv[1]) ? RT_TRUE : RT_FALSE;
    }
    rt_kprintf("asr_dbg = %d\n", s_asr_dbg ? 1 : 0);
}
MSH_CMD_EXPORT_ALIAS(asr_dbg_cmd, asr_dbg, toggle asr audio debug prints (asr_dbg 1|0));

#endif /* RT_USING_MSH */
