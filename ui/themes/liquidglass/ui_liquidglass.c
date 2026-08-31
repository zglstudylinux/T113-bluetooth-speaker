/*
 * ui_liquidglass.c — D1 液态玻璃主题（ui_backend_t 实现）
 *
 * 由 src/ui/ui_main.c 迁移：绘制/布局/唱盘旋转/乐观更新逻辑原样保留，
 * 事件侧从"post_* 投递通道"改为 drain 直调的 on_event（player_event_t 已在
 * UI 线程，栈上直接用）。依赖注入：字体经 ui_env_t，发命令经 env->cmd_request。
 *
 * 设计（assets/design/mockup2_liquidglass.png，生成器 scripts/gen_design.py --v2）：
 *   静态视觉烘进 bg.png（浅银白渐变+玻璃折射圆+玻璃卡片），LVGL 只画动态元素。
 *   素材缺失时降级纯浅色底（布局不变）；disc.png 缺失时隐藏盘体。
 */
#include "lvgl/lvgl.h"
#include "ui_backend.h"
#include "theme.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

/* 小号字体用 LVGL 内置 Montserrat（lv_conf.h 已开 12/14/16），数字/符号够用 */
#define UI_FONT_NUM   (&lv_font_montserrat_14)

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

/* 组装层注入的运行环境（init 时保存） */
static ui_env_t g_env;

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

/* ========== 通用信息更新（进度/时间/音量） ========== */
static void apply_info(int pos_ms, int len_ms, int volume)
{
    if (len_ms >= 0) {
        int pct = (len_ms > 0) ? (int)((long long)pos_ms * 100 / len_ms) : 0;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(g_bar, pct, LV_ANIM_OFF);

        char cur[16], total[16], t[40];
        fmt_time(cur, sizeof(cur), pos_ms);
        fmt_time(total, sizeof(total), len_ms);
        snprintf(t, sizeof(t), "%s / %s", cur, total);
        lv_label_set_text(g_time_label, t);
    }

    if (volume >= 0) {
        int v = volume;
        if (v > 127) v = 127;
        lv_bar_set_value(g_vol_bar, v, LV_ANIM_OFF);
        lv_label_set_text_fmt(g_vol_label, "%d", v);
    }
}

/* ---- 歌名显示（长名自适应：1 行 / 2 行 / 2 行+省略号）----
 * 布局约束：进度条 y=500，歌手行 y=444。每次先不限行数测量真实高度，
 * 再按行数重排（无跨调用状态）：
 *   单行 → 保持设计稿位置（top=384，视觉中心 420），歌手行正常显示；
 *   两行 → 整体上移使块中心仍 ≈420，隐藏歌手行（top=444 会撞第二行）；
 *   三行以上 → 固定两行高度截断 + "…"。 */
static void apply_song(const char *song)
{
    lv_coord_t line_h   = lv_font_get_line_height(g_env.font_large);
    lv_coord_t line_sp  = lv_obj_get_style_text_line_space(g_song_label, LV_PART_MAIN);
    lv_coord_t two_h    = line_h * 2 + line_sp;
    lv_coord_t top_1ln  = 384;                      /* 设计稿单行位置 */
    lv_coord_t top_2ln  = 420 - two_h / 2;          /* 两行块中心对齐 420 */

    /* 1) 按"不限行数"测量 */
    lv_obj_set_height(g_song_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(g_song_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_song_label, song);
    lv_obj_update_layout(g_song_label);
    lv_coord_t h = lv_obj_get_height(g_song_label);

    /* 2) 按行数重排 */
    if (h > two_h) {                                /* 3 行以上：截断两行 + … */
        lv_obj_set_height(g_song_label, two_h);
        lv_label_set_long_mode(g_song_label, LV_LABEL_LONG_DOT);
        lv_obj_align(g_song_label, LV_ALIGN_TOP_MID, 0, top_2ln);
        lv_obj_add_flag(g_artist_label, LV_OBJ_FLAG_HIDDEN);
    } else if (h > line_h) {                        /* 正好两行 */
        lv_obj_align(g_song_label, LV_ALIGN_TOP_MID, 0, top_2ln);
        lv_obj_add_flag(g_artist_label, LV_OBJ_FLAG_HIDDEN);
    } else {                                        /* 单行 */
        lv_obj_align(g_song_label, LV_ALIGN_TOP_MID, 0, top_1ln);
        lv_obj_clear_flag(g_artist_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_state(const char *status, const char *sub)
{
    lv_label_set_text(g_status_label, status);
    lv_label_set_text(g_addr_label, sub);
}

static void apply_play_icon(int play_state)
{
    g_playing = (play_state == 1);
    lv_label_set_text(g_play_icon, g_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

/* ========== on_event：drain 在 LVGL 线程直调 ========== */
static void ui_on_event(const player_event_t *ev)
{
    switch (ev->type) {
    case PLAYER_EVT_ADAPTER:
        if (ev->on == 1)
            apply_state("等待配对", THEME_BT_ALIAS);
        else
            apply_state("蓝牙关闭", "");
        break;

    case PLAYER_EVT_CONN:
        g_connected = (ev->connected == 1);
        if (g_connected) {
            apply_state("已连接", ev->addr);
            apply_play_icon(2);
        } else {
            apply_state("等待配对", "");
            apply_song("—");                     /* 清空歌曲信息（恢复单行占位） */
            lv_label_set_text(g_artist_label, "");
            lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
            lv_label_set_text(g_time_label, "0:00 / 0:00");
            apply_play_icon(2);
        }
        break;

    case PLAYER_EVT_PLAY_STATE:
        /* BTMG: 1=playing 2=paused */
        if (ev->play_state == 1) {
            apply_state("播放中", "");
            apply_play_icon(1);
        } else if (ev->play_state == 2) {
            apply_state("已暂停", "");
            apply_play_icon(2);
        }
        break;

    case PLAYER_EVT_TRACK: {
        const char *title = ev->title[0] ? ev->title : "未知歌曲";
        char artist_album[sizeof(ev->artist) + sizeof(ev->album) + 4];
        if (ev->artist[0] && ev->album[0])
            snprintf(artist_album, sizeof(artist_album), "%s - %s", ev->artist, ev->album);
        else if (ev->artist[0])
            snprintf(artist_album, sizeof(artist_album), "%s", ev->artist);
        else if (ev->album[0])
            snprintf(artist_album, sizeof(artist_album), "%s", ev->album);
        else
            snprintf(artist_album, sizeof(artist_album), "未知歌手");
        apply_song(title);
        lv_label_set_text(g_artist_label, artist_album);
        if (ev->len_ms >= 0) {
            lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
            char cur[16], total[16], t[40];
            fmt_time(cur, sizeof(cur), 0);
            fmt_time(total, sizeof(total), ev->len_ms);
            snprintf(t, sizeof(t), "%s / %s", cur, total);
            lv_label_set_text(g_time_label, t);
        }
        break;
    }

    case PLAYER_EVT_POS:
        apply_info(ev->pos_ms, ev->len_ms, -1);
        break;

    case PLAYER_EVT_VOL:
        apply_info(-1, -1, ev->volume);
        break;

    default:
        break;
    }
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
        g_env.cmd_request(PLAYER_CMD_PAUSE);
    } else {
        lv_label_set_text(g_play_icon, LV_SYMBOL_PAUSE);
        g_playing = true;
        g_env.cmd_request(PLAYER_CMD_PLAY);
    }
}

static void btn_prev_cb(lv_event_t *e)
{
    (void)e;
    if (!g_connected) return;
    /* 暂停态切歌，手机会恢复播放——乐观更新图标（同 btn_play_cb），不等 AVRCP 回调 */
    if (!g_playing) {
        g_playing = true;
        lv_label_set_text(g_play_icon, LV_SYMBOL_PAUSE);
    }
    g_env.cmd_request(PLAYER_CMD_PREV);
}

static void btn_next_cb(lv_event_t *e)
{
    (void)e;
    if (!g_connected) return;
    if (!g_playing) {
        g_playing = true;
        lv_label_set_text(g_play_icon, LV_SYMBOL_PAUSE);
    }
    g_env.cmd_request(PLAYER_CMD_NEXT);
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

/* ========== 界面构建（LVGL 线程，组装层经 init 调用） ========== */
static int ui_init(const ui_env_t *env)
{
    g_env = *env;
    lv_obj_t *scr = env->scr;
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
        lv_timer_create(disc_timer_cb, DISC_TICK_MS, g_disc);   /* ~30fps 递增角度 */
    }

    /* ---- 顶栏：蓝牙符文 + 标题 ---- */
    lv_obj_t *bt_sym = lv_label_create(scr);
    lv_obj_set_style_text_font(bt_sym, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bt_sym, lv_color_hex(COL_ACCENT), 0);
    lv_label_set_text(bt_sym, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(bt_sym, LV_ALIGN_TOP_LEFT, 20, 32);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, env->font_small, 0);
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
    lv_obj_set_style_text_font(g_status_label, env->font_small, 0);
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
    lv_obj_set_style_text_font(g_song_label, env->font_large, 0);
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
    lv_obj_set_style_text_font(g_artist_label, env->font_small, 0);
    lv_obj_set_style_text_color(g_artist_label, lv_color_hex(COL_ARTIST), 0);
    lv_obj_set_width(g_artist_label, 440);
    lv_label_set_long_mode(g_artist_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(g_artist_label, "");
    lv_obj_set_style_text_align(g_artist_label, LV_TEXT_ALIGN_CENTER, 0);
    /* 22px 行高 ~32，字形中心偏移 ~18 → top=444 使视觉中心 ≈ 462（同设计稿） */
    lv_obj_align(g_artist_label, LV_ALIGN_TOP_MID, 0, 444);

    /* ---- 进度条（胶囊形） ---- */
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

    return 0;
}

static void ui_deinit(void)
{
    /* LVGL 对象随 scr 销毁；这里只停运行态 */
    g_playing = false;
    g_connected = false;
}

const ui_backend_t ui_backend_liquidglass = {
    .init     = ui_init,
    .on_event = ui_on_event,
    .deinit   = ui_deinit,
};
