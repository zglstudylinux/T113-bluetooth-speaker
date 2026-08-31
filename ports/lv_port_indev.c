/*
 * lv_port_indev.c — 触摸输入端口：evdev /dev/input/event4（dx_touch）
 * （由 src/main.c 的触摸初始化拆出，行为不变）
 */
#include "lv_port_indev.h"
#include "lvgl/lvgl.h"
#include "lv_drivers/indev/evdev.h"

int lv_port_indev_init(void)
{
    evdev_init();
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);
    return 0;
}
