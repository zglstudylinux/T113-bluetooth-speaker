/*
 * ui_main.c — 蓝牙音箱主界面（480x640 竖屏）
 *
 * M2/M3 完整播放器 UI：
 *   标题 "蓝牙音箱"
 *   歌名（大字，中文 FreeType）
 *   "歌手 - 专辑"
 *   状态行（等待配对 / 已连接 / 播放中 / 已暂停）
 *   进度条 + 时间 "0:00 / 0:00"
 *   上一首 / 播放暂停 / 下一首 三个触摸按钮（AVRCP 控制）
 *   右侧竖向音量条
 *
 * BT 回调在 btmanager 线程 → lv_async_call 转到 LVGL 线程再操作 UI。
 * 参考 app_sdk/app/ui/page_bt_audio.c 的结构，按 480x640 重排坐标。
 */
#include "lvgl/lvgl.h"
#include "../bt_speaker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* 由 main.c 提供的字体 */
extern lv_font_t *ui_font_cn_48;
extern lv_font_t *ui_font_cn_32;

/* 小号字体用 LVGL 内置 Montserrat（lv_conf.h 已开 14/16），英文/数字/符号够用 */
#define UI_FONT_SMALL  (&lv_font_montserrat_16)
#define UI_FONT_TIME   (&lv_font_montserrat_16)

/* 颜色 */
#define COL_BG       0x101418
#define COL_TITLE    0xFFFFFF
#define COL_SONG     0xFFFFFF
#define COL_SUB      0xAAAAAA
#define COL_STATUS   0x3498DB
#define COL_ACCENT   0x2E86DE
#define COL_BAR_BG   0x333A44
#define COL_WARN     0xE74C3C

/* ========== UI 对象 ========== */
static lv_obj_t *g_status_label;    /* 状态大字：等待配对/已连接/播放中/已暂停 */
static lv_obj_t *g_song_label;      /* 歌名 */
static lv_obj_t *g_artist_label;    /* "歌手 - 专辑" */
static lv_obj_t *g_addr_label;      /* 对端地址 */
static lv_obj_t *g_bar;             /* 进度条 */
static lv_obj_t *g_time_label;      /* "0:00 / 0:00" */
static lv_obj_t *g_play_icon;       /* 播放/暂停图标（img，用 LV_SYMBOL） */
static lv_obj_t *g_vol_bar;         /* 音量条 */
static lv_obj_t *g_vol_label;       /* 音量数值 */

/* 运行态：是否有对端连接（决定控制按钮是否可用） */
static volatile bool g_connected = false;

/* ========== 时间格式化 ========== */
static void fmt_time(char *buf, size_t len, int ms)
{
    if (ms < 0) ms = 0;
    int total = ms / 1000;
    int m = total / 60;
    int s = total % 60;
    snprintf(buf, len, "%d:%02d", m, s);
}

/* ========== lv_async_call 载体 ========== */

/* 通用信息更新（歌名/歌手专辑/进度/时间/音量） */
typedef struct {
    char song[96];
    char artist_album[96];
    int  pos_ms;
    int  len_ms;
    int  volume;     /* -1 = 不更新 */
} ui_info_t;

/* 状态/连接更新 */
typedef struct {
    char status[48];
    char sub[64];     /* 对端地址或别名 */
} ui_state_t;

static void ui_info_cb(void *p)
{
    ui_info_t *m = (ui_info_t *)p;
    if (!m) return;

    if (m->song[0])
        lv_label_set_text(g_song_label, m->song);
    if (m->artist_album[0])
        lv_label_set_text(g_artist_label, m->artist_album);

    if (m->len_ms >= 0) {
        int pct = (m->len_ms > 0)
                  ? (int)((long long)m->pos_ms * 100 / m->len_ms)
                  : 0;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(g_bar, pct, LV_ANIM_OFF);

        char cur[16], total[16], t[40];
        fmt_time(cur, sizeof(cur), m->pos_ms);
        fmt_time(total, sizeof(total), m->len_ms);
        snprintf(t, sizeof(t), "%s / %s", cur, total);
        lv_label_set_text(g_time_label, t);
    }

    if (m->volume >= 0) {
        int v = m->volume;
        if (v > 127) v = 127;
        lv_bar_set_value(g_vol_bar, v, LV_ANIM_OFF);
        lv_label_set_text_fmt(g_vol_label, "%d", v);
    }

    free(m);
}

static void ui_state_cb(void *p)
{
    ui_state_t *m = (ui_state_t *)p;
    if (!m) return;
    lv_label_set_text(g_status_label, m->status);
    lv_label_set_text(g_addr_label, m->sub);
    free(m);
}

/* 播放图标更新（play_state: 1=播放 2=暂停） */
static void ui_play_icon_cb(void *p)
{
    int st = (int)(intptr_t)p;
    if (st == 1)
        lv_img_set_src(g_play_icon, LV_SYMBOL_PAUSE);
    else
        lv_img_set_src(g_play_icon, LV_SYMBOL_PLAY);
}

/* ========== 线程安全投递 ========== */
static void post_info(const ui_info_t *info)
{
    ui_info_t *m = malloc(sizeof(ui_info_t));
    if (!m) return;
    *m = *info;
    lv_async_call(ui_info_cb, m);
}

static void post_state(const char *status, const char *sub)
{
    ui_state_t *m = malloc(sizeof(ui_state_t));
    if (!m) return;
    snprintf(m->status, sizeof(m->status), "%s", status);
    snprintf(m->sub, sizeof(m->sub), "%s", sub);
    lv_async_call(ui_state_cb, m);
}

static void post_play_icon(int play_state)
{
    lv_async_call(ui_play_icon_cb, (void *)(intptr_t)play_state);
}

/* ========== bt_speaker 回调（btmanager 线程） ========== */
static void bt_on_adapter_on(const char *addr, const char *alias)
{
    (void)addr;
    post_state("等待配对", alias ? alias : "");
}

static void bt_on_adapter_off(void)
{
    post_state("蓝牙关闭", "");
}

static void bt_on_conn_state(const char *addr, int connected)
{
    g_connected = connected;
    if (connected) {
        post_state("已连接", addr ? addr : "");
        /* 连上后默认显示播放图标为 PLAY（尚未播放） */
        post_play_icon(2);
    } else {
        post_state("等待配对", "");
        /* 断开后清空歌曲信息 */
        ui_info_t info = { .song = "—", .artist_album = "",
                           .pos_ms = 0, .len_ms = 0, .volume = -1 };
        post_info(&info);
        post_play_icon(2);
    }
}

static void bt_on_play_state(const char *addr, int play_state)
{
    (void)addr;
    /* BTMG: 1=playing 2=paused */
    if (play_state == 1) {
        post_state("播放中", "");
        post_play_icon(1);
    } else if (play_state == 2) {
        post_state("已暂停", "");
        post_play_icon(2);
    }
}

static void bt_on_track(const char *addr, const char *title, const char *artist,
                        const char *album, int duration_ms)
{
    (void)addr;
    ui_info_t info;
    memset(&info, 0, sizeof(info));
    snprintf(info.song, sizeof(info.song), "%s", title ? title : "未知歌曲");
    if (artist && artist[0] && album && album[0])
        snprintf(info.artist_album, sizeof(info.artist_album), "%s - %s", artist, album);
    else if (artist && artist[0])
        snprintf(info.artist_album, sizeof(info.artist_album), "%s", artist);
    else if (album && album[0])
        snprintf(info.artist_album, sizeof(info.artist_album), "%s", album);
    else
        snprintf(info.artist_album, sizeof(info.artist_album), "未知歌手");
    info.pos_ms = 0;
    info.len_ms = duration_ms;
    info.volume = -1;
    post_info(&info);
}

static void bt_on_play_pos(const char *addr, int len_ms, int pos_ms)
{
    (void)addr;
    ui_info_t info;
    memset(&info, 0, sizeof(info));
    info.song[0] = 0;          /* 不更新歌名 */
    info.artist_album[0] = 0;  /* 不更新歌手 */
    info.pos_ms = pos_ms;
    info.len_ms = len_ms;
    info.volume = -1;
    post_info(&info);
}

static void bt_on_volume(const char *addr, unsigned int vol)
{
    (void)addr;
    ui_info_t info;
    memset(&info, 0, sizeof(info));
    info.song[0] = 0;
    info.artist_album[0] = 0;
    info.pos_ms = -1;          /* 不更新进度 */
    info.len_ms = -1;
    info.volume = (int)vol;
    post_info(&info);
}

static bt_speaker_observer_t g_bt_obs = {
    .on_adapter_on  = bt_on_adapter_on,
    .on_adapter_off = bt_on_adapter_off,
    .on_conn_state  = bt_on_conn_state,
    .on_play_state  = bt_on_play_state,
    .on_track       = bt_on_track,
    .on_play_pos    = bt_on_play_pos,
    .on_volume      = bt_on_volume,
};

/* ========== 控制按钮（LVGL 线程） ========== */
static void btn_play_cb(lv_event_t *e)
{
    (void)e;
    if (!g_connected) return;
    /* 读当前图标判断态：PAUSE 表示正在播放 → 发 pause；否则发 play。
     * 乐观更新：点击瞬间立刻切图标给即时视觉反馈，不等 AVRCP 回调往返
     * （手机确认要 1~2s，否则按钮看起来"没反应/很慢"）。回调回来会再校正。 */
    const void *src = lv_img_get_src(g_play_icon);
    if (src && strcmp((const char *)src, LV_SYMBOL_PAUSE) == 0) {
        lv_img_set_src(g_play_icon, LV_SYMBOL_PLAY);   /* 立刻切 */
        bt_speaker_avrcp_cmd(1);   /* pause */
    } else {
        lv_img_set_src(g_play_icon, LV_SYMBOL_PAUSE);  /* 立刻切 */
        bt_speaker_avrcp_cmd(0);   /* play */
    }
}

static void btn_prev_cb(lv_event_t *e)
{
    (void)e;
    if (g_connected) bt_speaker_avrcp_cmd(4);   /* backward */
}

static void btn_next_cb(lv_event_t *e)
{
    (void)e;
    if (g_connected) bt_speaker_avrcp_cmd(3);   /* forward */
}

/* 创建一个圆形按钮，里面放一个 SYMBOL 图标 */
static lv_obj_t *create_ctrl_btn(lv_obj_t *parent, const void *symbol,
                                 lv_coord_t size, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(COL_TITLE), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ico = lv_label_create(btn);
    lv_label_set_text(ico, symbol);
    lv_obj_set_style_text_font(ico, UI_FONT_SMALL, 0);
    lv_obj_center(ico);
    return btn;
}

/* ========== 界面构建（LVGL 线程，main.c 调用） ========== */
void ui_main_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 顶部标题 ---- */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, ui_font_cn_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TITLE), 0);
    lv_label_set_text(title, "蓝牙音箱");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    /* 标题下分隔线 */
    lv_obj_t *line = lv_obj_create(scr);
    lv_obj_set_size(line, 380, 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 88);

    /* ---- 歌名（大字，长歌名换行）---- */
    g_song_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_song_label, ui_font_cn_48, 0);
    lv_obj_set_style_text_color(g_song_label, lv_color_hex(COL_SONG), 0);
    lv_obj_set_width(g_song_label, 440);
    lv_label_set_long_mode(g_song_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_song_label, "—");
    lv_obj_set_style_text_align(g_song_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_song_label, LV_ALIGN_TOP_MID, 0, 110);

    /* ---- "歌手 - 专辑" ---- */
    g_artist_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_artist_label, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(g_artist_label, lv_color_hex(COL_SUB), 0);
    lv_obj_set_width(g_artist_label, 440);
    lv_label_set_long_mode(g_artist_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(g_artist_label, "");
    lv_obj_set_style_text_align(g_artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_artist_label, LV_ALIGN_TOP_MID, 0, 230);

    /* ---- 状态大字 ---- */
    g_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_status_label, ui_font_cn_32, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(COL_STATUS), 0);
    lv_label_set_text(g_status_label, "初始化中");
    lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 300);

    /* ---- 对端地址 ---- */
    g_addr_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_addr_label, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(g_addr_label, lv_color_hex(COL_SUB), 0);
    lv_label_set_text(g_addr_label, "");
    lv_obj_align(g_addr_label, LV_ALIGN_TOP_MID, 0, 348);

    /* ---- 进度条 ---- */
    g_bar = lv_bar_create(scr);
    lv_obj_set_size(g_bar, 420, 10);
    lv_bar_set_range(g_bar, 0, 100);
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(COL_BAR_BG), 0);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_bar, 5, 0);
    lv_obj_align(g_bar, LV_ALIGN_TOP_MID, 0, 400);

    /* ---- 时间 "0:00 / 0:00" ---- */
    g_time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_time_label, UI_FONT_TIME, 0);
    lv_obj_set_style_text_color(g_time_label, lv_color_hex(COL_SUB), 0);
    lv_label_set_text(g_time_label, "0:00 / 0:00");
    lv_obj_align(g_time_label, LV_ALIGN_TOP_MID, 0, 418);

    /* ---- 控制按钮行：上一首 / 播放暂停 / 下一首 ---- */
    lv_obj_t *btn_prev = create_ctrl_btn(scr, LV_SYMBOL_PREV, 56, btn_prev_cb);
    lv_obj_align(btn_prev, LV_ALIGN_TOP_MID, -120, 470);

    /* 播放/暂停用稍大按钮，里面放 img 以便切图标 */
    lv_obj_t *btn_play = lv_btn_create(scr);
    lv_obj_set_size(btn_play, 76, 76);
    lv_obj_set_style_radius(btn_play, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_play, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_shadow_width(btn_play, 0, 0);
    lv_obj_set_style_border_width(btn_play, 0, 0);
    lv_obj_align(btn_play, LV_ALIGN_TOP_MID, 0, 470);
    lv_obj_add_event_cb(btn_play, btn_play_cb, LV_EVENT_CLICKED, NULL);

    g_play_icon = lv_label_create(btn_play);
    lv_label_set_text(g_play_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(g_play_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_play_icon, lv_color_hex(COL_TITLE), 0);
    lv_obj_center(g_play_icon);

    lv_obj_t *btn_next = create_ctrl_btn(scr, LV_SYMBOL_NEXT, 56, btn_next_cb);
    lv_obj_align(btn_next, LV_ALIGN_TOP_MID, 120, 470);

    /* ---- 右侧竖向音量条 ---- */
    lv_obj_t *vol_cont = lv_obj_create(scr);
    lv_obj_set_size(vol_cont, 40, 150);
    lv_obj_set_style_bg_opa(vol_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_cont, 0, 0);
    lv_obj_set_style_pad_all(vol_cont, 0, 0);
    lv_obj_align(vol_cont, LV_ALIGN_RIGHT_MID, -8, 30);

    g_vol_label = lv_label_create(vol_cont);
    lv_obj_set_style_text_font(g_vol_label, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(g_vol_label, lv_color_hex(COL_SUB), 0);
    lv_label_set_text(g_vol_label, "0");
    lv_obj_align(g_vol_label, LV_ALIGN_TOP_MID, 0, 0);

    g_vol_bar = lv_bar_create(vol_cont);
    lv_obj_set_size(g_vol_bar, 10, 110);
    lv_bar_set_range(g_vol_bar, 0, 127);
    lv_bar_set_value(g_vol_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_vol_bar, lv_color_hex(COL_BAR_BG), 0);
    lv_obj_set_style_bg_color(g_vol_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_vol_bar, 5, 0);
    lv_obj_align(g_vol_bar, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* ---- 底部提示 ---- */
    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_label_set_text(hint, "手机搜索 ZGL_BT_SPEAKER 配对");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);
}

const bt_speaker_observer_t *ui_main_bt_observer(void)
{
    return &g_bt_obs;
}
