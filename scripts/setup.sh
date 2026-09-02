#!/bin/bash
#
# setup.sh — 首次准备：装好交叉编译工具链到 ./toolchain/
#
# 三种来源（按优先级）：
#   1. toolchain/ 已就位（重复运行 / 已装好）→ 跳过
#   2. GitHub Release 下载裁剪版工具链 tar.xz（约 80MB，走代理环境变量
#      http_proxy/https_proxy 或 HTTP_PROXY/HTTPS_PROXY；可用 TOOLCHAIN_URL 覆盖）
#   3. 本机 Tina SDK 复制（TINA_SDK_PATH 指定，默认开发者本机路径）
#
# 裁剪版 = 完整版删去 C++/fortran/sanitizer/gconv/man 等本项目用不到的组件
# （1.2GB → 204MB），编译产物与完整版逐字节一致（md5 已验证）。
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"

TINA_SDK_PATH="${TINA_SDK_PATH:-/home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2}"
TOOLCHAIN_SRC="$TINA_SDK_PATH/prebuilt/rootfsbuilt/arm/toolchain-sunxi-glibc-gcc-830"
TOOLCHAIN_DST="$PROJ_DIR/toolchain"
# Release 资产地址（tag/文件名变更时同步改这里）
TOOLCHAIN_URL="${TOOLCHAIN_URL:-https://github.com/zglstudylinux/T113-bluetooth-speaker/releases/download/toolchain-v1/toolchain-gcc830-armhf-trimmed.tar.xz}"

if [ -x "$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc" ]; then
    echo "[setup] toolchain 已存在：$TOOLCHAIN_DST（跳过）"
    exit 0
fi

# ---- 来源 2：GitHub Release 下载 ----
try_download() {
    command -v curl >/dev/null 2>&1 || return 1
    echo "[setup] 从 GitHub Release 下载裁剪版工具链（约 80MB）..."
    echo "[setup] $TOOLCHAIN_URL"
    local tmp
    tmp=$(mktemp /tmp/toolchain-dl.XXXXXX.tar.xz)
    if ! curl -fSL --retry 2 --connect-timeout 15 -o "$tmp" "$TOOLCHAIN_URL"; then
        rm -f "$tmp"
        return 1
    fi
    echo "[setup] 下载完成（$(du -h "$tmp" | cut -f1)），解压..."
    mkdir -p "$TOOLCHAIN_DST"
    if ! tar -xJf "$tmp" -C "$TOOLCHAIN_DST" --strip-components=1; then
        rm -rf "$TOOLCHAIN_DST"; rm -f "$tmp"; return 1
    fi
    rm -f "$tmp"
    return 0
}

# ---- 来源 3：本机 Tina SDK 复制 ----
try_copy_sdk() {
    if [ ! -d "$TOOLCHAIN_SRC" ]; then
        echo "[setup] 找不到本地 SDK 工具链：$TOOLCHAIN_SRC"
        return 1
    fi
    echo "[setup] 从本地 Tina SDK 复制完整工具链（约 1.2GB，需几分钟）..."
    mkdir -p "$TOOLCHAIN_DST"
    cp -a "$TOOLCHAIN_SRC/toolchain/." "$TOOLCHAIN_DST/"
}

if ! try_download; then
    echo "[setup] 下载不可用，回退本地 SDK 复制"
    try_copy_sdk
fi

# ---- 校验 ----
if [ ! -x "$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc" ]; then
    echo "[setup] 错误：工具链安装后仍不可用，请检查上方日志"
    exit 1
fi
echo "[setup] 完成：$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc"
"$TOOLCHAIN_DST/bin/arm-openwrt-linux-gcc" --version | head -1
