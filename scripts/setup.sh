#!/bin/bash
#
# setup.sh — 首次准备：从本机 Tina SDK 复制交叉编译工具链到 ./toolchain/
#
# 工具链不进 git（.gitignore），克隆仓库后运行本脚本。
# 源路径可在环境变量 TINA_SDK_PATH 覆盖。
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"

TINA_SDK_PATH="${TINA_SDK_PATH:-/home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2}"
TOOLCHAIN_SRC="$TINA_SDK_PATH/prebuilt/rootfsbuilt/arm/toolchain-sunxi-glibc-gcc-830"
TOOLCHAIN_DST="$PROJ_DIR/toolchain"

if [ -x "$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc" ]; then
    echo "[setup] toolchain 已存在：$TOOLCHAIN_DST（跳过复制）"
    exit 0
fi

if [ ! -d "$TOOLCHAIN_SRC" ]; then
    echo "[setup] 错误：找不到工具链源目录：$TOOLCHAIN_SRC"
    echo "[setup] 请确认 Tina SDK 路径，或用 TINA_SDK_PATH=<路径> $0 指定"
    exit 1
fi

echo "[setup] 复制工具链（约 1.2GB，需几分钟）..."
mkdir -p "$TOOLCHAIN_DST"
cp -a "$TOOLCHAIN_SRC/toolchain/." "$TOOLCHAIN_DST/"
echo "[setup] 完成：$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc"
"$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc" --version | head -1
