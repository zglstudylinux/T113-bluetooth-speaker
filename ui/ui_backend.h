/*
 * ui_backend.h — UI 层接口（换主题 = 换实现，业务无感知）
 *
 * 实现放在 ui/themes/<主题>/，如 ui_backend_liquidglass（现 D1 液态玻璃主题）。
 *
 * 主题规则：
 *   - 只依赖 LVGL API + player_event_t + 本头文件，绝不 include 业务头文件
 *   - 不创建字体、不发命令：字体由 ports 注入（ui_env_t），按钮点击经
 *     env->cmd_request() 上抛给组装层转 backend->cmd()
 *   - on_event 已在 UI 线程（drain 后直调），内部可直接操作控件
 */
#ifndef UI_BACKEND_H
#define UI_BACKEND_H

#include "lvgl/lvgl.h"
#include "player_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 组装层注入给主题的运行环境 */
typedef struct {
    lv_obj_t *scr;                    /* 活动屏幕 */
    const lv_font_t *font_large;      /* 中文大字（现 cn_44） */
    const lv_font_t *font_small;      /* 中文小字（现 cn_22） */
    void (*cmd_request)(player_cmd_t c);  /* 按钮 → 组装层 → backend->cmd() */
} ui_env_t;

typedef struct {
    int  (*init)(const ui_env_t *env);            /* 建控件/加载素材，0 成功 */
    void (*on_event)(const player_event_t *ev);   /* UI 线程，drain 直调 */
    void (*deinit)(void);
} ui_backend_t;

extern const ui_backend_t ui_backend_liquidglass;     /* ui/themes/liquidglass/ */

#ifdef __cplusplus
}
#endif

#endif /* UI_BACKEND_H */
