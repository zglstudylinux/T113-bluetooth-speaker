/*
 * ui_main.c — 蓝牙音箱主界面（480x640）
 *
 * M1：标题 + 蓝牙状态（初始化/等待配对/已连接）+ 对端地址显示。
 * BT 回调在 btmanager 线程 → lv_async_call 转到 LVGL 线程再操作 UI。
 */
#include "lvgl/lvgl.h"
#include "../bt_speaker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 由 main.c 提供的字体 */
extern lv_font_t *ui_font_cn_48;
extern lv_font_t *ui_font_cn_32;

static lv_obj_t *g_status_label;   /* 大状态字 */
static lv_obj_t *g_addr_label;     /* 对端地址/别名 */

/* ========== lv_async_call 的参数载体 ========== */
typedef struct {
    char text[96];
    char sub[64];
} ui_msg_t;

static void ui_update_cb(void *p)
{
    ui_msg_t *m = (ui_msg_t *)p;
    if (!m) return;
    lv_label_set_text(g_status_label, m->text);
    lv_label_set_text(g_addr_label, m->sub);
    free(m);
}

/* 线程安全：任意线程调用，转投 LVGL 线程 */
static void ui_post(const char *text, const char *sub)
{
    ui_msg_t *m = malloc(sizeof(ui_msg_t));
    if (!m) return;
    snprintf(m->text, sizeof(m->text), "%s", text);
    snprintf(m->sub, sizeof(m->sub), "%s", sub);
    lv_async_call(ui_update_cb, m);
}

/* ========== bt_speaker 回调（btmanager 线程） ========== */
static void bt_on_adapter_on(const char *addr, const char *alias)
{
    char sub[64];
    snprintf(sub, sizeof(sub), "%s", alias);
    ui_post("等待配对", sub);
}

static void bt_on_adapter_off(void)
{
    /* TURNING_ON/OFFING/OFF 都可能来，只在真正 OFF 时提示（避免初始化瞬态误报） */
    ui_post("蓝牙关闭", "");
}

static void bt_on_conn_state(const char *addr, int connected)
{
    if (connected)
        ui_post("已连接", addr ? addr : "");
    else
        ui_post("等待配对", "");
}

static void bt_on_play_state(const char *addr, int play_state)
{
    /* BTMG: 1=playing 2=paused（M2 用，M1 先显示） */
    if (play_state == 1)
        ui_post("播放中", addr ? addr : "");
    else if (play_state == 2)
        ui_post("已暂停", addr ? addr : "");
}

static void bt_on_track(const char *addr, const char *title, const char *artist,
                        const char *album, int duration_ms)
{
    char sub[64];
    snprintf(sub, sizeof(sub), "%.32s - %.24s", title, artist);
    ui_post("播放中", sub);
}

static void bt_on_play_pos(const char *addr, int len_ms, int pos_ms) { (void)addr; (void)len_ms; (void)pos_ms; }
static void bt_on_volume(const char *addr, unsigned int vol) { (void)addr; (void)vol; }

static bt_speaker_observer_t g_bt_obs = {
    .on_adapter_on  = bt_on_adapter_on,
    .on_adapter_off = bt_on_adapter_off,
    .on_conn_state  = bt_on_conn_state,
    .on_play_state  = bt_on_play_state,
    .on_track       = bt_on_track,
    .on_play_pos    = bt_on_play_pos,
    .on_volume      = bt_on_volume,
};

/* ========== 界面构建（LVGL 线程，main.c 调用） ========== */
void ui_main_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, ui_font_cn_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(title, "蓝牙音箱");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    /* 分隔线 */
    lv_obj_t *line = lv_obj_create(scr);
    lv_obj_set_size(line, 400, 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x2E86DE), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 150);

    /* 状态大字（初始"初始化中"，adapter ON 后变"等待配对"） */
    g_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_status_label, ui_font_cn_48, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0x3498DB), 0);
    lv_label_set_text(g_status_label, "初始化中");
    lv_obj_align(g_status_label, LV_ALIGN_CENTER, 0, -40);

    /* 副信息（别名/地址） */
    g_addr_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_addr_label, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(g_addr_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(g_addr_label, "");
    lv_obj_align(g_addr_label, LV_ALIGN_CENTER, 0, 40);

    /* 底部提示 */
    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_label_set_text(hint, "手机搜索 ZGL_BT_SPEAKER 配对");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -60);
}

const bt_speaker_observer_t *ui_main_bt_observer(void)
{
    return &g_bt_obs;
}
