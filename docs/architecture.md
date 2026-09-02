# T113 蓝牙音箱 — 架构重构方案（v1.0，待实施）

> 本文档是架构重构的**总计划**。当前（2026-08）代码已完成 M0~M5b 功能里程碑但三层耦合在一起；
> 本文定义目标架构（分层/接口/构建/CI）和分阶段迁移路线，供评审后按阶段实施。
> 实施过程中每完成一阶段，在 §9 路线图表格里打勾并在 `project-guide.md` 追加记录。

---

## 1. 为什么要重构

现状三个源文件（`src/main.c` / `src/bt_speaker.c` / `src/ui/ui_main.c`）分工是"按层写死"的：

| 痛点 | 现状表现 |
|---|---|
| **UI 与业务耦合** | `ui_main.c` 一半代码是 btmanager 回调 + `lv_async_call` 投递（`ui_info_t/ui_state_t` 两套消息结构），一半是画 D1 主题。换 UI 主题要动投递代码；加业务（USB 播放、WiFi 歌词）要动 UI 文件 |
| **绑死平台** | 事件投递依赖 LVGL 的 `lv_async_call` + Linux pthread + POSIX malloc；换单片机（裸机/RTOS）事件层全部重写 |
| **构建是手工 Makefile** | `include lvgl.mk` 通配收集，不跟踪头文件依赖——改 `lv_conf.h` 必须手删全部 `.o`（M4a 踩坑：PNG decoder 整个没编进去）；无法在其他机器/CI 复现 |
| **无编译门禁** | 推 GitHub 后没人知道编不编得过；本机 1.2GB 工具链被 gitignore，换机器必须先跑 setup.sh |

重构目标：**CMake 构建 + UI/业务通过事件契约解耦 + OSAL 分层 + 可移植单片机 + GitHub CI 编译门禁**。

---

## 2. 设计原则

1. **依赖只允许向下**：上层认识下层接口，下层完全不认识上层（UI 不 include 任何 bt 头文件；services 不 include 任何 lvgl 头文件）。
2. **一份事件契约**：全项目唯一数据交换格式 `player_event_t`（core 层，纯 C99），业务往里写、UI 从里读。沿用现约定：**整型 `-1` / 字符串空 = 本事件不更新该项**。
3. **静态烘背景，动态才上 LVGL**（M5 已验证的架构不变）：主题素材 PNG + 少量动态控件，主题可整体替换。
4. **踩坑固化平移，不重踩**：现 Makefile 里的 ABI/链接顺序/宏定义全部原样搬进 CMake，并标注"勿动"。
5. **每阶段独立可上板回归**：任何一步编译产物都等价或功能不变，随时可以停在一个稳定点 commit。

---

## 3. 目标分层

```
┌────────────────────────────────────────────────────────┐
│ apps/     应用组合层：选 UI 主题 + 选业务后端 + main     │  ← 全项目唯一"认识所有人"的地方
├────────────────────────────────────────────────────────┤
│ ui/       ui_core(字体注册/事件drain辅助)               │
│           themes/<主题>/  实现 ui_backend_t             │  只认 player_event_t，不认 btmanager
├────────────────────────────────────────────────────────┤
│ services/ player_backend.h 接口                         │
│           btmg/(Allwinner btmanager)  sim/(模拟播放器)  │  只向上抛 player_event_t，不认 LVGL
├────────────────────────────────────────────────────────┤
│ core/     领域模型：player_event_t / player_cmd_t      │  纯 C99，无 OS / 无 LVGL / 无 BT
├────────────────────────────────────────────────────────┤
│ osal/     queue / mutex / thread / sleep / now / log    │  osal_posix.c（Linux）
│           （裸机 osal_none.c 为后续移植预留）            │
├────────────────────────────────────────────────────────┤
│ ports/    板级移植：显示(fb) / 触摸(evdev) / 字体(FreeType)│
│           / main 入口 + 主循环 + drain                   │  唯一碰硬件和 LVGL 驱动的地方
├────────────────────────────────────────────────────────┤
│ third_party/  lvgl 8.3 / lv_drivers / freetype / btmg  │  vendor 不动（只新增构建描述）
└────────────────────────────────────────────────────────┘
```

依赖方向单向向下；**横向不认识**（ui ↔ services 之间只通过 core 的事件结构和 apps 层注入的函数指针交流）。

---

## 4. 核心接口（草案全文）

### 4.1 `core/player_types.h` — 唯一数据契约

```c
#ifndef PLAYER_TYPES_H
#define PLAYER_TYPES_H
#include <stdint.h>

/* 播放控制命令（UI 按钮 → 业务后端；各后端自行映射，btmg 后端映射到 AVRCP） */
typedef enum {
    PLAYER_CMD_PLAY = 0,
    PLAYER_CMD_PAUSE,
    PLAYER_CMD_TOGGLE,      /* 由 UI 乐观更新决定发 PLAY 还是 PAUSE 时用语义化命令 */
    PLAYER_CMD_NEXT,
    PLAYER_CMD_PREV,
} player_cmd_t;

/* 事件类型 */
typedef enum {
    PLAYER_EVT_ADAPTER = 0,   /* 蓝牙开关：evt.on=0/1 */
    PLAYER_EVT_CONN,          /* 连接状态：evt.connected + addr */
    PLAYER_EVT_PLAY_STATE,    /* evt.play_state: 1=playing 2=paused */
    PLAYER_EVT_TRACK,         /* title/artist/album/len_ms（ pos_ms=0） */
    PLAYER_EVT_POS,           /* pos_ms + len_ms，其余不更新 */
    PLAYER_EVT_VOL,           /* volume 0..127 */
} player_evt_type_t;

typedef struct {
    player_evt_type_t type;
    /* 通用字段：不适用的整型填 -1、字符串填空串 = 不更新 */
    char title[64];
    char artist[48];
    char album[48];
    char addr[18];            /* "AA:BB:CC:DD:EE:FF" */
    int32_t pos_ms;
    int32_t len_ms;
    int32_t volume;           /* 0..127（btmg 量程） */
    int32_t play_state;       /* 1=playing 2=paused */
    int32_t connected;        /* 0/1 */
    int32_t on;               /* adapter 开关（EVT_ADAPTER 用） */
} player_event_t;             /* ~210B，定长，MCU 友好 */

#endif
```

要点：
- **定长值类型**（不是指针），跨线程投递直接 memcpy 进队列，无生命周期问题——这是能退化到裸机环形缓冲的关键。
- 体积 ~210B 是刻意的：歌名 64B 够 AVRCP 现实曲目；比这长的歌名本来屏上也放不下。

### 4.2 `services/player_backend.h` — 业务接口（换业务源=换实现，UI 无感知）

```c
#include "player_types.h"

typedef struct {
    /* 初始化。emit 由上层注入：后端在**业务线程**里组好 player_event_t 调 emit 投递。
     * 返回 0 成功。 */
    int  (*init)(void (*emit)(const player_event_t *ev));
    void (*deinit)(void);
    /* 同步查询当前状态并补发事件（对应现 bt_speaker_query_state 的时序兜底） */
    int  (*query_state)(void);
    /* 播放控制。UI 线程直接调用（与现状一致：AVRCP 往返本来就是异步的） */
    int  (*cmd)(player_cmd_t c);
} player_backend_t;

/* 可选实现（链接期选择，apps 层 extern 引用） */
extern const player_backend_t player_backend_btmg;   /* services/btmg/：Allwinner btmanager */
extern const player_backend_t player_backend_sim;    /* services/sim/：模拟数据源（宿主/CI 用） */
```

### 4.3 `ui/ui_backend.h` — UI 接口（换主题=换实现，业务无感知）

```c
#include "lvgl/lvgl.h"      /* UI 层当然可以用 LVGL，只是不能用 bt 头文件 */
#include "player_types.h"

/* apps 层注入给主题的运行环境（主题自己不创建字体、不发命令） */
typedef struct {
    lv_obj_t *scr;                                  /* 活动屏幕 */
    const lv_font_t *font_large;                    /* ports 层加载好的中文字体（现 cn_44） */
    const lv_font_t *font_small;                    /* （现 cn_22） */
    void (*cmd_request)(player_cmd_t c);            /* 按钮点击 → apps 层转给 backend->cmd() */
} ui_env_t;

typedef struct {
    int  (*init)(const ui_env_t *env);              /* 建控件、探测/加载主题素材 */
    void (*on_event)(const player_event_t *ev);     /* **已在 UI 线程**（drain 后回调） */
    void (*deinit)(void);
} ui_backend_t;

extern const ui_backend_t ui_backend_liquidglass;   /* ui/themes/liquidglass/（现 D1 主题） */
```

要点：
- 现有 `ui_main.c` 里的 `bt_on_*` 回调全部消失——主题只实现 `on_event`，收到的是**已经在 UI 线程**的事件，内部不再需要 `lv_async_call`/malloc 投递那套（~100 行胶水直接删掉）。
- 按钮点击：主题调 `env->cmd_request(PLAYER_CMD_xxx)`，由 apps 层调 `backend->cmd()`。主题完全不认识后端。
- 唱盘旋转 timer、乐观更新（点击瞬间切图标不等 AVRCP 往返）、素材缺失降级——全部留在主题内部，接口不管。

### 4.4 `osal/osal.h` — OS 抽象

```c
/* 队列：定长元素、多生产者单消费者。满时按策略丢最旧（进度类事件新值覆盖旧值是正确语义） */
typedef struct osal_queue *osal_queue_handle_t;
osal_queue_handle_t osal_queue_create(uint16_t depth, uint16_t item_size);
bool osal_queue_send(osal_queue_handle_t q, const void *item);       /* 非阻塞，满则丢最旧 */
bool osal_queue_recv(osal_queue_handle_t q, void *out, uint32_t ms); /* ms=0 非阻塞 */

typedef struct osal_mutex *osal_mutex_handle_t;
osal_mutex_handle_t osal_mutex_create(void);
void osal_mutex_lock(osal_mutex_handle_t m);
void osal_mutex_unlock(osal_mutex_handle_t m);

void osal_sleep_ms(uint32_t ms);
uint32_t osal_now_ms(void);

typedef enum { OSAL_LOG_ERROR, OSAL_LOG_WARN, OSAL_LOG_INFO, OSAL_LOG_DEBUG } osal_log_level_t;
void osal_log(osal_log_level_t lv, const char *tag, const char *fmt, ...);
```

- **Linux**：`osal_posix.c` → pthread_mutex + mutex 保护的环形缓冲（或 msg queue）。
- **裸机（预留）**：`osal_none.c` → 关中断环形缓冲，`recv(ms>0)` 退化为轮询。接口同构，上层一行不改。
- 现阶段只需要 queue + sleep/now + log（thread/mutex 给 sim 后端和未来扩展用）。

---

## 5. 事件流（替代 lv_async_call）

```
【数据链路】
btmanager 线程                                UI (LVGL) 线程
─────────────                                ──────────────
btmg avrcp_track_changed_cb
  → 组装 player_event_t{EVT_TRACK,...}
  → emit()  [apps 注入]
      → osal_queue_send(q, &ev)   ──┐
                                    │ 定长环形队列(16槽)
btmg avrcp_audio_volume_cb          │
  → emit → send ───────────────────>│   lv_timer 33ms drain:
                                    │   while(osal_queue_recv(q,&ev,0))
                                    │       ui->on_event(&ev)
                                    │       → lv_label_set_text / lv_bar_set_value ...
                                    │
【控制链路】
用户点播放钮（UI 线程）
  → 主题：乐观更新图标（秒切），env->cmd_request(PLAYER_CMD_PAUSE)
  → apps：player_backend->cmd(PLAYER_CMD_PAUSE) → btmg → AVRCP → 手机
  → 手机 1~2s 后回 avrcp_play_state_cb → 走数据链路校正图标（乐观更新机制保留）
```

- 队列 16 槽 × ~210B ≈ 3.4KB。进度事件天然可丢旧（新值覆盖旧值），丢最旧策略正确。
- **早到事件自动消化**：现状的 `bt_speaker_query_state()` 时序坑（adapter ON 回调早于 UI 创建），新架构下队列在 backend init 之前创建，早到事件先攒在队列里、UI 起来后第一次 drain 就吃到——`query_state()` 仍保留作兜底。
- 裸机同构：`osal_none` 队列=关中断环形缓冲，主循环 drain=LVGL timer 的裸机等价物（lv_timer_handler 本来就是轮询的），业务层零改动。

---

## 6. 目录树与迁移映射

> M9 起源码收编进 `src/`（`services/btmg|sim`、`ui/themes/liquidglass` 压平；
> `cmake/toolchain/` 压平到 `cmake/`）。以下为当前实际布局：

```
.
├── CMakeLists.txt                      # 顶层：host/ARM 分流 + add_subdirectory
├── build.sh                            # 一键构建入口（arm / -host / -clean）
├── cmake/
│   ├── openwrt-armhf.cmake             # 本机 ./toolchain（STAGING_DIR 注入）
│   ├── gnueabihf.cmake                 # CI 用 apt 交叉工具链
│   └── third_party.cmake               # vendor 源码/库收集（lvgl/lv_drivers/freetype/bt）
├── src/
│   ├── core/player_types.h
│   ├── osal/{osal.h,osal_posix.c}
│   ├── services/
│   │   ├── player_backend.h
│   │   ├── btmg_player.c               # ← 原 src/bt_speaker.c 迁移（接口化）
│   │   └── sim_player.c                # 模拟数据源（host/CI 用）
│   ├── ui/
│   │   ├── ui_backend.h
│   │   ├── theme.h                     # 主题色宏/素材/布局常量
│   │   └── ui_liquidglass.c            # D1 液态玻璃主题
│   ├── ports/
│   │   ├── lv_port_disp.c / lv_port_indev.c / lv_port_font.c
│   │   ├── main_linux.c                # 组装 + 主循环 + drain
│   │   └── lv_conf.h                   # lvgl __has_include 发现（include 路径里有 src/ports）
│   ├── apps/app_player.c               # 组装层（队列 → UI init → backend init → drain）
│   └── tests/{osal_test.c,sim_loop_test.c}
├── third_party/                        # vendor 不动
├── scripts/                            # setup.sh / deploy.sh（产物路径不变）
└── .github/workflows/build.yml
```

迁移映射速查：

| 现有代码 | 去向 |
|---|---|
| `src/bt_speaker.c`（preinit/profile/回调组/AVRCP cmd） | `services/btmg/btmg_player.c`，包成 `player_backend_btmg`；回调里改为组 `player_event_t` 调 `emit` |
| `ui_main.c` 的 `bt_on_*` + `post_*` + `lv_async_call` 胶水（~100 行） | **删除**，由 OSAL 队列 + drain 取代 |
| `ui_main.c` 的控件构建/样式/唱盘旋转/乐观更新 | `ui/themes/liquidglass/ui_liquidglass.c`（逻辑零改动，`ui_info_t` 判断改读 `player_event_t` 字段） |
| `main.c` 的 fb/evdev/freetype 初始化 | `ports/lv_port_*.c` 三个文件 |
| `main.c` 的 main() 组装顺序 + 主循环 | `ports/main_linux.c` + apps 组装（main 里注入 cmd_request、建队列、选 backend/主题） |
| `lv_conf.h` / `lv_drv_conf.h` | 原地不动（include 路径照抄现 Makefile，见 §7） |

---

## 7. CMake 设计

### 7.1 顶层结构

```cmake
# CMakeLists.txt（示意）
cmake_minimum_required(VERSION 3.16)
project(bt_speaker C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)   # 产物路径不变 → deploy.sh 零改动

include(cmake/lvgl.cmake)          # static: lvgl
include(cmake/lv_drivers.cmake)    # static: lv_drivers
add_subdirectory(osal core)        # 实际按需组织，示意
# ... services / ui / ports
add_executable(bt_speaker
    ports/main_linux.c ports/lv_port_*.c
    services/btmg/btmg_player.c
    ui/themes/liquidglass/ui_liquidglass.c ...)
target_link_libraries(bt_speaker PRIVATE lvgl lv_drivers btmg_full freetype_lib)
```

### 7.2 必须原样平移的"勿动"清单

| 项 | 内容 | 为什么（踩坑史） |
|---|---|---|
| ABI 旗标 | `-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard` | 板上 rootfs 是 armhf；软浮点工具链编出的产物跑不了/链接不了 libbtmg |
| STAGING_DIR | toolchain 文件里 `set(ENV{STAGING_DIR} ...)` | OpenWrt wrapper gcc 没 this 直接 fatal |
| lodepng 宏 | 全局 `add_compile_definitions(LV_USE_PNG=1 LV_USE_FS_POSIX=1)` | lodepng.c 的 include 链不经过 lv_conf.h（M4a 坑） |
| lv_conf 发现 | include 路径里保留 `-I src`（或新 `ports/` 放置处） | lvgl 靠 `__has_include("lv_conf.h")` + `-I` 路径找到配置；机制照抄不改 |
| lv_drv_conf 发现 | include 路径保留 `third_party/` | lv_drivers 以相对路径找上级目录的 lv_drv_conf.h |
| 链接顺序 | `-lbtmg -lshared-mainloop -lbluetooth-internal -lwirelesscom -lgio-2.0 -lgobject-2.0 -lgmodule-2.0 -lglib-2.0 -lffi -ldbus-1 -lasound -ljson-c -lsbc -lreadline -lncursesw -lz -lbz2 -lpthread -lrt -ldl -Wl,-rpath,/usr/lib` | btmg→shared-mainloop→bluetooth-internal；gobject→ffi 依赖链，顺序错=undefined reference（M1 坑）。CMake 里写成一个 INTERFACE 库 `btmg_full` 的 INTERFACE_LINK_LIBRARIES，一次性固化 |
| 库路径 | `-L third_party/bt/lib`、`-L third_party/freetype/lib` | vendor 的 good 4.0.3 btmg（285460B）和 freetype/libbz2 都在这 |

### 7.3 LVGL/lv_drivers 的源码收集

- LVGL 8.3 官方树**不带 CMakeLists**（只有 .mk）。在 `cmake/lvgl.cmake` 里按 `lvgl.mk` 同款 GLOB 七个子目录（core/draw/extra/font/hal/misc/widgets）+ `src/` 根的头文件依赖，建 static lib。
- lv_drivers **自带** CMakeLists 会把 wayland/sdl/win32drv 全部 glob 并尝试 pkg-config wayland——不适用。**不用它**，`cmake/lv_drivers.cmake` 同样自己 GLOB（display/indev 根目录 .c；wayland/sdl 等目录文件因 `lv_drv_conf.h` 宏全关，现状 .mk 也在编它们，等价保留即可——编成空 TU 无害）。
- **CMake 的直接收益**：`lv_conf.h` 等头文件依赖被自动跟踪（`CMAKE_C_SCAN` 对 gcc 有效），改 lv_conf.h 增量编译自动全量重编相关目标——M4a 的"手删 .o"坑从机制上消失。

### 7.4 两个工具链文件

```cmake
# cmake/toolchain/openwrt-armhf.cmake —— 本机（./toolchain，setup.sh 产物）
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(TC ${CMAKE_SOURCE_DIR}/toolchain/bin/arm-openwrt-linux-gcc)
set(CMAKE_C_COMPILER ${TC})            # CMake 从编译器路径自动推 -fuse-ld/sysroot
set(ENV{STAGING_DIR} ${CMAKE_SOURCE_DIR}/toolchain/bin)   # 勿动
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard")
```

```cmake
# cmake/toolchain/gnueabihf.cmake —— CI（apt install gcc-arm-linux-gnueabihf）
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard")
# 注意：无 STAGING_DIR 需求（不是 OpenWrt wrapper）
```

---

## 8. GitHub CI 设计

`.github/workflows/build.yml`，两个并行 job：

```yaml
jobs:
  build-arm:                      # 真门禁：完整交叉编译 + 链接 vendor ARM 库
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y gcc-arm-linux-gnueabihf
      - run: cmake -B build-ci -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/gnueabihf.cmake -DCMAKE_BUILD_TYPE=Release
      - run: cmake --build build-ci -j2
      - run: file build/bt_speaker | tee /dev/stderr | grep -q "ELF 32-bit.*ARM"
      # ↑ 断言产物是 32-bit ARM EABI5 hard-float（与板上 rootfs 匹配）

  build-host:                     # 可移植层门禁：core/osal/sim 严格告警编译 + 自测
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake -B build-host -S cmake/sim -DCMAKE_BUILD_TYPE=Release   # 只编 core+osal+sim
      - run: cmake --build build-host -j2 && ./build-host/sim_test
      # ↑ 事件组装正确性 / 队列收发 / 丢最旧策略的自测可执行
```

- host job 不需要 LVGL/BT——这正是分层的验收：**可移植层可以在无板无 GUI 的环境编译运行**。
- sim 自测内容（阶段 5 写）：`sim_player` 吐 N 个事件 → 队列 → drain 全收到；灌 32 个事件 → 队列只留最新 16 个（丢最旧）；`-Wall -Wextra -Werror`。

### 已知风险与预案

| 风险 | 预案 |
|---|---|
| Debian gnueabihf（gcc 12/13）链 OpenWrt gcc8.3 的 vendor .so（glibc 2.29 armhf）报符号版本（`GLIBC_x.y not found`） | 一般只影响**运行**不影响链接（链接器按符号表解析，运行时才查版本）。若链接期就报错：CI 加变体 job 用 `-Wl,--allow-shlib-undefined` 只验编译+我们自己的 .o 链接；板上真机验证仍以本机 openwrt 工具链为准 |
| vendor 静态库（libbluetooth-internal.a/libshared-mainloop.a）与新版 binutils 不兼容（罕见） | 同上降级为"编译门禁"；或 CI 缓存 openwrt 工具链 tar（阶段外选项） |
| `file` 断言因 readelf 输出格式差异挂掉 | 用 `arm-linux-gnueabihf-readelf -h | grep -q 'Machine.*ARM'` 兜底 |

---

## 9. 分阶段实施路线图

每阶段独立 commit + 上板回归，可随时停在任意稳定点。

| 阶段 | 内容 | 验证标准 | 状态 |
|---|---|---|---|
| **0** | 本文档评审通过 | 用户确认 | ✅ 2026-08-30 |
| **1** | CMake 骨架：顶层 + lvgl/lv_drivers 收集 + 2 个 toolchain 文件；**源码不动**，仍编 src/ 三件套 | 本机 cmake 编出 ELF 与 Makefile 产物等价（`file`/大小/`nm` 关键符号比对）；deploy 上板跑通无回归 | ✅ 2026-08-31（见下方实施记录） |
| **2** | `core/player_types.h` + `osal/osal.h` + `osal_posix.c`；host 侧队列收发自测程序 | host 上跑通：多线程生产/消费、丢最旧策略正确 | ✅ 2026-08-31 |
| **3** | 业务迁移：`services/btmg/btmg_player.c`（事件化改造）+ main 改为队列 drain（此阶段 UI 仍用旧文件，drain 后转调现有 UI 更新函数） | **板上全回归**：配对/播放/进度/音量/三按钮/乐观更新/断连清屏 | ✅ 代码完成（板上回归待用户执行） |
| **4** | UI 迁移：`ui/themes/liquidglass/` + `ports/lv_port_*` + `main_linux.c` 组装；删 `src/` 与 `Makefile` | 板上全回归 + 删 bg.png 验证降级 UI；素材恢复验证主题 UI | ✅ 代码完成（板上回归待用户执行） |
| **5** | `services/sim/` + host 构建目标 + `.github/workflows/build.yml`；推送触发首次 CI | CI 两 job 全绿；sim 自测通过 | ✅ 代码完成（CI 首跑结果待推送后确认） |
| **6** | `project-guide.md` 追加架构重构节 + 本文档按实况修订（接口若有出入） | 文档与代码一致 | ✅ |

> 阶段 3 是风险最高的一步（动事件链路），单独成阶段以便出问题时精确回退。阶段 4 之后仓库才"正式"是新架构；之前 Makefile 一直是可用退路。

### 9.1 阶段 1 实施记录（2026-08-31）

新增文件：`CMakeLists.txt`、`cmake/third_party.cmake`、`cmake/toolchain/openwrt-armhf.cmake`、`cmake/toolchain/gnueabihf.cmake`。用法：

```bash
cmake -B build-cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/openwrt-armhf.cmake
cmake --build build-cmake -j
```

**实施中新踩的坑**（计划外的真实差异）：

1. **STAGING_DIR 注入方式**：toolchain 文件只在 configure 期执行，`set(ENV{...})` 不会带进 build 期。编译步用 `CMAKE_C_COMPILER_LAUNCHER env STAGING_DIR=…` 解决；**链接步没有对应 LAUNCHER 变量**，要用 `set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "env STAGING_DIR=…")`。实测只有 gcc wrapper（内部 gcc.bin）读 STAGING_DIR，ar/ranlib 不需要。
2. **lvgl 静态库自身要 freetype 头**：`lv_freetype.c`（在 lvgl 源码树里）include `ft2build.h` 和同目录 `lv_freetype.h`——旧 Makefile 靠全局 CFLAGS，CMake 里必须给 `lvgl` target 也加这两个 include 路径。
3. **`-lbz2` 路径**：工具链 sysroot 没有 libbz2，vendor 副本在 `third_party/freetype/lib/`，`bt` 接口库的 `target_link_directories` 要把该目录一并加上。
4. **lv_drivers 收集范围**：自带 CMakeLists 会 glob 全部（含 win32drv）+ pkg-config wayland，不可用；按 `lv_drivers.mk` 同款 6 处 glob（根/wayland/indev/gtkdrv/display/sdl，**不含 win32drv**）自写，行为等价。
5. **产物等价性结论**：CMake 产物（3.5MB @ -O2 -g）比 Makefile（4.2MB）小 ~700KB，原因是**链接方式差异**：Makefile 把全部 .o 直接塞进链接（不管引用与否），CMake 走静态库按需拉成员——未引用的 LVGL 组件（canvas/imgbtn/spangroup/qrcodegen/basic+mono 主题等 ~90 个符号）被链接器丢弃。全局符号比对：CMake 是 Makefile 的**行为等价子集**，无一个"多出来"的符号，所有被引用符号全部解析。属于收益（更小的 footprint），非功能差异。
6. **上板验证**：`file` 确认 ELF 32-bit ARM hard-float；deploy 后 UI 完整渲染（fb0 抓帧比对 D1 主题一致）、`hci0 UP RUNNING PSCAN ISCAN`、名字 ZGL_BT_SPEAKER、进程常驻。首验时出现过一次 `bring up hci0 failed`（`H5 sync timed out`），板子重启（模块彻底复位）后同一产物跑通——是 RTL8723DS 老坑（§ CLAUDE.md 坑 8），与构建系统无关。
7. **两套构建并存约定**：make 用 `build/`（中间 .o 平铺其中），CMake 用 `build-cmake/`（中间文件），但**产物都是 `build/bt_speaker`**（RUNTIME_OUTPUT_DIRECTORY 固定，deploy.sh 零改动）。两者产物会互相覆盖，属预期；阶段 4 删 Makefile 后只剩 CMake。

### 9.2 阶段 2+3 实施记录（2026-08-31）

**阶段 2**：`core/player_types.h`（事件 208B）+ `osal/osal.h` + `osal_posix.c`。
自测 `osal/osal_test.c` 用**自定义 CHECK 宏**而非 `assert()`——Release 下 NDEBUG 会把
assert 变空操作，导致"变量只被 assert 使用"误报 unused / 检查被静默跳过（实测踩到）。
host ctest 全过；ARM 交叉 `-Werror` 零警告。并发测试的正确性标准：丢最旧队列下
消费数 ≤ 生产数是**预期行为**（不能用"收满 3000 才退出"写测试——队列满了丢弃后
永远收不满，第一版测试就是这么死锁的）。

**阶段 3**（本次 commit）：
- `services/player_backend.h`：业务接口 + `player_backend_btmg`/`_sim` extern 声明
- `services/btmg/btmg_player.c`：`src/bt_speaker.c` 全量迁移改造——observer 回调表
  删除，btmanager 回调里直接组 `player_event_t` 调 `emit()`（btmanager 线程 → 队列）。
  `alias` 设置拆为 `player_backend_btmg_set_alias()`。行为逻辑（免 PIN/扫描模式/瞬态
  忽略/deinit 时序）一行未变
- `apps/app_player.c/.h`：组装层。**队列先于 backend init 创建**（早到事件天然缓冲，
  原 adapter ON 早于 UI 创建的时序坑从机制上消除；`query_state()` 仍保留兜底）；
  `lv_timer 33ms` drain → 翻译成 `ui_player_on_*()` 调用
- `src/ui/ui_main.c`：`bt_on_*` observer + `lv_async_call` 投递通道（~110 行）删除，
  换成 drain 直调的 6 个 `ui_player_on_*()` 事件入口；按钮改调 `app_player_cmd()`。
  乐观更新逻辑原样保留
- `src/bt_speaker.c/.h` 退役（构建已剔除，文件留到阶段4删）
- Makefile 同步改 MAINSRC（过渡期双构建可用）
- 新警告处理：btmg 的 track 字段（btmg 内部 char[512]）比事件契约长，snprintf 截断
  是刻意行为，用 `%.63s` 精度写法消除 `-Wformat-truncation`

**CMake include 布局教训**：新分层目录（core/services/osal）各自 `-I` 一份，头文件
互相 `#include "xxx.h"` 不带目录前缀——比 `#include "../../core/…"` 干净，但每加
一层目录 CMakeLists 要同步 `-I`（已记录）。

**板上回归清单**（用户执行）：手机搜到/免 PIN 配对 → 连接显示 → 播放出声 → 歌名/
歌手/进度/时间刷新 → 三按钮控制 → 音量联动 → 播放图标乐观秒切 → 断连清屏回等待配对。

### 9.3 阶段 4 实施记录（2026-08-31）

- `ui/ui_backend.h` 定稿：`ui_env_t`（scr/font_large/font_small/cmd_request 注入）+
  `init/on_event/deinit` 三函数。主题不建字体不发命令、只认 `player_event_t`。
- `ui/themes/liquidglass/`：`theme.h`（色板/素材路径/布局常量）+ `ui_liquidglass.c`
  （原 ui_main.c 绘制/旋转/乐观更新逻辑零改动迁移；`ui_info_t` 三通道合并为
  `on_event` 单入口，事件已在 UI 线程直接 apply）。
- `ports/`：`lv_port_disp.c`（fb）/ `lv_port_indev.c`（evdev）/ `lv_port_font.c`
  （FreeType 44/22 两号）+ `main_linux.c`（组装 + 主循环）。**`lv_conf.h` 移到
  `ports/`**——include 路径同步改，`__has_include` 发现机制不变。
- `src/` 整目录删除（含退役的 bt_speaker.c/.h）；**Makefile 删除**，构建只剩 CMake。
- `apps/app_player.c` 定稿：`app_player_start(alias, ui, env)` 一站式组装
  （队列 → UI init → backend init → drain timer）；阶段3 的临时翻译层
  （`ui_player_on_*` 6 个函数）随旧 UI 文件删除。
- CMake 定稿 host/ARM 分离：host 编译器（裸名 gcc/cc/clang）**只编 osal_test 并
  return()**（含 lvgl 在内的主程序目标不定义——host 链 vendor ARM .so 必报
  wrong format）；交叉工具链才定义 bt_speaker。部署 `scripts/deploy.sh` 零改动。
- 验证：host 构建 3 秒过 osal_test（ctest 绿）；ARM 全量零警告，产物
  1,285,396B ELF armhf，旧符号（`ui_player_on_*`/`bt_speaker_*`）零残留。

### 9.4 阶段 5 实施记录（2026-08-31）

- `services/sim/sim_player.c`：模拟播放器后端（`player_backend_sim`）。独立线程按
  剧本循环吐事件（adapter ON → 连接 → 曲目 → 音量 → playing → 进度推进 → 演示性
  暂停 3s → 曲末下一首），`cmd()` 同步改状态机（PLAY/PAUSE/NEXT/PREV）。零 LVGL/BT
  依赖，是新 UI 主题的开发数据源 + CI 整链路测试数据源。
- `tests/sim_loop_test.c`：**整链路自测**——sim → emit → OSAL 队列 → drain 线程 →
  事件序列断言（顺序/字段值/cmd 生效/deinit 干净退出）。host ctest 2/2 绿
  （osal_test + sim_loop_test，共 ~4s）。
- `.github/workflows/build.yml` 双 job：`build-arm`（apt gnueabihf 真交叉编译 +
  `file` 断言 32-bit ARM ELF）+ `build-host`（-Werror 编译可移植层 + ctest）。
  本机无 apt 工具链，build-arm job 由推送后 CI 首跑验证（链接 vendor 库的符号版本
  风险预案见 §8）。

---

## 10. 单片机移植路径（为什么这样分层）

以"换到一块 STM32F4 + SPI 屏 + external BT 模组（UART）"为例：

| 层 | 动不动 | 说明 |
|---|---|---|
| core/ | **不动** | 纯 C99 |
| ui/themes/ | **基本不动** | LVGL 本身支持 MCU；主题代码只依赖 LVGL API + player_event_t。字体句柄改由 ports 注入静态编译字体（FreeType 换 lv_font_conv 预生成） |
| services/ | **换实现** | `player_backend_uart`：解析 BT 模组透传协议组 player_event_t。接口不变，UI 无感 |
| osal/ | **换实现** | `osal_none.c`（裸机关中断环形缓冲）或 `osal_freertos.c` |
| ports/ | **重写** | fb→SPI/RGB 屏驱动、evdev→触摸 IC 驱动、FreeType→静态字体、main→裸机 main/FreeRTOS task |
| third_party | 部分 | lvgl 保留；btmg/glib 全套换掉 |

关键：**事件契约（§4.1）是整个可移植性的锚点**。业务和 UI 互相只认识这个 210B 的结构体，平台相关的线程/驱动细节全被 osal 和 ports 吸收。

---

## 11. 与现状对照：被新架构消除的坑

| 旧坑（project-guide 有记录） | 新架构下的状态 |
|---|---|
| 改 lv_conf.h 必须手删全部 .o | CMake 头文件依赖扫描，自动触发重编 |
| bt 回调早于 UI 创建 → query_state 兜底 | 队列先于 backend init 创建，早到事件天然缓冲（query_state 仍保留兜底） |
| lv_async_call + malloc 消息 + 忘 free 风险 | 定长事件值拷贝进队列，drain 侧栈上接收，零堆分配 |
| 换 UI/加业务要动对方代码 | 双侧各一个接口文件，互不 include |
| 推 GitHub 不知道编不编得过 | CI 双 job 门禁 |
