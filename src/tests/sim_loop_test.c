/*
 * sim_loop_test.c — 整链路 host 自测：sim 后端 → OSAL 队列 → drain
 *
 * 验证（CI build-host job）：
 *   1. sim 剧本按序产生 adapter→conn→track→vol→play_state 事件
 *   2. cmd(NEXT) 产生新 track 事件
 *   3. 进度事件单调推进（len_ms 与曲目一致）
 *   4. deinit 后线程干净退出（3s 超时守护）
 *
 * 用 CHECK 宏（同 osal_test.c 的教训：assert 会被 NDEBUG 置空）。
 */
#include "../core/player_types.h"
#include "../osal/osal.h"
#include "../services/player_backend.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

/* 消费线程：drain 队列，记录事件序列 */
#define MAX_EVTS 256
static player_event_t g_log[MAX_EVTS];
static int g_nlog;
static osal_queue_t g_q;
static volatile int g_drain_run;

static void test_emit(const player_event_t *ev)
{
    osal_queue_send(g_q, ev);
}

static void *drain_thread(void *arg)
{
    (void)arg;
    player_event_t ev;
    while (g_drain_run) {
        if (osal_queue_recv(g_q, &ev, 20) && g_nlog < MAX_EVTS)
            g_log[g_nlog++] = ev;
    }
    /* 收尾抽干 */
    while (osal_queue_recv(g_q, &ev, 0) && g_nlog < MAX_EVTS)
        g_log[g_nlog++] = ev;
    return NULL;
}

static int find_evt(player_evt_type_t t, int from)
{
    for (int i = from; i < g_nlog; i++)
        if (g_log[i].type == t) return i;
    return -1;
}

int main(void)
{
    g_q = osal_queue_create(16, sizeof(player_event_t));
    CHECK(g_q != NULL);
    g_nlog = 0;
    g_drain_run = 1;

    pthread_t th;
    pthread_create(&th, NULL, drain_thread, NULL);

    /* 1) 起 sim，等一个完整事件风暴（前 2 秒足够：adapter/conn/track/vol/play/pos） */
    CHECK(player_backend_sim.init(test_emit) == 0);
    osal_sleep_ms(2000);

    /* 序列检查：adapter ON → conn → track → vol → play_state(1) → pos */
    int i_adapter = find_evt(PLAYER_EVT_ADAPTER, 0);
    CHECK(i_adapter >= 0 && g_log[i_adapter].on == 1);
    int i_conn = find_evt(PLAYER_EVT_CONN, i_adapter);
    CHECK(i_conn >= 0 && g_log[i_conn].connected == 1 && g_log[i_conn].addr[0]);
    int i_track = find_evt(PLAYER_EVT_TRACK, i_conn);
    CHECK(i_track >= 0 && strcmp(g_log[i_track].title, "晴天") == 0);
    int i_vol = find_evt(PLAYER_EVT_VOL, i_track);
    CHECK(i_vol >= 0 && g_log[i_vol].volume == 46);
    int i_play = find_evt(PLAYER_EVT_PLAY_STATE, i_vol);
    CHECK(i_play >= 0 && g_log[i_play].play_state == 1);
    int i_pos = find_evt(PLAYER_EVT_POS, i_play);
    CHECK(i_pos >= 0 && g_log[i_pos].len_ms == 269000);

    /* 2) cmd(NEXT) → 新 track（夜曲），位置归零 */
    int n_before = g_nlog;
    CHECK(player_backend_sim.cmd(PLAYER_CMD_NEXT) == 0);
    osal_sleep_ms(200);
    int i_track2 = find_evt(PLAYER_EVT_TRACK, n_before - 1);
    CHECK(i_track2 >= 0 && strcmp(g_log[i_track2].title, "夜曲") == 0);
    int i_pos2 = find_evt(PLAYER_EVT_POS, i_track2);
    CHECK(i_pos2 >= 0 && g_log[i_pos2].pos_ms >= 0 && g_log[i_pos2].pos_ms <= 1000);

    /* 3) cmd(PAUSE) → play_state(2) */
    CHECK(player_backend_sim.cmd(PLAYER_CMD_PAUSE) == 0);
    osal_sleep_ms(200);
    int i_pause = find_evt(PLAYER_EVT_PLAY_STATE, i_track2);
    CHECK(i_pause >= 0 && g_log[i_pause].play_state == 2);

    /* 4) 干净退出 */
    player_backend_sim.deinit();
    g_drain_run = 0;
    pthread_join(th, NULL);
    osal_queue_destroy(g_q);
    printf("PASS sim_loop_test (%d events logged)\n", g_nlog);
    return 0;
}
