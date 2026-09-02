#!/bin/bash
#
# build.sh — 一键构建入口（仿 app_sdk）
#
#   ./build.sh          目标板 ARM 交叉编译（需先跑 scripts/setup.sh 装好 toolchain/）
#   ./build.sh -host    host 自测（可移植层：OSAL 队列 + sim 整链路 + ctest）
#   ./build.sh -clean   删除两个 build 中间目录（产物 build/bt_speaker 一并清除）
#
# 产物固定在 build/bt_speaker（CMAKE_RUNTIME_OUTPUT_DIRECTORY），deploy.sh 零改动。
# ⚠ 换平台/改 CMakeLists 后建议 -clean 再编（CMake 缓存的编译器不可换）。
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

case "$1" in
-host)
    echo "[build] host 自测构建（osal_test + sim_loop_test）"
    cmake -B build-host -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build-host -j"$(nproc)"
    ctest --test-dir build-host --output-on-failure
    ;;
-clean)
    rm -rf build-host build-cmake
    echo "[clean] 已删除 build-host/ build-cmake/（build/ 下产物一并清除）"
    ;;
""|-t113)
    if [ ! -x toolchain/bin/arm-openwrt-linux-gcc ]; then
        echo "[build] 错误：toolchain/ 不存在，先跑 ./scripts/setup.sh"
        exit 1
    fi
    echo "[build] 目标板 ARM 交叉编译"
    cmake -B build-cmake -DCMAKE_TOOLCHAIN_FILE=cmake/openwrt-armhf.cmake >/dev/null
    cmake --build build-cmake -j"$(nproc)"
    echo "[build] 产物：build/bt_speaker（./scripts/deploy.sh 部署）"
    ;;
*)
    echo "用法："
    echo "  ./build.sh          目标板 ARM 交叉编译"
    echo "  ./build.sh -host    host 自测（ctest）"
    echo "  ./build.sh -clean   清理构建目录"
    exit 1
    ;;
esac
