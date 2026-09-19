# 模块化指点设备：一份外设固件通吃两侧的可行性

> 生成时间：2026-09-19 · **仅调查记录，未改动仓库任何文件**
>
> 📌 **合并已完成（2026-09-20）。** 本文里的路径名是**分析当时的旧名**：实际落地时去掉了
> 左右前缀和 `_dongle` 后缀 —— `config/boards/shields/{bbtrackball,bbtrackpad,trackpoint}/`，
> 构建目标名见 `build.yaml`。§4 的引脚表仍是权威来源（驱动里的 `#error` 就指向它）。
> 所有结论来自**实测构建**：源码树副本放 `/tmp/probe_main`，构建产物放 `~/repositories/xa_m/zmk/app/build/<临时名>`（均已在调查结束后清理，未动 `build/` 下你自己的目录）
>
> ⚠️ **本文档已推翻初版结论。** 初版认为「shield 不能跨板复用」，那是**基于 HEAD 架构的误判**：HEAD 把 split 声明写死在 board dts 里，导致换 shield 就报断言。采用 main 的架构（声明下沉到 shield overlay）后，**同一个 shield 可以直接编到两侧**——已实测。

---

## 0. 结论

| 问题 | 答案 |
|---|---|
| 左右能否**公用同一份外设固件源码**？ | **编译层面能。** 实测：左手轨迹球 shield 编到右手板 → 成功（FLASH 38.44%）；左手触摸板 shield 编到右手板 → 成功（FLASH 38.78%）。但**能编 ≠ 能用**：右手座上轨迹球/触摸板的引脚源码里没有（§4.4），所以现阶段不这么做 |
| 会不会影响 ZMK 的自动链接？ | **不会。** shield 的 Kconfig 符号由**名字**决定，两侧取值相同；源文件挂载、shield 的 `.conf` 都两侧一致生效。区分左右用 **board 符号**（`CONFIG_BOARD_SOFLE_DONGLE_LEFT/RIGHT`，实测可用）或 per-side conf |
| 引脚能否用 conf 注入？ | **分两半**：**C 代码里的引脚可以**（Kconfig 值进 C 没问题）；**设备树里的引脚不行**（实测 `#if CONFIG_x` 在 overlay 里恒走 `#else`，DTS 预处理看不到 Kconfig） |
| 设备树要怎么改？ | 把「座子→MCU 引脚」从 shield overlay **移到每侧一份的 `config/<board>.overlay`**（ZMK 原生支持，实测有效）；shield overlay 只留设备节点 + label 引用 |
| 还有什么是必须按侧不同的？ | **matrix position 号**（驱动里的 `ev->position == 60/61`）。同一物理键在两侧的 position **不同，且差值逐行不一样**，必须按侧写死 |
| 中枢能不能分辨指针输入来自哪一侧？ | **main 的配置下不能。** 中枢按 `reg` 分发（`input_split.c:29-39` 命中即 return），main 三个节点 `reg` 全是 0 → 永远只命中第一个。左右行为相同的场合无所谓；要不同就必须让两侧 `reg` 不同（见 1.7） |
| 一份 dongle 够不够覆盖两侧三种设备？ | **够**（前提是左右行为一致）。main 的 dongle 三个 listener 全 `okay` 且 processor 相同，本身就是设备无关的 |
| 左右两座是同一批 MCU 脚吗？ | **不是，已证实**（4.3）：左座那 4 根线在右板上是矩阵行列线。左右互换设备需要另一组引脚，源码里不存在 |
| 那现在编几个固件？ | **4 个**：dongle + 左轨迹球 + 左触摸板 + 右小红点。即「每侧只编该侧已有定义」+ 厂商发布过的同一组合（§3、§4.4） |

---

## 1. 实测证据

### 1.1 同一个 shield 能编到两侧 ✅

临时树（main 的树 + 一个探针 shield），实测：

| 实验 | 命令 | 结果 |
|---|---|---|
| A_L_ball | `-b sofle_dongle_left -- -DSHIELD="lpm_view left_bbtrackball_dongle"` | ✅ exit=0，FLASH 33.41% / RAM 28.98% |
| **A_R_ball** | `-b sofle_dongle_right -- -DSHIELD="lpm_view left_bbtrackball_dongle"` | ✅ **exit=0，FLASH 38.44% / RAM 29.48%** |
| **A_R_pad** | `-b sofle_dongle_right -- -DSHIELD="lpm_view left_bbtrackpad_dongle"` | ✅ **exit=0，FLASH 38.78% / RAM 29.58%** |

**「左手轨迹球 shield 编到右手板」编译、链接全部通过。** 前提是 shield overlay **自己声明 split/listener 节点**（main 的做法），而不是依赖 board dts 声明（HEAD 的做法）。

> 旧结论为何错：HEAD 的 `sofle_dongle_left.dts:133` 无条件声明 `bbtrackball_split`，而 `input_split.c:55` 的 `BUILD_ASSERT` 要求外设侧该节点必须带 `device` 属性。盾换成触摸板时没人给它 `device` → 断言失败。这是 **HEAD 架构的问题，不是「跨板」的问题**。

### 1.2 ZMK 的自动链接不受影响 ✅

`config/boards/shields/probe_side/Kconfig.shield`：

```
config SHIELD_PROBE_SIDE
    def_bool $(shields_list_contains,probe_side)
```

`$(shields_list_contains,<名字>)` 只匹配 **shield 名字**，与 board 无关。实测两侧生成的 `autoconf.h`：

| 构建 | 符号 |
|---|---|
| 左板 + probe_side | `#define CONFIG_SHIELD_PROBE_SIDE 1` / `#define CONFIG_BOARD_SOFLE_DONGLE_LEFT 1` |
| 右板 + probe_side | `#define CONFIG_SHIELD_PROBE_SIDE 1` / `#define CONFIG_BOARD_SOFLE_DONGLE_RIGHT 1` |

→ shield 符号两侧相同（所以**不能**用它区分左右），board 符号两侧不同（**能**用它区分）。

因此：
- `CMakeLists.txt` 里的 `zephyr_library_sources_ifdef(...)`、`zephyr_library_sources(...)` 会照常挂载源文件 —— 换 board 不会漏挂或重复挂载。
- `config/boards/shields/<shield>/<shield>.conf` 里的配置两侧都生效。
- 唯一「副作用」是命名：`left_bbtrackball_dongle` 这个名字里的 `left_` 只是名字，不影响功能，但会误导。建议去掉侧前缀。

### 1.3 ❌ conf 注入的 Kconfig 值**进不了设备树**

实验设计：把引脚号做成 Kconfig（`config PROBE_SIDE_PIN int`），overlay 里按值选引脚：

```
&pinctrl {
    probe_pwm3_default: probe_pwm3_default {
        group1 {
#if CONFIG_PROBE_SIDE_PIN == 3
            psels = <NRF_PSEL(PWM_OUT0, 0, 3)>;
#elif CONFIG_PROBE_SIDE_PIN == 4
            psels = <NRF_PSEL(PWM_OUT0, 0, 4)>;
#else
            psels = <NRF_PSEL(PWM_OUT0, 0, 7)>;
#endif
        };
    };
};
```

`config/sofle_dongle_left.conf` → `CONFIG_PROBE_SIDE_PIN=3`；`config/sofle_dongle_right.conf` → `=4`。

实测：

| 构建 | `.config` 里的值 | `zephyr.dts` 里最终 psels | 走的哪个分支 |
|---|---|---|---|
| 左手 | `CONFIG_PROBE_SIDE_PIN=3` | `0x160007` = port0 pin7 | **`#else`** |
| 右手 | `CONFIG_PROBE_SIDE_PIN=4` | `0x160007` = port0 pin7 | **`#else`** |

**两次都走了 `#else`。** 原因：DTS 由 C 预处理器处理，而 Kconfig 宏（`autoconf.h`）在那个阶段还没生成/不可见，未定义符号被当成 `0`，两个比较都为假。

→ **`psels`、`cs-gpios` 这类设备树引脚号，无法通过 conf 注入。** C 侧可以，DT 侧不行。

### 1.4 ✅ 设备树差异的正确位置：`config/<board>.overlay`

ZMK 原生支持每块板一份根级 overlay。源码 `app/keymap-module/modules/modules.cmake:175-184`（注意 `break()`：只取**第一个存在**的候选）：

```cmake
list(APPEND overlay_candidates "${ZMK_CONFIG}/${s}_${BOARD}.overlay")
list(APPEND overlay_candidates "${ZMK_CONFIG}/${s}.overlay")
list(APPEND overlay_candidates "${ZMK_CONFIG}/${BOARD_DIR_NAME}.overlay")
list(APPEND overlay_candidates "${ZMK_CONFIG}/${BOARD}.overlay")   # ← config/sofle_dongle_left.overlay
list(APPEND overlay_candidates "${ZMK_CONFIG}/default.overlay")
```

实测（探针 shield 只写 `pinctrl-0 = <&probe_pwm3_default>`，**不含任何引脚号**；引脚号写在根级 per-side overlay 里）：

| 构建 | 日志 | 最终 psels |
|---|---|---|
| 左板 + probe_side | `ZMK Config devicetree overlay: .../config/sofle_dongle_left.overlay` | `0x160003` = **port0 pin3** ✅ |
| 右板 + probe_side | `ZMK Config devicetree overlay: .../config/sofle_dongle_right.overlay` | `0x160004` = **port0 pin4** ✅ |

**同一个 shield、同一份源码，两侧拿到各自的引脚号，都编译通过。这就是「左右公用外设固件」的完整骨架。**

### 1.5 ⚠️ 顺手发现：右板 dts 有个悬空引用

`sofle_dongle_right.dts:105-108`：

```
&pwm1 {
    status = "okay";
    pinctrl-0 = <&pwm1_default>;
    pinctrl-1 = <&pwm1_sleep>;
```

但右板 dts 自己**没有定义** `pwm1_default` / `pwm1_sleep`（它的 `&pinctrl` 里只有 pwm0/pwm2/spi2/spi3）。这两个 label 目前只存在于 **shield** 的 overlay 里。实测后果：

```
-b sofle_dongle_right -- -DSHIELD="lpm_view"
→ devicetree error: /soc/pwm@40021000: undefined node label 'pwm1_default'
```

**这就是为什么 1.1 里「左手轨迹球 shield 编到右板」能成功——纯粹因为它碰巧也定义了同名 label**（在 pin 0-7 上；那个引脚在右手座上是否成立是另一回事）。

改法：把 `pwm1_default`/`pwm1_sleep` 从 shield overlay 移到 `config/sofle_dongle_right.overlay`（或右板 dts），让右板自己持有自己的 LED 引脚。

### 1.6 ⚠️ matrix position 号必须按侧注入

`bbtrackball_input_handler.c` 里 `ev->position == 60`（方向键模式）/ `61`（鼠标·空格模式）是**矩阵 map 的索引**，即 `sofle_dongle.dtsi` 里 `map = < ... >` 中该条目的序号。map 的顺序是「左半键 → 右半键」，再叠加 `col-offset`（左 1 / 右 9，见 `matrix_transform.c:78` 的 `column += mt->col_offset`）。

实测算出的对照（position = map 条目序号）：

| 物理位置（行, 该侧 local col） | 左手 position | 右手 position |
|---|---|---|
| 底部行 row4 local col 4 | **60** = `RC(4,5)` | **66** = `RC(4,13)` |
| row1 local col 0 | 15 = `RC(1,1)` | 21 = `RC(1,9)` |
| row1 local col 5 | 20 = `RC(1,6)` | 26 = `RC(1,14)` |

**同一个物理键在两侧 position 不同，而且逐行差值不一样**（每行左右半的条目数不同：row0 半 7+7、row1 6+6、row2 7+7、row3 8+8、row4 5+5，再加上拇指键插在中间）。

→ 所以「在 conf 里定义激活鼠标层的矩阵键位，左右各一份」**是必需的，且必须写死具体数字，不能用统一偏移量换算**。

### 1.7 ⚠️ 中枢按 `reg` 分发指针输入——main 那三个节点其实只有第一个是活的

实测 main 的 dongle 构建产物（`build/REGCHK/zephyr/include/generated/devicetree_generated.h`）：

```
DT_N_INST_0_zmk_input_split  →  split_inputs/trackpoint_split@0    reg = <0>
DT_N_INST_1_zmk_input_split  →  split_inputs/bbtrackpad_split@0    reg = <0>
DT_N_INST_2_zmk_input_split  →  split_inputs/bbtrackball_split@0   reg = <0>
```

> 踩坑记录：实例宏用的是**小写** token（`zmk_input_split`）。grep `DT_N_INST_*_ZMK_INPUT_SPLIT` 会是空的，别据此判定「没有实例」。

`input_split.c:27` 据此生成 `proxy_inputs[] = {trackpoint, bbtrackpad, bbtrackball}`，而 `input_split.c:29-39` 的循环**命中即 `return`**：

```c
for (size_t i = 0; i < ARRAY_SIZE(proxy_inputs); i++) {
    if (reg == proxy_inputs[i].reg) {
        return input_report(proxy_inputs[i].dev, type, code, value, sync, K_NO_WAIT);
    }
}
return -ENODEV;
```

三个 `reg` 全是 0 → **永远命中 i=0，即 `trackpoint_split`**；`bbtrackpad_split` / `bbtrackball_split` 这两个 proxy 永远收不到事件。

而 `reg` 是**外设自己填的**：`central.c:51` 传的是 `ev.data.input_event.reg`（**不是** `source` 外设序号——`source` 只用于按键 position 事件），外设侧 `input_split.c:69` 把它赋成**自己** split 节点的 `DT_INST_REG_ADDR`。

→ **推论 1**：main 里中枢**无法分辨**指针事件来自哪一侧（两侧外设都用 `reg=0`）。
→ **推论 2**：main 现在能正常工作**纯属巧合**——三个 listener 的 `input-processors` 逐字相同（`zip_temp_layer 3 600` + `ip_behaviors0..3`），落到哪个 node 都一样。
→ **推论 3**：`bbtrackpad_listener` / `bbtrackball_listener` 编进了固件却永远收不到事件。dongle RAM 实测 **86.12%**（HEAD 是 84.78%），其中约 2/3 的 listener 内存是白占的。

**想让左右两侧行为不同时**（例如左侧触摸板用一套滚动/临时层参数、右侧轨迹球用另一套）：必须让两侧外设的 `reg` **不同**（左 0 / 右 1），并在中枢声明两个对应 `reg` 的节点。但 `reg` 是**设备树属性**，**Kconfig 注入不了**（见 1.3），只能靠 per-side overlay 去**覆盖 shield 已声明的节点属性**——

> ⚠️ **这一点尚未验证**：1.4 验证的是 per-side overlay **新增** pinctrl 节点，不是**覆盖 shield 声明的已有节点**。后者能否成立取决于 overlay 加载顺序（`config/<board>.overlay` 与 shield overlay 谁先谁后），需要单独测。若顺序不利，退路是让 shield 不在 overlay 里声明 split 节点，改用每侧一份的 `<board>.overlay` 声明（但那样 shield 就不再自洽，得两侧各写一遍）。

---

## 2. 驱动要改什么（C 侧）

当前硬编码，全部要变成 Kconfig 符号：

| 文件 | 硬编码内容 |
|---|---|
| `left_bbtrackball_dongle/custom_driver_left/bbtrackball_input_handler.c` | `DOWN_GPIO_PIN 9`、`LEFT_GPIO_PIN 12`、`UP_GPIO_PIN 5`、`RIGHT_GPIO_PIN 27`、`GPIO0_DEV`/`GPIO1_DEV`、`ARROW_TRIGGER_THRESHOLD 2`、`ARROW_REPEAT_MS 35`、`SCROLL_DIVISOR 6`，以及 `ev->position == 60/61` |
| `right_trackpoint_dongle/custom_driver_right/trackpoint_0x15.c` | `MOTION_GPIO_NODE DT_NODELABEL(gpio0)`、`MOTION_GPIO_PIN 14`、`MOTION_GPIO_FLAGS`、`TOGGLE_POSITION_CODE` |
| `left_bbtrackpad_dongle/custom_driver/a320.c` | `MOTION_GPIO_NODE DT_NODELABEL(gpio0)`、`MOTION_GPIO_PIN 5`、`MOTION_GPIO_FLAGS` |

**注意 GPIO port 也要参数化**：轨迹球的 `DOWN_GPIO_PIN` 在 `gpio1`，其余三个在 `gpio0`。若两侧座子的 port 分配不同，光传引脚号不够，得连 port 一起传（或统一传 `NRF_PSEL` 风格的 `(port<<5)|pin`）。

---

## 3. 落地结构

**当前决策（先落地这一条）**：每侧只编该侧已有定义的设备，得到下面 `build.yaml` 的 **4 个目标**。源码按现状不动，引脚等硬件资料到位后再补（§4.4）。

下面 `config/` 那段是**将来做「一份源码通吃两侧」重构时**的目标形态，不是现在要做的改动。

```
config/
  sofle_dongle.conf              # 两侧共用
  sofle_dongle_left.conf         # 左侧：引脚号、position 常量（Kconfig 注入）
  sofle_dongle_right.conf        # 右侧：同上
  sofle_dongle_left.overlay      # 左侧：座子→MCU 引脚的 pinctrl（psels）
  sofle_dongle_right.overlay     # 右侧：同上
  boards/shields/
    bbtrackball_dongle/          # 去掉 left_ 前缀；overlay 不含任何引脚号
    bbtrackpad_dongle/           # 同上（从 main 来）
    trackpoint_dongle/           # 同上
build.yaml           # 当前决策：每侧只编「该侧已有定义」的设备 = 4 个目标
  - board: sofle_dongle        shield: st7789_display                     # 中枢（设备无关，一份通吃）
  - board: sofle_dongle_left   shield: lpm_view;left_bbtrackball_dongle   # 左：轨迹球
  - board: sofle_dongle_left   shield: lpm_view;left_bbtrackpad_dongle    # 左：触摸板
  - board: sofle_dongle_right  shield: lpm_view;right_trackpoint_dongle   # 右：小红点
```

这 4 个目标恰好就是**厂商发布过的固件组合**（见 4.4）。左半用户刷「轨迹球」或「触摸板」二选一，右半刷「小红点」。dongle **一份通吃**（见 1.7），且用户刷错外设固件只是不工作，不会损坏设备，换一份重刷即可。

命名上建议把 `left_`/`right_` 前缀去掉（`bbtrackball_dongle` / `bbtrackpad_dongle` / `trackpoint_dongle`）——见 1.2：这个名字只是字符串，不影响功能，但会误导。

**实测成本**（容器内 west build，冷配置）：

| 目标 | 时间 | FLASH | RAM |
|---|---|---|---|
| dongle（main，3 listener） | 22.2s | 49.29% | **86.12%** ← 最紧 |
| 单侧外设 | 30–40s | 33–39% | 29–30% |

4 个目标 ≈ **2–3 分钟**一次全量；若再加 reset 固件（HEAD 有 3 个，main 全删了）≈ 4–5 分钟。编译时间完全不是瓶颈。

**要扩到「6 个外设固件」时，卡点是硬件引脚数据**（§4.4），不是编译：右座 / 左座互换设备的引脚源码里根本没有。当前决策先按 4 目标落地，引脚以后再补。

---

## 4. 硬件引脚映射（全部来自源码，不推断）

> 原则：**只记录源码里确实写了的**。没有写的一律留空待补，不做猜测。
> 目前每侧只支持「该侧已经定义过」的设备：**左 = 轨迹球 / 触摸板，右 = 小红点**。

### 4.1 左座 `sofle_dongle_left`

**4 根数据线**（`left_bbtrackball_dongle/custom_driver_left/bbtrackball_input_handler.c:31-37` 定义、`:75-78` 绑定到具体 gpio 控制器——注意 **DOWN 在 `gpio1`，其余三根在 `gpio0`**）：

| 轨迹球功能 | 引脚 | 触摸板复用同一根线做（main 的 `left_bbtrackpad_dongle`） |
|---|---|---|
| UP | **P0.05** | MOTION / DR 中断（`custom_driver/a320.c:71-72`） |
| LEFT | **P0.12** | I2C **SCL**（`left_bbtrackpad_dongle.overlay:114-119`） |
| DOWN | **P1.09** | I2C **SDA**（同上） |
| RIGHT | **P0.27** | 未使用 |

**2 路 LED**：

| 用途 | PWM 节点 | 引脚 | 出处 |
|---|---|---|---|
| 模块主 LED | `pwm1` | **P0.07** | `left_bbtrackball_dongle.overlay:64-77`；板级 `sofle_dongle_left.dts:194-205`（注释 "Backlight for Trackball"，两处同值） |
| 触摸板 custom LED | `pwm3` | **P0.16** | `left_bbtrackpad_dongle.overlay:101-112` |

→ **轨迹球与触摸板复用同一批导线**：P0.05 / P0.12 / P1.09 三根被两个设备各自赋予不同用途。这就是「换模块不用换排线」在源码上的证据。

### 4.2 右座 `sofle_dongle_right`（当前只定义了小红点）

| 功能 | 引脚 | 出处 |
|---|---|---|
| MOTION | **P0.14** | `right_trackpoint_dongle/custom_driver_right/trackpoint_0x15.c:70-72`（`MOTION_GPIO_NODE = gpio0`, `MOTION_GPIO_PIN = 14`） |
| I2C **SDA** | **P0.07** | `right_trackpoint_dongle.overlay:71-76` |
| I2C **SCL** | **P1.08** | 同上 |
| 模块主 LED | **P0.04** | `right_trackpoint_dongle.overlay:85-96`（`pwm1`） |

右座**没有** custom LED（`pwm3`）的定义，右板 dts 里也没有 `pwm3`。

### 4.3 已证实：左右两座不是同一批 MCU 脚

左座用的 P0.05 / P0.12 / P1.09 / P0.27，在**右板上是矩阵的行列线**（`sofle_dongle_right.dts:21-39`：row-gpios 含 `P0.12`、`P1.9`；col-gpios 含 `P0.5`、`P0.27`）。

→ 同一个 MCU 脚不可能既是矩阵线又是模块信号线，所以**两侧模块座分别接到了不同的 MCU 脚**。右座要用轨迹球/触摸板，必须另有一组引脚，这组引脚**源码里目前不存在**（见 4.4）。

### 4.4 待补充（源码中不存在，需硬件资料）

| 需要的东西 | 状态 |
|---|---|
| 右座的轨迹球引脚（4 根方向 + LED） | ❌ 源码无 |
| 右座的触摸板引脚（MOTION + I2C + LED + custom LED） | ❌ 源码无 |
| 左座的小红点引脚 | ❌ 源码无 |
| 「连接器 pin → 左/右板 MCU 脚」对照表 | ❌ 源码无（需原理图） |

**厂商自己也只发布过这 4 个固件的组合**（git 历史 `1b0042f` / `2730dc8` 的 `Normal_firmware/`、`Reset_firmware/`）：

```
Left_sofle_trackball · Left_sofle_trackpad · Right_sofle_trackpoint · Macintosch_dongle
```

即：**左座支持轨迹球+触摸板，右座只支持小红点**，与源码一致，也没有第四种组合的痕迹（`git log --all` 里从未出现过 `right_bbtrackpad` / `right_bbtrackball` / `left_trackpoint` 这类 shield）。

### 4.5 右板引脚占用清点（供将来补引脚时用）

统计口径：构建 `-b sofle_dongle_right -- -DSHIELD="lpm_view;right_trackpoint_dongle"` 后，取**合并设备树**里的 `<&gpioN M>` 与所有 `psels`，**再加上驱动里硬编码的 C 宏**（插座引脚不在设备树里，只看 DT 会漏）。

已占用 **P0（21 个）**：
`03 04 05 06 07 08 09 10 12 13 14 15 17 19 24 26 27 28 29 30 31`

已占用 **P1（8 个）**：
`00 02 04 08 09 10 14 15`

→ **空位 P0（11 个）**：`00 01 02 11 16 18 20 21 22 23 25`
→ **空位 P1（8 个）**：`01 03 05 06 07 11 12 13`

其中 P0.4 / P0.7 / P1.8 同时出现在 DT 与驱动宏里（互为交叉验证），P0.14 只出现在驱动宏里（`MOTION_GPIO_PIN`，设备树不知道）。

> 这 19 个空位只是「物理上还可能接线的引脚」，**不等于**模块座的引脚——哪些真正引到连接器上，必须由硬件资料确定。本节只列事实，不做推断。

---

## 5. 复现命令

```bash
# 1) 按 board 名合并的 conf（modules.cmake:166-181）
sed -n '160,182p' ~/repositories/xa_m/zmk/app/keymap-module/modules/modules.cmake

# 2) 按 board 名加载的根级 overlay（modules.cmake:175-184，注意 break() 只取第一个）
sed -n '173,190p' ~/repositories/xa_m/zmk/app/keymap-module/modules/modules.cmake

# 3) 外设侧 split 的硬性约束
sed -n '49,57p' ~/repositories/xa_m/zmk/app/src/pointing/input_split.c

# 4) col-offset 的作用点
sed -n '70,82p' ~/repositories/xa_m/zmk/app/src/matrix_transform.c

# 5) 算某物理键的 position：数 map 里的条目序号
git show HEAD:config/boards/arm/sofle_dongle/sofle_dongle.dtsi \
  | sed -n '/map = </,/>/p' | grep -oE "RC\([0-9]+, *[0-9]+\)" | nl -v0 -ba

# 6) 跨板构建（示例）
west build -p -d build/x -b sofle_dongle_right -- \
  -DSHIELD="lpm_view bbtrackball_dongle" -DZMK_CONFIG=/workspaces/zmk-config/config

# 7) 查 zmk,input-split 的实例顺序（注意 token 是【小写】，大写只有 compatible 字符串宏）
grep -nE 'DT_N_INST_[0-9]+_zmk_input_split' \
  build/<目标>/zephyr/include/generated/devicetree_generated.h

# 8) 重算某侧引脚占用 + 空位（§4.5 的方法）
#    先构建，再从合并设备树取 <&gpioN M> 与【全部】psels，最后并上驱动里的硬编码宏
west build -p -d build/PINFREE -b sofle_dongle_right -- \
  -DSHIELD="lpm_view;right_trackpoint_dongle" -DZMK_CONFIG=/workspaces/zmk-config/config
python3 - <<'PY'
import re, os
s = open(os.path.expanduser("~/repositories/xa_m/zmk/app/build/PINFREE/zephyr/zephyr.dts")).read()
occ = set()
for m in re.finditer(r'&gpio(\d)\s+(0x[0-9a-fA-F]+|\d+)', s):
    occ.add((int(m.group(1)), int(m.group(2), 0)))
for m in re.finditer(r'psels\s*=\s*([^;]*);', s):          # 必须取整段到分号，否则只抓到每组第一个值
    for v in re.findall(r'0x[0-9a-fA-F]+', m.group(1)):
        val = int(v, 16); occ.add(((val >> 5) & 0x3, val & 0x1F))
# 插座引脚是 C 宏，不在设备树里，必须手动并进来
occ |= {(0,14), (0,7), (1,8), (0,4)}                        # 右座小红点
for q in (0,1):
    n = 32 if q == 0 else 16
    used = {p for (qq,p) in occ if qq == q}
    print(f"P{q} 已占用: {sorted(used)}")
    print(f"P{q} 空位:   {sorted(set(range(n)) - used)}")
PY
```
