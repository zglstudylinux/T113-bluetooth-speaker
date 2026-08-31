# cmake/toolchain/gnueabihf.cmake — CI 用 apt 交叉工具链（gcc-arm-linux-gnueabihf）
#
# 用法（GitHub Actions）：
#   sudo apt-get install -y gcc-arm-linux-gnueabihf
#   cmake -B build-ci -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/gnueabihf.cmake
#
# 同为 armhf 硬浮点 ABI，能链接 vendor 的 ARM .so/.a（OpenWrt gcc8.3 产物，
# glibc 2.29 armhf 目标）。链接期一般不查符号版本；若首跑报 GLIBC 版本错误，
# 见 docs/architecture.md §8 预案。

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)

set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard")
