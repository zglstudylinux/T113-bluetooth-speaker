#!/bin/bash
#
# deploy.sh — 通过 adb 部署到 T113 开发板并启动
#
# 内容：
#   build/bt_speaker      → /mnt/UDISK/speaker/bt_speaker（rootfs overlay 太小放不下）
#   assets/fonts/*.otf    → /mnt/UDISK/speaker/fonts/
#   assets/image/*.png    → /mnt/UDISK/speaker/image/（M4 起，LVGL POSIX FS 'S:' 加载）
#   third_party/bt/lib/*  → /lib、freetype → /usr/lib/
#
# 启动用 start-stop-daemon（防 adb 会话退出杀进程，见移植文档坑 23）
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"

ADB="${ADB:-adb}"
APP_PATH="/mnt/UDISK/speaker/bt_speaker"

$ADB get-state >/dev/null 2>&1 || {
    echo "[deploy] 错误：adb 未连接（把 adb 连到虚拟机后再运行）"; exit 1; }

echo "[deploy] 推送应用（→ UDISK，rootfs overlay 放不下 4MB 二进制）..."
$ADB shell mkdir -p /mnt/UDISK/speaker
$ADB push "$PROJ_DIR/build/bt_speaker" "$APP_PATH"
$ADB shell chmod +x "$APP_PATH"

echo "[deploy] 推送字体（UDISK，rootfs overlay 放不下 8MB 字体）..."
$ADB shell mkdir -p /mnt/UDISK/speaker/fonts
$ADB push "$PROJ_DIR/assets/fonts/." /mnt/UDISK/speaker/fonts/

echo "[deploy] 推送图片资源..."
$ADB shell mkdir -p /mnt/UDISK/speaker/image
if [ -d "$PROJ_DIR/assets/image" ]; then
    $ADB push "$PROJ_DIR/assets/image/." /mnt/UDISK/speaker/image/
else
    echo "  （无 assets/image/，跳过图片推送）"
fi

echo "[deploy] 推送运行库（freetype/libbz2；BT 库 M1 起）..."
# freetype：只推真实文件 + .so 软链接，避免重复占用 rootfs 空间
$ADB push "$PROJ_DIR/third_party/freetype/lib/libfreetype.so.6.17.0" /usr/lib/
$ADB shell "cd /usr/lib && ln -sf libfreetype.so.6.17.0 libfreetype.so.6 && ln -sf libfreetype.so.6.17.0 libfreetype.so"
$ADB push "$PROJ_DIR/third_party/freetype/lib/libbz2.so.1.0" /usr/lib/ 2>/dev/null || true

echo "[deploy] 推送 BT 库（btmanager 4.0.3 good 版，重新烧录固件后必须重推）..."
$ADB push "$PROJ_DIR/third_party/bt/lib/libbtmg.so" /lib/libbtmg.so
$ADB push "$PROJ_DIR/third_party/bt/bin/bt_test" /usr/bin/bt_test
$ADB shell chmod +x /usr/bin/bt_test

echo "[deploy] 启动..."
$ADB shell "/sbin/start-stop-daemon -K -p /tmp/bt_speaker.pid -x $APP_PATH" 2>/dev/null || true
$ADB shell "/sbin/start-stop-daemon -b -m -S -p /tmp/bt_speaker.pid -x $APP_PATH"
echo "[deploy] 完成。查看进程：adb shell ps | grep bt_speaker"
