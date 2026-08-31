/*
 * app_player.h — 组装层接口（阶段3）
 */
#ifndef APP_PLAYER_H
#define APP_PLAYER_H

#include "player_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 组装：建队列 + 起 btmg 后端 + 建 drain timer。UI 必须已创建（ui_main_create 后调）。 */
int  app_player_start(const char *alias);
/* UI 就绪后补发当前状态（adapter ON 早到兜底，原 bt_speaker_query_state） */
void app_player_query_state(void);
/* 播放控制（UI 按钮调用；LVGL 线程直调） */
int  app_player_cmd(player_cmd_t c);
/* 停止并释放（正常退出路径） */
void app_player_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_PLAYER_H */
