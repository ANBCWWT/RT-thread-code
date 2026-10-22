/*
 * 板载 PTT 按键 + 麦克风实现（语音控制 · 阶段 2）
 *
 * 麦克风源说明（硬件结论，原理图确认）：
 *  板上唯一的模拟麦克风 U10 -> ES8388 ADC，但 ES8388 的 SDOUT（ADC 输出）
 *  在原理图上悬空、未连接 MCU，模拟 mic 音频无法到达 MCU。
 *  PDM(mic0, P8_5/P8_6) 信号引到底板 CN6 排针（PDM_DATA/PDM_CLK），
 *  板载未焊 PDM 麦克风。录音需在 CN6 外接 PDM 麦克风（如 INMP441），
 *  mic0 即按官方设计路线工作。驱动默认增益 40（20dB）过高，这里显式
 *  设 0dB 起步，若外接 mic 后音量偏小可回调。
 */
#include "record.h"
#include "asr.h"
#include "asr_config.h"

#include <rtthread.h>
#include <rtdevice.h>   /* rt_pin_mode / rt_pin_read / GET_PIN */

#define DBG_TAG "voice.rec"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define MIC_DEVICE_NAME     "mic0"
#define PTT_DEBOUNCE_MS     20
#define PTT_THREAD_STACK    1024
#define PTT_THREAD_PRIORITY 18

static rt_device_t s_mic = RT_NULL;

/* ---------------- mic0 (PDM, 外接于 CN6) ---------------- */

int mic_device_open(void)
{
    struct rt_audio_caps caps;

    if (s_mic == RT_NULL)
    {
        s_mic = rt_device_find(MIC_DEVICE_NAME);
        if (s_mic == RT_NULL)
        {
            LOG_E("device %s not found", MIC_DEVICE_NAME);
            return -RT_EIO;
        }
    }

    /* PDM 增益历程（0.5dB/步）：
     *  - 默认 40（20dB）：正常说话即削波（顶格），识别失真；
     *  - 0dB：说话峰值仅 500~2300（-24~-16dBFS），讯飞识别不稳；
     *  - 20（10dB，当前）：0dB×3.4 倍 → 说话峰值约 1700~7800，
     *    既远离削波又够讯飞稳定识别。若仍偏小可上调到 30（15dB）。 */
    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type  = AUDIO_MIXER_VOLUME;
    caps.udata.value = 20;   /* 10dB */
    rt_device_control(s_mic, AUDIO_CTL_CONFIGURE, &caps);

    return rt_device_open(s_mic, RT_DEVICE_OFLAG_RDONLY);
}

void mic_device_close(void)
{
    if (s_mic != RT_NULL)
    {
        rt_device_close(s_mic);
    }
}

int mic_read(uint8_t *buf, int len)
{
    if (s_mic == RT_NULL)
    {
        return -RT_EIO;
    }
    return (int)rt_device_read(s_mic, 0, buf, len);
}

/* ---------------- PTT 按键 ---------------- */

static void ptt_thread_entry(void *param)
{
    rt_bool_t pressed = RT_FALSE;

    RT_UNUSED(param);

    /* 等 asr 模块初始化完成 */
    rt_thread_mdelay(300);

    while (1)
    {
        rt_bool_t level = rt_pin_read(VOICE_PTT_PIN);   /* 默认按下=低电平 */

        if (!pressed && level == PIN_LOW)
        {
            rt_thread_mdelay(PTT_DEBOUNCE_MS);          /* 消抖 */
            if (rt_pin_read(VOICE_PTT_PIN) == PIN_LOW)
            {
                pressed = RT_TRUE;
                asr_ptt_pressed();
            }
        }
        else if (pressed && level == PIN_HIGH)
        {
            rt_thread_mdelay(PTT_DEBOUNCE_MS);
            if (rt_pin_read(VOICE_PTT_PIN) == PIN_HIGH)
            {
                pressed = RT_FALSE;
                asr_ptt_released();
            }
        }
        rt_thread_mdelay(PTT_DEBOUNCE_MS);
    }
}

/* ---------------- 板载 LED 指示（录音闪烁 / 平时常亮） ---------------- */

static void led_thread_entry(void *param)
{
    rt_bool_t on = RT_TRUE;

    RT_UNUSED(param);

    /* 等 asr 模块初始化完成 */
    rt_thread_mdelay(300);

    while (1)
    {
        if (asr_is_recording())
        {
            /* 录音中：200ms 周期闪烁 */
            rt_pin_write(VOICE_LED_PIN, on ? PIN_HIGH : PIN_LOW);
            on = !on;
        }
        else
        {
            /* 平时：常亮（极性相反时对调 PIN_HIGH/PIN_LOW） */
            rt_pin_write(VOICE_LED_PIN, PIN_HIGH);
            on = RT_TRUE;
        }
        rt_thread_mdelay(200);
    }
}

int record_ptt_init(void)
{
    rt_thread_t tid;

    /* 板载按键：输入上拉（按下到地） */
    rt_pin_mode(VOICE_PTT_PIN, PIN_MODE_INPUT_PULLUP);

    /* 板载蓝色 LED：输出，默认点亮 */
    rt_pin_mode(VOICE_LED_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(VOICE_LED_PIN, PIN_HIGH);

    tid = rt_thread_create("voice_ptt", ptt_thread_entry, RT_NULL,
                           PTT_THREAD_STACK, PTT_THREAD_PRIORITY, 10);
    if (tid == RT_NULL)
    {
        return -RT_ENOMEM;
    }
    rt_thread_startup(tid);

    tid = rt_thread_create("voice_led", led_thread_entry, RT_NULL,
                           1024, PTT_THREAD_PRIORITY, 10);
    if (tid == RT_NULL)
    {
        return -RT_ENOMEM;
    }
    rt_thread_startup(tid);

    /* 等 asr 模块也完成初始化（asr_init 与 record_ptt_init 同为 INIT_APP_EXPORT，
     * 链接顺序通常 asr 在前，这里显式确认，确保"all ok"在所有线程建好之后打印） */
    while (!asr_is_ready())
    {
        rt_thread_mdelay(10);
    }

    LOG_I("all ok");
    return RT_EOK;
}

INIT_APP_EXPORT(record_ptt_init);
