/*
 * lv_port_disp.c — 显示端口：SDL 窗口（host/x86 模拟器）
 *
 * 与板级 src/ports/lv_port_disp.c（sunxifb）同构：lv_init → 端口 init →
 * draw_buf + flush_cb 注册。分辨率 = 板上屏 480×640（sdl/lv_drv_conf.h），
 * LV_COLOR_DEPTH 32 与 SDL 的 ARGB8888 texture 天然匹配，flush 就是 memcpy。
 */
#include "lv_port_disp.h"
#include "lvgl/lvgl.h"
#include "lv_drivers/sdl/sdl.h"

#include <stdio.h>
#include <stdlib.h>

int lv_port_disp_init(void)
{
    static lv_disp_drv_t disp_drv;

    lv_init();
    sdl_init();

    static lv_color_t *buf;
    buf = (lv_color_t *)malloc(SDL_HOR_RES * SDL_VER_RES * sizeof(lv_color_t));
    if (buf == NULL) {
        printf("malloc draw buffer fail\n");
        return -1;
    }

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, SDL_HOR_RES * SDL_VER_RES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res = SDL_HOR_RES;
    disp_drv.ver_res = SDL_VER_RES;
    lv_disp_drv_register(&disp_drv);
    printf("sdl: %dx%d window\n", SDL_HOR_RES, SDL_VER_RES);
    return 0;
}

void lv_port_disp_exit(void)
{
    /* SDL 清理由 vendor 驱动在退出路径处理（SDL_Quit），这里无需动作 */
}
