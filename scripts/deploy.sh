#!/bin/bash
#
# deploy.sh — 通过 adb 部署到 T113 开发板并启动
#
# 内容：
#   build/bt_speaker      → /usr/bin/bt_speaker
#   assets/fonts/*.otf    → /usr/res/speaker/fonts/
#   （M1 起）third_party/bt/lib/*.so → /usr/lib/、freetype → /usr/lib/
#
# 启动用 start-stop-daemon（防 adb 会话退出杀进程，见移植文档坑 23）
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"

ADB="${ADB:-adb}"

$ADB get-state >/dev/null 2>&1 || {
    echo "[deploy] 错误：adb 未连接（把 adb 连到虚拟机后再运行）"; exit 1; }

echo "[deploy] 推送应用..."
$ADB push "$PROJ_DIR/build/bt_speaker" /usr/bin/bt_speaker
$ADB shell chmod +x /usr/bin/bt_speaker

echo "[deploy] 推送字体（UDISK，rootfs overlay 放不下 8MB 字体）..."
$ADB shell mkdir -p /mnt/UDISK/speaker/fonts
$ADB push "$PROJ_DIR/assets/fonts/." /mnt/UDISK/speaker/fonts/

echo "[deploy] 推送运行库（freetype/libbz2；BT 库 M1 起）..."
$ADB push "$PROJ_DIR/third_party/freetype/lib/libfreetype.so.6.17.0" /usr/lib/
$ADB push "$PROJ_DIR/third_party/freetype/lib/libfreetype.so.6" /usr/lib/ 2>/dev/null || true
$ADB push "$PROJ_DIR/third_party/freetype/lib/libfreetype.so" /usr/lib/ 2>/dev/null || true
$ADB push "$PROJ_DIR/third_party/freetype/lib/libbz2.so.1.0" /usr/lib/ 2>/dev/null || true

echo "[deploy] 启动..."
$ADB shell "/sbin/start-stop-daemon -K -p /tmp/bt_speaker.pid -x /usr/bin/bt_speaker" 2>/dev/null || true
$ADB shell "/sbin/start-stop-daemon -b -m -S -p /tmp/bt_speaker.pid -x /usr/bin/bt_speaker"
echo "[deploy] 完成。查看进程：adb shell ps | grep bt_speaker"
