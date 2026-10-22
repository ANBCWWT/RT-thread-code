/*
 * WiFi 连接模块（语音控制 · 第一阶段）
 *
 * 设计要点：
 *  - 自动初始化（INIT_APP_EXPORT），不改 main.c / kitchen_panel.c；
 *  - 连接由独立线程负责（线程内允许延时），wlan 事件回调只做
 *    置标志 + 释放信号量（回调运行在 wlan 线程上下文，禁止阻塞）；
 *  - 断线事件唤醒连接线程 → 3s 退避重连，直到 READY 为止；
 *  - 默认网络来自 wifi_config.h 宏；msh `wifi_join` 可临时指定
 *    其它网络（不落盘，重启回落宏）。
 *
 * 依赖：RT_USING_WLAN（驱动已在固件内）、RT_USING_LWIP（lwIP 1.4+/2.x）
 * 头文件：wlan_mgnt.h（RT-Thread 标准 wlan API）
 */
#include "wifi_link.h"
#include "wifi_config.h"

#include <rtthread.h>
#include <rtdevice.h>
#include <wlan_mgnt.h>
#include <finsh.h>

#if defined(RT_USING_LWIP)
#include <lwip/netif.h>
#include <lwip/ip4_addr.h>
#endif

#define DBG_TAG "voice.wifi"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* 连接失败/断线后的重试间隔（ms） */
#define WIFI_RETRY_INTERVAL_MS   3000
/* 启动后等待 wlan 驱动就绪的时长（ms）：WLAN init success 出现在 main 之后 */
#define WIFI_READY_WAIT_MS       3000
/* 默认网络宏为空时（用户尚未填写）的检查间隔（ms） */
#define WIFI_EMPTY_CONFIG_MS     10000
/* 连接线程栈与优先级（RT-Thread 数值越小优先级越高） */
#define WIFI_THREAD_STACK        2048
#define WIFI_THREAD_PRIORITY     15

static rt_sem_t s_wifi_wake_sem = RT_NULL;
static volatile rt_bool_t s_wifi_connected = RT_FALSE;
static volatile rt_bool_t s_wifi_enabled   = RT_FALSE;

/* 手动连接参数（msh wifi_join 设置；为空则使用 wifi_config.h 宏） */
static char s_manual_ssid[33] = {0};
static char s_manual_pwd[65]  = {0};

static void wifi_get_target(char *ssid, int ssid_len, char *pwd, int pwd_len)
{
    if (s_manual_ssid[0] != '\0')
    {
        rt_strncpy(ssid, s_manual_ssid, ssid_len - 1);
        rt_strncpy(pwd, s_manual_pwd, pwd_len - 1);
    }
    else
    {
        rt_strncpy(ssid, WIFI_SSID, ssid_len - 1);
        rt_strncpy(pwd, WIFI_PASSWORD, pwd_len - 1);
    }
}

/* ---------------- wlan 事件回调（禁止阻塞/延时） ---------------- */
static void wifi_event_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    RT_UNUSED(buff);
    RT_UNUSED(parameter);

    switch (event)
    {
    case RT_WLAN_EVT_READY:
        /* 连接 + DHCP 完成，可以收发数据 */
        s_wifi_connected = RT_TRUE;
#if defined(RT_USING_LWIP)
        if (netif_default != RT_NULL)
        {
            LOG_I("connected, ip=%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
        }
        else
        {
            LOG_I("connected");
        }
#else
        LOG_I("connected");
#endif
        break;

    case RT_WLAN_EVT_STA_DISCONNECTED:
        if (s_wifi_connected)
        {
            LOG_W("disconnected, will retry");
        }
        s_wifi_connected = RT_FALSE;
        if (s_wifi_enabled && s_wifi_wake_sem != RT_NULL)
        {
            /* 唤醒连接线程发起重连 */
            rt_sem_release(s_wifi_wake_sem);
        }
        break;

    default:
        break;
    }
}

/* ---------------- 连接线程：发起连接与断线重连 ---------------- */
static void wifi_link_thread_entry(void *param)
{
    char ssid[33];
    char pwd[65];

    RT_UNUSED(param);

    /* 等 wlan 驱动初始化完成，避免过早连接失败 */
    rt_thread_mdelay(WIFI_READY_WAIT_MS);

    while (1)
    {
        if (s_wifi_enabled && !s_wifi_connected)
        {
            wifi_get_target(ssid, sizeof(ssid), pwd, sizeof(pwd));
            if (rt_strlen(ssid) == 0)
            {
                /* 宏还没填：周期性检查，填好后无需改代码即生效 */
                LOG_I("wifi config empty, waiting (fill WIFI_SSID in voice_ctrl/wifi_config.h)");
                rt_thread_mdelay(WIFI_EMPTY_CONFIG_MS);
                continue;
            }

            LOG_I("connecting to \"%s\"...", ssid);
            if (rt_wlan_connect(ssid, pwd) != RT_EOK)
            {
                LOG_W("connect failed, retry in %dms", WIFI_RETRY_INTERVAL_MS);
            }
            /* 无论成败都退避等待：READY 事件会置 s_wifi_connected，
             * 随后循环转入信号量等待；断线事件会再次唤醒本线程。 */
            rt_thread_mdelay(WIFI_RETRY_INTERVAL_MS);
        }
        else
        {
            /* 已连接：挂起等待断线事件 */
            rt_sem_take(s_wifi_wake_sem, RT_WAITING_FOREVER);
        }
    }
}

/* ---------------- msh 调试命令 ---------------- */
#ifdef RT_USING_MSH

/* wifi_join <ssid> <password>：临时连接指定网络（不落盘，重启回落宏） */
static void wifi_join_cmd(int argc, char **argv)
{
    if (argc < 3)
    {
        rt_kprintf("usage: wifi_join <ssid> <password>\n");
        return;
    }

    rt_strncpy(s_manual_ssid, argv[1], sizeof(s_manual_ssid) - 1);
    rt_strncpy(s_manual_pwd,  argv[2], sizeof(s_manual_pwd) - 1);

    s_wifi_connected = RT_FALSE;
    rt_wlan_disconnect();
    if (s_wifi_wake_sem != RT_NULL)
    {
        rt_sem_release(s_wifi_wake_sem);
    }
    rt_kprintf("will connect to \"%s\"\n", s_manual_ssid);
}
MSH_CMD_EXPORT_ALIAS(wifi_join_cmd, wifi_join, connect to a wifi network (manual, not saved));

/* wifi_info：打印连接状态与 IP */
static void wifi_info_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("enabled=%s connected=%s\n",
               s_wifi_enabled ? "yes" : "no",
               s_wifi_connected ? "yes" : "no");
#if defined(RT_USING_LWIP)
    if (netif_default != RT_NULL)
    {
        rt_kprintf("ip=%s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    }
#endif
}
MSH_CMD_EXPORT_ALIAS(wifi_info_cmd, wifi_info, show wifi link status);

#endif /* RT_USING_MSH */

/* ---------------- 对外接口 ---------------- */
rt_bool_t wifi_link_is_connected(void)
{
    return s_wifi_connected;
}

int wifi_link_init(void)
{
    rt_thread_t tid;

    if (s_wifi_enabled)
    {
        return 0;   /* 已初始化 */
    }

    s_wifi_wake_sem = rt_sem_create("wifi_wake", 0, RT_IPC_FLAG_PRIO);
    if (s_wifi_wake_sem == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    /* 注册事件：READY = 连接 + DHCP 完成；STA_DISCONNECTED = 断线（触发重连） */
    rt_wlan_register_event_handler(RT_WLAN_EVT_READY, wifi_event_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED, wifi_event_handler, RT_NULL);

    s_wifi_enabled = RT_TRUE;

    tid = rt_thread_create("wifi_link", wifi_link_thread_entry, RT_NULL,
                           WIFI_THREAD_STACK, WIFI_THREAD_PRIORITY, 10);
    if (tid == RT_NULL)
    {
        return -RT_ENOMEM;
    }
    rt_thread_startup(tid);

    LOG_I("wifi link module ready");
    return 0;
}

/* 组件初始化阶段自动启动（早于 main；wlan 就绪由线程内等待兜底） */
INIT_APP_EXPORT(wifi_link_init);
