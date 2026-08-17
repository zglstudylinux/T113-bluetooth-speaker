/*
 * T113 蓝牙音箱 — 主入口
 *
 * 显示：sunxifb /dev/fb0，480x640 竖屏（与 lvgl_demo_build 验证配置一致）
 * 触摸：evdev /dev/input/event4（dx_touch）
 * 构建：Makefile（镜像 lvgl_demo_build/src/Makefile），gcc-830 硬浮点动态链接
 */
#include "lvgl/lvgl.h"
#include "lv_drivers/display/sunxifb.h"
#include "lv_drivers/indev/evdev.h"
#include "lv_freetype.h"
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>

/* 板上资源路径（deploy.sh 推送目标）。
 * rootfs overlay 只有 ~8MB 放不下 8MB 字体，资源放 /mnt/UDISK（36MB）。 */
#define BOARD_RES_PATH   "/mnt/UDISK/speaker"
#define FONT_CN_REGULAR  BOARD_RES_PATH "/fonts/SOURCEHANSANSCN_REGULAR.OTF"

static lv_font_t *g_font_cn_32;
static lv_font_t *g_font_cn_48;

/* LVGL tick：LV_TICK_CUSTOM=1 时 lv_tick_inc 被宏屏蔽（custom expr 直接供时基），
 * lv_timer 也无需手动喂 tick —— 主循环只跑 lv_timer_handler 即可 */
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if (start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = ((uint64_t)tv_start.tv_sec * 1000000
                    + (uint64_t)tv_start.tv_usec) / 1000;
    }
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms = ((uint64_t)tv_now.tv_sec * 1000000
                       + (uint64_t)tv_now.tv_usec) / 1000;
    return (uint32_t)(now_ms - start_ms);
}

/* 触摸测试：按钮点击计数 */
static int g_click_count = 0;
static lv_obj_t *g_btn_label;

static void btn_click_cb(lv_event_t *e)
{
    (void)e;
    g_click_count++;
    lv_label_set_text_fmt(g_btn_label, "点击 %d 次", g_click_count);
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    uint32_t rotated = LV_DISP_ROT_NONE;

    /* LittlevGL init */
    lv_init();

    /* Linux frame buffer device init（480x640，不旋转） */
    sunxifb_init(rotated);

    /* 显示缓冲 */
    static uint32_t width, height;
    sunxifb_get_sizes(&width, &height);
    printf("fb: %ux%u\n", width, height);

    static lv_color_t *buf;
    buf = (lv_color_t *)sunxifb_alloc(width * height * sizeof(lv_color_t),
                                      "bt_speaker");
    if (buf == NULL) {
        sunxifb_exit();
        printf("malloc draw buffer fail\n");
        return 1;
    }

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, width * height);

    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = sunxifb_flush;
    disp_drv.hor_res = width;
    disp_drv.ver_res = height;
    lv_disp_drv_register(&disp_drv);

    /* 触摸输入 */
    evdev_init();
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);

    /* ===== M0 UI：标题 + 中文 + 触摸计数 ===== */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    /* FreeType 中文字体（运行时加载 .otf，本 lvgl 树用 lv_ft_font_init API） */
    lv_freetype_init(4, 4, 128 * 1024);
    static lv_ft_info_t ft48 = {
        .name = FONT_CN_REGULAR,
        .weight = 48,
        .style = FT_FONT_STYLE_NORMAL,
    };
    static lv_ft_info_t ft32 = {
        .name = FONT_CN_REGULAR,
        .weight = 32,
        .style = FT_FONT_STYLE_NORMAL,
    };
    if (lv_ft_font_init(&ft48) && lv_ft_font_init(&ft32)) {
        g_font_cn_48 = ft48.font;
        g_font_cn_32 = ft32.font;
    } else {
        printf("freetype font load FAIL: %s\n", FONT_CN_REGULAR);
        /* 降级：用内嵌 Montserrat 14 继续（中文会显示不出来） */
    }

    /* 标题 */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, g_font_cn_48 ? g_font_cn_48 : &lv_font_montserrat_14, 0);
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

    /* 状态区（M1 起显示蓝牙状态） */
    lv_obj_t *status = lv_label_create(scr);
    lv_obj_set_style_text_font(status, g_font_cn_32 ? g_font_cn_32 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(status, "480x640 M0");
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 180);

    /* 触摸测试按钮：点击计数 */
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 300, 90);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_radius(btn, 20, 0);
    g_btn_label = lv_label_create(btn);
    lv_obj_set_style_text_font(g_btn_label, g_font_cn_32 ? g_font_cn_32 : &lv_font_montserrat_14, 0);
    lv_label_set_text(g_btn_label, "点击 0 次");
    lv_obj_center(g_btn_label);
    lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED, NULL);

    /* ===== 主循环 ===== */
    while (1) {
        uint32_t time_till_next = lv_timer_handler();
        usleep((time_till_next > 0 ? time_till_next : 1) * 1000);
    }

    lv_ft_font_destroy(g_font_cn_48);
    lv_ft_font_destroy(g_font_cn_32);
    sunxifb_exit();
    return 0;
}
