/*
 * T113 蓝牙音箱 — 主入口
 *
 * 显示：sunxifb /dev/fb0，480x640 竖屏（与 lvgl_demo_build 验证配置一致）
 * 触摸：evdev /dev/input/event4（dx_touch）
 * 蓝牙：btmanager 4.0.3（libbtmg.so）A2DP Sink + AVRCP
 */
#include "lvgl/lvgl.h"
#include "lv_drivers/display/sunxifb.h"
#include "lv_drivers/indev/evdev.h"
#include "lv_freetype.h"
#include "ui/ui_main.h"
#include "../apps/app_player.h"
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>

/* 板上资源路径（deploy.sh 推送目标）。
 * rootfs overlay 只有 ~8MB 放不下 8MB 字体，资源放 /mnt/UDISK（36MB）。 */
#define BOARD_RES_PATH   "/mnt/UDISK/speaker"
#define FONT_CN_REGULAR  BOARD_RES_PATH "/fonts/SOURCEHANSANSCN_REGULAR.OTF"

#define BT_ALIAS  "ZGL_BT_SPEAKER"

/* UI 用字体（ui_main.c extern 引用） */
lv_font_t *ui_font_cn_22;
lv_font_t *ui_font_cn_44;

/* LVGL tick：LV_TICK_CUSTOM=1 时 custom_tick_get 直接供时基 */
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

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    uint32_t rotated = LV_DISP_ROT_NONE;

    lv_init();
    sunxifb_init(rotated);

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

    evdev_init();
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);

    /* ===== UI ===== */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    /* FreeType 中文字体（运行时加载 .otf，本 lvgl 树用 lv_ft_font_init API） */
    lv_freetype_init(4, 4, 128 * 1024);
    static lv_ft_info_t ft44 = {
        .name = FONT_CN_REGULAR,
        .weight = 44,
        .style = FT_FONT_STYLE_NORMAL,
    };
    static lv_ft_info_t ft22 = {
        .name = FONT_CN_REGULAR,
        .weight = 22,
        .style = FT_FONT_STYLE_NORMAL,
    };
    if (lv_ft_font_init(&ft44) && lv_ft_font_init(&ft22)) {
        ui_font_cn_44 = ft44.font;
        ui_font_cn_22 = ft22.font;
    } else {
        printf("freetype font load FAIL: %s\n", FONT_CN_REGULAR);
    }

    ui_main_create();

    /* ===== 蓝牙初始化（在 UI 之后；队列先建，早到事件会缓冲在队列里，
     * query_state 再兜底补发一次）===== */
    if (app_player_start(BT_ALIAS) != 0) {
        lv_obj_t *status = lv_label_create(scr);
        lv_obj_set_style_text_font(status, ui_font_cn_22, 0);
        lv_obj_set_style_text_color(status, lv_color_hex(0xE74C3C), 0);
        lv_label_set_text(status, "蓝牙初始化失败");
        lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -120);
    } else {
        app_player_query_state();
    }

    /* ===== 主循环 ===== */
    while (1) {
        uint32_t time_till_next = lv_timer_handler();
        usleep((time_till_next > 0 ? time_till_next : 1) * 1000);
    }

    app_player_stop();
    lv_ft_font_destroy(ui_font_cn_44);
    lv_ft_font_destroy(ui_font_cn_22);
    sunxifb_exit();
    return 0;
}
