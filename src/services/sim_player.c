/*
 * sim_player.c — player_backend 的模拟实现（host 开发/CI 用）
 *
 * 不依赖任何 BT/LVGL：起一个线程按剧本循环吐 player_event_t（曲目→进度→
 * 音量→播放态），供 host 侧验证"业务→队列→drain→UI"整条数据链路，
 * 也作为新 UI 主题的开发数据源。
 *
 * 剧本（2 首歌循环）：
 *   adapter ON → 曲目 → playing → 进度推进 → 中途暂停 3s → 继续到曲末 → 下一首
 *   cmd 命令同样生效（PLAY/PAUSE/NEXT/PREV 改变模拟状态机）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "player_backend.h"

typedef struct {
    const char *title;
    const char *artist;
    const char *album;
    int         len_ms;
} sim_track_t;

static const sim_track_t g_playlist[] = {
    { "晴天",     "周杰伦", "叶惠美",   269000 },
    { "夜曲",     "周杰伦", "十一月的萧邦", 226000 },
};
#define N_TRACKS (sizeof(g_playlist) / sizeof(g_playlist[0]))

static void (*g_emit)(const player_event_t *) = NULL;
static pthread_t g_thread;
static volatile int g_running = 0;

/* 模拟状态（线程内私有） */
static int  g_cur;          /* 当前曲目下标 */
static int  g_pos_ms;       /* 播放位置 */
static int  g_playing;      /* 0/1 */
static int  g_volume;       /* 0..127 */

static void ev_adapter(int on)
{
    player_event_t ev; memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_ADAPTER; ev.on = on;
    g_emit(&ev);
}

static void ev_conn(int connected)
{
    player_event_t ev; memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_CONN; ev.connected = connected;
    snprintf(ev.addr, sizeof(ev.addr), "AA:BB:CC:DD:EE:%02X", 0x10 + g_cur);
    g_emit(&ev);
}

static void ev_play_state(int st)
{
    player_event_t ev; memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_PLAY_STATE; ev.play_state = st;
    g_emit(&ev);
}

static void ev_track(void)
{
    player_event_t ev; memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_TRACK;
    snprintf(ev.title,  sizeof(ev.title),  "%s", g_playlist[g_cur].title);
    snprintf(ev.artist, sizeof(ev.artist), "%s", g_playlist[g_cur].artist);
    snprintf(ev.album,  sizeof(ev.album),  "%s", g_playlist[g_cur].album);
    ev.len_ms = g_playlist[g_cur].len_ms;
    g_emit(&ev);
}

static void ev_pos(void)
{
    player_event_t ev; memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_POS;
    ev.pos_ms = g_pos_ms;
    ev.len_ms = g_playlist[g_cur].len_ms;
    g_emit(&ev);
}

static void ev_vol(void)
{
    player_event_t ev; memset(&ev, 0, sizeof(ev));
    ev.type = PLAYER_EVT_VOL; ev.volume = g_volume;
    g_emit(&ev);
}

static void *sim_thread(void *arg)
{
    (void)arg;
    ev_adapter(1);
    ev_conn(1);
    ev_track();
    ev_vol();
    g_playing = 1;
    ev_play_state(1);

    int paused_once = 0;
    while (g_running) {
        if (g_playing) {
            g_pos_ms += 500;                       /* tick = 500ms */
            if (g_pos_ms >= g_playlist[g_cur].len_ms) {
                /* 曲末 → 下一首 */
                g_cur = (g_cur + 1) % (int)N_TRACKS;
                g_pos_ms = 0;
                ev_track();
                continue;
            }
            ev_pos();
            /* 第一首歌放到 1/3 时演示一次"手机端暂停 3 秒再播" */
            if (!paused_once && g_cur == 0 &&
                g_pos_ms > g_playlist[0].len_ms / 3) {
                paused_once = 1;
                g_playing = 0;
                ev_play_state(2);
                sleep(3);
                if (!g_running) break;
                g_playing = 1;
                ev_play_state(1);
            }
        }
        usleep(500 * 1000);
    }
    return NULL;
}

static int sim_init(void (*emit)(const player_event_t *ev))
{
    if (g_running) return 0;
    g_emit = emit;
    g_cur = 0;
    g_pos_ms = 0;
    g_playing = 0;
    g_volume = 46;
    g_running = 1;
    if (pthread_create(&g_thread, NULL, sim_thread, NULL) != 0) {
        g_running = 0;
        return -1;
    }
    return 0;
}

static void sim_deinit(void)
{
    if (!g_running) return;
    g_running = 0;
    pthread_join(g_thread, NULL);
    g_emit = NULL;
}

static int sim_query_state(void)
{
    /* 模拟源自回放初始事件，无需补发 */
    (void)g_emit;
    return 0;
}

static int sim_cmd(player_cmd_t c)
{
    switch (c) {
    case PLAYER_CMD_PLAY:  g_playing = 1; ev_play_state(1); break;
    case PLAYER_CMD_PAUSE: g_playing = 0; ev_play_state(2); break;
    case PLAYER_CMD_NEXT:
        g_cur = (g_cur + 1) % (int)N_TRACKS;
        g_pos_ms = 0;
        ev_track();
        break;
    case PLAYER_CMD_PREV:
        g_cur = (g_cur + (int)N_TRACKS - 1) % (int)N_TRACKS;
        g_pos_ms = 0;
        ev_track();
        break;
    default: return -1;
    }
    return 0;
}

const player_backend_t player_backend_sim = {
    .init        = sim_init,
    .deinit      = sim_deinit,
    .query_state = sim_query_state,
    .cmd         = sim_cmd,
};
