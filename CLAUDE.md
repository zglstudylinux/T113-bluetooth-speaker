# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 本仓库是什么

**T113 蓝牙音箱**独立项目：Allwinner T113-S3（`zgl_board` 底板，480×640 MIPI 竖屏 + dx_touch 触摸）上的 LVGL 8.3 蓝牙音箱应用。手机经 A2DP 连板子放歌，屏幕实时显示歌名/歌手/进度/音量（AVRCP），板子出声。技术栈：LVGL 8.3.1（sunxifb `/dev/fb0` 32bpp + evdev `/dev/input/event4`）+ Allwinner btmanager 4.0.3（`libbtmg.so`）+ FreeType 中文字体 + RTL8723DS 蓝牙模块。

所有第三方库 vendor 进仓库（`third_party/`），自包含；工具链太大用脚本复制（`toolchain/`，gitignore）。宿主机是本机 Tina SDK（`/home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2`），但**本项目构建不依赖 SDK 环境变量**，只需 `toolchain/` 就位。

详细的小白向讲解文档在 `docs/project-guide.md`（每完成一个功能追加一节，含完整调试记录）——**给项目加新功能后必须同步追加该文档**。

## 常用命令

```bash
# 首次（或 toolchain/ 被删）：从本机 Tina SDK 复制交叉工具链（~1.2GB）
./scripts/setup.sh

# 编译（增量）
make -j$(nproc)

# 部署到板子并启动（adb push + start-stop-daemon）
./scripts/deploy.sh

# 清理
make clean
```

- 编译产物：`build/bt_speaker`（ARM 硬浮点 ELF，动态链接）。
- `deploy.sh` 需要 adb 连到虚拟机（板子 USB OTG）；会把 app/字体/图片推到板上 `/mnt/UDISK/speaker/`（rootfs overlay 只有 ~8MB 放不下大文件），BT 库推到 `/lib`、`/usr/lib`，最后 `start-stop-daemon -b` 后台启动。
- **板侧验证**：`adb shell "ps | grep bt_speaker"`、`adb shell hciconfig hci0`（应 `UP RUNNING PSCAN ISCAN`，ACL MTU 1021）；显示效果用 `adb pull /dev/fb0` 抓帧分析（**字节序 BGRX**，可见页是前 480×640×4 字节）。

## 构建/部署的关键约定（坑）

1. **工具链 ABI 是项目基石**：必须用 `toolchain-sunxi-glibc-gcc-830`（gcc 8.3，**armhf 硬浮点**），与板上 rootfs（glibc 2.29 armhf）和 `libbtmg.so` 匹配。**不能用** SDK 默认的 linaro 5.3.1 `arm-linux-gnueabi`（软浮点）。`Makefile` 里 `CC` 强制指向 `./toolchain/bin/arm-openwrt-linux-gcc`。
2. **OpenWrt wrapper 编译器要求 `STAGING_DIR`**——Makefile 里 `export STAGING_DIR` 已处理，别删。
3. **改 `src/lv_conf.h` 后必须全量重编**：Makefile 不跟踪 lv_conf.h 依赖，旧 `.o` 里宏开关还是旧值，症状千奇百怪（如 PNG decoder 整个没编进、字体符号 undefined）：
   ```bash
   find build -name "*.o" -delete && make -j$(nproc)
   ```
4. **`lodepng.c` 的 `#if LV_USE_PNG` 不经过 lv_conf.h**（include 链断了）→ Makefile CFLAGS 里加了 `-DLV_USE_PNG=1 -DLV_USE_FS_POSIX=1` 兜底，勿删。
5. **链接顺序讲究**（`-lbtmg -lshared-mainloop -lbluetooth-internal -lwirelesscom -lgio-2.0 …`，libbtmg 依赖 shared-mainloop，gobject 依赖 libffi）——调整 LDFLAGS 时保持顺序。
6. **板上大文件一律放 `/mnt/UDISK`**（app 二进制、字体、图片），rootfs `/` 只有 ~8MB overlay；`adb push` 报 `No space left` 就是放错地方。
7. **进程必须用 `start-stop-daemon -b -m -S` 启动**（deploy.sh 已做）：直接 `nohup &` 会被 adb 会话退出杀掉。
8. **换 BT 固件/config 后必须拔 5V 电彻底断电 15s**（RTL8723DS 常供电无复位脚，软 reboot 不清模块 RAM，否则 `H5 sync timed out`）。
9. **BT 库是"good 版"快照**：`third_party/bt/lib/libbtmg.so` 必须是 285460B 的 4.0.3 版（md5 `2b8d26e3…`）。SDK 自带 4.0.5 版配对要 PIN。固件/工具的"好的"版本集固化在 `firmware/rtl8723ds/`（含 md5 表，见其 README.md）。

## 代码结构（big picture）

```
src/
├── main.c            # 入口：lv_init + sunxifb/evdev 初始化 + FreeType 字体
│                     #   （ui_font_cn_48/32 全局）+ bt_speaker_init + lv_timer 主循环
├── bt_speaker.c/.h   # btmanager 封装：preinit→enable_profile(A2DP_SINK|AVRCP)→init→enable
│                     #   observer 回调（adapter/conn/play_state/track/play_pos/volume）
│                     #   + bt_speaker_avrcp_cmd()（play/pause/forward/backward）
└── ui/ui_main.c      # 480×640 主界面。全屏产品图（POSIX FS "S:" 加载，缺图降级深色 UI）
                      #   + 双配色 scheme（SCHEME_LIGHT 叠图 / SCHEME_DARK 降级）
                      #   + post_state/post_info/post_play_icon 三条异步通道
```

**核心数据流**：btmanager 回调（btmanager 线程）→ `lv_async_call` 投递 malloc 的消息 → LVGL 线程回调更新 UI。**绝不能在 btmanager 回调里直接操作 LVGL**（会乱码/崩溃）。`ui_info_t` 字段用 -1/空串表示"不更新"。

**播放按钮乐观更新**：点击瞬间本地切图标（不等 AVRCP 往返 1~2s），回调回来再校正——这是刻意的，勿"修复"。

**图片加载**：`IMG_BG_PATH "S:/mnt/UDISK/speaker/image/bt.png"`（`LV_FS_POSIX_LETTER 'S'`，decoder 剥盘符按绝对路径 open）；启动时 `access()` 探测文件存在与否选配色方案。换图同名覆盖重启 app 即生效。

## LVGL 配置要点（src/lv_conf.h）

- `LV_COLOR_DEPTH 32`（匹配 fb0 XRGB8888）
- `LV_MEM_CUSTOM 1`（系统 malloc）——PNG 解码 481×641 需 ~1.2MB，LVGL 内存池放不下
- `LV_USE_PNG 1`、`LV_USE_FS_POSIX 1`（盘符 `'S'`）
- `LV_USE_FREETYPE 1`（运行时加载 `assets/fonts/*.otf`，中文歌名必需）
- Montserrat 开 12/14/16/24（24 是播放符号；`LV_SYMBOL_*` 是 FontAwesome 字形不是图片）

## 工作流约定

- **里程碑制**：完成一个功能 → 用户上板肉眼/听感确认 → 才 commit/push。不要未经确认就 push。
- 推送：HTTPS + PAT（`~/.git-credentials` 已存），`git push origin main` 直推即可。
- 新功能完成后：`docs/project-guide.md` 追加一节（做了什么/踩的坑/验证结果）。
- 用户负责烧录/接线/adb 连接虚拟机；板侧 log 由 Claude 经 adb 抓取分析。
