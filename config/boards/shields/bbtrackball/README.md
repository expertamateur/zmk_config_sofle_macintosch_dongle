# bbtrackball

黑莓轨迹球模块盾。源码左右通用，插哪一侧由**板子**决定（引脚定义不在这个目录里）：

- 左座：引脚号在 `config/boards/arm/sofle_dongle/Kconfig.defconfig`，
  pinctrl（座子 → MCU 脚的 psels）在 `lf.dtsi`
- 右座：引脚资料还没有，驱动顶部 `#error` 会挡住编译；
  `build.yaml` 里 `rt_*_ball` 那两行是**注释**状态，不会参与编译

引脚表的权威来源（含「为什么查不到」）是仓库根的 `MODULAR_POINTER_ANALYSIS.md §4`。

## 语义

默认输出滚轮，按住 `CONFIG_BBTRACKBALL_MOUSE_KEY_POSITION_1/2`（默认 61、62）时输出鼠标移动。
没有方向键功能。

## 接线的两种角色

同侧 master/slaver 的区别只在**这块盾是插在自己身上还是插在对面**：

- **slaver（外设，有 dongle）**：盾里的 split 代理把事件发给中央
  （`bbtrackball_split`，`device = <&bbtrackball>`）。
- **master（中央，没 dongle）**：本半的球就是板载设备，listener 直接听它
  (`boards/lf_master.overlay`)，同时把代理关掉，免得它抢走对侧上报的事件。
  对侧（右半）的模块代理由 `rt_master.dts` 声明。

## 编译

推荐直接用仓库根的构建脚本（目标名见 `build.yaml`）：

```bash
zmk_build lf_slaver_ball    # 左半当外设（有 dongle）
zmk_build lf_master_ball    # 左半当主机（没 dongle）
```

等价的 west 调用：

```bash
west build -p -b lf_slaver -- -DSHIELD="lpm_view;bbtrackball"
west build -p -b lf_master -- -DSHIELD="lpm_view;bbtrackball"
```
