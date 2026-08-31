/*
 * ui_main.c — 蓝牙音箱主界面（480x640 竖屏）· 深空玻璃主题
 *
 * 设计（assets/design/mockup_midnight.png，生成器 scripts/gen_design.py）：
 *   静态视觉全部烘进背景图 bg.png（深蓝黑渐变 + 光晕 + 玻璃卡片 + 均衡器条），
 *   LVGL 只画动态元素：
 *     顶栏      蓝牙符文+标题（cn_22） | 状态胶囊（圆点+状态字）
 *     玻璃卡片   唱盘 disc.png（播放时 12s/圈 慢转）叠在卡片中央
 *     曲目区    歌名（cn_44）+ 歌手专辑（cn_22）
 *     进度条    滑条样式 bar（含 knob），左右时间
 *     控制区    上一首 / 播放暂停（大，青色发光）/ 下一首，右侧竖音量条
 *
 * 素材从 POSIX FS 加载（LV_FS_POSIX_LETTER 'S'），deploy.sh 推到
 * /mnt/UDISK/speaker/image/。背景缺失时降级为纯深色底（布局不变，
 * 只是少了渐变/卡片；唱盘缺失时隐藏盘体）。
 *
 * BT 回调在 btmanager 线程 → lv_async_call 转到 LVGL 线程再操作 UI。
 */
#include "lvgl/lvgl.h"
#include "../../apps/app_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

/* adapter ON 时胶囊下方的别名行（与 main.c 的 BT_ALIAS 一致） */
#define BT_ALIAS_NAME  "ZGL_BT_SPEAKER"

/* 由 main.c 提供的字体 */
extern lv_font_t *ui_font_cn_44;
extern lv_font_t *ui_font_cn_22;

/* 小号字体用 LVGL 内置 Montserrat（lv_conf.h 已开 12/14/16），数字/符号够用 */
#define UI_FONT_NUM   (&lv_font_montserrat_14)

/* ---------- 主题色（D1 液态玻璃：浅银白底 + 深色文字 + iOS 蓝） ---------- */
#define COL_BG        0xE8ECF2   /* 降级底色（无背景图时） */
#define COL_TITLE     0x26303C
#define COL_SONG      0x182030
#define COL_ARTIST    0x606C80
#define COL_SUB       0x8C98AA
#define COL_ACCENT    0x0A84FF   /* iOS 蓝 */
#define COL_PLAY_BG   0x1C202C   /* 播放大圆钮：近黑（玻璃上的深色锚点） */
#define COL_PLAY_ICO  0xFFFFFF
#define COL_SIDE_BG   0xFFFFFF   /* 前后曲玻璃白圆钮 */
#define COL_SIDE_ICO  0x303A4C
#define COL_PILL_BG   0xFFFFFF   /* 状态胶囊 */
#define COL_PILL_TXT  0x303A4C

/* 图片资源路径：deploy.sh 推到 /mnt/UDISK/speaker/image/ */
#define IMG_BG_FILE   "/mnt/UDISK/speaker/image/bg.png"
#define IMG_BG_PATH   "S:" IMG_BG_FILE
#define IMG_DISC_FILE "/mnt/UDISK/speaker/image/disc.png"
#define IMG_DISC_PATH "S:" IMG_DISC_FILE

/* 布局常量（与设计稿一致，单位 px） */
#define DISC_CX       240
#define DISC_CY       226
#define DISC_SIZE     184
#define DISC_TICK_MS  33        /* 旋转定时器周期 ~30fps */
#define DISC_SPIN_MS  30000     /* 播放时唱盘一圈周期（缓慢优雅） */

/* ========== UI 对象 ========== */
static lv_obj_t *g_status_label;    /* 状态字：等待配对/已连接/播放中/已暂停 */
static lv_obj_t *g_status_dot;      /* 状态胶囊前圆点 */
static lv_obj_t *g_song_label;      /* 歌名 */
static lv_obj_t *g_artist_label;    /* "歌手 - 专辑" */
static lv_obj_t *g_addr_label;      /* 对端地址 */
static lv_obj_t *g_bar;             /* 进度条 */
static lv_obj_t *g_time_label;      /* "1:24 / 4:02" */
static lv_obj_t *g_play_icon;       /* 播放/暂停图标（label，用 LV_SYMBOL） */
static lv_obj_t *g_vol_bar;         /* 音量条 */
static lv_obj_t *g_vol_label;       /* 音量数值 */
static lv_obj_t *g_disc;            /* 唱盘图片（旋转） */

/* 运行态 */
static volatile bool g_connected = false;
static bool g_playing = false;      /* 唱盘是否在转 */

/* ========== 时间格式化 ========== */
static void fmt_time(char *buf, size_t len, int ms)
{
    if (ms < 0) ms = 0;
    int total = ms / 1000;
    int m = total / 60;
    int s = total % 60;
    snprintf(buf, len, "%d:%02d", m, s);
}

/* ========== 唱盘旋转 timer ========== */
static void disc_timer_cb(lv_timer_t *t)
{
    lv_obj_t *img = (lv_obj_t *)t->user_data;
    if (!img || !g_playing) return;
    int16_t a = lv_img_get_angle(img);
    /* 角度单位 0.1°，一圈 3600。lv_conf.h LV_IMG_CACHE_DEF_SIZE=2：
     * bg+disc 解码常驻，每帧旋转不再重新解码 PNG */
    int16_t step = (int16_t)(3600 * DISC_TICK_MS / DISC_SPIN_MS);
    lv_img_set_angle(img, (a + step) % 3600);
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

/* 播放/暂停 → 图标 + 唱盘转停 */
static void ui_play_icon_cb(void *p)
{
    int st = (int)(intptr_t)p;
    g_playing = (st == 1);
    lv_label_set_text(g_play_icon, g_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
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

/* ========== 事件入口（阶段3：由 apps/app_player.c 的 drain 在 LVGL 线程直调；
 * 原_observer 回调 + lv_async_call 投递机制已删除，事件经 OSAL 队列过来） ========== */

void ui_player_on_adapter(int on)
{
    if (on)
        post_state("等待配对", BT_ALIAS_NAME);
    else
        post_state("蓝牙关闭", "");
}

void ui_player_on_conn(const char *addr, int connected)
{
    g_connected = connected;
    if (connected) {
        post_state("已连接", addr ? addr : "");
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

void ui_player_on_play_state(int play_state)
{
    /* BTMG: 1=playing 2=paused */
    if (play_state == 1) {
        post_state("播放中", "");
        post_play_icon(1);
    } else if (play_state == 2) {
        post_state("已暂停", "");
        post_play_icon(2);
    }
}

void ui_player_on_track(const char *title, const char *artist,
                        const char *album, int duration_ms)
{
    ui_info_t info;
    memset(&info, 0, sizeof(info));
    snprintf(info.song, sizeof(info.song), "%s", title && title[0] ? title : "未知歌曲");
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

void ui_player_on_pos(int len_ms, int pos_ms)
{
    ui_info_t info;
    memset(&info, 0, sizeof(info));
    info.song[0] = 0;
    info.artist_album[0] = 0;
    info.pos_ms = pos_ms;
    info.len_ms = len_ms;
    info.volume = -1;
    post_info(&info);
}

void ui_player_on_volume(int vol)
{
    ui_info_t info;
    memset(&info, 0, sizeof(info));
    info.song[0] = 0;
    info.artist_album[0] = 0;
    info.pos_ms = -1;
    info.len_ms = -1;
    info.volume = vol;
    post_info(&info);
}

/* ========== 控制按钮（LVGL 线程） ========== */
static void btn_play_cb(lv_event_t *e)
{
    (void)e;
    if (!g_connected) return;
    /* 读当前图标判断态：PAUSE 表示正在播放 → 发 pause；否则发 play。
     * 乐观更新：点击瞬间立刻切图标给即时视觉反馈，不等 AVRCP 回调往返
     * （手机确认要 1~2s，否则按钮看起来"没反应"）。回调回来会再校正。 */
    const char *txt = lv_label_get_text(g_play_icon);
    if (txt && strcmp(txt, LV_SYMBOL_PAUSE) == 0) {
        lv_label_set_text(g_play_icon, LV_SYMBOL_PLAY);
        g_playing = false;
        app_player_cmd(PLAYER_CMD_PAUSE);
    } else {
        lv_label_set_text(g_play_icon, LV_SYMBOL_PAUSE);
        g_playing = true;
        app_player_cmd(PLAYER_CMD_PLAY);
    }
}

static void btn_prev_cb(lv_event_t *e)
{
    (void)e;
    if (g_connected) app_player_cmd(PLAYER_CMD_PREV);
}

static void btn_next_cb(lv_event_t *e)
{
    (void)e;
    if (g_connected) app_player_cmd(PLAYER_CMD_NEXT);
}

/* 次级圆钮：玻璃白底 + 白描边 + 深色图标 */
static lv_obj_t *create_ctrl_btn(lv_obj_t *parent, const void *symbol,
                                 lv_coord_t size, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_SIDE_BG), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(COL_SIDE_ICO), 0);
    /* 按下反馈：底色变实 */
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ico = lv_label_create(btn);
    lv_label_set_text(ico, symbol);
    lv_obj_set_style_text_font(ico, UI_FONT_NUM, 0);
    lv_obj_center(ico);
    return btn;
}

/* ========== 界面构建（LVGL 线程，main.c 调用） ========== */
void ui_main_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    bool has_bg = (access(IMG_BG_FILE, R_OK) == 0);
    bool has_disc = (access(IMG_DISC_FILE, R_OK) == 0);

    /* ---- 全屏背景（渐变+光晕+玻璃卡片+均衡器 已全部烘入） ---- */
    if (has_bg) {
        lv_obj_t *bg_img = lv_img_create(scr);
        lv_img_set_src(bg_img, IMG_BG_PATH);
        lv_obj_clear_flag(bg_img, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(bg_img, LV_ALIGN_TOP_MID, 0, 0);
    }

    /* ---- 玻璃卡片里的唱盘（184x184 透明 PNG，播放时旋转）---- */
    if (has_disc) {
        g_disc = lv_img_create(scr);
        lv_img_set_src(g_disc, IMG_DISC_PATH);
        lv_obj_clear_flag(g_disc, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(g_disc, LV_ALIGN_TOP_MID, 0, DISC_CY - DISC_SIZE / 2);
        /* 旋转以图片中心为轴 */
        lv_img_set_pivot(g_disc, DISC_SIZE / 2, DISC_SIZE / 2);
        lv_timer_create(disc_timer_cb, 33, g_disc);   /* ~30fps 递增角度 */
    }

    /* ---- 顶栏：蓝牙符文 + 标题 ---- */
    lv_obj_t *bt_sym = lv_label_create(scr);
    lv_obj_set_style_text_font(bt_sym, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bt_sym, lv_color_hex(COL_ACCENT), 0);
    lv_label_set_text(bt_sym, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(bt_sym, LV_ALIGN_TOP_LEFT, 20, 32);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, ui_font_cn_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TITLE), 0);
    lv_label_set_text(title, "蓝牙音箱");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 44, 31);

    /* ---- 右上状态胶囊（宽度容纳 4 个 22px 汉字："等待配对/蓝牙关闭"）---- */
    lv_obj_t *pill = lv_btn_create(scr);
    lv_obj_set_size(pill, 152, 34);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -26, 27);
    lv_obj_set_style_radius(pill, 17, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(COL_PILL_BG), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_60, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_shadow_width(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(pill, 0, 0);   /* lv_btn 默认 pad 会把子对象挤偏 */
    lv_obj_set_style_bg_opa(pill, LV_OPA_80, LV_STATE_PRESSED);

    g_status_dot = lv_obj_create(pill);
    lv_obj_set_size(g_status_dot, 8, 8);
    lv_obj_set_style_radius(g_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(g_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_status_dot, 0, 0);
    lv_obj_set_style_shadow_width(g_status_dot, 0, 0);
    lv_obj_clear_flag(g_status_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(g_status_dot, LV_ALIGN_LEFT_MID, 12, 0);

    g_status_label = lv_label_create(pill);
    lv_obj_set_style_text_font(g_status_label, ui_font_cn_22, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(COL_PILL_TXT), 0);
    lv_label_set_text(g_status_label, "初始化");
    lv_obj_align(g_status_label, LV_ALIGN_LEFT_MID, 26, 1);

    /* ---- 对端地址（胶囊正下方右对齐） ---- */
    g_addr_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_addr_label, UI_FONT_NUM, 0);
    lv_obj_set_style_text_color(g_addr_label, lv_color_hex(COL_SUB), 0);
    lv_label_set_text(g_addr_label, "");
    lv_obj_align(g_addr_label, LV_ALIGN_TOP_RIGHT, -26, 66);

    /* ---- 歌名（大字，长歌名换行） ---- */
    g_song_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_song_label, ui_font_cn_44, 0);
    lv_obj_set_style_text_color(g_song_label, lv_color_hex(COL_SONG), 0);
    lv_obj_set_width(g_song_label, 440);
    lv_label_set_long_mode(g_song_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_song_label, "—");
    lv_obj_set_style_text_align(g_song_label, LV_TEXT_ALIGN_CENTER, 0);
    /* FreeType 行高 ~1.45em，CJK 字形中心约在 0.83em 处：
     * 44px → 中心偏移 ~36px。top=384 使字形视觉中心 ≈ 420（同设计稿） */
    lv_obj_align(g_song_label, LV_ALIGN_TOP_MID, 0, 384);

    /* ---- "歌手 - 专辑" ---- */
    g_artist_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_artist_label, ui_font_cn_22, 0);
    lv_obj_set_style_text_color(g_artist_label, lv_color_hex(COL_ARTIST), 0);
    lv_obj_set_width(g_artist_label, 440);
    lv_label_set_long_mode(g_artist_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(g_artist_label, "");
    lv_obj_set_style_text_align(g_artist_label, LV_TEXT_ALIGN_CENTER, 0);
    /* 22px 行高 ~32，字形中心偏移 ~18 → top=444 使视觉中心 ≈ 462（同设计稿） */
    lv_obj_align(g_artist_label, LV_ALIGN_TOP_MID, 0, 444);

    /* ---- 进度条（胶囊形，带 knob） ---- */
    g_bar = lv_bar_create(scr);
    lv_obj_set_size(g_bar, 304, 6);
    lv_bar_set_range(g_bar, 0, 100);
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_bar, 3, 0);
    lv_obj_set_style_radius(g_bar, 3, LV_PART_INDICATOR);
    lv_obj_align(g_bar, LV_ALIGN_TOP_MID, -28, 500);

    /* knob（进度圆点；lv_bar 无内置 knob，用小 obj 跟随——先放静态简化版：
     * 因为 8px 圆点视觉弱，改为加亮 indicator 本身，knob 省略，与稿子差异极小） */

    /* ---- 时间 ---- */
    g_time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_time_label, UI_FONT_NUM, 0);
    lv_obj_set_style_text_color(g_time_label, lv_color_hex(COL_SUB), 0);
    lv_label_set_text(g_time_label, "0:00 / 0:00");
    lv_obj_align(g_time_label, LV_ALIGN_TOP_MID, -28, 512);

    /* ---- 右下竖向音量条 + 数值（设计稿：条 x424..434 y462..556，数字中心 y571）---- */
    g_vol_bar = lv_bar_create(scr);
    lv_obj_set_size(g_vol_bar, 10, 94);
    lv_bar_set_range(g_vol_bar, 0, 127);
    lv_bar_set_value(g_vol_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_vol_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_vol_bar, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(g_vol_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_vol_bar, 5, 0);
    lv_obj_set_style_radius(g_vol_bar, 5, LV_PART_INDICATOR);
    lv_obj_align(g_vol_bar, LV_ALIGN_TOP_RIGHT, -46, 462);

    g_vol_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_vol_label, UI_FONT_NUM, 0);
    lv_obj_set_style_text_color(g_vol_label, lv_color_hex(COL_SUB), 0);
    lv_label_set_text(g_vol_label, "0");
    lv_obj_align(g_vol_label, LV_ALIGN_TOP_RIGHT, -48, 563);

    /* ---- 控制按钮行：上一首 / 播放暂停 / 下一首 ---- */
    lv_obj_t *btn_prev = create_ctrl_btn(scr, LV_SYMBOL_PREV, 58, btn_prev_cb);
    lv_obj_align(btn_prev, LV_ALIGN_TOP_MID, -110, 553);

    /* 播放/暂停：大近黑圆钮（玻璃上的深色锚点，参照设计稿） */
    lv_obj_t *btn_play = lv_btn_create(scr);
    lv_obj_set_size(btn_play, 82, 82);
    lv_obj_set_style_radius(btn_play, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_play, lv_color_hex(COL_PLAY_BG), 0);
    lv_obj_set_style_shadow_color(btn_play, lv_color_hex(0x8FA0BC), 0);
    lv_obj_set_style_shadow_width(btn_play, 26, 0);
    lv_obj_set_style_shadow_spread(btn_play, 2, 0);
    lv_obj_set_style_border_width(btn_play, 0, 0);
    lv_obj_align(btn_play, LV_ALIGN_TOP_MID, 0, 541);
    lv_obj_add_event_cb(btn_play, btn_play_cb, LV_EVENT_CLICKED, NULL);

    g_play_icon = lv_label_create(btn_play);
    lv_label_set_text(g_play_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(g_play_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_play_icon, lv_color_hex(COL_PLAY_ICO), 0);
    lv_obj_center(g_play_icon);

    lv_obj_t *btn_next = create_ctrl_btn(scr, LV_SYMBOL_NEXT, 58, btn_next_cb);
    lv_obj_align(btn_next, LV_ALIGN_TOP_MID, 110, 553);
}

/* 阶段3起无 observer 暴露：事件经 OSAL 队列 → app_player drain → ui_player_on_* */
