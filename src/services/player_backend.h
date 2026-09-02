/*
 * player_backend.h — 业务层接口（换业务源 = 换实现，UI 无感知）
 *
 * 实现：
 *   services/btmg_player.c  → player_backend_btmg（Allwinner btmanager，板上）
 *   services/sim_player.c    → player_backend_sim（模拟数据源，host/CI 用）
 *
 * 上层（ports/main）只在组装期认识具体实现（链接期选择），UI 层完全不认识本文件。
 */
#ifndef PLAYER_BACKEND_H
#define PLAYER_BACKEND_H

#include "player_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 初始化。emit 由上层注入：后端在**业务线程**里组好 player_event_t 调 emit 投递
     * （emit 必须线程安全；上层实现=OSAL 队列 send）。返回 0 成功。 */
    int  (*init)(void (*emit)(const player_event_t *ev));
    void (*deinit)(void);
    /* 同步查询当前状态并补发事件（时序兜底：adapter ON 回调可能早于 UI 就绪，
     * 见 project-guide §5.4） */
    int  (*query_state)(void);
    /* 播放控制。UI 线程直接调用（AVRCP 往返本来就是异步的，不阻塞） */
    int  (*cmd)(player_cmd_t c);
} player_backend_t;

/* 可选实现（链接期选择，组装层 extern 引用） */
extern const player_backend_t player_backend_btmg;   /* services/btmg_player.c：Allwinner btmanager */
extern const player_backend_t player_backend_sim;    /* services/sim_player.c：模拟源（host/CI） */

/* btmg 实现的附加步骤：设置对外广播的蓝牙名（原 bt_speaker_init 的 alias 参数）。
 * 放在 init() 之后调用。 */
void player_backend_btmg_set_alias(const char *alias);

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_BACKEND_H */
