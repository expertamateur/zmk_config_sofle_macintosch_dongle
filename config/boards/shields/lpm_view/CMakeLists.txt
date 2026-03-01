if(CONFIG_ZMK_DISPLAY AND CONFIG_NICE_VIEW_WIDGET_STATUS)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  
  # 【保留驱动部分】保证屏幕硬件能正常点亮工作
  zephyr_library_sources(widgets/bolt.c)
  zephyr_library_sources(widgets/util.c)
  zephyr_library_sources(display_driver/lpm009m360a.c)

  # 【增加宏开关控制UI部分】
  # 只有当开启自定义屏幕 (CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y) 时，才编译下面这些会冲突的UI文件
  if(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
    zephyr_library_sources(custom_status_screen.c)
    
    if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
      zephyr_library_sources(widgets/status.c)
    else()
      zephyr_library_sources(widgets/art.c)
      zephyr_library_sources(widgets/peripheral_status.c)
      zephyr_library_sources(widgets/landspace/landspace1.c)
      zephyr_library_sources(widgets/bunnygirl_anima/bunnygirl1.c)
      zephyr_library_sources(widgets/bunnygirl_anima/bunnygirl3.c)
      zephyr_library_sources(widgets/bunnygirl_anima/bunnygirl6.c)
      zephyr_library_sources(widgets/bunnygirl_anima/bunnygirl9.c)
      zephyr_library_sources(widgets/bunnygirl_anima/bunnygirl12.c)
      zephyr_library_sources(widgets/bunnygirl_anima/bunnygirl15.c)
      zephyr_library_sources(widgets/bunnygirl_anima/bunnygirl18.c)
      zephyr_library_sources(widgets/bunnygirl_anima/bunnygirl21.c)
    endif()
  endif()

endif()