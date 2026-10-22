# 智能厨房面板终端（Edgi-Talk PSoC E84）

基于 **RT-Thread 5.0.2 + LVGL 9.2** 的智能厨房面板终端，运行在 Edgi-Talk（PSoC Edge E84）开发板上。应用代码全部位于 `applications/kitchen`，平台层（rt-thread / libraries / board）不改动。

## 功能

- **五页 UI**：Home / Device / Env / Sys / Set，右侧垂直导航，亮/暗主题
- **环境感知**：板载 AHT20 温湿度每 2 秒采集，实时显示 + 128 条历史趋势曲线（60s/条，落盘 /flash/history.dat）
- **实时天气**：Open-Meteo 数据源，30 分钟自动刷新，断网保留旧数据降级显示
- **厨房定时器**：预设 3/5/10 分钟 + 自定义 1~99 分钟，息屏照常走时，到点提示音 + 强制唤醒
- **语音控制**：PTT 按住说话 → 讯飞流式 ASR → 本地解析 → 驱动 5 路继电器通道（厨房灯/油烟机/风扇/洗碗机/烤箱）
- **系统监控**：CPU 占用率（空闲钩子防抖）、内存、线程列表、网络状态、版本信息
- **息屏低功耗**：60s 无触摸息屏，触摸 500ms 轮询唤醒（带防误触保护），息屏暂停温湿度采样
- **设置持久化**：亮度/音量/息屏/主题写 /flash/settings.dat，掉电保持
- **WiFi 自动连接/重连**：断线 3s 退避自动重连，顶部自绘 WiFi 信号图标（联网绿色 / 断线灰色动画）
- **SNTP 对时**：联网后自动同步时间，顶部显示 HH:MM（24 小时制）
- **开机动画 + 关于弹层**

## 硬件 / 软件

| 项 | 说明 |
| --- | --- |
| 主控 | Edgi-Talk（PSoC Edge E84，Cortex-M55 + M33，VG-Lite GPU） |
| 屏幕 | 5" MIPI LCD，800×480 横屏，RGB565 |
| 传感器 | 板载 AHT20 温湿度；PDM 麦克风（经底板 CN6 外接 INMP441） |
| 输出 | 5 路继电器 GPIO（P5_0~P5_4，高电平吸合）；I2S 提示音 |
| RTOS | RT-Thread 5.0.2 |
| GUI | LVGL 9.2 |

## 目录结构

```
applications/
  kitchen/          # 本作品全部应用代码（UI/业务/驱动）
    ui_page.c       # 页面框架（右侧导航 + 5 页容器）
    kitchen_ui.c    # LVGL 主循环与页面创建
    kitchen_panel.c # 业务线程（采样/SNTP/历史落盘）
    weather.c       # Open-Meteo 天气
    kitchen_timer.c # 厨房定时器状态机
    screen_saver.c  # 息屏/唤醒
    settings_store.c# 设置持久化
    voice_ctrl/     # WiFi / SNTP / 录音 / 讯飞 ASR / 指令解析
    xiaozhi/        # 原小智语音代码（本作品不使用，保留）
board/              # 板级配置
packages/           # 软件包（LVGL 等）
```

## 构建

1. 使用 RT-Thread Studio 打开本工程；
2. 确认 `applications/main.c` 中已定义 `BSP_USING_KITCHEN_PANEL`（默认开启，注释掉即回原小智模式）；
3. 构建：`scons -j16`（RT-Thread Studio 内执行 `scons.bat -j16`）；
4. 烧录 Debug/rtthread.hex 后复位，串口观察日志。

## 使用前必改（重要）

1. **WiFi（自己改自己的）**：编辑 `applications/kitchen/voice_ctrl/wifi_config.h`，把 `WIFI_SSID` / `WIFI_PASSWORD` 改成你自己的路由器名称和密码，重新编译烧录即可开机自动连接。
2. **语音密钥**：语音识别使用讯飞流式听写。请自行创建 `applications/API.h`，内容为三个宏 `APIID` / `APISecret` / `APIKey`（参考 `applications/kitchen/voice_ctrl/asr_config.h` 头注释）。密钥文件未随仓库提交，注意不要公开。
3. **天气城市（可选）**：修改 `applications/kitchen/weather.c` 中的 `WEATHER_LAT` / `WEATHER_LON` 为你所在城市坐标。

## 演示要点

- 语音："打开厨房灯" / "关闭厨房灯"（卡片同步亮灭 + 提示音）
- 天气：联网后主页天气卡自动显示温度与描述
- 定时器：预设 3min 演示到点提示音 + 强制唤醒
- 息屏：60s 无触摸黑屏，触摸唤醒不误触
- 系统页：CPU / RAM / 线程列表实时监控

## 说明

继电器通道当前为演示性输出接口（UI/语音驱动状态），面向真实设备的电气适配与控制时序不在本仓库范围内。

## 参考文章

- [基于 RT-Thread 与 Edgi-Talk PSoC E84 的智能厨房面板终端（参赛作品技术文章）](https://club.rt-thread.org/ask/article/756e87b5b780f0da.html)
