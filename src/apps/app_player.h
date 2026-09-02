/*
 * app_player.h — 组装层接口
 */
#ifndef APP_PLAYER_H
#define APP_PLAYER_H

#include "player_types.h"
#include "ui_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 组装：建队列 + init UI 主题 + 起 btmg 后端 + 建 drain timer。
 * env.cmd_request 内部会被 app_player_cmd 承接（无需调用方再接线）。 */
int  app_player_start(const char *alias, const ui_backend_t *ui, const ui_env_t *env);
/* UI 就绪后补发当前状态（adapter ON 早到兜底） */
void app_player_query_state(void);
/* 播放控制（主题按钮经 env.cmd_request 到这里） */
int  app_player_cmd(player_cmd_t c);
/* 停止并释放（正常退出路径） */
void app_player_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_PLAYER_H */
