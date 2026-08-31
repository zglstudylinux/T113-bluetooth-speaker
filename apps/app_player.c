/*
 * app_player.c — 组装层（阶段3过渡实现）
 *
 * 职责（全项目唯一"认识所有人"的地方）：
 *   1. 建 OSAL 事件队列（先于 backend init——早到事件天然缓冲）
 *   2. init player_backend_btmg，emit=入队
 *   3. lv_timer 33ms drain：player_event_t → 翻译成现 UI 的更新调用
 *      （阶段3过渡：UI 仍用 src/ui/ui_main.c，经 ui_player_event() 入口；
 *       阶段4 UI 迁 themes/ 后此文件变薄为纯组装）
 *
 * 替代原 lv_async_call+malloc 通道：定长事件值拷贝、drain 在 LVGL 线程、
 * 栈上接收零堆分配。
 */
#include "lvgl/lvgl.h"
#include "player_backend.h"
#include "osal.h"
#include "app_player.h"
#include "../src/ui/ui_main.h"

#include <stdio.h>
#include <stdlib.h>

#define QUEUE_DEPTH   16
#define DRAIN_MS      33      /* 与唱盘旋转 tick 同频；裸机上=主循环轮询同构 */

static osal_queue_t g_evtq;
static const player_backend_t *g_backend;
static lv_timer_t *g_drain_timer;

/* ---------- emit（btmanager 线程调用）：入队 ---------- */
static void backend_emit(const player_event_t *ev)
{
    osal_queue_send(g_evtq, ev);
}

/* ---------- 事件 → 现 UI 更新（LVGL 线程） ----------
 * 翻译逻辑从旧 ui_main.c 的 bt_on_* 回调原样搬来，行为不变：
 * 状态字/地址行 + 三条信息通道（歌名/进度/音量，-1=不更新）。 */
static void apply_event(const player_event_t *ev)
{
    switch (ev->type) {
    case PLAYER_EVT_ADAPTER:
        ui_player_on_adapter(ev->on == 1);
        break;
    case PLAYER_EVT_CONN:
        ui_player_on_conn(ev->addr, ev->connected == 1);
        break;
    case PLAYER_EVT_PLAY_STATE:
        ui_player_on_play_state(ev->play_state);
        break;
    case PLAYER_EVT_TRACK:
        ui_player_on_track(ev->title, ev->artist, ev->album, ev->len_ms);
        break;
    case PLAYER_EVT_POS:
        ui_player_on_pos(ev->len_ms, ev->pos_ms);
        break;
    case PLAYER_EVT_VOL:
        ui_player_on_volume(ev->volume);
        break;
    default:
        break;
    }
}

/* ---------- drain timer（LVGL 线程） ---------- */
static void drain_timer_cb(lv_timer_t *t)
{
    (void)t;
    player_event_t ev;
    while (osal_queue_recv(g_evtq, &ev, 0))
        apply_event(&ev);
}

/* ---------- 对外组装接口（ports/main 调用） ---------- */

int app_player_start(const char *alias)
{
    /* 1. 队列先建（早到事件有地方放） */
    g_evtq = osal_queue_create(QUEUE_DEPTH, sizeof(player_event_t));
    if (!g_evtq) {
        fprintf(stderr, "app_player: queue create fail\n");
        return -1;
    }

    /* 2. 业务后端（btmanager 线程开始往队列灌事件） */
    g_backend = &player_backend_btmg;
    if (g_backend->init(backend_emit) != 0) {
        fprintf(stderr, "app_player: backend init fail\n");
        osal_queue_destroy(g_evtq);
        g_evtq = NULL;
        return -1;
    }
    player_backend_btmg_set_alias(alias);

    /* 3. drain timer：LVGL 线程消费 */
    g_drain_timer = lv_timer_create(drain_timer_cb, DRAIN_MS, NULL);
    return 0;
}

void app_player_query_state(void)
{
    if (g_backend)
        g_backend->query_state();   /* 补发 adapter 状态（时序兜底） */
}

/* UI 按钮 → 播放控制（LVGL 线程直调，乐观更新在 UI 侧，此路只发命令） */
int app_player_cmd(player_cmd_t c)
{
    return g_backend ? g_backend->cmd(c) : -1;
}

void app_player_stop(void)
{
    if (g_drain_timer) { lv_timer_del(g_drain_timer); g_drain_timer = NULL; }
    if (g_backend)     { g_backend->deinit(); g_backend = NULL; }
    if (g_evtq)        { osal_queue_destroy(g_evtq); g_evtq = NULL; }
}
