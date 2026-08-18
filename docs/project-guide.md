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
├── .gitignore                # 排除 toolchain/ build/
├── docs/                     # 本文档 + 截图
│   └── project-guide.md
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
│   └── images/              # 界面图片（后续 M4 添加）
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

**验证结果**（待用户上板确认）：
- fb 像素分析确认布局完整渲染：标题(y≈88)、状态行(y≈320-360)、进度条(y≈424)、控制按钮行(y≈472-544，三个圆形按钮+中心白色播放符号)、底部提示(y≈616)、右侧音量条
- hci0 `UP RUNNING PSCAN ISCAN`、ACL MTU 1021（good 4.0.3 库在位）
- 待手机连接确认：歌名/歌手/进度/时间实时刷新、播放暂停按钮可控制、音量条跟随手机音量

**提交**：（本次 push）

---

## 7. 后续里程碑（待做）

- [ ] **M4**：图片资源挂载点（放 PNG 即生效）+ 开机自启 + 收尾

---

## 8. 常见问题排查

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
