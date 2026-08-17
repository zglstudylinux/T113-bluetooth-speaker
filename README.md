# T113 蓝牙音箱（LVGL8 + A2DP Sink）

基于 LVGL 8.3 的 Allwinner T113 蓝牙音箱应用：手机连接开发板蓝牙播放音乐，
屏幕（480×640）显示歌名 / 歌手 / 专辑 / 进度 / 音量。

- 硬件：T113-S3（zgl_board），D240N2501V1 屏（ST7701SN 480×640），dx_touch 触摸，RTL8723DS WiFi/BT
- 音频链路：手机 A2DP → bluetoothd → bluealsa(SBC 解码) → libbtmg(btmanager 4.0.3) → ALSA default → 片内 codec
- 屏显链路：sunxifb(/dev/fb0 32bpp) + evdev(/dev/input/event4)，FreeType 运行时加载中文字体

## 目录结构

```
├── Makefile               # 交叉编译（镜像 lvgl_demo_build 验证过的 Makefile）
├── scripts/setup.sh       # 首次：从 Tina SDK 复制 toolchain 到 ./toolchain/（gitignore）
├── scripts/deploy.sh      # adb 部署到板子 + start-stop-daemon 启动
├── third_party/
│   ├── lvgl/              # LVGL 8.3.1（仅 src，去 demos/examples/docs）
│   ├── lv_drivers/        # sunxifb + evdev
│   ├── freetype/          # 头文件 + libfreetype.so（中文字体渲染）
│   └── bt/                # libbtmg 及依赖 .so + bt_manager.h（M1 起使用）
├── assets/fonts/          # 思源黑体 .otf
└── src/                   # 应用源码（lv_conf.h / lv_drv_conf.h 基线=已验证配置）
```

## 构建

```bash
./scripts/setup.sh     # 首次：复制工具链（gcc-830 硬浮点，armhf，匹配板上 glibc 2.29）
make -j$(nproc)        # 产物 build/bt_speaker（动态链接）
```

> 工具链必须用 `toolchain-sunxi-glibc-gcc-830`（app 已验证可用）。它不进 git，
> 克隆后跑 `TINA_SDK_PATH=<SDK路径> ./scripts/setup.sh`。

## 部署运行

```bash
./scripts/deploy.sh    # adb push app/字体/库 + 启动
# 或手动：
adb shell /sbin/start-stop-daemon -K -p /tmp/bt_speaker.pid 2>/dev/null
adb shell /sbin/start-stop-daemon -b -m -S -p /tmp/bt_speaker.pid -x /usr/bin/bt_speaker
```

> 注意：不能直接 `bt_speaker &`——会被 adb 会话退出杀掉，必须用 start-stop-daemon。

## 里程碑状态

- [x] M0：编译环境 + "蓝牙音箱"中文显示 + 触摸验证
- [ ] M1：蓝牙 bring-up（手机搜到 ZGL_BT_SPEAKER、免 PIN 配对、UI 显示已连接）
- [ ] M2：A2DP 出声 + 播放/暂停状态
- [ ] M3：歌名/歌手/专辑/进度/时间/音量 UI
- [ ] M4：图片挂载点 + 开机自启 + 收尾

## 已知事项

- **重新烧录固件后**：板上 `/lib/libbtmg.so` 会回退到镜像里的 4.0.5（有配对 bug），
  需重推 4.0.3 good 版（285460B）；bt 库 bundle 见 `third_party/bt/lib/`。
- **换 BT 库/固件后必须拔电彻底重启**（模块常供电，软 reboot 不清 RAM 固件）。
- app_sdk 参考代码的屏幕是 280×1424，本项目 UI 全部按 480×640 重排。
