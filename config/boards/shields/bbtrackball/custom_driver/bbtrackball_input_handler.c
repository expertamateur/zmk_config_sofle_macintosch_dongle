/*
 * bbtrackball_input_handler.c
 * BB Trackball FULL interrupt-driven version
 *
 * + Dedicated workqueue version (NO system workqueue)
 *
 * 指针语义（三个指点设备统一，见 MODULAR_POINTER_ANALYSIS.md）：
 *
 *   | 状态                          | 输出                          |
 *   |-------------------------------|-------------------------------|
 *   | 默认                          | 滚轮 WHEEL / HWHEEL           |
 *   | 按住 MOUSE_KEY_POSITION_1 或 2 | 鼠标移动 REL_X / REL_Y        |
 *
 * 即「指针设备跟随层」：键位在 mouse 层上，按住就切到鼠标移动。
 * 本文件不产生任何按键事件（无 input_report_key），不做方向键。
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_bbtrackball

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>

#include <math.h>
#include <stdlib.h>

#include <zmk/events/position_state_changed.h>

LOG_MODULE_REGISTER(bbtrackball_input_handler, LOG_LEVEL_INF);

/* =========================================================
 * Workqueue config  ⭐⭐⭐新增
 * ========================================================= */

#define BBTRACKBALL_WORKQ_STACK_SIZE 2048
#define BBTRACKBALL_WORKQ_PRIORITY 5

K_THREAD_STACK_DEFINE(bbtrackball_workq_stack, BBTRACKBALL_WORKQ_STACK_SIZE);
static struct k_work_q bbtrackball_work_q;

/* =========================================================
 * 模块插座引脚（四根方向线）
 * =========================================================
 *
 * 引脚号/端口号都在 Kconfig 里，具体值写在 config/boards/arm/sofle_dongle/Kconfig.defconfig（左半段）；
 * 本文件不再出现任何写死的脚号。设备树预处理看不到 Kconfig 值，所以下面要用
 * #if 把端口号翻成节点标签——翻不出来（-1 = 该侧还没定义）就直接编译失败，
 * 与「没有引脚定义就拒绝编译」是同一套待遇。
 *
 * 轨迹球目前只有左座数据（MODULAR_POINTER_ANALYSIS.md §4.1）：
 *   UP P0.05 · LEFT P0.12 · DOWN P1.09 · RIGHT P0.27
 *
 * 右座引脚资料到位后：把八个 CONFIG_BBTRACKBALL_*_{PORT,PIN} 补进
 * config/boards/arm/sofle_dongle/Kconfig.defconfig（右半段），再把下面那道 #error 换成 #elif。
 */

#if !defined(CONFIG_BOARD_LF_SLAVER) && !defined(CONFIG_BOARD_LF_MASTER)
#error "bbtrackball 目前只有左座引脚定义（见 MODULAR_POINTER_ANALYSIS.md §4.1）；右座引脚源码里不存在，拒绝编出一份接不上的固件。"
#endif

#if CONFIG_BBTRACKBALL_UP_GPIO_PORT == 0
#define UP_GPIO_DEV DT_NODELABEL(gpio0)
#elif CONFIG_BBTRACKBALL_UP_GPIO_PORT == 1
#define UP_GPIO_DEV DT_NODELABEL(gpio1)
#else
#error "CONFIG_BBTRACKBALL_UP_GPIO_PORT 未定义（-1）或超出 0/1；见 config/boards/arm/sofle_dongle/Kconfig.defconfig（左半段）"
#endif

#if CONFIG_BBTRACKBALL_LEFT_GPIO_PORT == 0
#define LEFT_GPIO_DEV DT_NODELABEL(gpio0)
#elif CONFIG_BBTRACKBALL_LEFT_GPIO_PORT == 1
#define LEFT_GPIO_DEV DT_NODELABEL(gpio1)
#else
#error "CONFIG_BBTRACKBALL_LEFT_GPIO_PORT 未定义（-1）或超出 0/1；见 config/boards/arm/sofle_dongle/Kconfig.defconfig（左半段）"
#endif

#if CONFIG_BBTRACKBALL_DOWN_GPIO_PORT == 0
#define DOWN_GPIO_DEV DT_NODELABEL(gpio0)
#elif CONFIG_BBTRACKBALL_DOWN_GPIO_PORT == 1
#define DOWN_GPIO_DEV DT_NODELABEL(gpio1)
#else
#error "CONFIG_BBTRACKBALL_DOWN_GPIO_PORT 未定义（-1）或超出 0/1；见 config/boards/arm/sofle_dongle/Kconfig.defconfig（左半段）"
#endif

#if CONFIG_BBTRACKBALL_RIGHT_GPIO_PORT == 0
#define RIGHT_GPIO_DEV DT_NODELABEL(gpio0)
#elif CONFIG_BBTRACKBALL_RIGHT_GPIO_PORT == 1
#define RIGHT_GPIO_DEV DT_NODELABEL(gpio1)
#else
#error "CONFIG_BBTRACKBALL_RIGHT_GPIO_PORT 未定义（-1）或超出 0/1；见 config/boards/arm/sofle_dongle/Kconfig.defconfig（左半段）"
#endif

#define UP_GPIO_PIN CONFIG_BBTRACKBALL_UP_GPIO_PIN
#define LEFT_GPIO_PIN CONFIG_BBTRACKBALL_LEFT_GPIO_PIN
#define DOWN_GPIO_PIN CONFIG_BBTRACKBALL_DOWN_GPIO_PIN
#define RIGHT_GPIO_PIN CONFIG_BBTRACKBALL_RIGHT_GPIO_PIN

/* =========================================================
 * Config
 * ========================================================= */

#define BASE_MOVE_PIXELS 3
#define EXPONENTIAL_BASE 1.12f
#define SPEED_SCALE 60.0f

#define MOVE_IDLE_TIMEOUT 30

/* 按住其中任意一个键位 -> 鼠标移动；都不按 -> 滚轮 */
#define MOUSE_KEY_POSITION_1 CONFIG_BBTRACKBALL_MOUSE_KEY_POSITION_1
#define MOUSE_KEY_POSITION_2 CONFIG_BBTRACKBALL_MOUSE_KEY_POSITION_2

/* =========================================================
 * Runtime State
 * ========================================================= */

static bool moved = false;
static bool mouse_key_1_pressed = false;
static bool mouse_key_2_pressed = false;

static int dx_acc = 0;
static int dy_acc = 0;

static uint32_t last_move_time = 0;

/* =========================================================
 * GPIO Input Description
 * ========================================================= */

typedef struct {
    const struct device *gpio_dev;
    int pin;
    int last_state;
    uint32_t last_time;
    int sign;
} DirInput;

static DirInput dir_inputs[] = {
    {DEVICE_DT_GET(LEFT_GPIO_DEV), LEFT_GPIO_PIN, 1, 0, -1},
    {DEVICE_DT_GET(RIGHT_GPIO_DEV), RIGHT_GPIO_PIN, 1, 0, +1},
    {DEVICE_DT_GET(UP_GPIO_DEV), UP_GPIO_PIN, 1, 0, -1},
    {DEVICE_DT_GET(DOWN_GPIO_DEV), DOWN_GPIO_PIN, 1, 0, +1},
};

/* ========================================================= */

struct bbtrackball_dev_config {
    uint16_t x_input_code;
    uint16_t y_input_code;
};

struct bbtrackball_data;

/* ========================================================= */

struct bb_gpio_cb {
    struct gpio_callback cb;
    struct bbtrackball_data *parent;
};

struct bbtrackball_data {
    const struct device *dev;
    struct k_work work;
    struct bb_gpio_cb gpio_cbs[ARRAY_SIZE(dir_inputs)];
};

/* ========================================================= */

bool trackball_is_active(void) { return (k_uptime_get_32() - last_move_time) < 40; }

/* =========================================================
 * 模式键 listener
 * ========================================================= */

static int mouse_key_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (!ev)
        return ZMK_EV_EVENT_BUBBLE;

    if (ev->position == MOUSE_KEY_POSITION_1) {
        mouse_key_1_pressed = ev->state;
    } else if (ev->position == MOUSE_KEY_POSITION_2) {
        mouse_key_2_pressed = ev->state;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(bbtrackball_mouse_key_listener, mouse_key_listener_cb);
ZMK_SUBSCRIPTION(bbtrackball_mouse_key_listener, zmk_position_state_changed);

/* =========================================================
 * GPIO interrupt callback
 * ========================================================= */

static void dir_edge_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {

    struct bb_gpio_cb *wrapper = CONTAINER_OF(cb, struct bb_gpio_cb, cb);
    struct bbtrackball_data *data = wrapper->parent;

    for (size_t i = 0; i < ARRAY_SIZE(dir_inputs); i++) {

        DirInput *d = &dir_inputs[i];

        if ((dev == d->gpio_dev) && (pins & BIT(d->pin))) {

            int val = gpio_pin_get(dev, d->pin);

            if (val != d->last_state) {

                uint32_t now = k_uptime_get_32();
                uint32_t delta = now - d->last_time;
                if (delta == 0)
                    delta = 1;

                float speed_factor = SPEED_SCALE / (float)delta;
                float mult = powf(EXPONENTIAL_BASE, speed_factor);
                int delta_px = (int)roundf(BASE_MOVE_PIXELS * mult);

                if (i < 2)
                    dx_acc += d->sign * delta_px;
                else
                    dy_acc += d->sign * delta_px;

                d->last_state = val;
                d->last_time = now;

                if (!k_work_is_pending(&data->work)) {
                    k_work_submit_to_queue(&bbtrackball_work_q, &data->work); // ⭐修改点
                }
            }
        }
    }
}

/* =========================================================
 * Work Handler
 * ========================================================= */

static void bbtrackball_work_handler(struct k_work *work) {

    struct bbtrackball_data *data = CONTAINER_OF(work, struct bbtrackball_data, work);
    const struct device *dev = data->dev;

    uint32_t now = k_uptime_get_32();

    int dx = dx_acc;
    int dy = dy_acc;

    dx_acc = 0;
    dy_acc = 0;

    if (dx == 0 && dy == 0) {
        if (now - last_move_time > MOVE_IDLE_TIMEOUT) {
            moved = false;
        }
        return;
    }

    last_move_time = now;
    moved = true;

    if (mouse_key_1_pressed || mouse_key_2_pressed) {
        input_report_rel(dev, INPUT_REL_X, -dx, false, K_NO_WAIT);
        input_report_rel(dev, INPUT_REL_Y, -dy, true, K_NO_WAIT);
        return;
    }

    input_report_rel(dev, INPUT_REL_HWHEEL, -dx, false, K_NO_WAIT);
    input_report_rel(dev, INPUT_REL_WHEEL, dy, true, K_NO_WAIT);
}

/* =========================================================
 * Init
 * ========================================================= */

static int bbtrackball_init(const struct device *dev) {

    struct bbtrackball_data *data = dev->data;

    LOG_INF("Initializing BBtrackball");

    data->dev = dev;

    /* ⭐ 启动独立 workqueue */
    k_work_queue_start(&bbtrackball_work_q, bbtrackball_workq_stack,
                       K_THREAD_STACK_SIZEOF(bbtrackball_workq_stack), BBTRACKBALL_WORKQ_PRIORITY,
                       NULL);

    k_work_init(&data->work, bbtrackball_work_handler);

    for (size_t i = 0; i < ARRAY_SIZE(dir_inputs); i++) {

        DirInput *d = &dir_inputs[i];

        gpio_pin_configure(d->gpio_dev, d->pin, GPIO_INPUT | GPIO_PULL_UP);

        d->last_state = gpio_pin_get(d->gpio_dev, d->pin);
        d->last_time = k_uptime_get_32();

        data->gpio_cbs[i].parent = data;

        gpio_init_callback(&data->gpio_cbs[i].cb, dir_edge_cb, BIT(d->pin));
        gpio_add_callback(d->gpio_dev, &data->gpio_cbs[i].cb);

        gpio_pin_interrupt_configure(d->gpio_dev, d->pin, GPIO_INT_EDGE_BOTH);
    }

    return 0;
}

/* ========================================================= */

#define BBTRACKBALL_INIT_PRIORITY CONFIG_INPUT_INIT_PRIORITY

#define BBTRACKBALL_DEFINE(inst)                                                                   \
    static struct bbtrackball_data bbtrackball_data_##inst;                                        \
                                                                                                   \
    static const struct bbtrackball_dev_config bbtrackball_config_##inst = {                       \
        .x_input_code = DT_PROP_OR(DT_DRV_INST(inst), x_input_code, INPUT_REL_X),                  \
        .y_input_code = DT_PROP_OR(DT_DRV_INST(inst), y_input_code, INPUT_REL_Y),                  \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(inst, bbtrackball_init, NULL, &bbtrackball_data_##inst,                  \
                          &bbtrackball_config_##inst, POST_KERNEL, BBTRACKBALL_INIT_PRIORITY,      \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(BBTRACKBALL_DEFINE);
