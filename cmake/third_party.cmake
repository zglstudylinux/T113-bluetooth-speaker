# cmake/third_party.cmake — LVGL / lv_drivers 静态库（vendor 源码零改动）
#
# 等价性说明（与旧 Makefile include 的 *.mk 逐一对过）：
# - lvgl.mk  = src/{core,draw,hal,misc,font,widgets} 的显式列表
#              + `find src/extra -name '*.c'`（整个 extra 递归）
#              → 等价于 GLOB_RECURSE lvgl/src/**.c（186 个 .c 全量）
# - lv_drivers.mk = 根目录 + wayland/ + indev/ + gtkdrv/ + display/ + sdl/
#              （win32drv 不在 .mk 里，保持排除；sdl/wayland 等文件因
#               lv_drv_conf.h 宏全关而编译成空 TU，与现状一致、无害）
#
# lodepng 宏：lodepng.c 的 `#if LV_USE_PNG` 不经过 lv_conf.h（include 链断了），
# 必须命令行注入 —— 由顶层 CMakeLists 的 add_compile_definitions 提供（勿删）。

# ---------------- lvgl ----------------
file(GLOB_RECURSE LVGL_SOURCES ${PROJECT_SOURCE_DIR}/third_party/lvgl/src/*.c)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl SYSTEM PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party            # lvgl.h 在 third_party/lvgl/…，源码里 #include "lvgl/lvgl.h"
)
# lv_conf.h 发现机制：lvgl 的 lv_conf_internal.h 用 __has_include("lv_conf.h")，
# src/ 在 include 路径里即可命中（与旧 Makefile 的 -I src 等价）
target_include_directories(lvgl SYSTEM PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party/lvgl/src/extra/libs/freetype   # lv_freetype.c 需要同目录头
    ${PROJECT_SOURCE_DIR}/third_party/freetype/include)              # ft2build.h（lv_freetype.c 用）

# ---------------- lv_drivers ----------------
set(LV_DRIVERS_DIR ${PROJECT_SOURCE_DIR}/third_party/lv_drivers)
set(LV_DRIVERS_SOURCES)
foreach(_d . wayland indev gtkdrv display sdl)   # 与 lv_drivers.mk 的 6 处 wildcard 一致，勿加 win32drv
    file(GLOB _s ${LV_DRIVERS_DIR}/${_d}/*.c)
    list(APPEND LV_DRIVERS_SOURCES ${_s})
endforeach()
add_library(lv_drivers STATIC ${LV_DRIVERS_SOURCES})
target_include_directories(lv_drivers SYSTEM PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party            # lv_drv_conf.h 在 third_party/ 下（相对上级目录查找）
)
target_link_libraries(lv_drivers PUBLIC lvgl)

# ---------------- freetype（vendor 预编译 .so + 头文件）----------------
add_library(freetype SHARED IMPORTED GLOBAL)
set_target_properties(freetype PROPERTIES
    IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/third_party/freetype/lib/libfreetype.so
    INTERFACE_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/third_party/freetype/include)

# ---------------- btmanager 全家桶（vendor 预编译库，链接顺序=踩坑固化，勿动）----------------
# 依赖链：libbtmg → libshared-mainloop(静态) + libbluetooth-internal(静态) + libwirelesscom
#         wirelesscom → json-c/dbus；gobject → ffi
set(BT_LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/bt/lib)
add_library(bt INTERFACE)
target_include_directories(bt INTERFACE ${PROJECT_SOURCE_DIR}/third_party/bt/include)
# bz2/z 的 vendor 副本在 freetype/lib（工具链 sysroot 没有，旧 Makefile 同样靠 -L 搜到）
target_link_directories(bt INTERFACE ${BT_LIB_DIR}
    ${PROJECT_SOURCE_DIR}/third_party/freetype/lib)
target_link_libraries(bt INTERFACE
    btmg shared-mainloop bluetooth-internal wirelesscom
    gio-2.0 gobject-2.0 gmodule-2.0 glib-2.0 ffi
    dbus-1 asound json-c sbc
    readline ncursesw
    z bz2 pthread rt dl)
