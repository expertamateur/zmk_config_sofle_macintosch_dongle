# main → expert_amateur_set2 合并差异清单

> 生成时间：2026-09-19 · **仅调查记录，未对仓库做任何改动**
>
> 📌 **合并已完成（2026-09-20）。** 本文描述的是**合并前**两边的状态，里面的
> `left_*` / `right_*_dongle` 等路径名是当时的旧名，落地时已改为
> `config/boards/shields/{bbtrackball,bbtrackpad,trackpoint}/`。保留本文只为记录当时的取舍依据。
> 生成方式：`git diff`（两点 / 三点）+ `git merge-tree --write-tree`（无副作用试合并）

---

## 0. 元信息

| 项 | 值 |
|---|---|
| 本分支（接收方） | `expert_amateur_set2` = `a9e0948`，工作树 `zmk_config_sofle_macintosch_dongle` |
| 来源分支 | `main` = `2cd41d1`，工作树 `zmk_config_sofle_macintosch_dongle_main` |
| 共同祖先 | `d685dfe` "Create sofle_dongle.json"（2026-02-12） |
| 分叉后提交数 | main 侧 67 个，本分支侧 73 个 |
| 关系 | **无直系祖先关系**，只能合并或路径级拣选 |

复现命令：

```bash
git merge-base HEAD main                 # -> d685dfe
git diff --stat HEAD main                # 两点：两棵树最终内容的差异
git diff --stat HEAD...main              # 三点：main 相对共同祖先的净变化
git merge-tree --write-tree main HEAD    # 试合并（不落盘），列出冲突
```

### ⚠️ 阅读说明（很重要）

`git diff HEAD main`（两点对比）有两个误导：

1. 会把**本分支删除的文件**显示成「main 新增」（例如 `bunnygirl_anima/*`：那些文件在共同祖先里就有，main 从未动过，是本分支删掉的）。
2. 会把**仅本分支存在的文件**显示成「main 删除」（例如 `.gitignore`、`zmk_build`、`MERGE_DECISIONS.md`）。

判断合并结果**必须以三方合并为准**。本文档已按三方合并行为分类，而不是按两点 diff。

---

## 1. 分类总览

| 类别 | 文件数 | 合并时的行为 |
|---|---|---|
| **[M] 仅 main 改过**（本分支未动） | 36 | 干净并入，覆盖本分支同名文件 |
| **[H] 仅本分支改过**（main 未动） | 36 | 原样保留（含本分支的删除动作） |
| **[B-同] 双方都改、最终内容相同** | 13 | 无动作 |
| **[B-自动] 双方都改、git 自动合并** | 1 | 生成混合体，需人工确认（实际无害） |
| **[B-冲突] 双方都改、内容冲突** | 14 | **需要逐个决策** |

---

## 2. [M] 可直接并入 —— main 单方面更新

### 2.1 `st7789_display/` 屏幕驱动重写（11 文件）★ 主要价值

这就是你说的「屏幕驱动更新」：上游把 Macintosh 风格状态屏整体重写了。

| 文件 | 行数变化 |
|---|---|
| `widgets/layer_status.c` | +217 −123 |
| `widgets/output_status.c` | +137 −40 |
| `widgets/action_button.c` | +117 −24 |
| `widgets/battery_status.c` | +100 −51 |
| `widgets/modifier.c` | +49 −46 |
| `widgets/splash.c` | +46 −69 |
| `widgets/helpers/display.c` | +34 −1 |
| `custom_status_screen.c` | +17 −3 |
| `widgets/helpers/display.h` | +9 −1 |
| `widgets/splash.h` | +4 −3 |
| `widgets/layer_status.h` | +3 −1 |

**可整包采用的依据**：该 shield 内**其余文件两边完全一致**（`CMakeLists.txt`、`st7789_display.conf`、`st7789_display.overlay`、`Kconfig.shield`、`widgets/wpm.c`、`snake_image.h`、`display_backlight.c` 等），所以这 11 个文件构成一个自洽集合，不会出现「新代码引用到两边不一致的头文件」。

注意 `widgets/logo.c` **不在**这 11 个里 —— 那是**本分支**的改动（+1 −1），main 没动。

### 2.2 `left_bbtrackpad_dongle/` 全新 shield（12 文件，共同祖先中不存在）

| 文件 | 行数 |
|---|---|
| `custom_driver/a320.c` | +513 |
| `custom_driver/trackpad_led.c` | +186 |
| `left_bbtrackpad_dongle.overlay` | +129 |
| `Kconfig.shield` | +121 |
| `custom_driver/custom_led.c` | +74 |
| `custom_driver/a320.h` | +27 |
| `custom_driver/trackpad_led.h` | +26 |
| `left_bbtrackpad_dongle.zmk.yml` | +8 |
| `CMakeLists.txt` | +7 |
| `left_bbtrackpad_dongle.conf` | +5 |
| `Kconfig.defconfig` | +3 |
| `README.md` | +1 |

⚠️ 这是**左手触控板**（A320 传感器）的 shield，和你现在用的 `left_bbtrackball_dongle`（轨迹球）是两条不同的硬件路线。**光新增这些文件不会生效**，还需要配套改：

- `build.yaml`（左手目标换成 `lpm_view;left_bbtrackpad_dongle`）
- `sofle_dongle.dts`（新增 `bbtrackpad_split` + `bbtrackpad_listener`，属冲突文件，见 §5.2）
- keymap 层配置

### 2.3 左右半 dts / defconfig 精简

| 文件 | main 的改动 | 影响 |
|---|---|---|
| `sofle_dongle_left.dts` | 删除 `bbtrackball_split` + `bbtrackball_listener` 声明（−21 行） | 外设侧不再自带声明，改为中枢统一声明，见 §5.2（实测不影响构建） |
| `sofle_dongle_right.dts` | 删除 `trackpoint_split` + `trackpoint_listener` 声明（−22 行） | 同上 |
| `sofle_dongle_left_defconfig` | + `CONFIG_INPUT_THREAD_STACK_SIZE=2048` | 无害（你 `conf` 里已有） |
| `sofle_dongle_right_defconfig` | 空白行 / 注释整理 | 无害 |

### 2.4 `build.yaml` ⚠️ 会直接覆盖你的构建矩阵

main 版相对你的：

- 左手 `lpm_view;left_bbtrackball_dongle` → **`lpm_view;left_bbtrackpad_dongle`**
- 全部 `settings_reset` 目标被注释掉

你现在的左手是**轨迹球**。若照抄 main 的 `build.yaml`，`zmk_build left` 会变成构建触控板，**轨迹球左手不再产出固件**，reset 固件也一起消失。这一项属于 [M] 类会被自动覆盖，必须单独决策。

### 2.5 固件 blob（8 文件）

`Normal_firmware/`、`Reset_firmware/` 下的 `.uf2`（约 3.2MB），共同祖先中没有，main 全新引入。与本仓库自己的 `zmk_build` 流程无关，建议不纳入。

---

## 3. [H] 本分支独有 —— 合并后原样保留

### 3.1 本分支修改 / 新增（8 个）

| 文件 | 本分支的改动 |
|---|---|
| `lpm_view/display_driver/lpm009m360a.c` | +86 −135（重写显示驱动） |
| `right_trackpoint_dongle/custom_driver_right/custom_led.c` | +76 −60（改事件驱动 + `CONFIG_CUSTOM_LED_BRT_DEFAULT`；main 侧仍是轮询 + `BRT_MIN`） |
| `st7789_display/widgets/logo.c` | +1 −1 |
| `custom_driver_right/ARCHITECTURE.md` | 新增 +103 |
| `config/bt_force_macros.dtsi` | 新增 +16 |
| `.gitignore` | 新增 +3 |
| `MERGE_DECISIONS.md` | 新增 +178 |
| `zmk_build` | 新增 +263 |

### 3.2 本分支**删除**的文件（28 个）→ 合并后仍保持删除

| 文件 | 说明 |
|---|---|
| `lpm_view/hello_world.c` | 上游示例文件 |
| `lpm_view/widgets/bunnygirl_anima/`（25 个 `.c`） | 你已改为 `picture/` 随机图切换 |
| `lpm_view/widgets/landspace/landspace1.c` | 同上 |
| `right_trackpoint_dongle/Kconfig.defconfig` | 空文件 |

这些文件在共同祖先里存在、main 从未改动，是你删除的。三方合并会**保留删除**，不会把兔女郎动画带回来。

---

## 4. [B] 双方都改过的文件

### 4.1 最终内容完全相同（13）—— 无需处理

`bbtrackball_input_handler.h`、`lpm_view/CMakeLists.txt`、`lpm_view/widgets/picture/{astronaut,blackhole,cat,david,macintosch,mounta,plane,vader}.c`、`config/sofle_dongle.json`、`.github/workflows/draw.yml`、`.github/workflows/main.yml`

（双方做了同样的改动，或者一方改动被另一方覆盖成同一结果）

### 4.2 git 会自动合并、但结果是混合体（1）

`config/boards/shields/left_bbtrackball_dongle/custom_driver_left/trackball_led.c`

双方各改 1 行，冲突点是注释分隔符（`/* ====...`）。**无功能影响**，可接受自动合并结果。

### 4.3 冲突（14）—— 需要决策

| # | 文件 | 双方差异 | 性质 | 初步建议 |
|---|---|---|---|---|
| 1 | `config/sofle_dongle.conf` *(add/add)* | 你 162 行全套调参（去抖 15ms、trackpoint 全参数、LED/DPI）vs main 2 行（去抖 10ms） | **你的定制** | **必须保留你的** |
| 2 | `config/sofle_dongle.keymap` *(add/add)* | 你 5 层 `default/num_syb/direction/fn/mouse` vs main 上游默认 `QWERTY/LOWER/RAISE/MOUSE/RES` | **你的键位** | **必须保留你的** |
| 3 | `keymap-drawer/sofle_dongle.svg` / `.yaml` *(add/add)* | main 那份画的是上游默认键位 | 生成物 | 保留流程，合并后由 draw workflow 重新生成 |
| 4 | `config/boards/arm/sofle_dongle/sofle_dongle.dts` | 见 §5.2、§5.3 | **架构冲突** | 待决策 |
| 5 | `left_bbtrackball_dongle/left_bbtrackball_dongle.overlay` | 见 §5.2 | **架构冲突** | 待决策 |
| 6 | `right_trackpoint_dongle/custom_driver_right/trackpoint_0x15.c` | 你 383 行 vs main 656 行，见 §5.1 | **两种产品思路** | 保留你的 |
| 7 | `right_trackpoint_dongle/Kconfig.shield` | main 缺 `TRACKPOINT_TOGGLE_KEY_POSITION`、`START_IN_SCROLL_MODE`、`CUSTOM_LED_BRT_*`；默认值 DEADZONE 1→2、SLOW 24→60、FAST 6→8 | 与 §5.1 绑定 | 保留你的 |
| 8 | `left_bbtrackball_dongle/.../bbtrackball_input_handler.c` | 你 286 行 vs main 334 行；main 多 `hid_indicators_listener`(CapsLock)、`space_listener_cb`、阈值 4；你阈值 2、有 `trackball_key_listener_cb` | 同 §5.1 的取舍 | 保留你的 |
| 9 | `config/boards/arm/sofle_dongle/custom_driver/keyboard_backlight.c` | 你：事件驱动 + 去掉 WPM + RGB 边缘检测；main：轮询 + WPM 计算 | 你上一轮的有意简化 | 保留你的 |
| 10 | `config/boards/arm/sofle_dongle/sofle_dongle_defconfig` | main 只多 `CONFIG_INPUT_THREAD_STACK_SIZE=2048` + 1 行注释 | 你 `conf` 里已有 2048 | 保留你的 |
| 11 | `config/boards/arm/sofle_dongle/sofle_dongle.keymap` | main 上游默认 vs 你的 `default/func/RES` | **回退键位**，实际不生效 | 保留你的 |
| 12 | `lpm_view/widgets/peripheral_status.c` | 差异极小：main 多注释 + `current_img_index = 0` / `work_initialized = false` 初始化 | 无功能差异 | 二选一 |
| 13 | `README.md` | main 是上游文档（约 196 行差异），你记录自己的内容 | 文档 | 待决策 |

> 关于 #11：已确认**实际生效**的 keymap 是 `config/sofle_dongle.keymap`（你那份），`config/boards/arm/sofle_dongle/sofle_dongle.keymap` 只是回退。依据：`keymap-drawer/sofle_dongle.yaml` 的默认层内容（`&bootloader, ESC, &td_f1_f11, &td_f2_f12, ...`）与你的 `config/sofle_dongle.keymap` 一致，而与 board 那份不一致。

---

## 5. 三个必须决策的技术点

### 5.1 `trackpoint_0x15.c`：不是新旧版本，是两种设计

| | 你的分支 | main |
|---|---|---|
| 行数 | 383 | 656 |
| 模式切换 | 单键 toggle（`CONFIG_TRACKPOINT_TOGGLE_KEY_POSITION`）+ `CONFIG_TRACKPOINT_START_IN_SCROLL_MODE` | 无这两项配置 |
| 方向键模式 | 无 | `process_arrow_axis()` + `ARROW_DEADZONE` |
| 慢速键 | 无 | `slow_key_pressed`（矩阵 pos 36，×0.5） |
| CapsLock 感知 | 无 | `hid_indicators_listener()` + `HID_INDICATORS_CAPS_LOCK` |
| 滚轮曲线 | t² 除数曲线 | t² 除数曲线（思路相同） |
| 批量读 | 有（你上一轮做的批量读 + 节拍发送） | 有（最多 5 包） |

这正是 `MERGE_DECISIONS.md` 第 1 节记录的选择（「TrackPoint 模式切换 = Dongle 单键 toggle」「**不添加**方向键模式、慢速键、CapsLock 监听」）。

⚠️ 若改采 main 版：`config/sofle_dongle.conf` 里的 `CONFIG_TRACKPOINT_TOGGLE_KEY_POSITION`、`CONFIG_TRACKPOINT_START_IN_SCROLL_MODE` 会失效（main 的 `Kconfig.shield` 里没有这两项），滚轮默认值也会变。

### 5.2 input-processors 挂载架构：两套，不能混

| | 你的分支 | main |
|---|---|---|
| `bbtrackball_split` 声明位置 | `sofle_dongle.dts` **和** `sofle_dongle_left.dts` 各一份 | 只在中枢 `sofle_dongle.dts` |
| listener 的 `device` | overlay 里 `device = <&bbtrackball>`（直连外设侧节点） | `device = <&bbtrackball_split>`（走 `split_inputs`） |
| `input-processors` 挂在哪 | 只在 `left_bbtrackball_dongle.overlay`，**1 个 listener** | 中枢 `sofle_dongle.dts` 里的 **3 个 listener**（trackpoint / trackpad / trackball） |
| 新增节点 | — | `bbtrackpad_split`、`bbtrackpad_listener`、`MOUSE_LAYER_ID 3` |

天真合并会同时保留两套写法 → 同一个 listener 被配置两次，必然编译失败。

✅ **已实测：你的这套架构没有冲突**。本分支里 `sofle_dongle_left.dts` 与 `left_bbtrackball_dongle.overlay` 都定义了 `bbtrackball_listener`，但两者指向的是**同一个节点路径** `/bbtrackball_listener`，dtc 接受这种重复声明（只在同一 label 挂到不同路径时才报错）。见 §7.1 的构建结果。

所以 main 的这次「集中化」是**结构/风格上的选择，不是在修 bug**：它把三路输入设备的声明统一收到中枢，代价是外设侧不再自带声明。是否跟随，取决于你想不想要 trackpad。

另外：本分支里 `ip_behaviors0~3`、`zip_temp_layer` **只在那一个 overlay 里被引用**，小红点侧（中枢 dts）没有挂。如果方向键 / 临时层是你想在小红点上用的功能，这里可能是**漏挂**。

### 5.3 `zip_temp_layer 3 600` 的层号对不上

- main 的 dts 写 `zip_temp_layer 3 600`；在 **main 的 keymap** 里层 3 = `MOUSE_layer`。
- 你的**生效 keymap** 层序：`0 default` / `1 num_syb` / `2 direction` / **`3 fn_layer(func)`** / **`4 mouse_layer(mouse)`**。
- 所以照抄 main 的 dts 会变成「推小红点 / 滚球 600ms 内临时切到 **func 层**」，而不是 mouse 层。
- 你自己的 `left_bbtrackball_dongle.overlay` 里同样写的是 `3 600`。如果本意是切 mouse 层，**这个 `3` 现在就需要复查**。

---

## 6. 待决策清单

| 编号 | 问题 | 选项 | 你的决定 |
|---|---|---|---|
| D1 | 合并范围 | ①只取 [M]（含屏幕驱动，冲突全保留本地）②再加 trackpad shield ③全量 main ④只取 st7789 | |
| D2 | dts / listener 架构（§5.2） | ①保留你的（实测可构建）②采用 main 的架构（想上 trackpad 就得走这条）③混合 | |
| D3 | `build.yaml`（§2.4） | ①保留你的 ②取 main 的 ③手工合成（两个左手目标都留） | |
| D4 | `README.md` | ①保留你的 ②取 main 的 ③手工合成 | |
| D5 | `keymap-drawer/*` | ①合并后重新生成 ②取 main 的 | |
| D6 | 固件 blob（§2.5） | ①不纳入 ②纳入 | |
| D7 | `trackpoint_0x15.c`（§5.1） | ①保留你的 ②改采 main 的（含方向键/慢速键/CapsLock） | |
| D8 | `zip_temp_layer` 层号 `3`（§5.3） | ①维持 `3` ②改成 `4`（mouse 层） | |

---

## 7. 验证记录

### 7.1 `zmk_build left` 实测（验证 §5.2 的 label 重复疑点）

- 命令：`./zmk_build left > /tmp/zmk_build_left.log 2>&1`（在本分支当前代码上执行，未做任何代码改动）
- 结果：**编译成功**
  - `[OK] left 编译成功 -> build/left/lpm_view;left_bbtrackball_dongle-sofle_dongle_left-zmk.uf2`
  - FLASH 33.27%（287532 / 844 KB），RAM 28.93%（75828 / 256 KB）
  - 无 `Duplicate label` 错误
- 结论：**§5.2 的疑点不成立**，你现在的 input-processors 架构是能正常构建的。

### 7.1.1 本次构建顺带看到的情况（与本主题无关，仅记录）

| 现象 | 说明 |
|---|---|
| `config/sofle_dongle.conf` 被合并进**左手（外设）**构建 | 日志显示它和 `sofle_dongle_left_defconfig`、`lpm_view.conf`、`left_bbtrackball_dongle.conf` 一起 merged。即这份 conf 对**所有**目标是全局生效的 —— 所以拿 main 那 2 行的版本替换它，影响面不止 dongle |
| `ZMK_USB` 被赋 `y` 但实得 `n` | 外设侧不提供 USB，属预期 |
| `ZMK_WIDGET_BATTERY_STATUS_SHOW_PERCENTAGE=y` 但 `ZMK_WIDGET_BATTERY_STATUS=n` | 依赖不满足，被忽略；外设侧无电池显示，属预期 |
| `ZMK_IDLE_SLEEP_TIMEOUT=1800000` 被忽略（`ZMK_SLEEP=n`） | 与你 `conf` 里「关闭自动休眠」的意图一致 |
| `LOG_PROCESS_THREAD_STARTUP_DELAY_MS` 被忽略 | 依赖不满足，属预期 |
| `lpm009m360a.c:235:25: warning: braces around scalar initializer` | **你自己改过的那份**显示驱动里的写法警告，不影响构建，可顺手清掉 |

### 7.2 合并后的验收步骤（建议）

```bash
git switch -c merge-main-<日期>            # 在新分支上做，不动 expert_amateur_set2
# ... 合并 / 拣选 ...
./zmk_build all > /tmp/zmk_build.log 2>&1
grep -E "(error|warning|FAILED|成功|完成|firmware)" /tmp/zmk_build.log
```

（按 `CLAUDE.md` 约定：输出重定向到日志再用 grep 过滤；日志文件需 gitignore。）

---

## 8. 附：三种合并方式对比（供 D1 参考）

| 方式 | 做法 | 优点 | 风险 |
|---|---|---|---|
| **路径级拣选** | `git checkout main -- <[M] 类路径>`，其余不动 | 精确、可回退、不产生 merge commit | 路径要一条条确认；`sofle_dongle.dts` 这类 [B-冲突] 文件不能直接 checkout |
| **完整 merge** | `git merge main` + 手工解 14 个冲突 | 历史完整，一次收敛 | 冲突集中在架构差异（§5.2），解错就编译不过 |
| **最小改动** | 只 `git checkout main -- config/boards/shields/st7789_display/` | 改动面最小，只吃屏幕驱动 | 放弃了 trackpad、左右半 dts 精简等 |
