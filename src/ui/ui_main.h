/*
 * ui_main.h — 主界面（阶段3：事件入口由 drain 直调，不再暴露 bt observer）
 */
#ifndef UI_MAIN_H
#define UI_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

void ui_main_create(void);

/* ---- 事件入口（apps/app_player.c 的 drain 在 LVGL 线程调用） ---- */
void ui_player_on_adapter(int on);          /* 1=ON 0=OFF */
void ui_player_on_conn(const char *addr, int connected);
void ui_player_on_play_state(int play_state);  /* 1=playing 2=paused（BTMG 定义） */
void ui_player_on_track(const char *title, const char *artist,
                        const char *album, int duration_ms);
void ui_player_on_pos(int len_ms, int pos_ms);
void ui_player_on_volume(int vol);          /* 0..127 */

#ifdef __cplusplus
}
#endif

#endif /* UI_MAIN_H */
