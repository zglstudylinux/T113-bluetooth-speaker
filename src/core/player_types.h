/*
 * player_types.h — 全项目唯一数据契约（core 层，纯 C99）
 *
 * 业务后端（services/）往 player_event_t 里写，UI 主题（ui/themes/）从里读。
 * 两层互相不 include 对方头文件，只认识这个结构体——这是 UI/业务解耦
 * 和单片机可移植性的锚点（见 docs/architecture.md §4.1/§10）。
 *
 * 约定：不适用的整型字段填 -1、字符串填空串 = "本事件不更新该项"。
 * 结构体定长（值语义，无指针），跨线程直接 memcpy 进 OSAL 队列。
 */
#ifndef PLAYER_TYPES_H
#define PLAYER_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 播放控制命令（UI 按钮 → 业务后端；各后端自行映射，btmg 后端映射到 AVRCP） */
typedef enum {
    PLAYER_CMD_PLAY = 0,
    PLAYER_CMD_PAUSE,
    PLAYER_CMD_NEXT,
    PLAYER_CMD_PREV,
} player_cmd_t;

/* 事件类型 */
typedef enum {
    PLAYER_EVT_ADAPTER = 0,   /* 蓝牙开关：on=0/1 */
    PLAYER_EVT_CONN,          /* 连接状态：connected + addr */
    PLAYER_EVT_PLAY_STATE,    /* play_state: 1=playing 2=paused */
    PLAYER_EVT_TRACK,         /* 曲目元数据：title/artist/album/len_ms（pos_ms=0） */
    PLAYER_EVT_POS,           /* 播放进度：pos_ms + len_ms，其余不更新 */
    PLAYER_EVT_VOL,           /* 音量：volume 0..127 */
} player_evt_type_t;

typedef struct {
    player_evt_type_t type;
    char    title[64];        /* 歌名（UTF-8） */
    char    artist[48];       /* 歌手 */
    char    album[48];        /* 专辑 */
    char    addr[18];         /* 对端地址 "AA:BB:CC:DD:EE:FF" */
    int32_t pos_ms;           /* 播放位置（ms） */
    int32_t len_ms;           /* 曲目时长（ms） */
    int32_t volume;           /* 0..127（btmg 量程） */
    int32_t play_state;       /* 1=playing 2=paused（沿用 btmg 定义） */
    int32_t connected;        /* 0=断开 1=已连接 */
    int32_t on;               /* adapter 开关（EVT_ADAPTER 用）0/1 */
} player_event_t;             /* ~216B 定长，MCU 友好 */

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_TYPES_H */
