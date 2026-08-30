# T113 蓝牙音箱项目说明文档

> 本文档随项目进度持续更新，每完成一个功能就在后面追加一节。
> 目标读者：有 Linux 基础但不熟嵌入式/蓝牙的开发者（小白也能看懂）。

---

## 0. 这个项目是做什么的

用 Allwinner T113 开发板做一个**蓝牙音箱**：

- 手机通过蓝牙连接开发板
- 在手机上播放音乐，声音从开发板接的喇叭/耳机出来
- 开发板的屏幕（480×640）实时显示**歌名、歌手、专辑、播放进度、音量**
- 后续会用 LVGL8 做一个好看的界面

一句话概括数据流：

```
手机 ──蓝牙(A2DP)──> 开发板 ──> 喇叭出声
                         └──> 屏幕显示歌曲信息
```

### 硬件 / 软件环境

| 项 | 说明 |
|---|---|
| 开发板 | Allwinner T113-S3（`zgl_board` 底板） |
| 系统 | Tina 5.0 SDK（Linux 5.4.61，OpenWrt 风格 rootfs） |
| 屏幕 | D240N2501V1（ST7701SN，480×640 竖屏，MIPI 接口） |
| 触摸 | dx_touch（I2C，`/dev/input/event4`） |
| 蓝牙模块 | RTL8723DS（WiFi+蓝牙二合一；本项目只用蓝牙，走 UART1） |
| 图形库 | LVGL 8.3.1（开源嵌入式 GUI） |
| 蓝牙协议栈 | BlueZ 5.54 + bluez-alsa + Allwinner btmanager 4.0.3 |

### 为什么不用 SDK 自带的 linaro 工具链？

Tina SDK 默认工具链 `arm-linux-gnueabi`（linaro 5.3.1）是**软浮点**，而开发板 rootfs 是**硬浮点（armhf）** glibc 2.29。本项目要动态链接板上的 `libbtmg.so`（蓝牙管理库，硬浮点），两套 ABI 不兼容。所以本项目用 SDK 里另一套 `toolchain-sunxi-glibc-gcc-830`（gcc 8.3，硬浮点），和 rootfs 完全匹配。这点是整个项目能编译运行的基石。

---

## 1. 项目结构总览

```
T113-bluetooth-speaker/
├── Makefile                  # 交叉编译入口（见 §2）
├── README.md
├── CLAUDE.md                 # 给 Claude Code 的仓库操作指南
├── .gitignore                # 排除 toolchain/ build/
├── docs/                     # 本文档 + 截图
│   └── project-guide.md
├── firmware/
│   └── rtl8723ds/           # 实测好的 BT 固件集（fw/config/rtk_hciattach + md5）
├── scripts/
│   ├── setup.sh             # 首次：从 Tina SDK 复制工具链到 ./toolchain/
│   └── deploy.sh            # adb 部署到开发板 + 启动
├── third_party/             # 所有第三方库（vendor 进仓库，自包含）
│   ├── lvgl/                # LVGL 8.3.1 源码
│   ├── lv_drivers/          # 显示(sunxifb) + 触摸(evdev) 驱动
│   ├── freetype/            # 中文字体渲染库（头文件 + libfreetype.so）
│   └── bt/                  # 蓝牙库 bundle
│       ├── lib/             # libbtmg.so（4.0.3 good 版）+ 依赖
│       ├── include/         # bt_manager.h 等 API 头文件
│       └── bin/             # bt_test（调试用）
├── assets/
│   ├── fonts/               # 中文字体（思源黑体 .otf，运行时加载）
│   └── image/               # 界面图片（bt.png，运行时从板上加载）
└── src/                     # 我们自己的代码
    ├── main.c               # 程序入口
    ├── lv_conf.h            # LVGL 配置（颜色深度、字体、各功能开关）
    ├── lv_drv_conf.h        # 显示/触摸驱动配置
    ├── bt_speaker.c/.h      # 蓝牙封装（初始化、配对、播放控制）
    └── ui/
        ├── ui_main.c/.h     # 主界面
```

**核心思想**：除了工具链（太大，用脚本复制），所有依赖都 vendor 进仓库。克隆后跑一个 `setup.sh` + `make` 就能编译，不依赖本机其他环境。

---

## 2. 构建系统（Makefile）

本项目的 `Makefile` 参考了 SDK 里一个已经跑通的 LVGL demo（`lvgl_demo_build`），用最朴素的 Makefile 而不是 CMake，降低理解门槛。

### 关键部分解读

```makefile
# 编译器：强制用项目内的工具链（不用宿主机 gcc）
CC := .../toolchain/bin/arm-openwrt-linux-gcc

# OpenWrt 的 wrapper 编译器要求 STAGING_DIR 环境变量
export STAGING_DIR := $(dir $(firstword $(CC)))

CFLAGS := -std=gnu99 -O2 -g \
    -march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard \  # ARM 硬浮点
    -I.../src \                              # 我们的源码
    -I.../third_party/ \                     # LVGL
    -I.../third_party/lvgl/src/extra/libs/freetype \  # FreeType 头
    -I.../third_party/freetype/include \     # ft2build.h
    -I.../third_party/bt/include             # bt_manager.h

LDFLAGS := -lm \
    -L.../third_party/freetype/lib -lfreetype \
    -L.../third_party/bt/lib \               # 蓝牙库链接
    -lbtmg -lshared-mainloop -lbluetooth-internal -lwirelesscom \
    -lgio-2.0 -lgobject-2.0 -lgmodule-2.0 -lglib-2.0 -lffi \  # glib 全家桶
    -ldbus-1 -lasound -ljson-c -lsbc -lreadline -lncursesw \
    -lz -lbz2 -lpthread -lrt -ldl \
    -Wl,-rpath,/usr/lib                       # 运行时库搜索路径

include third_party/lvgl/lvgl.mk             # LVGL 自动收集所有 .c
include third_party/lv_drivers/lv_drivers.mk # 驱动同理
```

### 踩过的坑（构建阶段）

| 坑 | 现象 | 解决 |
|---|---|---|
| `CC ?=` 被覆盖 | 用成宿主机 x86 gcc，报 `-mfpu=neon` 不识别 | 改成 `CC :=` 强制赋值 |
| `STAGING_DIR` 未定义 | OpenWrt wrapper 编译器直接 fatal | Makefile 里 `export STAGING_DIR` |
| `lv_drv_conf.h` 找不到 | win_drv/wayland 报 `../lv_drv_conf.h: No such file` | 把它放到 `third_party/`（lv_drivers 的上级目录），相对路径自然解析 |
| FreeType API 名不对 | `lv_freetype_font_create` 未定义 | 本 lvgl 树是旧 API：`lv_ft_font_init(&info)` + `lv_ft_info_t` |
| `lv_tick_inc` 未定义 | `LV_TICK_CUSTOM=1` 时该函数被宏屏蔽 | 删掉手动喂 tick，只留 `custom_tick_get()` 供时基 |
| `libz`/`libbz2` 链接不到 | 工具链 sysroot 里没有 | 从 rootfs vendor 进来一起链 |

### 构建产物

`build/bt_speaker` —— ARM EABI5 硬浮点 ELF，解释器 `/lib/ld-linux-armhf.so.3`（与开发板 rootfs 匹配），动态链接 libbtmg/glib/dbus/asound 等板上已有库。

---

## 3. 部署运行

### 3.1 首次准备（克隆仓库后）

```bash
# 1. 复制工具链（从本机 Tina SDK，约 1.2GB，2 分钟）
TINA_SDK_PATH=/home/zgl/SDK/T113_SDK/T113-Tina5.0-V1.2 ./scripts/setup.sh

# 2. 编译
make -j$(nproc)
```

> 工具链不进 git（太大），所以每次新克隆都要跑 `setup.sh`。

### 3.2 部署到开发板

```bash
./scripts/deploy.sh
```

`deploy.sh` 做了这些事：

1. `adb push build/bt_speaker /usr/bin/bt_speaker` —— 应用
2. `adb push assets/fonts /mnt/UDISK/speaker/fonts/` —— 字体
   - 注意：rootfs overlay 只有 8MB，放不下 8MB 字体，所以放 UDISK（36MB）
3. `adb push` 各运行库到 `/usr/lib`（freetype、libbz2）
4. `adb push libbtmg.so /lib/`、`bt_test /usr/bin/` —— 蓝牙库 4.0.3 good 版
5. 用 `start-stop-daemon` 后台启动

### 3.3 为什么用 start-stop-daemon 而不是 `&`

不能直接 `bt_speaker &` —— adb 会话退出时会把子进程一起杀掉。必须用：

```bash
/sbin/start-stop-daemon -b -m -S -p /tmp/bt_speaker.pid -x /usr/bin/bt_speaker
```

停掉：`start-stop-daemon -K -p /tmp/bt_speaker.pid`

### 3.4 重新烧录固件后的注意事项

⚠️ **重新烧录 Tina 镜像后，板上的 `libbtmg.so` 会回退到镜像里的 4.0.5 版（有配对 bug）**。必须重跑 `deploy.sh` 把 4.0.3 good 版推回去，否则配对会要 PIN 码。

---

## 4. 蓝牙原理（小白向）

### 4.1 蓝牙音箱用到的协议

手机放歌给蓝牙音箱，用的是 **A2DP**（Advanced Audio Distribution Profile，高级音频分发协议）。音箱端叫 **A2DP Sink**（接收端）。

手机还能远程控制音箱的播放/暂停/上一首/下一首/音量，用的是 **AVRCP**（Audio/Video Remote Control Profile）。歌名/歌手/专辑这些元数据也是 AVRCP 传过来的。

### 4.2 完整音频链路

```
手机
 │ A2DP（压缩的 SBC 音频流）
 ▼
bluetoothd（BlueZ 守护进程，内核和用户态的桥梁）
 │
 ▼
bluealsa 守护进程（把 SBC 解码成 PCM）
 │ 通过 D-Bus 给一个文件描述符
 ▼
btmanager / libbtmg.so（Allwinner 封装，我们调用的库）
 │ bt_a2dp_sink 读 PCM
 ▼
ALSA "default"（Linux 标准音频接口）
 ▼
sun8iw20 片内音频 codec（数模转换）
 ▼
喇叭 / 耳机出声
```

关键点：**SBC 解码不是我们做的**，是 `bluealsa` 守护进程做的。我们的程序（通过 libbtmg）只是「管理者」——负责配对、连接、AVRCP 控制，音频数据由底层自动流转。

### 4.3 配对（Pairing）和免 PIN

蓝牙配对有几种方式，由**双方设备的 IO 能力**决定：

| 本地 IO 能力 | 配对方式 |
|---|---|
| DisplayYesNo | Numeric Comparison（显示 6 位数字，两边确认） |
| KeyboardOnly | Passkey Entry（输 PIN） |
| **NoInputNoOutput** | **Just Works（直接成功，免 PIN）** |

开发板没有屏幕键盘，设成 `NoInputNoOutput`，配对走 Just Works，手机点一下就成。这就是代码里 `bt_manager_agent_set_io_capability(BTMG_IO_CAP_NOINPUTNOOUTPUT)` 的作用。

> ⚠️ 免 PIN 的关键不在我们的应用代码，而在 **libbtmg.so 的版本**。SDK 自带的 4.0.5 版有 bug（adapter 上电回调不触发，导致 IO 能力没设成 NoInputNoOutput，回退成要 PIN 的 Numeric Comparison）。必须用 4.0.3 good 版（285460 字节）。本项目已把 good 版 vendor 进 `third_party/bt/lib/`。

### 4.4 蓝牙模块的硬件特性（重要）

RTL8723DS 模块**常供电**（3.3V 上电就给），**没有复位/使能 GPIO**。固件是上电后由主机通过 UART 下载到模块 RAM 的。

**后果**：软 `reboot` 不会清掉模块 RAM 里的固件。如果换了 BT 库/固件/配置，重新 `rtk_hciattach` 会报 `H5 sync timed out`。**必须拔掉开发板电源（等 15 秒以上）再上电**，让模块彻底复位重新下载固件。

---

## 5. 代码走读

### 5.1 `main.c` —— 程序入口

启动顺序（顺序很重要）：

```c
lv_init();              // 1. LVGL 初始化
sunxifb_init(rotated);  // 2. 显示驱动（打开 /dev/fb0，480×640）
// 3. 分配显示缓冲、注册显示驱动
evdev_init();           // 4. 触摸驱动（打开 /dev/input/event4）
// 5. 注册触摸输入

// 6. FreeType 加载中文字体（运行时读 .otf）
lv_freetype_init(4, 4, 128*1024);
lv_ft_font_init(&ft48);  // 48 号
lv_ft_font_init(&ft32);  // 32 号

ui_main_create();        // 7. 构建界面

// 8. 蓝牙初始化（在 UI 之后）
bt_speaker_init("ZGL_BT_SPEAKER", observer);
bt_speaker_query_state(); // 补发状态（见 §5.3 坑）

// 9. 主循环
while (1) {
    lv_timer_handler();
    usleep(...);
}
```

### 5.2 `bt_speaker.c` —— 蓝牙封装

基于 `app_sdk` 的 `app_bt_audio.c` 精简。核心初始化：

```c
bt_manager_preinit(&g_cb);                                    // 拿回调结构体
bt_manager_enable_profile(BTMG_A2DP_SINK_ENABLE | BTMG_AVRCP_ENABLE); // 启用 A2DP+AVRCP
// 注册各种回调
g_cb->btmg_adapter_cb.adapter_state_cb = ...;                 // 适配器状态
g_cb->btmg_a2dp_sink_cb.a2dp_sink_connection_state_cb = ...;  // A2DP 连接
g_cb->btmg_avrcp_cb.avrcp_play_state_cb = ...;                // 播放状态
g_cb->btmg_avrcp_cb.avrcp_track_changed_cb = ...;             // 歌曲切换
g_cb->btmg_avrcp_cb.avrcp_audio_volume_cb = ...;              // 音量
bt_manager_init(g_cb);                                        // 正式初始化
bt_manager_enable(true);                                      // 上电 adapter
```

adapter ON 回调里做免 PIN + 可发现：

```c
static void adapter_state_cb(btmg_adapter_state_t status) {
    if (status == BTMG_ADAPTER_ON) {
        bt_manager_agent_set_io_capability(BTMG_IO_CAP_NOINPUTNOOUTPUT); // 免 PIN
        bt_manager_set_scan_mode(BTMG_SCAN_MODE_CONNECTABLE_DISCOVERABLE); // 可发现
        // 通知 UI 显示"等待配对"
    }
}
```

### 5.3 线程安全：回调 → UI 的桥梁

**关键坑**：btmanager 的回调（adapter_state_cb、avrcp_*_cb 等）运行在 **btmanager 自己的线程**，而 LVGL 的 UI 操作**必须在 LVGL 主线程**做。直接在回调里 `lv_label_set_text()` 会崩溃或乱码。

解决方法：用 LVGL 的 `lv_async_call()` 把更新请求投递到主线程：

```c
// btmanager 线程调用：
static void ui_post(const char *text, const char *sub) {
    ui_msg_t *m = malloc(sizeof(ui_msg_t));   // 在堆上分配（栈上会被覆盖）
    snprintf(m->text, ...);
    lv_async_call(ui_update_cb, m);            // 投递到 LVGL 线程
}

// LVGL 线程执行：
static void ui_update_cb(void *p) {
    ui_msg_t *m = p;
    lv_label_set_text(g_status_label, m->text); // 安全！
    free(m);
}
```

### 5.4 adapter 状态时序坑

实际调试发现：`bt_manager_enable(true)` 内部 adapter 上电后，ON 回调**可能在 `bt_manager_init` 返回前就触发了**。而此时 UI 还没创建，`lv_async_call` 把消息发到不存在的控件上 → UI 显示「蓝牙关闭」（收到的是 TURNING 瞬态）。

解决：在 UI 创建后、`bt_speaker_init` 之后，主动调一次 `bt_speaker_query_state()`：

```c
int bt_speaker_query_state(void) {
    if (bt_manager_get_adapter_state() == BTMG_ADAPTER_ON)
        obs.on_adapter_on(addr, alias);  // 补发一次
}
```

同时把 `TURNING_ON`/`TURNING_OFF` 瞬态忽略，只在真正 `ON`/`OFF` 时更新 UI。

### 5.5 `ui_main.c` —— 界面

M2/M3 完整播放器界面（480×640 竖屏）：

```
┌────────────────────────────┐
│        蓝牙音箱            │  ← 标题（48 号白色）
│   ─────────────────        │  ← 蓝色分隔线
│        歌名（大字）        │  ← 48 号白色，长名换行
│      歌手 - 专辑           │  ← 32 号灰色，过长省略号
│        等待配对            │  ← 状态（32 号蓝色）
│      34:75:63:...:D5       │  ← 对端地址（16 号灰色）     ┌──┐
│                            │                                │音│
│  ████████░░░░░░░░░░░░      │  ← 进度条（0~100）             │量│
│      1:23 / 3:45           │  ← 时间（当前/总）             │条│
│                            │                                └──┘
│    ◀◀      ▶      ▶▶       │  ← 上一首 / 播放暂停 / 下一首（触摸按钮）
│  手机搜索 ZGL_BT_SPEAKER   │  ← 底部提示
└────────────────────────────┘
```

状态会随回调变化：
- 初始化中（蓝）→ 等待配对（蓝）→ 已连接 + 手机MAC（蓝）→ 播放中（蓝）/ 已暂停（蓝）

**两套异步投递通道**（区别于 M1 的单一 `ui_post`）：

- `post_state(status, sub)`：状态/连接变化（状态大字 + 地址行）
- `post_info(ui_info_t)`：歌曲信息（歌名/歌手专辑/进度/时间/音量），用字段值 `-1`/空串表示"本次不更新该项"，这样一个回调只刷需要变的部分
- `post_play_icon(play_state)`：切换播放/暂停图标（`LV_SYMBOL_PLAY` ↔ `LV_SYMBOL_PAUSE`）

**控制按钮（触摸）**：三个圆形 `lv_btn`，点击经 `bt_speaker_avrcp_cmd()` 发 AVRCP 指令：
- 上一首 ◀◀ → `BTMG_AVRCP_BACKWARD`
- 播放/暂停 ▶/⏸ → 读当前图标判断态，`PLAY`/`PAUSE` 切换
- 下一首 ▶▶ → `BTMG_AVRCP_FORWARD`

未连接时按钮不响应（`g_connected` 守卫）。

**符号字体坑**：`LV_SYMBOL_PLAY/PAUSE/PREV/NEXT` 是 FontAwesome 字形，只有 Montserrat 字体里带。lv_conf.h 默认只开 14/16，播放图标用了 `montserrat_24`（24 号），所以必须把 `LV_FONT_MONTSERRAT_24` 设 1，否则链接报 `undefined reference to lv_font_montserrat_24`。

---

## 6. 里程碑记录

### ✅ M0：编译环境 + 基础显示（已完成 2026-08-17）

**目标**：搭好交叉编译环境，屏幕能显示中文、触摸有响应。

**做了什么**：
- 建项目骨架，vendor LVGL/lv_drivers/freetype/BT 库/字体
- 写 Makefile（gcc-830 硬浮点动态链接）、setup.sh、deploy.sh
- 最小 main.c：480×640 显示「蓝牙音箱」标题 + 触摸按钮计数

**验证结果**：
- 编译出 `build/bt_speaker`（ARM 硬浮点 ELF）
- 板上运行：FreeType 渲染中文正常、触摸点击计数正常（实测点了 9 次）
- 修复两处部署问题：字体改放 `/mnt/UDISK`（rootfs 太小）、补推 `libbz2.so`

**提交**：`0657f57`、`a35b565`

---

### ✅ M1：蓝牙 bring-up（已完成 2026-08-18）

**目标**：手机能搜到开发板蓝牙、免 PIN 配对、UI 显示连接状态。

**做了什么**：
1. **核对 BT 库版本**：vendor 进来的 libbtmg.so 是 1268576 字节的版本（不对），从板上 `adb pull` 了验证过的 good 4.0.3 版（285460 字节，md5 `2b8d26e3...`）替换。同时拉了 `bt_test`（158268 字节）。
2. **写 `bt_speaker.c/.h`**：基于 `app_sdk/component/bt_audio/app_bt_audio.c` 精简。`bt_manager_preinit → enable_profile(A2DP_SINK|AVRCP) → init → enable(true)`，adapter ON 回调设 NoInputNoOutput + CONNECTABLE_DISCOVERABLE。
3. **写 `ui_main.c`**：标题 + 蓝色状态大字 + 副信息 + 底部提示。BT 回调用 `lv_async_call` 转线程。
4. **Makefile 加 BT 库链接**：链接顺序很讲究——`-lbtmg -lshared-mainloop -lbluetooth-internal -lwirelesscom -lgio-2.0 -lgobject-2.0 -lgmodule-2.0 -lglib-2.0 -lffi ...`（libbtmg 依赖 shared-mainloop 里的 `bt_att_new` 等；gobject 依赖 libffi）。
5. **deploy.sh 加推 BT 库**：libbtmg.so → /lib、bt_test → /usr/bin。

**调试过程**：
- 初次部署，BT 栈正常（hci0 UP、名字 ZGL_BT_SPEAKER、Discoverable yes），但 UI 显示「蓝牙关闭」。
- 排查：发现是 adapter ON 回调早于 UI 创建，`lv_async_call` 投递失败；且 TURNING_ON 瞬态被误判为 OFF。
- 修复：①`bt_speaker_query_state()` 在 UI 后补发状态；②忽略 TURNING_ON/OFF 瞬态。
- 重新部署，UI 正确显示「等待配对 + ZGL_BT_SPEAKER」。

**验证结果**（用户实测确认）：
- ✅ 手机搜索到 `ZGL_BT_SPEAKER`
- ✅ 免 PIN 配对成功
- ✅ 连接后 UI 显示「已连接」+ 手机 MAC
- ✅ 手机播放音乐，板子出声
- ✅ 屏幕显示正在播放的歌曲名

> 实际上 M1 一次到位验证了 M2（A2DP 出声）和 M3（歌名显示）的核心功能。

**截图**：

等待配对状态（`docs/m1-waiting-pair.png`）：
- 蓝色大字「等待配对」，灰色副信息「ZGL_BT_SPEAKER」

已连接状态（`docs/m1-connected.png`）：
- 蓝色大字「播放中」，灰色副信息显示歌名

**板上关键状态**（验证用）：
```bash
# 蓝牙栈进程
adb shell "ps | grep -E 'bt_speaker|bluetoothd|bluealsa|hciattach'"
# hci0 状态（应 UP RUNNING PSCAN ISCAN，ACL MTU 1021）
adb shell hciconfig hci0
# 蓝牙名字（应 ZGL_BT_SPEAKER）
adb shell "hciconfig hci0 name"
# BlueZ adapter（Discoverable: yes, Audio Sink UUID）
adb shell bluetoothctl show
```

**提交**：（本次 push）

---

### ✅ M2 + M3：完整播放器 UI（已完成 2026-08-18）

**目标**：把界面从「单行状态」升级成完整播放器——歌名/歌手/专辑、进度条+时间、播放/暂停+上一首+下一首控制按钮、音量条。M1 已通底层（A2DP 出声、AVRCP 回调全接好），本次只改 `ui_main.c`。

**做了什么**：
1. **重写 `ui_main.c`**：参考 `app_sdk/app/ui/page_bt_audio.c` 结构，按 480×640 重排坐标。歌名 48 号白色（长名 `LV_LABEL_LONG_WRAP` 换行）、歌手-专辑 32 号灰色（过长 `LV_LABEL_LONG_DOT` 省略号）。
2. **进度条 + 时间**：`lv_bar` 0~100，由 `on_play_pos` 回调算 `pos*100/len` 更新；时间格式化 `分:秒`。
3. **控制按钮**：三个圆形 `lv_btn`（上一首/播放暂停/下一首），点击经 `bt_speaker_avrcp_cmd()` 发 AVRCP。播放按钮里放 `lv_label` 显示 `LV_SYMBOL_PLAY`/`PAUSE`，随 `on_play_state` 切换。未连接时按钮不响应。
4. **音量条**：右侧竖向 `lv_bar` 0~127 + 数值 label，由 `on_volume` 回调更新。
5. **两套异步投递**：`post_state`（状态/地址）+ `post_info`（歌名/进度/音量，字段 -1=不更新）+ `post_play_icon`（图标）。
6. **开 `LV_FONT_MONTSERRAT_24`**：播放图标符号需要 24 号 Montserrat 字体（FontAwesome 字形）。

**踩的坑**：
- 改了 `lv_conf.h` 开 `montserrat_24` 后第一次 `make` 增量编译，链接仍报 `undefined reference to lv_font_montserrat_24`——因为旧的 `.o` 还在。删掉 `build/lv_font_montserrat_24.o` 重新编译即可。

**验证结果**（用户上板确认）：
- ✅ 布局完整渲染：标题、状态行、进度条、三个圆形控制按钮（中心白色播放符号）、底部提示、右侧音量条
- ✅ hci0 `UP RUNNING PSCAN ISCAN`、ACL MTU 1021（good 4.0.3 库在位）
- ✅ 手机连接后歌名/歌手/进度/时间实时刷新
- ✅ 播放/暂停/上一首/下一首按钮可控制手机
- ✅ 音量条跟随手机音量
- ✅ 播放按钮乐观更新：点击瞬间图标秒切（不等 AVRCP 往返）

**关于歌词（重要说明）**：
AVRCP 协议**拿不到歌词**，只能拿歌名/歌手/专辑/时长/进度/播放状态/音量。所以本项目不显示歌词，用歌名大字 + 歌手小字代替。真要做歌词只有联网查歌词 API（需 WiFi）或本地预置 .lrc 两条路，均不在当前范围。app_sdk 参考代码里的 `label_lyric` 也是用歌名当占位，并非真歌词。

**已知延迟（蓝牙协议固有，非 bug，暂不优化）**：
1. **点板子按钮 → 声音停止/恢复有延迟**：AVRCP 是"遥控器"协议，指令发到手机、手机处理、再控制 bluealsa 音频流启停，整条链路有几百毫秒往返。图标已用乐观更新秒切，声音延迟是协议固有，无法消除。真机蓝牙音箱同样如此。
2. **手机手动按暂停/播放 → 板子图标切换慢（约 1 秒）**：板子靠 AVRCP `on_play_state` 回调切图标，而该回调是手机**周期性上报**（约 1s 一次，间隔由手机决定），不是状态变化瞬间上报。板子侧改不了上报频率。优化需自行监听 bluealsa 音频流状态判断播放态，复杂度高收益低，暂不做。

**提交**：`ac2faea`

---

### ✅ M4a：产品图片 UI（已完成 2026-08-19）

**目标**：把屏厂给的 `bt.png` 产品图（白色球形音箱）上屏，整体 UI 布局优化。

**做了什么**：
1. **图片从文件系统加载**（不走 C 数组内嵌）：lv_conf.h 开 `LV_USE_PNG 1` +
   `LV_USE_FS_POSIX 1`（盘符 `'S'`，路径 `"S:/mnt/UDISK/speaker/image/bt.png"`，
   decoder 自动剥盘符后按绝对路径 open）。图片由 `deploy.sh` 推到板上
   `/mnt/UDISK/speaker/image/`，**换图不用重编译**，同名覆盖重启 app 即生效。
2. **`LV_MEM_CUSTOM 1`**（改用系统 malloc）：PNG 解码 481×641 ARGB 需 ~1.2MB，
   超 LVGL 默认内存池；system malloc 后一次到位。
3. **布局方案两次迭代**：
   - 先试"zoom 缩小放顶部"：发现 lv_img 的 zoom 行为是 **对象保持原图尺寸、
     缩放后的内容在对象内居中**，文字排布与实际显示内容对不上；
   - 像素分析 bt.png 后发现 **481×641 与屏幕 480×640 几乎 1:1**、主体（球形
     音箱）在 y≈240..500、顶/底是干净渐变背景 → 改为**全屏铺底不缩放**，
     文字用深蓝色叠在浅色图上，布局一次到位。
4. **双色方案自动切换**：图片存在（`access()` 检测）→ 深蓝文字/深蓝按钮叠图；
   图片缺失 → 自动回退旧深色 UI（浅色文字），不会白底白字。
5. **修复音量条不渲染的隐藏 bug**：右侧音量条从 M2/M3 起就只有"0"数字、
   110px 竖条本体一直没画出来（旧深色主题下条与背景色接近，肉眼没发现）。
   原因：`lv_obj` 容器默认可滚动（`LV_OBJ_FLAG_SCROLLABLE`），干扰子对象
   `lv_bar` 的对齐定位。修复：清滚动 flag + 改用 `lv_obj_set_pos` 绝对坐标。
6. **播放图标改回 lv_label**：`LV_SYMBOL_*` 本质是字体字形，label 比 img 直接。

**踩的坑（重要）**：
- **Makefile 不跟踪 lv_conf.h 依赖**：改 lv_conf.h 后必须
  `find build -name "*.o" -delete && make` 全量重编，否则旧 .o 里宏开关还是旧值，
  症状千奇百怪（这次是 PNG decoder 注册代码整个没编进去，图片不显示但无报错）。
- **lodepng.c 的 include 链不经过 lv_conf.h**：它有 `#if LV_USE_PNG` 保护但只
  include lodepng.h → 编译成空目标文件。Makefile CFLAGS 加
  `-DLV_USE_PNG=1 -DLV_USE_FS_POSIX=1` 解决。
- **fb 抓屏字节序是 BGRX**（不是 RGBX），分析截图时搞反会得出"颜色不对"的
  错误结论（这次把 accent 蓯误判成橙色，绕了一圈）。

**验证结果**（用户上板确认）：
- ✅ 全屏产品图铺满（fb 采样与原图 9/9 像素级一致）
- ✅ 深蓝标题/歌名/状态/时间叠图清晰可读
- ✅ 三个圆形深蓝按钮叠在球体图上，观感协调
- ✅ 右侧音量条竖条本体正常渲染（x≈447..456，高 110px）
- ✅ 图缺失时优雅降级回深色 UI

**提交**：（本次 push）

---

### ✅ M5：界面重设计「深空玻璃」（代码完成，待上板确认）

**背景**：M4a 的"深色文字直接叠在浅色产品照上"缺乏设计感，推翻重来。

**方案选择**：写了 `scripts/gen_design.py`（PIL+numpy 程序化生成，2x 超采样），
一次出三套 480×640 设计稿（`assets/design/mockup_*.png` + `compare.png` 对比图），
用户选了 **A 深空玻璃**（深蓝黑渐变 + 青色霓虹 + 玻璃卡片 + 唱盘）。
另两套：B 落日唱盘（紫粉橙渐变+毛玻璃操作带）、C 云白极简（苹果风+产品照圆窗）。

**核心架构思想——"静态烘背景，动态才上 LVGL"**：
- 渐变/光晕/玻璃卡片/均衡器装饰条全部烘进一张 480×640 `bg.png`（PIL 画完 2x 降采样，
  渐变无色带、玻璃质感 LVGL 8.3 画不出来）
- 唱盘单独出 184×184 透明 PNG（`disc.png`），播放时 LVGL `lv_img_set_angle` 慢转
  （30s/圈，33ms tick；`LV_IMG_CACHE_DEF_SIZE 0→2` 让 bg+disc 解码常驻，否则每帧
  重新解码 PNG）
- LVGL 只画动态元素：文字/进度条/音量条/按钮/旋转唱盘 → 代码里的 UI 逻辑非常薄

**素材脚本**（`scripts/gen_design.py`）：
- `python3 scripts/gen_design.py` → 三套设计稿+对比图（迭代设计用）
- `python3 scripts/gen_design.py --assets` → 上屏素材 `bg_midnight.png`+`disc.png`
- 已固化到 `assets/image/bg.png`、`assets/image/disc.png`（deploy.sh 原样推板）

**代码改动**：
- `ui_main.c` 全部重写：顶栏（蓝牙符文+标题+状态胶囊）、玻璃卡片内旋转唱盘、
  歌名 cn_44 / 歌手 cn_22（main.c 字体 48/32 → 44/22，22px 用于小字更精致）、
  进度条+时间、右下竖音量条、三控制按钮（播放键青色底+霓虹 shadow 发光，
  前后曲半透明白圆钮）
- `lv_conf.h`：`LV_IMG_CACHE_DEF_SIZE 2`（唱盘旋转性能关键）
- 播放状态 `g_playing` 驱动唱盘转停；乐观更新逻辑保留

**踩的坑**：
- **PIL `rounded_rectangle` 对极小矩形抛 `x1 must be >= x0`**：包了一层 rrect()，
  宽高 < 2r 时 clamp 半径、r<1 退化直角矩形
- **设计稿生成器里 2x 超采样坐标系不匹配**：`paste_tile` 把 2x 贴片贴进 1x 坐标
  → 唱盘/产品照全部偏左上。修为中心 ×S 对齐。教训：混合坐标系统一在函数边界换算
- **bt.png 里找产品球心**：用"网罩深色像素（亮度 70~150）"聚类才准；
  全图色差质心会被背景光带带偏（先猜 300 实际 238）
- **板上 FreeType 字形视觉中心 ≠ label top**：44px 字行高 ~1.45em，字形中心
  偏移 ~0.83em ≈ 36px。要按视觉中心对齐设计稿坐标：top = 目标中心 − 0.83×字号
- **lv_btn 默认 pad 会把子对象挤偏**：状态胶囊里圆点+文字溢出右边。
  `lv_obj_set_style_pad_all(pill, 0, 0)` 解决
- **LVGL 内置蓝牙符号可用**：`LV_SYMBOL_BLUETOOTH`（U+F293）在 montserrat 12/14/16/24
  的 FontAwesome 字形表里都有（grep `62099` 确认），但 24 没有——顶栏用 16
- **UDISK 满导致字体推一半**：旧 bt.png 没删 + 双份字体占满 36MB 分区。
  删旧图重推，`wc -c` 校验大小（板上无 md5sum）

**验证结果**（fb0 抓帧，BGRX）：
- ✅ 背景渐变/玻璃卡片/均衡器装饰与设计稿一致
- ✅ 唱盘居中卡片内，透明 PNG 旋转无方角（素材层面已验证 40° 旋转四角 alpha=0）
- ✅ 顶栏符文+标题+状态胶囊（"等待配对"完整放下）+ 下方别名
- ✅ 进度条/时间/音量条/三按钮位置与设计稿对齐
- ✅ hci0 `UP RUNNING PSCAN ISCAN` 正常，蓝牙功能无回归
- 待办：手机连上后看播放态（唱盘转动/暂停停转）实机效果

---

### ✅ M5b：界面重设计 V2「液态玻璃」（代码完成，待上板确认）

**背景**：用户觉得 M5 深空玻璃风格偏老（"深色+霓虹"是 2018-2020 风），
要求出五套 2025-26 潮流方向、贴合白色球形产品本体的方案。

**五套方案**（`scripts/gen_design.py --v2`，稿子 `assets/design/mockup2_*.png`、
对比 `compare2.png`）：
- **D1 液态玻璃**（✅用户选定）：iOS 26 风，浅银白底+透明玻璃层叠+折射圆，深色文字+iOS 蓝
- D2 极光流彩：紫粉青柔光 mesh 渐变+磨砂白卡+流光弧线
- D3 软糖黏土：粉彩底+3D 黏土控件（内高光+厚投影），clay_rrect/clay_circle
- D4 液态铬：Y2K 铬银镜面渐变边框+深空底，chrome_rrect（numpy 竖向铬渐变条）
- D5 Bento 便当盒：分格卡片网格，产品实拍大图做封面格，布局重排（信息架构不变）

**素材与代码改动**：
- `--assets D1` 出 `bg.png`（浅银白渐变+两个玻璃折射圆+玻璃卡片含顶高光）
  + `disc.png`（深灰盘面+白标，与设计稿同参数）
- `ui_main.c` 仅换主题色宏 + 控件样式：播放大钮 青色霓虹→近黑（COL_PLAY_BG 0x1C202C，
  玻璃上的深色锚点），前后曲钮 半透明白→玻璃白（OPA_70+白描边），文字全部转深色，
  状态胶囊白底 60% + iOS 蓝状态点。布局/坐标/旋转逻辑零改动
- 音量条/进度条轨道改浅灰（0xFFFFFF OPA_20 在浅底上即为浅灰），指示器 iOS 蓝

**新踩的坑（PIL 重度警告）**：
- **PIL 的 RGBA 画布上 ImageDraw 不做 alpha 混合**：半透明色直接替换像素
  （写死 (17,24,39,22) 的"浅灰"实际渲染成近黑！）。`Draw(im,"RGBA")` 混合模式
  只在 **RGB 画布**上生效。V2 全部改 RGB 画布 + `composite()` helper
  （RGBA→alpha_composite / RGB→paste+自身 alpha）
- **正则批量改代码会把 for 循环内缩进搞乱**：`img.alpha_composite(X)` →
  `composite(img, X)` 的替换产生非法缩进，两处需手工修——批量重构后必须跑一遍
  验证
- **半透明 UI 色值必须在"真实混合"下验收**：V1 深色主题里半透明白叠深底，
  替换≠混合的偏差肉眼难辨；V2 浅色主题立刻炸出来。教训：玻璃/毛玻璃类设计
  在 LVGL 侧天然正确（真 alpha 混合），设计稿侧必须用 RGB 画布混合模式渲

**验证结果**（fb0 抓帧与设计稿并排比对）：
- ✅ 背景浅银白渐变+玻璃卡片+折射圆与设计稿一致
- ✅ 新唱盘（深灰+白标）居中，顶部高光自然
- ✅ 状态胶囊/歌名/进度/控制按钮全部深色文字，iOS 蓝点缀
- ✅ hci0 `UP RUNNING PSCAN ISCAN` 无回归
- 待办：手机连接播放态实机确认

---

## 7. 后续里程碑（待做）

- [ ] **M4b**：开机自启（rc.final / init 脚本）+ README 收尾

---

## 8. 可优化项（已记录，暂不做）

以下为已知可优化点，当前版本未实现，记录于此供后续参考。均为**低收益/高成本**或**协议限制**，不影响核心功能。

### 8.1 手机手动控制 → 板子图标切换延迟（约 1 秒）
- **现象**：在手机上按暂停/播放，板子屏幕的播放图标约 1 秒后才切换。
- **原因**：板子靠 AVRCP `on_play_state` 回调切图标，该回调由手机**周期性上报**（约 1s/次，间隔由手机蓝牙栈决定），非状态变化瞬间上报。
- **优化思路**：不依赖 AVRCP 回调，改为监听 bluealsa 的音频流状态（`bluealsa-aplay` 进程或 PCM 流启停事件）自行判断播放态。需引入 bluealsa D-Bus 或 PCM 状态监听，复杂度高。
- **为何暂不做**：收益仅"图标快 ~1 秒切换"，成本是引入新的状态监听机制且要和 AVRCP 状态去重，得不偿失。真机蓝牙音箱亦有此延迟。

### 8.2 点板子按钮 → 声音停止/恢复延迟
- **现象**：点板子暂停按钮，图标秒切，但声音几百毫秒后才停。
- **原因**：AVRCP 是"遥控器"协议，指令经 手机蓝牙栈 → bluealsa 音频流启停 整条链路，固有往返延迟。
- **优化思路**：无有效优化。这是 A2DP/AVRCP 协议设计决定的，板子侧无法绕过。
- **为何暂不做**：协议固有，无法消除。

### 8.3 歌词显示
- **现状**：不显示歌词，用歌名大字 + 歌手小字代替。
- **原因**：AVRCP 协议无歌词字段，手机不会发送歌词。
- **优化思路**：
  1. **联网歌词 API**：板子经 WiFi 联网，按歌名查网易云/QQ音乐歌词库 + 时间轴，滚动显示。需 WiFi 驱动 + 联网 + HTTP + 歌词解析，工作量大。
  2. **本地 .lrc 预置**：提前把歌词文件放板子，按歌名匹配。不现实——歌是手机放的，板子无法预知用户放什么。
- **为何暂不做**：方案 1 是独立大功能（需 WiFi 联网生态），超出"蓝牙音箱"范畴；方案 2 不可行。

### 8.4 图片资源（✅ 已在 M4a 实现）
- **现状**：M4a 已实现全屏产品图（`assets/image/bt.png` → 板上 `/mnt/UDISK/speaker/image/bt.png`），文件系统加载 + 缺图优雅降级。换图同名覆盖重启 app 即生效，不用重编译。
- **剩余可优化**：图片是 270KB PNG 每次启动解码 ~1.2MB 内存 + 解码耗时；若后续多图/动画需求，可考虑预转换 LVGL 二进制格式（`lv_img_conv`）或缩到屏幕精确尺寸。当前单图无感，不做。

---

## 9. 常见问题排查

| 现象 | 原因 / 解决 |
|---|---|
| 编译报 `-mfpu=neon` 不识别 | 用成宿主机 gcc 了，检查 `make` 时 `CC` 是否指向 toolchain |
| 编译报 `STAGING_DIR not defined` | Makefile 里 `export STAGING_DIR` 没生效，重新 `make` |
| 运行报 `cannot open shared object: libbtmg.so` | 板上缺库，跑 `deploy.sh` 推一次 |
| 运行报 `error while loading shared libraries: libbz2.so.1.0` | 板上无此库，`deploy.sh` 已补推 |
| 手机搜不到蓝牙 | ① 检查 `hciconfig hci0` 是否 `PSCAN ISCAN`；② 换库后没彻底断电重启（拔电源 15s） |
| 配对要 PIN 码 | libbtmg.so 不是 4.0.3 good 版（285460B），重推 `deploy.sh` |
| `H5 sync timed out` | 模块没彻底断电，固件没重下；拔电源 15s 再上电 |
| UI 显示「蓝牙关闭」但 hci 是 UP | adapter 回调时序问题，确认调了 `bt_speaker_query_state()` |
| UI 乱码/崩溃 | 在 btmanager 回调里直接操作 UI 了，必须用 `lv_async_call` 转线程 |
| 中文显示不出来 | FreeType 字体没加载成功，检查 `/mnt/UDISK/speaker/fonts/*.otf` 是否存在 |
| `adb push` 报 `No space left` | rootfs 满了，字体/大文件放 `/mnt/UDISK` |
