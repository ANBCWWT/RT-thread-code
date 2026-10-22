/*
 * Copyright (c) 2026, Kitchen Panel Application
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AHT20 温湿度传感器驱动实现
 *
 * 协议要点（AHT20 数据手册）：
 * - I2C 从机地址：0x38（7bit）
 * - 初始化：写 0xBE 0x08 0x00，等待 >10ms，确认校准位 bit7=1
 * - 触发测量：写 0xAC 0x33 0x00，等待约 80ms（状态寄存器 bit3 busy 清零）
 * - 读取 6 字节：status + 湿度 20bit + 温度 20bit
 * - 换算：H = raw_h / 2^20 * 100 %
 *         T = raw_t / 2^20 * 200 - 50 ℃
 */
#include "aht20.h"

#define LOG_TAG              "kitchen.aht20"
#define LOG_LVL              LOG_LVL_I
#include <rtdevice.h>
#include <finsh.h>
#include <board.h>
#include <drv_log.h>
#include <drivers/i2c-bit-ops.h>

/* I2C 总线与从机地址
 * 板载 AHT20 官方接法（Edgi_Talk_M55_Driver_All/rtconfig.h 实测）：
 *   BSP_USING_SOFT_I2C1 / BSP_SOFT_I2C1_SCL_PIN=75 / BSP_SOFT_I2C1_SDA_PIN=74
 * 本 BSP 引脚编号 = 端口号*8 + 引脚号：75 = P9_3(SCL)、74 = P9_2(SDA)
 * （该两脚在 cycfg 中同时定义为 UART1 TX/RX，但本工程未启用 UART1，
 *   运行期 rt_pin_mode 会切回 GPIO 开漏模式，二者不冲突）
 * 本工程 "i2c1" 已被触摸屏占用（105/106=P13_1/P13_2），故在应用层注册
 * 独立的软总线 "i2c2"（SCL=75、SDA=74）专门驱动 AHT20，不改动任何基础文件。
 */
#define AHT20_I2C_BUS          "i2c2"
#define AHT20_ADDR             0x38

/* ===== 应用层软 I2C 总线（i2c2，引脚 74/75）===== */
#define KITCHEN_I2C_BUS_NAME   "i2c2"
#define KITCHEN_I2C_SCL_PIN    75   /* P9_3，官方 BSP_SOFT_I2C1_SCL_PIN=75 */
#define KITCHEN_I2C_SDA_PIN    74   /* P9_2，官方 BSP_SOFT_I2C1_SDA_PIN=74 */
#define KITCHEN_I2C_DELAY_US   5    /* 约 100kHz，AHT20 稳定工作 */

static struct rt_i2c_bus_device s_kitchen_i2c_bus;
static struct rt_i2c_bit_ops s_kitchen_i2c_ops;

static void kitchen_i2c_set_scl(void *data, rt_int32_t state)
{
    rt_pin_write(KITCHEN_I2C_SCL_PIN, state ? PIN_HIGH : PIN_LOW);
}

static void kitchen_i2c_set_sda(void *data, rt_int32_t state)
{
    rt_pin_write(KITCHEN_I2C_SDA_PIN, state ? PIN_HIGH : PIN_LOW);
}

static rt_int32_t kitchen_i2c_get_scl(void *data)
{
    return (rt_pin_read(KITCHEN_I2C_SCL_PIN) == PIN_HIGH) ? 1 : 0;
}

static rt_int32_t kitchen_i2c_get_sda(void *data)
{
    return (rt_pin_read(KITCHEN_I2C_SDA_PIN) == PIN_HIGH) ? 1 : 0;
}

/* 注册应用层软总线，成功返回 RT_EOK */
static int kitchen_i2c_bus_init(void)
{
    rt_pin_mode(KITCHEN_I2C_SCL_PIN, PIN_MODE_OUTPUT_OD);
    rt_pin_mode(KITCHEN_I2C_SDA_PIN, PIN_MODE_OUTPUT_OD);
    rt_pin_write(KITCHEN_I2C_SCL_PIN, PIN_HIGH);
    rt_pin_write(KITCHEN_I2C_SDA_PIN, PIN_HIGH);

    s_kitchen_i2c_ops.data    = RT_NULL;
    s_kitchen_i2c_ops.set_scl = kitchen_i2c_set_scl;
    s_kitchen_i2c_ops.set_sda = kitchen_i2c_set_sda;
    s_kitchen_i2c_ops.get_scl = kitchen_i2c_get_scl;
    s_kitchen_i2c_ops.get_sda = kitchen_i2c_get_sda;
    s_kitchen_i2c_ops.udelay  = rt_hw_us_delay;
    s_kitchen_i2c_ops.delay_us = KITCHEN_I2C_DELAY_US;
    s_kitchen_i2c_ops.timeout = 100;

    /* 必须先把 ops 挂到 bus->priv（i2c-bit-ops.c 从 priv 解引用 ops，
     * 不设置会导致空指针调用 hard fault，pc=0x00000000） */
    s_kitchen_i2c_bus.priv = (void *)&s_kitchen_i2c_ops;

    return rt_i2c_bit_add_bus(&s_kitchen_i2c_bus, KITCHEN_I2C_BUS_NAME);
}

/* 命令字 */
#define AHT20_CMD_INIT         0xBE
#define AHT20_CMD_TRIGGER      0xAC
#define AHT20_INIT_PARAM0      0x08
#define AHT20_INIT_PARAM1      0x00
#define AHT20_TRIG_PARAM0      0x33
#define AHT20_TRIG_PARAM1      0x00

/* 状态寄存器位 */
#define AHT20_STATUS_BUSY      0x08   /* bit3：忙标志 */
#define AHT20_STATUS_CALIB     0x80   /* bit7：校准使能标志 */

/* 测量等待参数 */
#define AHT20_POLL_INTERVAL_MS 10
#define AHT20_MAX_POLL_MS      100

/* 读取失败时的重试次数（AHT20 偶发 NACK 时可自恢复） */
#define AHT20_READ_RETRY       2

static struct rt_i2c_bus_device *s_aht20_bus = RT_NULL;

int aht20_init(void)
{
    rt_uint8_t cmd[3];
    rt_uint8_t status;

    /* 先确保应用层软总线 i2c2 已注册（重复调用无副作用） */
    kitchen_i2c_bus_init();

    s_aht20_bus = (struct rt_i2c_bus_device *)rt_device_find(AHT20_I2C_BUS);
    if (s_aht20_bus == RT_NULL)
    {
        LOG_E("i2c bus '%s' not found (try i2c_scan)", AHT20_I2C_BUS);
        return -RT_ENOSYS;
    }

    /* AHT20 上电后 40ms 内不得通信（数据手册），等待稳定再初始化 */
    rt_thread_mdelay(60);

    cmd[0] = AHT20_CMD_INIT;
    cmd[1] = AHT20_INIT_PARAM0;
    cmd[2] = AHT20_INIT_PARAM1;
    if (rt_i2c_master_send(s_aht20_bus, AHT20_ADDR, 0, cmd, 3) != 3)
    {
        LOG_E("init cmd send failed (addr 0x%02X)", AHT20_ADDR);
        return -RT_ERROR;
    }

    /* 初始化命令已发出：AHT20 手册要求等待 >10ms，固定等 80ms 保证稳定 */
    rt_thread_mdelay(80);

    /* 单次读取状态寄存器，核对校准位（bit7）。未校准会警告但不阻塞，
     * 避免软 I2C 时序下状态位误读导致初始化假失败。 */
    status = 0;
    if (rt_i2c_master_recv(s_aht20_bus, AHT20_ADDR, 0, &status, 1) != 1)
    {
        LOG_W("init status read failed, continue anyway");
    }
    else if (status & AHT20_STATUS_CALIB)
    {
        LOG_I("init done on %s (calibrated)", AHT20_I2C_BUS);
    }
    else
    {
        LOG_W("init done on %s but not calibrated (status 0x%02X)", AHT20_I2C_BUS, status);
    }
    return 0;
}

static int aht20_read_once(float *temperature_c, float *humidity_pct)
{
    rt_uint8_t cmd[3];
    rt_uint8_t data[6];
    rt_uint32_t raw_hum;
    rt_uint32_t raw_temp;
    /* 1. 发送触发测量命令 */
    cmd[0] = AHT20_CMD_TRIGGER;
    cmd[1] = AHT20_TRIG_PARAM0;
    cmd[2] = AHT20_TRIG_PARAM1;
    if (rt_i2c_master_send(s_aht20_bus, AHT20_ADDR, 0, cmd, 3) != 3)
    {
        return -RT_ERROR;
    }

    /* 2. 等待测量完成：AHT20 手册触发后约 80ms 完成，固定等待 100ms。
     *    不轮询 busy 位——软 I2C 时序下读到的状态可能不准确，
     *    而 AHT20 测量期间也允许读（首字节即状态寄存器），直接读最可靠。 */
    rt_thread_mdelay(AHT20_MAX_POLL_MS);

    /* 3. 读取 6 字节测量结果（首字节为状态寄存器，busy 位已在等待中清除） */
    if (rt_i2c_master_recv(s_aht20_bus, AHT20_ADDR, 0, data, 6) != 6)
    {
        return -RT_ERROR;
    }

    /* 4. 拆包：湿度 20bit、温度 20bit */
    raw_hum  = ((rt_uint32_t)data[1] << 12)
             | ((rt_uint32_t)data[2] << 4)
             | ((rt_uint32_t)data[3] >> 4);
    raw_temp = ((rt_uint32_t)(data[3] & 0x0F) << 16)
             | ((rt_uint32_t)data[4] << 8)
             | ((rt_uint32_t)data[5]);

    /* 5. 换算（2^20 = 1048576） */
    if (humidity_pct != RT_NULL)
    {
        *humidity_pct = (float)raw_hum * 100.0f / 1048576.0f;
    }
    if (temperature_c != RT_NULL)
    {
        *temperature_c = (float)raw_temp * 200.0f / 1048576.0f - 50.0f;
    }

    return 0;
}

int aht20_read(float *temperature_c, float *humidity_pct)
{
    int ret;
    int attempt;

    for (attempt = 0; attempt <= AHT20_READ_RETRY; attempt++)
    {
        if (attempt > 0)
        {
            rt_thread_mdelay(200);
        }

        ret = aht20_read_once(temperature_c, humidity_pct);
        if (ret == 0)
        {
            return 0;
        }
    }

    LOG_W("read failed after %d attempts (last err %d)", AHT20_READ_RETRY + 1, ret);
    return ret;
}

#ifdef RT_USING_MSH
/* msh 调试命令：aht20 —— 立即读取一次温湿度 */
static void aht20_cmd(int argc, char **argv)
{
    float temp = 0.0f;
    float hum  = 0.0f;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (aht20_init() != 0)
    {
        rt_kprintf("AHT20 init failed (check i2c1)\n");
        return;
    }

    if (aht20_read(&temp, &hum) == 0)
    {
        rt_kprintf("temp = %.1f C, hum = %.1f %%\n", temp, hum);
    }
    else
    {
        rt_kprintf("AHT20 read failed\n");
    }
}
MSH_CMD_EXPORT_ALIAS(aht20_cmd, aht20, read AHT20 temperature and humidity);

/* msh 调试命令：i2c_scan —— 扫描 i2c0 / i2c1 上所有有响应的从机地址
 * 用于定位板载 AHT20 实际挂在哪条总线（AHT20 地址 0x38）。
 * 用法：i2c_scan
 */
static void i2c_scan_cmd(int argc, char **argv)
{
    rt_uint8_t data;
    rt_uint16_t addr;
    struct rt_i2c_bus_device *bus;
    struct rt_i2c_msg msgs;
    const char *bus_names[] = { "i2c0", "i2c1", "i2c2" };
    int b;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    kitchen_i2c_bus_init();   /* 确保 i2c2 已注册 */

    for (b = 0; b < (int)(sizeof(bus_names) / sizeof(bus_names[0])); b++)
    {
        bus = (struct rt_i2c_bus_device *)rt_device_find(bus_names[b]);
        if (bus == RT_NULL)
        {
            rt_kprintf("%s: bus not found\n", bus_names[b]);
            continue;
        }

        rt_kprintf("%s devices:", bus_names[b]);
        for (addr = 0x08; addr < 0x78; addr++)
        {
            msgs.addr  = addr;
            msgs.flags = RT_I2C_RD;
            msgs.buf   = &data;
            msgs.len   = 1;
            if (rt_i2c_transfer(bus, &msgs, 1) == 1)
            {
                rt_kprintf(" 0x%02X", addr);
            }
        }
        rt_kprintf("\n");
    }
}
MSH_CMD_EXPORT_ALIAS(i2c_scan_cmd, i2c_scan, scan i2c0/i2c1 for AHT20 (0x38));

#endif /* RT_USING_MSH */
