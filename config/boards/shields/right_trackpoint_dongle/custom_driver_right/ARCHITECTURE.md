# TrackPoint 驱动架构设计：累加 + 定时轮询

## 问题

原始实现（773effb）是逐包独立加速+逐包发送：

```
ISR → work → while(GPIO活跃) { 读包 → 加速 → input_report_rel() → BLE通知 }
```

每调一次 `input_report_rel()` 产生一次 BLE 通知。蓝牙连接间隔由主机控制（7.5-30ms），
包到达速率受 TrackPoint 物理运动决定（快速甩动时 1-3ms 一个包）。
BLE 槽位有限，通知排队发送，导致光标移动抖动——同样力度的甩动在不同 BLE 条件下表现不一致。

## 目标

改为累加+定时轮询，消除 BLE 抖动，同时保持与原始模型的数据完整性等价。

## 设计

```
                    GPIO ISR                        8ms Timer
                       │                                │
                       ▼                                ▼
                  read_work                        report_work
              (I2C批量读取)                      (读累加器→发送)
                       │                                │
                       ▼                                ▼
              ┌──────────────────────────────────────────┐
              │  sum_dx, sum_dy  (累加器，无锁访问)        │
              └──────────────────────────────────────────┘
```

### 数据流

```
read_work (GPIO触发，工作队列线程):
    while (GPIO 活跃 && packets < MAX) {
        读 I2C → sum_dx += dx; sum_dy += dy;
    }

report_work (Timer触发，工作队列线程):
    delta_x = sum_dx; sum_dx = 0;   // 读出并清零
    delta_y = sum_dy; sum_dy = 0;
    if (delta_x || delta_y) { 加速 → input_report_rel(); }
```

### 与原始模型的等价性

原始模型中每个包单独通过 `input_report_rel()` 发送到电脑端，Linux 内核 input 子系统
在两次 `input_sync()` 之间自动累加所有相对位移。USB HID 报告间隔通常 1-8ms。

```
原始: 包1(dx=2) → 内核累加:2
      包2(dx=3) → 内核累加:5
      包3(dx=1) → 内核累加:6 → input_sync → 用户态收到 6

轮询: 包1+2+3 → sum_dx=6 → input_report_rel(6) → 内核得 6 → 用户态收到 6
```

累加+清零 等价于内核的 `input_report_rel` 累加 + `input_sync` 提交。

### 加速计算

鼠标模式下的指数加速使用 delta_ms（本批数据首尾包时间差），反映物理运动速度：

```
speed = (|delta_x| + |delta_y|) / delta_ms
mult = min(exp(speed × 1.307357), 2.0)
fx = delta_x × BASE_SPEED × sens × mult
fy = delta_y × BASE_SPEED × sens × mult
```

快速甩动时包密集（delta_ms ~1-3ms），speed 高，加速强。
慢速移动时 delta_ms 大，speed 低，加速弱。
静止时 delta_ms 无意义，但 delta_x/delta_y 为 0，不发送。

### 无数据时不发包

如果 8ms 内没有包到达，`sum_dx = sum_dy = 0`，report_work 直接返回，不调用 `input_report_rel()`。
与原始模型一致——停止时原本也没有包可读。

## 无锁设计

`sum_dx/sum_dy` 没有 spinlock、互斥锁、原子操作或 volatile 修饰。
安全性不依赖单核假设，不依赖目标架构。

原因：

1. **ISR 只提交 work，不碰数据。** `motion_isr` 和 `timer_cb` 的唯一操作是
   `k_work_submit_to_queue()`，这是 Zephyr 保证 ISR 安全的 API。
   累加器的读写全部发生在工作队列线程内，ISR 永远不会访问累加器。

2. **两个 work 在同一 k_work_q 上串行执行。** `k_work_q` 是单线程设计，
   逐个从队列取 work 执行。`read_work` 和 `report_work` 被提交到同一个队列，
   绝不会并发运行。`sum_dx += dx` 不会和 `delta_x = sum_dx; sum_dx = 0` 交错。

同步正确性由架构保证，而非锁。锁只会增加开销和死锁风险。

## 涉及文件

- `config/boards/shields/right_trackpoint_dongle/custom_driver_right/trackpoint_0x15.c`
  — 全部改动在此文件
