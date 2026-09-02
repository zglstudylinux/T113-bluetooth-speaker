/*
 * btmg_player.c — player_backend 的 Allwinner btmanager 实现
 *
 * 由 src/bt_speaker.c 迁移改造（行为保持一致）：
 *   bt_manager_preinit → enable_profile(A2DP_SINK|AVRCP) → 注册回调 →
 *   bt_manager_init → bt_manager_enable(true) → adapter ON 回调里设
 *   NoInputNoOutput + CONNECTABLE_DISCOVERABLE（免 PIN 配对关键）。
 *
 * 事件化改造点（阶段3）：原 bt_speaker_observer_t 观察者回调全部删除，
 * btmanager 回调里直接组装 player_event_t 调 emit()（emit 由上层注入，
 * 实现=OSAL 队列 send），业务层不再 include 任何 UI/LVGL 头文件。
 *
 * btmanager 回调运行在 btmanager 自己的线程 → emit 必须线程安全。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "player_backend.h"
#include "bt_manager.h"
#include "bt_log.h"

#ifndef BT_LOGI
#define BT_LOGI(fmt, ...) printf("[BT][%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif

static btmg_callback_t *g_cb = NULL;
static void (*g_emit)(const player_event_t *ev) = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_peer_addr[18] = {0};
static int g_has_peer = 0;
static volatile int g_shutting_down = 0;

/* 组装事件并投递（emit 为空时静默丢弃——上层未就绪） */
static void emit_event(const player_event_t *ev)
{
    void (*cb)(const player_event_t *) = g_emit;
    if (cb)
        cb(ev);
}

static void emit_adapter(int on)
{
    player_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_ADAPTER;
    ev.on = on;
    emit_event(&ev);
}

static void emit_conn(const char *addr, int connected)
{
    player_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_CONN;
    ev.connected = connected;
    snprintf(ev.addr, sizeof(ev.addr), "%s", addr ? addr : "");
    emit_event(&ev);
}

/* ========== btmanager 回调（在 btmanager 线程） ========== */
static void adapter_state_cb(btmg_adapter_state_t status)
{
    if (g_shutting_down) return;

    BT_LOGI("adapter state -> %d", (int)status);

    if (status == BTMG_ADAPTER_ON) {
        char addr[18] = {0};
        char alias[64] = {0};
        bt_manager_get_adapter_address(addr);
        bt_manager_get_adapter_name(alias);
        BT_LOGI("Adapter ON addr=%s alias=%s", addr, alias);

        /* 免 PIN 配对关键：IO 能力 NoInputNoOutput → Just Works */
        bt_manager_agent_set_io_capability(BTMG_IO_CAP_NOINPUTNOOUTPUT);
        /* 可连接 + 可发现（手机能搜到） */
        bt_manager_set_scan_mode(BTMG_SCAN_MODE_CONNECTABLE_DISCOVERABLE);

        emit_adapter(1);
    } else if (status == BTMG_ADAPTER_OFF) {
        BT_LOGI("Adapter OFF");
        emit_adapter(0);
    }
    /* TURNING_ON/TURNING_OFF 瞬态忽略 */
}

static void a2dp_sink_conn_state_cb(const char *addr, btmg_a2dp_sink_connection_state_t state)
{
    if (g_shutting_down) return;
    BT_LOGI("A2DP conn state %d: %s", (int)state, addr ? addr : "?");

    switch (state) {
    case BTMG_A2DP_SINK_CONNECTED:
        if (!addr) return;
        pthread_mutex_lock(&g_lock);
        snprintf(g_peer_addr, sizeof(g_peer_addr), "%s", addr);
        g_has_peer = 1;
        pthread_mutex_unlock(&g_lock);
        emit_conn(addr, 1);
        break;
    case BTMG_A2DP_SINK_DISCONNECTED:
        pthread_mutex_lock(&g_lock);
        g_peer_addr[0] = '\0';
        g_has_peer = 0;
        pthread_mutex_unlock(&g_lock);
        emit_conn(addr, 0);
        break;
    default:
        return;   /* CONNECTING/DISCONNECTING 瞬态忽略 */
    }
}

static void a2dp_sink_audio_state_cb(const char *addr, btmg_a2dp_sink_audio_state_t state)
{
    if (g_shutting_down) return;
    BT_LOGI("A2DP audio state %d: %s", (int)state, addr ? addr : "?");
}

static void avrcp_play_state_cb(const char *addr, btmg_avrcp_play_state_t state)
{
    if (g_shutting_down) return;
    BT_LOGI("Play state %d: %s", (int)state, addr ? addr : "?");

    player_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_PLAY_STATE;
    ev.play_state = (int)state;   /* 1=playing 2=paused（BTMG 定义直接透传） */
    emit_event(&ev);
}

static void avrcp_play_pos_cb(const char *addr, int song_len, int song_pos)
{
    if (g_shutting_down) return;

    player_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_POS;
    ev.pos_ms = song_pos;
    ev.len_ms = song_len;
    emit_event(&ev);
}

static void avrcp_track_changed_cb(const char *addr, btmg_track_info_t info)
{
    if (g_shutting_down) return;
    BT_LOGI("Track: title=\"%s\" artist=\"%s\" album=\"%s\"",
            info.title ? info.title : "", info.artist ? info.artist : "",
            info.album ? info.album : "");

    player_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_TRACK;
    /* btmg 的字段比事件契约长，截断是刻意行为（屏上放不下的歌名本来显示不全），
     * 用 %.Ns 精度消除 -Wformat-truncation */
    snprintf(ev.title,  sizeof(ev.title),  "%.63s", info.title  ? info.title  : "");
    snprintf(ev.artist, sizeof(ev.artist), "%.47s", info.artist ? info.artist : "");
    snprintf(ev.album,  sizeof(ev.album),  "%.47s", info.album  ? info.album  : "");
    ev.len_ms = atoi(info.duration ? info.duration : "0");
    emit_event(&ev);
}

static void avrcp_volume_cb(const char *addr, unsigned int volume)
{
    if (g_shutting_down) return;
    BT_LOGI("Volume %u: %s", volume, addr ? addr : "?");

    player_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_VOL;
    ev.volume = (int32_t)volume;
    emit_event(&ev);
}

/* ========== player_backend 接口实现 ========== */

static int btmg_init(void (*emit)(const player_event_t *ev))
{
    if (g_cb) return 0;   /* 已初始化 */

    g_emit = emit;

    btmg_set_log_file_path("/tmp/btmg.log");

    if (bt_manager_preinit(&g_cb) != 0 || !g_cb) {
        BT_LOGI("bt_manager_preinit failed");
        g_cb = NULL;
        return -1;
    }

    bt_manager_enable_profile(BTMG_A2DP_SINK_ENABLE | BTMG_AVRCP_ENABLE);

    g_cb->btmg_adapter_cb.adapter_state_cb = adapter_state_cb;
    g_cb->btmg_a2dp_sink_cb.a2dp_sink_connection_state_cb = a2dp_sink_conn_state_cb;
    g_cb->btmg_a2dp_sink_cb.a2dp_sink_audio_state_cb = a2dp_sink_audio_state_cb;
    g_cb->btmg_avrcp_cb.avrcp_play_state_cb = avrcp_play_state_cb;
    g_cb->btmg_avrcp_cb.avrcp_play_position_cb = avrcp_play_pos_cb;
    g_cb->btmg_avrcp_cb.avrcp_track_changed_cb = avrcp_track_changed_cb;
    g_cb->btmg_avrcp_cb.avrcp_audio_volume_cb = avrcp_volume_cb;

    if (bt_manager_init(g_cb) != 0) {
        BT_LOGI("bt_manager_init failed");
        g_cb = NULL;
        return -1;
    }

    bt_manager_enable(true);
    bt_manager_set_scan_mode(BTMG_SCAN_MODE_CONNECTABLE_DISCOVERABLE);

    pthread_mutex_lock(&g_lock);
    g_peer_addr[0] = '\0';
    g_has_peer = 0;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

/* alias 设置从 init 拆出为独立步骤（原 bt_speaker_init(alias) 的 alias 参数，
 * btmg_init 签名统一后经此设置；UI 创建后、或 init 后立刻调用均可） */
void player_backend_btmg_set_alias(const char *alias)
{
    if (alias && alias[0])
        bt_manager_set_adapter_name(alias);
}

static void btmg_deinit(void)
{
    if (!g_cb) return;

    g_shutting_down = 1;
    pthread_mutex_lock(&g_lock);
    int has_peer = g_has_peer;
    char peer[18];
    snprintf(peer, sizeof(peer), "%s", g_peer_addr);
    pthread_mutex_unlock(&g_lock);

    if (has_peer)
        bt_manager_disconnect(peer);
    bt_manager_set_scan_mode(BTMG_SCAN_MODE_NONE);
    usleep(200 * 1000);

    bt_manager_enable(false);
    usleep(300 * 1000);
    bt_manager_deinit(g_cb);
    g_cb = NULL;
    g_emit = NULL;
    g_shutting_down = 0;
}

static int btmg_query_state(void)
{
    if (!g_cb) return -1;
    /* adapter 可能已 ON 且回调早于 UI 就绪（事件已入队但队列可能未建）——
     * 上层在 UI 起来后调一次补发（与旧 bt_speaker_query_state 行为一致） */
    if (bt_manager_get_adapter_state() == BTMG_ADAPTER_ON)
        emit_adapter(1);
    return 0;
}

static int btmg_cmd(player_cmd_t c)
{
    pthread_mutex_lock(&g_lock);
    int has_peer = g_has_peer;
    char peer[18];
    snprintf(peer, sizeof(peer), "%s", g_peer_addr);
    pthread_mutex_unlock(&g_lock);
    if (!g_cb || !has_peer) return -1;

    btmg_avrcp_command_t ac;
    switch (c) {
    case PLAYER_CMD_PLAY:  ac = BTMG_AVRCP_PLAY; break;
    case PLAYER_CMD_PAUSE: ac = BTMG_AVRCP_PAUSE; break;
    case PLAYER_CMD_NEXT:  ac = BTMG_AVRCP_FORWARD; break;
    case PLAYER_CMD_PREV:  ac = BTMG_AVRCP_BACKWARD; break;
    default: return -1;
    }
    return bt_manager_avrcp_command(peer, ac);
}

const player_backend_t player_backend_btmg = {
    .init        = btmg_init,
    .deinit      = btmg_deinit,
    .query_state = btmg_query_state,
    .cmd         = btmg_cmd,
};
