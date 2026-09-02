/*
 * lv_port_font.h — FreeType 中文字体端口
 */
#ifndef LV_PORT_FONT_H
#define LV_PORT_FONT_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 加载中文大/小两号字体（板上 /mnt/UDISK/speaker/fonts/，rootfs 放不下 8MB 字体）。
 * 成功返回 0 并填充 large/small 指针（UI 用）；失败返回 -1（UI 需自行降级提示）。 */
int  lv_port_font_init(const lv_font_t **large, const lv_font_t **small);
void lv_port_font_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_FONT_H */
