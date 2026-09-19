# trackpoint

IBM 小红点（TrackPoint，i2c 0x15）模块盾。源码左右通用，插哪一侧由**板子**决定（引脚定义不在这个目录里）：

- 右座：`config/sofle_dongle_right.conf` + `config/sofle_dongle_right.overlay`
- 左座：引脚资料还没有，驱动顶部 `#error` 会让 `left_point` 直接编译失败

## 语义

默认输出滚轮，按住 `CONFIG_TRACKPOINT_MOUSE_KEY_POSITION_1/2`（默认 61、62）时输出鼠标移动。
没有方向键功能。三个指点设备共用这一套语义。

## 编译

推荐直接用仓库根的构建脚本（目标名见 `build.yaml`）：

```bash
zmk_build right_point
```

等价的 west 调用：

```bash
west build -p -b sofle_dongle_right -- -DSHIELD="lpm_view;trackpoint"
```
