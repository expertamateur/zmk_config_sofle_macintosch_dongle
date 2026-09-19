# trackpoint

IBM 小红点（TrackPoint，i2c 0x15）模块盾。源码左右通用，插哪一侧由**板子**决定（引脚定义不在这个目录里）：

- 右座：引脚号在 `config/boards/arm/sofle_dongle/Kconfig.defconfig`，
  pinctrl（座子 → MCU 脚的 psels）在 `rt.dtsi`
- 左座：引脚资料还没有，驱动顶部 `#error` 会挡住编译；
  `build.yaml` 里 `lf_*_point` 那两行是**注释**状态，不会参与编译

引脚表的权威来源（含「为什么查不到」）是仓库根的 `MODULAR_POINTER_ANALYSIS.md §4`。

## 语义

默认输出滚轮，按住 `CONFIG_TRACKPOINT_MOUSE_KEY_POSITION_1/2`（默认 61、62）时输出鼠标移动。
没有方向键功能。三个指点设备共用这一套语义。

主控轴防误触（`CONFIG_TRACKPOINT_DOMINANT_NUMERATOR/DENOMINATOR`，默认 3/2）来自本分支：
一条轴的位移超过另一条的这个比例时锁成单轴，屏蔽对角线抖动。触摸板后来也照着做了
（它自己那套 `CONFIG_A320_DOMINANT_*`），轨迹球没有。

## 接线的两种角色

同侧 master/slaver 的区别只在**这块盾是插在自己身上还是插在对面**：

- **slaver（外设，有 dongle）**：盾里的 split 代理把事件发给中央
  （`trackpoint_split`，`device = <&trackpoint>`）。
- **master（中央，没 dongle）**：右半的小红点就是板载设备，listener 直接听它
  (`boards/rt_master.overlay`)，同时把代理关掉，免得它抢走对侧上报的事件。
  对侧（左半）的模块代理由 `lf_master.dts` 声明。

## 编译

推荐直接用仓库根的构建脚本（目标名见 `build.yaml`）：

```bash
zmk_build rt_slaver_point   # 右半当外设（有 dongle）
zmk_build rt_master_point   # 右半当主机（没 dongle）
```

等价的 west 调用：

```bash
west build -p -b rt_slaver -- -DSHIELD="lpm_view;trackpoint"
west build -p -b rt_master -- -DSHIELD="lpm_view;trackpoint"
```
