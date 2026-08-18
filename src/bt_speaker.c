/*
 * bt_speaker.c — 蓝牙音箱 BT 侧封装（基于 app_sdk component/bt_audio/app_bt_audio.c 精简）
 *
 * 流程：bt_manager_preinit → enable_profile(A2DP_SINK|AVRCP) → 注册回调 →
 *       bt_manager_init → bt_manager_enable(true) → adapter ON 回调里设
 *       NoInputNoOutput + CONNECTABLE_DISCOVERABLE（免 PIN 配对关键，见
 *       SDK 文档 docs/rtl8723ds-bluetooth-porting.md 坑 22）
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

#include "bt_speaker.h"
#include "bt_manager.h"
#include "bt_log.h"

#ifndef BT_LOGI
#define BT_LOGI(fmt, ...) printf("[BT][%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif

static btmg_callback_t *g_cb = NULL;
static bt_speaker_observer_t g_obs;
static pthread_mutex_t g_obs_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_peer_addr[18] = {0};
static int g_has_peer = 0;
static volatile int g_shutting_down = 0;

static void obs_snapshot(bt_speaker_observer_t *dst)
{
    pthread_mutex_lock(&g_obs_lock);
    *dst = g_obs;
    pthread_mutex_unlock(&g_obs_lock);
}

/* ========== btmanager 回调（在 btmanager 线程） ========== */
static void adapter_state_cb(btmg_adapter_state_t status)
{
    if (g_shutting_down) return;
    bt_speaker_observer_t obs;
    obs_snapshot(&obs);

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

        if (obs.on_adapter_on)
            obs.on_adapter_on(addr, alias);
    } else if (status == BTMG_ADAPTER_OFF) {
        BT_LOGI("Adapter OFF");
        if (obs.on_adapter_off)
            obs.on_adapter_off();
    }
    /* TURNING_ON/TURNING_OFF 瞬态忽略 */
}

static void a2dp_sink_conn_state_cb(const char *addr, btmg_a2dp_sink_connection_state_t state)
{
    if (g_shutting_down) return;
    BT_LOGI("A2DP conn state %d: %s", (int)state, addr ? addr : "?");
    bt_speaker_observer_t obs;
    obs_snapshot(&obs);

    int connected = 0;
    switch (state) {
    case BTMG_A2DP_SINK_CONNECTED:
        if (addr) {
            strncpy(g_peer_addr, addr, sizeof(g_peer_addr) - 1);
            g_peer_addr[sizeof(g_peer_addr) - 1] = '\0';
            g_has_peer = 1;
            connected = 1;
        }
        break;
    case BTMG_A2DP_SINK_DISCONNECTED:
        g_peer_addr[0] = '\0';
        g_has_peer = 0;
        connected = 0;
        break;
    default:
        return;
    }

    if (obs.on_conn_state)
        obs.on_conn_state(addr, connected);
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
    bt_speaker_observer_t obs;
    obs_snapshot(&obs);
    if (obs.on_play_state)
        obs.on_play_state(addr, (int)state);
}

static void avrcp_play_pos_cb(const char *addr, int song_len, int song_pos)
{
    if (g_shutting_down) return;
    bt_speaker_observer_t obs;
    obs_snapshot(&obs);
    if (obs.on_play_pos)
        obs.on_play_pos(addr, song_len, song_pos);
}

static void avrcp_track_changed_cb(const char *addr, btmg_track_info_t info)
{
    if (g_shutting_down) return;
    BT_LOGI("Track: title=\"%s\" artist=\"%s\" album=\"%s\"",
            info.title ? info.title : "", info.artist ? info.artist : "",
            info.album ? info.album : "");
    bt_speaker_observer_t obs;
    obs_snapshot(&obs);
    if (obs.on_track)
        obs.on_track(addr,
                     info.title ? info.title : "",
                     info.artist ? info.artist : "",
                     info.album ? info.album : "",
                     atoi(info.duration ? info.duration : "0"));
}

static void avrcp_volume_cb(const char *addr, unsigned int volume)
{
    if (g_shutting_down) return;
    BT_LOGI("Volume %u: %s", volume, addr ? addr : "?");
    bt_speaker_observer_t obs;
    obs_snapshot(&obs);
    if (obs.on_volume)
        obs.on_volume(addr, volume);
}

/* ========== 对外接口 ========== */
int bt_speaker_init(const char *alias, const bt_speaker_observer_t *obs)
{
    if (g_cb) return 0;

    pthread_mutex_lock(&g_obs_lock);
    if (obs)
        g_obs = *obs;
    else
        memset(&g_obs, 0, sizeof(g_obs));
    pthread_mutex_unlock(&g_obs_lock);

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

    if (alias && alias[0])
        bt_manager_set_adapter_name(alias);

    bt_manager_set_scan_mode(BTMG_SCAN_MODE_CONNECTABLE_DISCOVERABLE);

    g_peer_addr[0] = '\0';
    g_has_peer = 0;
    return 0;
}

void bt_speaker_deinit(void)
{
    if (!g_cb) return;

    g_shutting_down = 1;
    if (g_has_peer)
        bt_manager_disconnect(g_peer_addr);
    bt_manager_set_scan_mode(BTMG_SCAN_MODE_NONE);
    usleep(200 * 1000);

    bt_manager_enable(false);
    usleep(300 * 1000);
    bt_manager_deinit(g_cb);
    g_cb = NULL;
    g_shutting_down = 0;
}

/* UI 创建后调用：若 adapter 已经 ON（回调早于 UI 创建），补发一次状态 */
int bt_speaker_query_state(void)
{
    if (!g_cb) return -1;
    bt_speaker_observer_t obs;
    obs_snapshot(&obs);
    if (bt_manager_get_adapter_state() == BTMG_ADAPTER_ON && obs.on_adapter_on) {
        char addr[18] = {0};
        char alias[64] = {0};
        bt_manager_get_adapter_address(addr);
        bt_manager_get_adapter_name(alias);
        obs.on_adapter_on(addr, alias);
    }
    return 0;
}

int bt_speaker_set_discoverable(bool on)
{
    if (!g_cb) return -1;
    return bt_manager_set_scan_mode(on ? BTMG_SCAN_MODE_CONNECTABLE_DISCOVERABLE
                                       : BTMG_SCAN_MODE_NONE);
}

int bt_speaker_avrcp_cmd(int cmd)
{
    if (!g_cb || !g_has_peer) return -1;
    btmg_avrcp_command_t c;
    switch (cmd) {
    case 0: c = BTMG_AVRCP_PLAY; break;
    case 1: c = BTMG_AVRCP_PAUSE; break;
    case 2: c = BTMG_AVRCP_STOP; break;
    case 3: c = BTMG_AVRCP_FORWARD; break;
    case 4: c = BTMG_AVRCP_BACKWARD; break;
    default: return -1;
    }
    return bt_manager_avrcp_command(g_peer_addr, c);
}
