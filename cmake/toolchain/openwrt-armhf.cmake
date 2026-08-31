# cmake/toolchain/openwrt-armhf.cmake — 本机交叉工具链（./toolchain，setup.sh 产物）
#
# 用法：
#   cmake -B build-cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/openwrt-armhf.cmake
#   cmake --build build-cmake -j
#
# 坑位说明（勿动）：
# - OpenWrt 的 gcc 是个 wrapper 脚本，内部再调 arm-openwrt-linux-gnueabi-gcc.bin，
#   .bin 会读环境变量 STAGING_DIR（不设则 fatal error）。
#   CMake 的 toolchain 文件只在 configure 期执行，set(ENV{...}) 不会带进 build 期，
#   所以用 CMAKE_C_COMPILER_LAUNCHER 给每次编译/链接注入 STAGING_DIR。
# - ABI 必须 armhf 硬浮点，与板上 rootfs（glibc 2.29 armhf）和 libbtmg.so 匹配。

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_BIN "${CMAKE_CURRENT_LIST_DIR}/../../toolchain/bin")
if(NOT EXISTS "${TOOLCHAIN_BIN}/arm-openwrt-linux-gcc")
    message(FATAL_ERROR
        "找不到 ${TOOLCHAIN_BIN}/arm-openwrt-linux-gcc\n"
        "请先执行: TINA_SDK_PATH=/path/to/T113-Tina5.0-V1.2 ./scripts/setup.sh")
endif()

set(CMAKE_C_COMPILER "${TOOLCHAIN_BIN}/arm-openwrt-linux-gcc")
set(CMAKE_AR         "${TOOLCHAIN_BIN}/arm-openwrt-linux-ar" CACHE FILEPATH "ar")
set(CMAKE_RANLIB     "${TOOLCHAIN_BIN}/arm-openwrt-linux-ranlib" CACHE FILEPATH "ranlib")
set(CMAKE_STRIP      "${TOOLCHAIN_BIN}/arm-openwrt-linux-strip" CACHE FILEPATH "strip")

# STAGING_DIR：wrapper 的 gcc.bin 每次运行都要读（编译步经 CMAKE_C_COMPILER_LAUNCHER 注入；
# 链接步 LAUNCHER 机制不覆盖，用全局 RULE_LAUNCH_LINK；ar 不需要，多传无害）
set(STAGING_DIR_VALUE "${TOOLCHAIN_BIN}" CACHE INTERNAL "OpenWrt STAGING_DIR")
set(CMAKE_C_COMPILER_LAUNCHER env "STAGING_DIR=${STAGING_DIR_VALUE}")
set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "env STAGING_DIR=${STAGING_DIR_VALUE}")

set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard")
