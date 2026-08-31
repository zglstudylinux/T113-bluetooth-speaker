# T113 蓝牙音箱 — 顶层 CMake（阶段4：分层目录定稿）
#
# 目录分层（依赖只向下，详见 docs/architecture.md §3）：
#   core/     事件契约（纯 C99）
#   osal/     OS 抽象（posix 实现）
#   services/ 业务后端（btmg / sim）
#   ui/       界面（themes/liquidglass 主题）
#   ports/    板级移植（fb/evdev/freetype/main）+ lv_conf.h
#   apps/     组装层（队列 + drain + 选后端/主题）
#
# 用法：
#   cmake -B build-cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/openwrt-armhf.cmake
#   cmake --build build-cmake -j
# 产物 build/bt_speaker（deploy.sh 零改动）。
#
# ⚠ 勿动清单（与旧 Makefile 逐项等价，踩坑固化，详见 docs/architecture.md §7.2）：
#   - ABI: armv7-a/neon/hard-float（toolchain 文件内）
#   - LV_USE_PNG/LV_USE_FS_POSIX 命令行宏（lodepng include 链断了，CFLAGS 兜底）
#   - 链接顺序 -lbtmg -lshared-mainloop …（cmake/third_party.cmake 的 bt INTERFACE 库）
#   - lv_conf.h 发现：ports/ 在 include 路径里（lvgl 用 __has_include("lv_conf.h")）
cmake_minimum_required(VERSION 3.16)
project(bt_speaker C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_C_STANDARD 99)

# lodepng.c / lv_fs_posix.c 的宏开关不经 lv_conf.h（M4a 踩坑，CFLAGS 兜底勿删）
add_compile_definitions(LV_USE_PNG=1 LV_USE_FS_POSIX=1)

# 与旧 Makefile 相同的告警集（vendor 代码量大，不升警级别，避免噪音）
add_compile_options(-Wall -Wno-unused-function -Wno-unused-parameter
    -Wno-missing-prototypes -Wno-sign-compare -Wno-format-nonliteral)

# ---------------- host 环境（编译器非交叉前缀）只编可移植层自测 ----------------
# 判据：gcc/cc/clang 裸名 = host gcc。目标板主程序依赖 vendor ARM 库（lvgl 静态库
# 在 host 链接 libfreetype.so ARM 版会报 wrong format），host 环境整块跳过。
# 用法：cmake -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure
get_filename_component(_cc_name "${CMAKE_C_COMPILER}" NAME)
if(_cc_name MATCHES "^(gcc|cc|clang)(-[0-9.]+)?$")
    add_library(osal STATIC osal/osal_posix.c)
    target_include_directories(osal PUBLIC
        ${PROJECT_SOURCE_DIR}/osal ${PROJECT_SOURCE_DIR}/core)
    find_package(Threads REQUIRED)
    target_link_libraries(osal PUBLIC Threads::Threads)

    add_executable(osal_test osal/osal_test.c)
    target_include_directories(osal_test PRIVATE
        ${PROJECT_SOURCE_DIR}/osal ${PROJECT_SOURCE_DIR}/core)
    target_link_libraries(osal_test PRIVATE osal)
    target_compile_options(osal_test PRIVATE -Wall -Wextra -Werror)
    enable_testing()
    add_test(NAME osal_test COMMAND osal_test)
    message(STATUS "host 编译器：只构建可移植层自测（osal_test）；目标板用交叉 toolchain 文件构建")
    return()
endif()

include(cmake/third_party.cmake)

# ---------------- osal（目标板） ----------------
add_library(osal STATIC osal/osal_posix.c)
target_include_directories(osal PUBLIC
    ${PROJECT_SOURCE_DIR}/osal
    ${PROJECT_SOURCE_DIR}/core)
find_package(Threads REQUIRED)
target_link_libraries(osal PUBLIC Threads::Threads)

# ---------------- 目标板主程序（交叉工具链） ----------------
add_executable(bt_speaker
    ports/main_linux.c
    ports/lv_port_disp.c
    ports/lv_port_indev.c
    ports/lv_port_font.c
    apps/app_player.c
    services/btmg/btmg_player.c
    ui/themes/liquidglass/ui_liquidglass.c)

target_include_directories(bt_speaker PRIVATE
    ${PROJECT_SOURCE_DIR}/osal                                 # osal.h
    ${PROJECT_SOURCE_DIR}/core                                 # player_types.h
    ${PROJECT_SOURCE_DIR}/services                             # player_backend.h
    ${PROJECT_SOURCE_DIR}/ui                                   # ui_backend.h
    ${PROJECT_SOURCE_DIR}/ports                                # lv_port_*.h + lv_conf.h 发现
    ${PROJECT_SOURCE_DIR}/third_party                          # lvgl.h / lv_drivers 头
    ${PROJECT_SOURCE_DIR}/third_party/lvgl/src/extra/libs/freetype   # lv_freetype.h（lvgl 树旧 API）
    ${PROJECT_SOURCE_DIR}/third_party/freetype/include         # ft2build.h
    ${PROJECT_SOURCE_DIR}/third_party/bt/include               # bt_manager.h
)

target_link_libraries(bt_speaker PRIVATE lvgl lv_drivers freetype bt osal m)
