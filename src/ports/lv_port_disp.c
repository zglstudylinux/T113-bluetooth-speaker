/*
 * lv_port_disp.c — 显示端口：sunxifb /dev/fb0，480x640 竖屏
 * （由 src/main.c 的显示初始化拆出，行为不变）
 */
#include "lv_port_disp.h"
#include "lv_drivers/display/sunxifb.h"

#include <stdio.h>
#include <stdlib.h>

int lv_port_disp_init(void)
{
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
        return -1;
    }

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, width * height);

    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = sunxifb_flush;
    disp_drv.hor_res = width;
    disp_drv.ver_res = height;
    lv_disp_drv_register(&disp_drv);
    return 0;
}

void lv_port_disp_exit(void)
{
    sunxifb_exit();
}
