/*
 * lv_port_indev.h — 触摸输入端口（evdev /dev/input/event4）
 */
#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

/* 打开 evdev + 注册 pointer 输入；0 成功 */
int lv_port_indev_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_INDEV_H */
