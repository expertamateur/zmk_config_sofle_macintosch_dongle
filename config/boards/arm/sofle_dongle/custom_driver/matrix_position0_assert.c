/*
 * 0 号位不变式：半板（lf_* / rt_*）上 keymap 的 0 号位必须不可达。
 *
 * Copyright (c) 2025 ZitaoTech
 * SPDX-License-Identifier: MIT
 *
 * 为什么需要它：仓库里只有一套 keymap（config/sofle_dongle.keymap），0 号位
 * 是 dongle 上那颗独立按键。半板没有 dongle，也要能编同一份 keymap —— 靠的是
 * 「半板的矩阵扫描够不着列 0」：
 *
 *   matrix_transform.c:78   column += mt->col_offset;          // 先加偏移
 *   matrix_transform.c:82   lookup_index = row * columns + column;
 *   matrix_transform.c:90   return mt->lookup_table[lookup_index] - 1;
 *
 * 半板有 8 根列线、col_offset 为 1（左）或 9（右），所以 kscan 报上来的列一定
 * 落在 1..8 / 9..16，lookup 下标永远 > 0，而 0 号位正好是 map[0]（= RC(0,0)，
 * 见 sofle_dongle.dtsi）。dongle 自己的扫描是 1×1、没有偏移，0 号位在它上面
 * 反而可达，所以这份断言只编进半板。
 *
 * 将来谁把 col-offset 改没了（或把 map[0] 挪到别的行列），这里直接编译失败，
 * 而不是悄悄把 dongle 的启动键漏到半板的某个实体键上。改完记得同步更新
 * MODULAR_POINTER_ANALYSIS.md 与 lf.dtsi / rt.dtsi 里的注释。
 */

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

/* 无 dongle 时的屏幕/按键布局来自 sofle_dongle.dtsi，两个标签在半板上都存在 */
#define SOFLE_TRANSFORM_NODE DT_NODELABEL(default_transform)
#define SOFLE_KSCAN_NODE     DT_NODELABEL(kscan0)

/* 0 号位就是 map[0]（这个映射表是「按 keymap 位置索引」的，见 matrix_transform.c:47） */
#define SOFLE_TRANSFORM_POSITION0 DT_PROP_BY_IDX(SOFLE_TRANSFORM_NODE, map, 0)

/* 断言消息用英文：GCC 会把 _Static_assert 里的非 ASCII 字符转义成八进制，
 * 中文提示在编译错误里会变成一串 \377…，反而看不懂。解释写在上面和下面的注释里。*/

/* 0 号位一旦挪到别的行列，col-offset 就护不住它了（半板的列 0 不可达，别的列可达） */
BUILD_ASSERT(SOFLE_TRANSFORM_POSITION0 == 0,
             "half board: map[0] must stay RC(0,0) - see config/boards/arm/sofle_dongle/"
             "custom_driver/matrix_position0_assert.c");

/* 核心不变式：没有这条，半板上的实体键就能触发 keymap 的 0 号位（dongle 的独立按键） */
BUILD_ASSERT(DT_PROP(SOFLE_TRANSFORM_NODE, col_offset) >= 1,
             "half board: default_transform needs col-offset >= 1, otherwise matrix column 0 "
             "is reachable and keymap position 0 (the dongle button) fires from a real key - "
             "see config/boards/arm/sofle_dongle/{lf,rt}.dtsi");

/* 位移后不能越过变换表的列数，否则扫描出的查表下标越界 */
BUILD_ASSERT(DT_PROP(SOFLE_TRANSFORM_NODE, col_offset) +
                     DT_PROP_LEN(SOFLE_KSCAN_NODE, col_gpios) <=
                 DT_PROP(SOFLE_TRANSFORM_NODE, columns),
             "half board: col-offset + kscan col-gpios exceeds the transform's columns - "
             "see config/boards/arm/sofle_dongle/{lf,rt}.dtsi");
