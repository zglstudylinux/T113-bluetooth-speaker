# T113 蓝牙音箱（LVGL8 + A2DP Sink）

基于 LVGL 8.3 的 Allwinner T113 蓝牙音箱应用：手机连接开发板蓝牙播放音乐，
屏幕（480×640）显示歌名 / 歌手 / 专辑 / 进度 / 音量，板子出声。

- 硬件：T113-S3（zgl_board），D240N2501V1 屏（ST7701SN 480×640），dx_touch 触摸，RTL8723DS WiFi/BT
- 音频链路：手机 A2DP → bluetoothd → bluealsa(SBC 解码) → libbtmg(btmanager 4.0.3) → ALSA default → 片内 codec
- 屏显链路：sunxifb(/dev/fb0 32bpp) + evdev(/dev/input/event4)，FreeType 运行时加载中文字体

## 目录结构

```
├── build.sh                  # 一键构建入口（arm / -host / -clean）
├── scripts/                  # setup.sh（装工具链）+ deploy.sh（adb 部署启动）
├── src/                      # 全部第一方源码（分层：依赖只向下）
│   ├── core/                 #   领域模型：player_event_t（唯一数据契约，纯 C99）
│   ├── osal/                 #   OS 抽象：队列/mutex/time/log（posix 实现）
│   ├── services/             #   业务后端：btmg_player.c（板上）+ sim_player.c（模拟）
│   ├── ui/                   #   液态玻璃主题（ui_backend_t，只认 player_event_t）
│   ├── ports/                #   板级：fb 显示 / evdev 触摸 / FreeType 字体 / main
│   ├── apps/                 #   组装层：队列 → UI init → 后端 init → 33ms drain
│   └── tests/                #   host 自测（OSAL 队列 / sim 整链路，ctest）
├── assets/                   # 板上运行素材（fonts/ + image/），deploy.sh 推板
├── docs/                     # 文档 + 设计稿（design/）
├── firmware/rtl8723ds/       # 实测好的 BT 固件集（含 md5 表）
├── third_party/              # vendor：LVGL 8.3 / lv_drivers / freetype / btmg 库
└── cmake/                    # 构建：toolchain 文件 + vendor 库收集
```

分层与接口详见 [`docs/architecture.md`](docs/architecture.md)。

## 构建

克隆后两步（工具链约 80MB，自动从 GitHub Release 下载）：

```bash
./scripts/setup.sh     # 装 toolchain/（也可 TINA_SDK_PATH=<SDK路径> 从本地复制完整版）
./build.sh             # 产物 build/bt_speaker（ARM 硬浮点 ELF，动态链接）
```

其他命令：

```bash
./build.sh -host       # host 自测：OSAL 队列 + sim 整链路（ctest，无需工具链/板子）
./build.sh -clean      # 清理构建目录
```

工具链说明：必须用 `toolchain-sunxi-glibc-gcc-830`（gcc 8.3 **armhf 硬浮点**），
与板上 rootfs（glibc 2.29 armhf）和 `libbtmg.so` 匹配。SDK 默认的 linaro 5.3.1
软浮点工具链不可用。仓库分发的是裁剪版（204MB，删 C++/fortran/sanitizer 等未用
组件），编译产物与完整版一致；不能用网络时从本地 Tina SDK 复制完整版。

## 部署运行

```bash
./scripts/deploy.sh    # adb push app/字体/图片/BT库 到板上 + start-stop-daemon 后台启动
```

> 注意：不能直接 `bt_speaker &`——会被 adb 会话退出杀掉，必须用 start-stop-daemon
> （deploy.sh 已处理）。大文件放板上 `/mnt/UDISK/speaker/`（rootfs 只有 ~8MB）。

## 里程碑状态

- [x] M0~M5b：环境/蓝牙 bring-up/完整 UI/产品图/液态玻璃主题（详见 project-guide）
- [x] M6：架构重构（CMake + UI/业务解耦 + OSAL 分层 + GitHub CI）
- [x] M7：板上回归 + 长歌名自适应 / 暂停态切歌图标修复
- [ ] M4b：开机自启 + 收尾

📖 **详细说明文档**：[`docs/project-guide.md`](docs/project-guide.md)（原理、代码走读、踩坑记录、排查指南，小白向）

## 已知事项

- **重新烧录固件后**：板上 `/lib/libbtmg.so` 会回退到镜像里的 4.0.5（有配对 bug），
  需重推 4.0.3 good 版（285460B）；库 bundle 见 `third_party/bt/lib/`。
- **换 BT 库/固件后必须拔电彻底重启**（模块常供电，软 reboot 不清 RAM 固件）。

## CI

push/PR 自动跑 GitHub Actions（[`.github/workflows/build.yml`](.github/workflows/build.yml)）：

- `build-arm`：apt 交叉工具链完整编译 + 断言产物为 32-bit ARM hard-float ELF
- `build-host`：`-Werror` 编译可移植层 + ctest 自测
