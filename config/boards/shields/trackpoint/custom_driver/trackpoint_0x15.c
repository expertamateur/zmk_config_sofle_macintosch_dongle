/*
 * TrackPoint HID over I2C Driver — Accumulate + timer-driven report
 * Stability infra from ZitaoTech + toggle-key mode preserved.
 * SPDX-License-Identifier: MIT
 *
 * 指针语义（三个指点设备统一，见 MODULAR_POINTER_ANALYSIS.md）：
 *
 *   | 状态                           | 输出                   |
 *   |--------------------------------|------------------------|
 *   | 默认                           | 滚轮 WHEEL / HWHEEL    |
 *   | 按住 MOUSE_KEY_POSITION_1 或 2 | 鼠标移动 REL_X / REL_Y |
 *
 * 本文件不产生任何按键事件（无 input_report_key），不做方向键。
 */

#define DT_DRV_COMPAT zmk_trackpoint

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdlib.h>
#include <math.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include "custom_led.h"

LOG_MODULE_REGISTER(trackpoint, LOG_LEVEL_DBG);

/* ===== Dedicated Work Queue ===== */
#define TP_WORKQ_STACK_SIZE 2048
#define TP_WORKQ_PRIORITY 5

K_THREAD_STACK_DEFINE(tp_workq_stack, TP_WORKQ_STACK_SIZE);
static struct k_work_q tp_workq;

/* ===== I2C Mutex ===== */
static struct k_mutex trackpoint_i2c_mutex;

/* ===== Watchdog ===== */
static uint32_t last_activity_time;
#define TRACKPOINT_WDT_TIMEOUT 200

/* ========================================================================= */
/* 鼠标与滚轮可调参数 (已映射至 Kconfig，用户可在 .conf 中配置)                 */
/* ========================================================================= */

// --- 滚轮方向配置 ---
#define SCROLL_X_DIR (-CONFIG_TRACKPOINT_SCROLL_X_DIR)
#define SCROLL_Y_DIR CONFIG_TRACKPOINT_SCROLL_Y_DIR

// --- 滚轮灵敏度与粒度配置 ---
#define SCROLL_DEADZONE CONFIG_TRACKPOINT_SCROLL_DEADZONE
#define SCROLL_INPUT_MAX CONFIG_TRACKPOINT_SCROLL_INPUT_MAX
#define SCROLL_DIVISOR_SLOW CONFIG_TRACKPOINT_SCROLL_DIVISOR_SLOW
#define SCROLL_DIVISOR_FAST CONFIG_TRACKPOINT_SCROLL_DIVISOR_FAST

// --- 防误触锁定比例配置 ---
#define DOMINANT_NUMERATOR CONFIG_TRACKPOINT_DOMINANT_NUMERATOR
#define DOMINANT_DENOMINATOR CONFIG_TRACKPOINT_DOMINANT_DENOMINATOR

// --- 鼠标指针基础配置 ---
#define MOUSE_BASE_SPEED (CONFIG_TRACKPOINT_MOUSE_BASE_SPEED_PERCENT / 100.0f)
#define MOUSE_SENS_BASE (CONFIG_TRACKPOINT_MOUSE_SENS_BASE_PERCENT / 100.0f)
#define MOUSE_SENS_STEP (CONFIG_TRACKPOINT_MOUSE_SENS_STEP_PERCENT / 100.0f)

/* ========================================================================= */

/* ========= TrackPoint 硬件常量 ========= */
#define TRACKPOINT_PACKET_LEN 7
#define TRACKPOINT_MAGIC_BYTE0 0x50

/* 模块插座引脚（MOTION 中断线）
 *
 * 引脚号/端口号在 Kconfig 里，具体值写在 config/boards/arm/sofle_dongle/Kconfig.defconfig（右半段）；
 * 本文件不写死脚号。设备树预处理看不到 Kconfig 值，所以用 #if 把端口号翻成
 * 节点标签，翻不出来（-1 = 该侧还没定义）就编译失败。
 *
 * 小红点只有右座数据（MODULAR_POINTER_ANALYSIS.md §4.2）：MOTION P0.14。
 * I2C 那两根在 rt.dtsi（板目录） 里。
 */
#if !defined(CONFIG_BOARD_RT_SLAVER) && !defined(CONFIG_BOARD_RT_MASTER)
#error "trackpoint 目前只有右座引脚定义（见 MODULAR_POINTER_ANALYSIS.md §4.2）；左座引脚源码里不存在，拒绝编出一份接不上的固件。"
#endif

#if CONFIG_TRACKPOINT_MOTION_GPIO_PORT == 0
#define MOTION_GPIO_NODE DT_NODELABEL(gpio0)
#elif CONFIG_TRACKPOINT_MOTION_GPIO_PORT == 1
#define MOTION_GPIO_NODE DT_NODELABEL(gpio1)
#else
#error "CONFIG_TRACKPOINT_MOTION_GPIO_PORT 未定义（-1）或超出 0/1；见 config/boards/arm/sofle_dongle/Kconfig.defconfig（右半段）"
#endif

#define MOTION_GPIO_PIN CONFIG_TRACKPOINT_MOTION_GPIO_PIN
#define MOTION_GPIO_FLAGS (GPIO_ACTIVE_LOW | GPIO_PULL_UP)

#define MAX_PACKETS_PER_WORK 32
#define REPORT_INTERVAL_MS 8

/* 按住其中任意一个键位 -> 鼠标移动；都不按 -> 滚轮（默认滚轮） */
#define MOUSE_KEY_POSITION_1 CONFIG_TRACKPOINT_MOUSE_KEY_POSITION_1
#define MOUSE_KEY_POSITION_2 CONFIG_TRACKPOINT_MOUSE_KEY_POSITION_2

/* ========= 全局状态 ========= */
static bool mouse_key_1_pressed;
static bool mouse_key_2_pressed;

/* ========= 累加器 — 无锁，由 k_work_q 串行化保证安全 ========= */
struct tp_snapshot {
	int32_t sum_dx;
	int32_t sum_dy;
	int32_t packet_count;
	uint32_t first_ts;
	uint32_t last_ts;
};
static struct tp_snapshot snap;

/* ========= 模式切换按键监听 ========= */
static int mouse_key_listener_cb(const zmk_event_t *eh) {
	const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
	if (!ev) return 0;

	if (ev->position == MOUSE_KEY_POSITION_1) {
		mouse_key_1_pressed = ev->state;
	} else if (ev->position == MOUSE_KEY_POSITION_2) {
		mouse_key_2_pressed = ev->state;
	}
	return 0;
}
ZMK_LISTENER(trackpoint_mouse_key_listener, mouse_key_listener_cb);
ZMK_SUBSCRIPTION(trackpoint_mouse_key_listener, zmk_position_state_changed);

struct trackpoint_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec motion_gpio;
};

struct trackpoint_data {
	const struct device *dev;
	struct k_work read_work;
	struct k_work report_work;
	struct k_timer report_timer;
	struct gpio_callback motion_cb_data;
	struct k_work_delayable enable_irq_work;
	uint32_t last_packet_time;
	int16_t scroll_residue_x;
	int16_t scroll_residue_y;
};

/* ========= 指数加速计算 ========= */
#ifdef CONFIG_TRACKPOINT_EXPONENTIAL
#define TP_MAX_MULT 2.0f
static inline float trackpoint_exponential_factor(int32_t dx, int32_t dy, uint32_t delta_ms) {
	if (delta_ms == 0) delta_ms = 1;

	int dist = abs(dx) + abs(dy);
	if (dist < 1) return 1.0f;

	float speed = (float)dist / (float)delta_ms;
	float mult = expf(speed * 1.307357f);

	return (mult > TP_MAX_MULT) ? TP_MAX_MULT : mult;
}
#endif

/* ========= 读取数据包（I2C mutex 保护） ========= */
static int trackpoint_read_packet(const struct device *dev, int8_t *dx, int8_t *dy) {
	const struct trackpoint_config *cfg = dev->config;
	uint8_t buf[TRACKPOINT_PACKET_LEN] = {0};

	k_mutex_lock(&trackpoint_i2c_mutex, K_NO_WAIT);

	int ret = i2c_read_dt(&cfg->i2c, buf, TRACKPOINT_PACKET_LEN);

	k_mutex_unlock(&trackpoint_i2c_mutex);

	if (ret < 0) return ret;

	if (buf[0] != TRACKPOINT_MAGIC_BYTE0) return -EIO;

	*dx = (int8_t)buf[2];
	*dy = (int8_t)buf[3];
	return 0;
}

/* ========= 滚轮单轴处理（线性曲线，无阻尼，看门狗防漂移） ========= */
static inline void process_scroll_axis(const struct device *dev, int8_t delta,
					int16_t *residue, uint16_t input_code, int8_t dir_mult) {
	int abs_delta = abs(delta);

	if (abs_delta <= SCROLL_DEADZONE) {
		return;
	}

	if (abs_delta > SCROLL_INPUT_MAX) {
		abs_delta = SCROLL_INPUT_MAX;
	}

	/* 线性除数曲线：力度越大除数越小，加速更灵敏 */
	float t = (float)abs_delta / SCROLL_INPUT_MAX;
	float f_div = SCROLL_DIVISOR_SLOW - (SCROLL_DIVISOR_SLOW - SCROLL_DIVISOR_FAST) * t;

	int divisor = (int)f_div;
	if (divisor < 1) divisor = 1;

	*residue += (delta * dir_mult);

	int16_t scroll_ticks = *residue / divisor;
	if (scroll_ticks != 0) {
		input_report_rel(dev, input_code, scroll_ticks, true, K_NO_WAIT);
		*residue %= divisor;
	}
}

/* ========= read_work — GPIO 触发，批量读取 I2C 并累加 ========= */
static void read_work_cb(struct k_work *work) {
	struct trackpoint_data *data = CONTAINER_OF(work, struct trackpoint_data, read_work);
	const struct device *dev = data->dev;
	const struct trackpoint_config *cfg = dev->config;

	int32_t sum_dx = 0, sum_dy = 0;
	int packets = 0;
	uint32_t now = 0;

	while (packets < MAX_PACKETS_PER_WORK && gpio_pin_get_dt(&cfg->motion_gpio) > 0) {
		int8_t dx = 0, dy = 0;
		if (trackpoint_read_packet(dev, &dx, &dy) != 0) {
			break;
		}
		now = k_uptime_get_32();
		sum_dx += dx;
		sum_dy += dy;
		packets++;
	}

	if (packets == 0) {
		return;
	}

	last_activity_time = now;

	/* 累加到全局快照 — 无锁，同一 k_work_q 串行化保证 */
	if (snap.packet_count == 0) {
		snap.first_ts = now;
	}
	snap.sum_dx += sum_dx;
	snap.sum_dy += sum_dy;
	snap.packet_count += packets;
	snap.last_ts = now;
}

/* ========= report_work — Timer 触发，读清零累加器并发送 HID 报告 ========= */
static void report_work_cb(struct k_work *work) {
	struct trackpoint_data *data = CONTAINER_OF(work, struct trackpoint_data, report_work);
	const struct device *dev = data->dev;

	/* 读并清零累加器 — 无锁 */
	int32_t sum_dx = snap.sum_dx; snap.sum_dx = 0;
	int32_t sum_dy = snap.sum_dy; snap.sum_dy = 0;
	snap.packet_count = 0;
	uint32_t first_ts = snap.first_ts;
	uint32_t last_ts = snap.last_ts;

	if (sum_dx == 0 && sum_dy == 0) {
		return;
	}

	uint32_t now = k_uptime_get_32();

	/* ===== 看门狗恢复 ===== */
	if (now - last_activity_time > TRACKPOINT_WDT_TIMEOUT) {
		LOG_WRN("TrackPoint watchdog recovery");
		data->scroll_residue_x = 0;
		data->scroll_residue_y = 0;
		return;
	}

	/* ===== 模式判定：默认滚轮，按住模式键才走鼠标 ===== */
	bool is_scroll_mode = !(mouse_key_1_pressed || mouse_key_2_pressed);

	if (is_scroll_mode) {
		/* 进入滚轮模式时用当前位移初始化残留值 */
		if (data->last_packet_time == 0 ||
		    now - data->last_packet_time > 500) {
			data->scroll_residue_x = (int16_t)(sum_dx * SCROLL_X_DIR);
			data->scroll_residue_y = (int16_t)(sum_dy * SCROLL_Y_DIR);
		}

		/* 主导轴锁定防误触 */
		int abs_x = abs(sum_dx);
		int abs_y = abs(sum_dy);

		if (abs_y * DOMINANT_DENOMINATOR > abs_x * DOMINANT_NUMERATOR) {
			sum_dx = 0;
		} else if (abs_x * DOMINANT_DENOMINATOR > abs_y * DOMINANT_NUMERATOR) {
			sum_dy = 0;
		} else {
			sum_dx = 0;
			sum_dy = 0;
		}

		process_scroll_axis(dev, (int8_t)CLAMP(sum_dx, -128, 127),
				    &data->scroll_residue_x, INPUT_REL_HWHEEL, SCROLL_X_DIR);
		process_scroll_axis(dev, (int8_t)CLAMP(sum_dy, -128, 127),
				    &data->scroll_residue_y, INPUT_REL_WHEEL, SCROLL_Y_DIR);

	} else {
		uint8_t tp_led_brt = custom_led_get_last_valid_brightness();
		float tp_factor = MOUSE_SENS_BASE + MOUSE_SENS_STEP * tp_led_brt;

		uint32_t delta_ms = last_ts - first_ts;
		if (delta_ms == 0) delta_ms = 1;

#ifdef CONFIG_TRACKPOINT_EXPONENTIAL
		float exp_mult = trackpoint_exponential_factor(sum_dx, sum_dy, delta_ms);
#else
		float exp_mult = 1.0f;
#endif
		float fx = sum_dx * MOUSE_BASE_SPEED * tp_factor * exp_mult;
		float fy = sum_dy * MOUSE_BASE_SPEED * tp_factor * exp_mult;

		input_report_rel(dev, INPUT_REL_X, -(int)fx, false, K_NO_WAIT);
		input_report_rel(dev, INPUT_REL_Y, -(int)fy, true, K_NO_WAIT);
	}
	data->last_packet_time = now;
}

/* ========= report_timer ISR — 提交 report_work 到工作队列 ========= */
static void report_timer_cb(struct k_timer *timer) {
	struct trackpoint_data *data = CONTAINER_OF(timer, struct trackpoint_data, report_timer);
	k_work_submit_to_queue(&tp_workq, &data->report_work);
}

/* ========= GPIO 中断 ISR — 提交 read_work ========= */
static void motion_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
	struct trackpoint_data *data = CONTAINER_OF(cb, struct trackpoint_data, motion_cb_data);
	last_activity_time = k_uptime_get_32();
	k_work_submit_to_queue(&tp_workq, &data->read_work);
}

/* ========= 延迟 IRQ 启用回调 ========= */
static void trackpoint_enable_irq_work_cb(struct k_work *work) {
	struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
	struct trackpoint_data *data = CONTAINER_OF(dwork, struct trackpoint_data, enable_irq_work);
	const struct device *dev = data->dev;
	const struct trackpoint_config *cfg = dev->config;

	gpio_pin_interrupt_configure_dt(&cfg->motion_gpio, GPIO_INT_EDGE_TO_ACTIVE);

	LOG_INF("TrackPoint IRQ enabled (delayed)");
}

/* ========= 初始化函数 ========= */
static int trackpoint_init(const struct device *dev) {
	const struct trackpoint_config *cfg = dev->config;
	struct trackpoint_data *data = dev->data;

	if (!i2c_is_ready_dt(&cfg->i2c)) return -ENODEV;
	if (!gpio_is_ready_dt(&cfg->motion_gpio)) return -ENODEV;

	k_mutex_init(&trackpoint_i2c_mutex);

	data->dev = dev;
	data->scroll_residue_x = 0;
	data->scroll_residue_y = 0;
	data->last_packet_time = k_uptime_get_32();

	/* 启动独立 Work Queue */
	k_work_queue_start(&tp_workq, tp_workq_stack,
			   K_THREAD_STACK_SIZEOF(tp_workq_stack),
			   TP_WORKQ_PRIORITY, NULL);

	/* read_work — GPIO 触发，读取 I2C 并累加 */
	k_work_init(&data->read_work, read_work_cb);

	/* report_work — Timer 触发，发送累加后的 HID 报告 */
	k_work_init(&data->report_work, report_work_cb);

	/* 8ms 定时器 — 125Hz 报告频率 */
	k_timer_init(&data->report_timer, report_timer_cb, NULL);
	k_timer_start(&data->report_timer, K_MSEC(REPORT_INTERVAL_MS), K_MSEC(REPORT_INTERVAL_MS));

	/* GPIO 中断 — 延迟 200ms 启用，避免开机误触发 */
	gpio_pin_configure_dt(&cfg->motion_gpio, GPIO_INPUT);
	gpio_init_callback(&data->motion_cb_data, motion_isr, BIT(cfg->motion_gpio.pin));
	gpio_add_callback(cfg->motion_gpio.port, &data->motion_cb_data);

	k_work_init_delayable(&data->enable_irq_work, trackpoint_enable_irq_work_cb);
	k_work_schedule(&data->enable_irq_work, K_MSEC(200));

	LOG_INF("TrackPoint Driver Initialized (timer-driven, IRQ delayed)");
	return 0;
}

#define TRACKPOINT_DEFINE(inst)                                                \
	static struct trackpoint_data trackpoint_data_##inst;                      \
	static const struct trackpoint_config trackpoint_config_##inst = {         \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                     \
		.motion_gpio = {                                                       \
			.port = DEVICE_DT_GET(MOTION_GPIO_NODE),                           \
			.pin = MOTION_GPIO_PIN,                                            \
			.dt_flags = MOTION_GPIO_FLAGS                                      \
		},                                                                     \
	};                                                                         \
	DEVICE_DT_INST_DEFINE(inst, trackpoint_init, NULL, &trackpoint_data_##inst,\
			      &trackpoint_config_##inst, POST_KERNEL,              \
			      CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(TRACKPOINT_DEFINE);
