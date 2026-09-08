/*
 * lv_port_indev.c — 输入端口：SDL 鼠标（host/x86 模拟器，鼠标 = 板上触摸）
 *
 * 与板级 src/ports/lv_port_indev.c（evdev）同构：注册 POINTER 输入。
 */
#include "lv_port_indev.h"
#include "lvgl/lvgl.h"
#include "lv_drivers/sdl/sdl.h"

int lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read;   /* 鼠标左键按下 = 触摸 */
    lv_indev_drv_register(&indev_drv);
    return 0;
}
