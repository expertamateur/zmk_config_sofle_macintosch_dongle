# bbtrackball

黑莓轨迹球模块盾。源码左右通用，插哪一侧由**板子**决定（引脚定义不在这个目录里）：

- 左座：`config/sofle_dongle_left.conf` + `config/sofle_dongle_left.overlay`
- 右座：引脚资料还没有，驱动顶部 `#error` 会让 `right_ball` 直接编译失败

## 语义

默认输出滚轮，按住 `CONFIG_BBTRACKBALL_MOUSE_KEY_POSITION_1/2`（默认 61、62）时输出鼠标移动。
没有方向键功能。

## 编译

推荐直接用仓库根的构建脚本（目标名见 `build.yaml`）：

```bash
zmk_build left_ball
```

等价的 west 调用：

```bash
west build -p -b sofle_dongle_left -- -DSHIELD="lpm_view;bbtrackball"
```
