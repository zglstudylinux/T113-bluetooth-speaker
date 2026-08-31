# T113 蓝牙音箱 — 顶层 CMake（阶段1：源码不动，仍编 src/ 三件套）
#
# 用法：
#   cmake -B build-cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/openwrt-armhf.cmake
#   cmake --build build-cmake -j
# 产物仍为 build/bt_speaker（deploy.sh 零改动）。旧 Makefile 与本文件并存过渡，
# 两者各自维护独立 build 目录（make 用 build/，cmake 中间目录用 build-cmake/），
# 产物同名同路径——先 make 再 cmake --build 会覆盖，属预期。
#
# ⚠ 阶段1构建约定（与旧 Makefile 逐项等价，勿动清单见 docs/architecture.md §7.2）：
#   - ABI: armv7-a/neon/hard-float（toolchain 文件内）
#   - LV_USE_PNG/LV_USE_FS_POSIX 命令行宏（lodepng include 链断了，CFLAGS 兜底）
#   - 链接顺序 -lbtmg -lshared-mainloop …（cmake/third_party.cmake 的 bt INTERFACE 库）
cmake_minimum_required(VERSION 3.16)
project(bt_speaker C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_C_STANDARD 99)

# lodepng.c / lv_fs_posix.c 的宏开关不经 lv_conf.h（M4a 踩坑，CFLAGS 兜底勿删）
add_compile_definitions(LV_USE_PNG=1 LV_USE_FS_POSIX=1)

# 与旧 Makefile 相同的告警集（vendor 代码量大，不升警级别，避免噪音）
add_compile_options(-Wall -Wno-unused-function -Wno-unused-parameter
    -Wno-missing-prototypes -Wno-sign-compare -Wno-format-nonliteral)

include(cmake/third_party.cmake)

# ---------------- osal（目标板 + host 自测共用的 OS 抽象） ----------------
add_library(osal STATIC osal/osal_posix.c)
target_include_directories(osal PUBLIC ${PROJECT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(osal PUBLIC Threads::Threads)

add_executable(bt_speaker
    src/main.c
    src/ui/ui_main.c
    apps/app_player.c
    services/btmg/btmg_player.c)

target_include_directories(bt_speaker PRIVATE
    ${PROJECT_SOURCE_DIR}/osal                                 # osal.h
    ${PROJECT_SOURCE_DIR}/core                                 # player_types.h
    ${PROJECT_SOURCE_DIR}/services                             # player_backend.h
    ${PROJECT_SOURCE_DIR}/src                                  # lv_conf.h 发现 + 我们的代码
    ${PROJECT_SOURCE_DIR}/third_party                          # lvgl.h / lv_drivers 头
    ${PROJECT_SOURCE_DIR}/third_party/lvgl/src/extra/libs/freetype   # lv_freetype.h（lvgl 树旧 API）
    ${PROJECT_SOURCE_DIR}/third_party/freetype/include         # ft2build.h
    ${PROJECT_SOURCE_DIR}/third_party/bt/include               # bt_manager.h
)

target_link_libraries(bt_speaker PRIVATE lvgl lv_drivers freetype bt osal m)

# ---------------- host 自测（在 x86 上验证可移植层，与工具链无关） ----------------
# 用法：cmake -B build-host -S cmake/host && cmake --build build-host && ctest --test-dir build-host
# 或直接： cmake -B build-host -DBTSP_HOST_TESTS=ON （非交叉工具链时）
get_filename_component(_cc_name "${CMAKE_C_COMPILER}" NAME)
if(_cc_name MATCHES "^(gcc|cc|clang)(-[0-9.]+)?$")
    add_executable(osal_test osal/osal_test.c)
    target_include_directories(osal_test PRIVATE ${PROJECT_SOURCE_DIR})
    target_link_libraries(osal_test PRIVATE osal)
    target_compile_options(osal_test PRIVATE -Wall -Wextra -Werror)
    enable_testing()
    add_test(NAME osal_test COMMAND osal_test)
endif()
