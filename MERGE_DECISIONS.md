# merge 实施计划: expert_amateur_set → expert_amateur_set2

基于逐项对比的决策结果，以下是具体实施步骤。

---

## 已决策摘要

| 组件 | 决策 |
|------|------|
| TrackPoint 底层 | ZitaoTech（独立WQ、I2C mutex、看门狗、K_NO_WAIT、延迟IRQ） ⚠️ 单次读取已知问题，计划改为有限批量+合并 |
| TrackPoint 滚轮算法 | ZitaoTech（t²曲线、阻尼、保留死区残留、进入模式初始化残留） |
| TrackPoint 模式切换 | **Dongle**（单键 toggle，不加方向键/慢速键/CapsLock） |
| TrackPoint Kconfig | EXPONENTIAL→y，保留 TOGGLE_KEY_POSITION、START_IN_SCROLL_MODE、CUSTOM_LED_BRT_* |
| Custom LED | **Dongle** v5 独立版，不动 |
| BB Trackball | ZitaoTech 架构（事件驱动、独立WQ、K_NO_WAIT、方向键、trackball_is_active），但仅 pos 61，不加 CapsLock |
| 键盘背光 | **Dongle** 轮询版 + RGB 边缘检测，去掉 WPM |
| 其他文件 | 全部保留 Dongle |

---

## 实施任务清单

### 1. TrackPoint 驱动融合 `trackpoint_0x15.c`

**文件**: `config/boards/shields/right_trackpoint_dongle/custom_driver_right/trackpoint_0x15.c`

以 Dongle 版为基底，注入 ZitaoTech 的稳定性改进：

- [ ] 添加独立 work queue（`tp_workq`，2048栈，优先级5）
- [ ] 添加 I2C 互斥锁（`trackpoint_i2c_mutex`），包裹 `i2c_read_dt`
- [ ] 添加看门狗（`last_activity_time`，200ms 超时重置残留值）
- [ ] 所有 `input_report_rel` 改用 `K_NO_WAIT`
- [ ] 延迟 IRQ 启用（`enable_irq_work`，200ms 后 `gpio_pin_interrupt_configure_dt`）
- [ ] ISR 改为 `k_work_submit_to_queue(&tp_workq, ...）`
- [ ] work handler 改为单次读取（去掉 while 循环）
- [ ] `process_scroll_axis`：非线性 t² 除数曲线
- [ ] `process_scroll_axis`：残留值阻尼 `*residue = (*residue * 3) / 4`
- [ ] `process_scroll_axis`：死区保留残留值（return 而非清零）
- [ ] 进入滚轮模式时用当前位移初始化残留值
- [ ] **保留** toggle 单键模式切换逻辑
- [ ] **不添加** 方向键模式、慢速键、CapsLock 监听
- [ ] 初始化优先级保持 `CONFIG_INPUT_INIT_PRIORITY`

### 2. TrackPoint Kconfig 更新

**文件**: `config/boards/shields/right_trackpoint_dongle/Kconfig.shield`

- [ ] `TRACKPOINT_EXPONENTIAL` 默认值 `n` → `y`

### 3. BB Trackball 驱动重写

**文件**: `config/boards/shields/left_bbtrackball_dongle/custom_driver_left/bbtrackball_input_handler.c`
**文件**: `config/boards/shields/left_bbtrackball_dongle/custom_driver_left/bbtrackball_input_handler.h`

以 ZitaoTech 版为基底，保留 Dongle 差异：

- [ ] 独立 work queue（`bbtrackball_work_q`，2048栈，优先级5）
- [ ] 事件驱动架构（去掉双定时器轮询）
- [ ] `struct bb_gpio_cb` wrapper + 单 `k_work` 替代 `k_work_delayable`
- [ ] ISR 中 `k_work_submit_to_queue` + `!k_work_is_pending` 防护
- [ ] 所有 HID 报告用 `K_NO_WAIT`
- [ ] 方向键模式（INPUT_BTN_0~3，阈值4，重复间隔35ms）
- [ ] `trackball_is_active()` 替换 `trackball_is_moving()`（同步更新 trackball_led.c 的调用）
- [ ] Space 键**仅**监听 pos 61
- [ ] **不添加** CapsLock 感知

### 4. 键盘背光简化

**文件**: `config/boards/arm/sofle_dongle/custom_driver/keyboard_backlight.c`

- [ ] 去掉 WPM 计算逻辑（`key_pressed_count`、`wpm_state`、`wpm_tick`、`wpm_work`）
- [ ] 自动熄灭时间改为固定 `AUTO_OFF_MIN_MS`
- [ ] 其余保留 Dongle 轮询版不变

### 5. bbtrackball_dongle.overlay 修复

<!-- 修复 -->

**文件**: `config/boards/shields/left_bbtrackball_dongle/left_bbtrackball_dongle.overlay`

- [ ] 移除对不存在的 `&bbtrackball_split` 节点引用
- [ ] 参考 zitaotech 添加 `&bbtrackball_listener` 配置（`status = "okay"` + `input-processors`）

### 6. lpm_view 动画移植

<!-- 移植 -->

**文件**: `config/boards/shields/lpm_view/` 下相关文件

- [ ] 移植 zitaotech 的 `widgets/peripheral_status.c`（随机图片切换，60s 间隔）
- [ ] 移植 zitaotech 的 `widgets/picture/` 目录（cat, astronaut, macintosch, david, vader, blackhole, plane, mounta）
- [ ] 更新 `CMakeLists.txt` 编译新图片文件
- [ ] 移除 dongle 原有的兔女郎动画帧编译

### 7. 不动的文件（确认清单）

以下文件不修改，仅作记录：

- [x] `config/boards/shields/right_trackpoint_dongle/custom_driver_right/custom_led.c` — 保留
- [x] `config/boards/shields/right_trackpoint_dongle/custom_driver_right/custom_led.h` — 保留
- [x] `config/boards/shields/right_trackpoint_dongle/right_trackpoint_dongle.overlay` — 保留
- [x] `config/boards/shields/left_bbtrackball_dongle/custom_driver_left/trackball_led.c` — 保留（同步改 API 调用名）
- [x] `config/boards/shields/lpm_view/custom_status_screen.c` — 保留
- [x] `config/boards/shields/st7789_display/` — 保留
- [x] `config/boards/arm/sofle_dongle/` 下所有 dts/dtsi/keymap/defconfig — 保留
- [x] `config/sofle_dongle.keymap` — 保留
- [x] `config/sofle_dongle.conf` — 保留
- [x] `build.yaml` / `west.yml` / `README.md` — 保留

---

## 已知问题：TrackPoint 精确度下降

### 根因分析（2026-06-14 更新：纠正队列误判）

**之前关于 HID 消息队列的分析是错误的。** 实测路径上没有 `k_msgq`，数据链路是直通管道：

```
TrackPoint 驱动 (右键键盘 nRF52840)
  │
  ├─ input_report_rel(dx, dy, K_NO_WAIT)
  │
  └─→ Zephyr input 子系统 (同步回调)
       │
       └─→ split_input_handler (input_split.c)
            │
            └─→ zmk_split_peripheral_report_event()
                 │
                 └─→ bt_gatt_notify()  ← 直接 BLE GATT 通知，无队列
                      │
                      └─→ BLE 控制器 TX buffer (nRF52840 约 7 个槽位)
                           │
                           └─→ BLE 连接事件 (间隔 7.5~30ms) → 空中发送
```

管道瓶颈在 **BLE 控制器 TX buffer**，不在软件队列。`K_NO_WAIT` 防止的是 `bt_gatt_notify` 在上一个通知未完成时返回 `-EAGAIN` 导致驱动阻塞。

**真正的精确度下降原因：单次读取 + 独立发送**

```
旧版 while 循环:
  IRQ → 读空所有包(5-8个) → 每个包独立调 bt_gatt_notify
  → 连接间隔内连调 5-8 次 → TX buffer 溢出(7槽), 后面失败
  → 但数据全读了,Dongle 端 HID report 累积,效果仍然精确 ✓

当前单次读取:
  IRQ → 读1个包 → bt_gatt_notify → 等下一个下降沿
  → TX buffer 不溢出了,但数据"断断续续"到达
  → 光标响应比手指慢半拍 ✗
```

| 版本 | 读取策略 | BLE 发送 | 效果 |
|------|---------|---------|------|
| 你的 773effb | while 清空缓冲区 | 连调 notify, TX buffer 顶着上限 | 精确, 但 TX buffer 满时 notify 失败率高 |
| 当前融合版 | 单次读取 | 每次只1个 notify | BT 友好, 但数据分批延迟送达 |
| **目标方案** | 批量读完 + 合并 | 1 次 notify | 精确 + BT 友好 |

### 方案：有限批量读取 + 累积合并 + 节拍发送

**TrackPoint 驱动改动 (`trackpoint_0x15.c`)：**

1. 每次 work callback 循环读取最多 5 个包（防止 I2C 长时间占用）
2. 累加 dx/dy 位移
3. 按固定间隔（≥BLE 连接间隔）做一次合并报告
4. 使用 `K_MSEC(5)` 替代 `K_NO_WAIT`（利用独立 WQ 的安全性）

**BB Trackball 不需要改动** — 它已经是事件驱动架构，ISR 中直接累加到 `dx_acc`/`dy_acc`，`!k_work_is_pending` 防止重复提交，work handler 一次发送累积值。天然合并。

**键盘扫描不需要改动** — 矩阵扫描用轮询，频率低(≤20Hz)，单次数据量固定，不涉及 I2C 批量读取问题。

### 改动范围

| 设备 | 是否改动 | 原因 |
|------|---------|------|
| TrackPoint (`trackpoint_0x15.c`) | **改** | 单次读 → 批量读+合并 |
| BB Trackball (`bbtrackball_input_handler.c`) | 不动 | 已天然合并 (ISR 累加 + work handler 一次发送) |
| 键盘矩阵扫描 | 不动 | 数据量小，轮询机制，不涉及此问题 |
