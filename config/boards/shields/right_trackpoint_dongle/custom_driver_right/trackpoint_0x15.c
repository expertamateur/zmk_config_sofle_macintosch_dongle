/*
 * TrackPoint HID over I2C Driver — Dongle fusion version
 * Stability infra from ZitaoTech + toggle-key mode preserved.
 * SPDX-License-Identifier: MIT
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

#define MOTION_GPIO_NODE DT_NODELABEL(gpio0)
#define MOTION_GPIO_PIN 14
#define MOTION_GPIO_FLAGS (GPIO_ACTIVE_LOW | GPIO_PULL_UP)

#define MAX_PACKETS_PER_WORK 5

#define TOGGLE_POSITION_CODE CONFIG_TRACKPOINT_TOGGLE_KEY_POSITION

/* ========= 全局状态 ========= */
static bool toggle_key_pressed;

/* ========= 模式切换按键监听（保留 Dongle toggle 逻辑） ========= */
static int toggle_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev) return 0;

    if (ev->position == TOGGLE_POSITION_CODE) {
        toggle_key_pressed = ev->state;
    }
    return 0;
}
ZMK_LISTENER(trackpoint_toggle_listener, toggle_listener_cb);
ZMK_SUBSCRIPTION(trackpoint_toggle_listener, zmk_position_state_changed);

struct trackpoint_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec motion_gpio;
};

struct trackpoint_data {
    const struct device *dev;
    struct k_work work;
    struct gpio_callback motion_cb_data;
    struct k_work_delayable enable_irq_work;
    uint32_t last_packet_time;
    int16_t scroll_residue_x;
    int16_t scroll_residue_y;
};

/* ========= 指数加速计算 ========= */
#ifdef CONFIG_TRACKPOINT_EXPONENTIAL
#define TP_MAX_MULT 2.0f
static inline float trackpoint_exponential_factor(int8_t dx, int8_t dy, uint32_t delta_ms) {
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

/* ========= 滚轮单轴处理（t² 曲线 + 阻尼） ========= */
static inline void process_scroll_axis(const struct device *dev, int8_t delta,
                                        int16_t *residue, uint16_t input_code, int8_t dir_mult) {
    int abs_delta = abs(delta);

    if (abs_delta <= SCROLL_DEADZONE) {
        return; /* 保留残留值，不丢滚动连续性 */
    }

    if (abs_delta > SCROLL_INPUT_MAX) {
        abs_delta = SCROLL_INPUT_MAX;
    }

    /* 非线性 t² 除数曲线 */
    float t = (float)abs_delta / SCROLL_INPUT_MAX;
    t = t * t;
    float f_div = SCROLL_DIVISOR_SLOW - (SCROLL_DIVISOR_SLOW - SCROLL_DIVISOR_FAST) * t;

    int divisor = (int)f_div;
    if (divisor < 1) divisor = 1;

    *residue += (delta * dir_mult);

    int16_t scroll_ticks = *residue / divisor;
    if (scroll_ticks != 0) {
        input_report_rel(dev, input_code, scroll_ticks, true, K_NO_WAIT);
        *residue %= divisor;
    }

    /* 阻尼，防止漂移 */
    *residue = (*residue * 7) / 8;
}

/* ========= 工作队列处理（批量读取 + 合并发送） ========= */
static void trackpoint_work_cb(struct k_work *work) {
    struct trackpoint_data *data = CONTAINER_OF(work, struct trackpoint_data, work);
    const struct device *dev = data->dev;
    const struct trackpoint_config *cfg = dev->config;

    uint32_t now = k_uptime_get_32();

    /* ===== 看门狗恢复 ===== */
    if (now - last_activity_time > TRACKPOINT_WDT_TIMEOUT) {
        LOG_WRN("TrackPoint watchdog recovery");
        data->scroll_residue_x = 0;
        data->scroll_residue_y = 0;
        return;
    }

    /* ===== 批量读取（最多 MAX_PACKETS_PER_WORK 个包） ===== */
    int32_t sum_dx = 0, sum_dy = 0;
    int packets = 0;

    while (packets < MAX_PACKETS_PER_WORK && gpio_pin_get_dt(&cfg->motion_gpio) > 0) {
        int8_t dx = 0, dy = 0;
        if (trackpoint_read_packet(dev, &dx, &dy) != 0) {
            break;
        }
        sum_dx += dx;
        sum_dy += dy;
        packets++;
    }

    if (packets == 0) {
        return;
    }

    last_activity_time = now;

    /* ===== 模式判定（保留 Dongle toggle 逻辑） ===== */
    bool is_scroll_mode = IS_ENABLED(CONFIG_TRACKPOINT_START_IN_SCROLL_MODE)
                          ? !toggle_key_pressed
                          : toggle_key_pressed;

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

#ifdef CONFIG_TRACKPOINT_EXPONENTIAL
        uint32_t delta = now - data->last_packet_time;
        float exp_mult = trackpoint_exponential_factor((int8_t)CLAMP(sum_dx, -128, 127),
                                                        (int8_t)CLAMP(sum_dy, -128, 127), delta);
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

/* ========= GPIO 中断 ISR ========= */
static void motion_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    struct trackpoint_data *data = CONTAINER_OF(cb, struct trackpoint_data, motion_cb_data);

    last_activity_time = k_uptime_get_32();

    k_work_submit_to_queue(&tp_workq, &data->work);
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
    k_work_init(&data->work, trackpoint_work_cb);

    /* 启动独立 Work Queue */
    k_work_queue_start(&tp_workq, tp_workq_stack,
                       K_THREAD_STACK_SIZEOF(tp_workq_stack),
                       TP_WORKQ_PRIORITY, NULL);

    gpio_pin_configure_dt(&cfg->motion_gpio, GPIO_INPUT);
    /* IRQ 延迟 200ms 启用，避免开机误触发 */
    gpio_init_callback(&data->motion_cb_data, motion_isr, BIT(cfg->motion_gpio.pin));
    gpio_add_callback(cfg->motion_gpio.port, &data->motion_cb_data);

    k_work_init_delayable(&data->enable_irq_work, trackpoint_enable_irq_work_cb);
    k_work_schedule(&data->enable_irq_work, K_MSEC(200));

    LOG_INF("TrackPoint Driver Initialized (IRQ delayed)");
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
