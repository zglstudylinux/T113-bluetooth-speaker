/*
 * lv_port_disp.h — 显示端口（sunxifb /dev/fb0）
 */
#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 打开 fb0 + 注册显示驱动；0 成功。失败时内部已清理。 */
int  lv_port_disp_init(void);
void lv_port_disp_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_DISP_H */
