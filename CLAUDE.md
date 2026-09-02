# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 本仓库是什么

**T113 蓝牙音箱**独立项目：Allwinner T113-S3（`zgl_board` 底板，480×640 MIPI 竖屏 + dx_touch 触摸）上的 LVGL 8.3 蓝牙音箱应用。手机经 A2DP 连板子放歌，屏幕实时显示歌名/歌手/进度/音量（AVRCP），板子出声。技术栈：LVGL 8.3.1（sunxifb `/dev/fb0` 32bpp + evdev `/dev/input/event4`）+ Allwinner btmanager 4.0.3（`libbtmg.so`）+ FreeType 中文字体 + RTL8723DS 蓝牙模块。

所有第三方库 vendor 进仓库（`third_party/`），自包含；工具链不进 git（gitignore），由 `scripts/setup.sh` 装入 `./toolchain/`——优先从 GitHub Release 下载裁剪版（204MB，tag `toolchain-v1`），失败回退本机 Tina SDK 复制（完整版 1.2GB）。宿主机是本机 Tina SDK（`/home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2`），但**本项目构建不依赖 SDK 环境变量**，只需 `toolchain/` 就位。裁剪前完整版备份在 `/home/zgl/SDK/toolchain-backup-gcc830-pretrim/`（仓库外）。

架构总计划在 `docs/architecture.md`（分层/接口/六阶段路线图/各阶段实施记录）；详细的小白向讲解文档在 `docs/project-guide.md`（每完成一个功能追加一节，含完整调试记录）——**给项目加新功能后必须同步追加该文档**。

## 常用命令

```bash
# 首次（或 toolchain/ 被删）：装工具链（Release 下载裁剪版 ~80MB；或 TINA_SDK_PATH=<SDK> 本地复制）
./scripts/setup.sh

# 一键构建目标板（= cmake -B build-cmake -DCMAKE_TOOLCHAIN_FILE=… && cmake --build；增量）
./build.sh

# 部署到板子并启动（adb push + start-stop-daemon）
./scripts/deploy.sh

# host 自测（可移植层：OSAL 队列 + sim 整链路 ctest，无需工具链/板子）
./build.sh -host

# 清理构建目录
./build.sh -clean
```

- 编译产物：`build/bt_speaker`（ARM 硬浮点 ELF，动态链接；CMake 中间文件在 `build-cmake/`，与产物分开）。
- `deploy.sh` 需要 adb 连到虚拟机（板子 USB OTG）；会把 app/字体/图片推到板上 `/mnt/UDISK/speaker/`（rootfs overlay 只有 ~8MB 放不下大文件），BT 库推到 `/lib`、`/usr/lib`，最后 `start-stop-daemon -b` 后台启动。
- **板侧验证**：`adb shell "ps | grep bt_speaker"`、`adb shell hciconfig hci0`（应 `UP RUNNING PSCAN ISCAN`，ACL MTU 1021）；显示效果用 `adb pull /dev/fb0` 抓帧分析（**字节序 BGRX**，可见页是前 480×640×4 字节）。
- GitHub CI（`.github/workflows/build.yml`）：push/PR 触发双 job——`build-arm`（apt gnueabihf 真交叉编译 + 断言 ARM ELF）+ `build-host`（-Werror 可移植层 + ctest）。

## 构建/部署的关键约定（坑）

1. **工具链 ABI 是项目基石**：必须用 `toolchain-sunxi-glibc-gcc-830`（gcc 8.3，**armhf 硬浮点**），与板上 rootfs（glibc 2.29 armhf）和 `libbtmg.so` 匹配。**不能用** SDK 默认的 linaro 5.3.1 `arm-linux-gnueabi`（软浮点）。CMake toolchain 文件 `cmake/openwrt-armhf.cmake` 强制指向 `./toolchain/bin/arm-openwrt-linux-gcc`。**若需再裁剪工具链**（M8 有先例）：`arm-openwrt-linux-gnueabi/bin/`（as/ld 等，gcc.bin 靠 PATH 找）和 `arm-openwrt-linux-gnueabi/sys-include`（→../include 符号链接，断了 stdint 报错）不可删；裁完必须重编产物比对 md5。
2. **OpenWrt wrapper 编译器每次运行都读 `STAGING_DIR`**——toolchain 文件已用 `CMAKE_C_COMPILER_LAUNCHER` + 全局 `RULE_LAUNCH_LINK` 注入（configure 期 `set(ENV)` 不会带进 build 期），勿删。
3. **`lodepng.c` 的 `#if LV_USE_PNG` 不经过 lv_conf.h**（include 链断了）→ 顶层 CMakeLists 的 `add_compile_definitions(LV_USE_PNG=1 LV_USE_FS_POSIX=1)` 兜底，勿删。
4. **链接顺序讲究**（`-lbtmg -lshared-mainloop -lbluetooth-internal -lwirelesscom -lgio-2.0 …`，libbtmg 依赖 shared-mainloop，gobject 依赖 libffi）——顺序固化在 `cmake/third_party.cmake` 的 `bt` INTERFACE 库里，勿重排。
5. **host 环境不能构建 `bt_speaker` 目标**（vendor ARM .so 在 host 链接报 wrong format）——CMakeLists 检测裸名 gcc/cc/clang 时只编 src/tests/ 下两个自测程序并 return；给目标板构建必须带 toolchain 文件。
6. **板上大文件一律放 `/mnt/UDISK`**（app 二进制、字体、图片），rootfs `/` 只有 ~8MB overlay；`adb push` 报 `No space left` 就是放错地方。
7. **进程必须用 `start-stop-daemon -b -m -S` 启动**（deploy.sh 已做）：直接 `nohup &` 会被 adb 会话退出杀掉。
8. **换 BT 固件/config 后必须拔 5V 电彻底断电 15s**（RTL8723DS 常供电无复位脚，软 reboot 不清模块 RAM，否则 `H5 sync timed out`）。
9. **BT 库是"good 版"快照**：`third_party/bt/lib/libbtmg.so` 必须是 285460B 的 4.0.3 版（md5 `2b8d26e3…`）。SDK 自带 4.0.5 版配对要 PIN。固件/工具的"好的"版本集固化在 `firmware/rtl8723ds/`（含 md5 表，见其 README.md）。

## 代码结构（big picture，M6 分层架构，M9 收编进 src/）

```
src/core/player_types.h    # 唯一数据契约：player_event_t（208B 定长值类型；-1/空串=不更新）
src/osal/                  # OS 抽象：队列（16槽满丢最旧）/mutex/time/log；osal_posix.c（Linux）
src/services/
├── player_backend.h       # 业务接口：init(emit)/query_state/cmd
├── btmg_player.c          # btmanager 实现：preinit→A2DP Sink|AVRCP→回调组事件 emit 入队
└── sim_player.c           # 模拟播放器（剧本吐事件；host 开发/CI 用）
src/ui/
├── ui_backend.h           # UI 接口：init(ui_env_t)/on_event/deinit
├── theme.h                # D1 液态玻璃主题：色板/素材/布局常量
└── ui_liquidglass.c       # 绘制/唱盘旋转/乐观更新
src/ports/                 # 板级：lv_port_disp(fb)/indev(evdev)/font(FreeType) + main_linux.c + lv_conf.h
src/apps/app_player.c      # 组装层：建队列 → UI init → 后端 init → lv_timer 33ms drain
src/tests/                 # host 自测：osal_test / sim_loop_test（ctest）
```

**核心数据流**：btmanager 回调（btmanager 线程）→ 组 `player_event_t` → OSAL 队列（`emit`）→ LVGL 线程 33ms timer drain → 主题 `on_event` 直接更新控件。**绝不能在 btmanager 回调里直接操作 LVGL**；也不再用 `lv_async_call`（M6 已删）。队列先于后端 init 创建，早到事件天然缓冲。

**播放按钮乐观更新**：点击瞬间本地切图标（不等 AVRCP 往返 1~2s），事件回来再校正——这是刻意的，勿"修复"。

**图片加载**：bg.png/disc.png（`LV_FS_POSIX_LETTER 'S'`，`src/ui/theme.h` 定义路径）；启动时 `access()` 探测降级。换图同名覆盖重启 app 即生效。

## LVGL 配置要点（src/ports/lv_conf.h）

- `LV_COLOR_DEPTH 32`（匹配 fb0 XRGB8888）
- `LV_MEM_CUSTOM 1`（系统 malloc）——PNG 解码需 ~1.2MB，LVGL 内存池放不下
- `LV_IMG_CACHE_DEF_SIZE 2`——bg+disc 解码常驻，唱盘每帧旋转（33ms tick）不重新解码
- `LV_USE_PNG 1`、`LV_USE_FS_POSIX 1`（盘符 `'S'`）
- `LV_USE_FREETYPE 1`（运行时加载 `assets/fonts/*.otf`，中文歌名必需）
- Montserrat 开 12/14/16/24（24 是播放符号；`LV_SYMBOL_*` 是 FontAwesome 字形不是图片）

## 工作流约定

- **里程碑制**：完成一个功能 → 用户上板肉眼/听感确认 → 才 commit/push。不要未经确认就 push。
- 推送：HTTPS + PAT（`~/.git-credentials` 已存），`git push origin main` 直推即可。
- 新功能完成后：`docs/project-guide.md` 追加一节（做了什么/踩的坑/验证结果）。
- 用户负责烧录/接线/adb 连接虚拟机；板侧 log 由 Claude 经 adb 抓取分析。
