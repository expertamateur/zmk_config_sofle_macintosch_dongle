# 键盘背光驱动按「半」编：同侧 master/slaver 硬件相同，角色不影响这份代码。
# matrix_position0_assert.c 是纯编译期断言（「0 号位在半板上不可达」），同样按半编：
# 只有半板才需要这个不变式，dongle 自己的 0 号位是可用的那颗键。

if(CONFIG_BOARD_LF_SLAVER OR CONFIG_BOARD_LF_MASTER)
zephyr_library()
zephyr_library_sources(custom_driver/keyboard_backlight.c)
zephyr_library_sources(custom_driver/matrix_position0_assert.c)
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
zephyr_library_include_directories(../../../include)
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
endif()


if(CONFIG_BOARD_RT_SLAVER OR CONFIG_BOARD_RT_MASTER)
  zephyr_library()
  zephyr_library_sources(custom_driver/keyboard_backlight.c)
  zephyr_library_sources(custom_driver/matrix_position0_assert.c)
  zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
  zephyr_library_include_directories(../../../include)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
endif()
