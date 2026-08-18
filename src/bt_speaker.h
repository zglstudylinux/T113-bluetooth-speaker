/*
 * bt_speaker.h — 蓝牙音箱 BT 侧封装（基于 app_sdk app_bt_audio 精简）
 *
 * btmanager 4.0.3（libbtmg.so）：A2DP Sink + AVRCP，免 PIN 配对。
 */
#ifndef BT_SPEAKER_H
#define BT_SPEAKER_H

#include <stdbool.h>

/* UI 更新回调（在 btmanager 线程被调用！UI 里必须用 lv_async_call 转到 LVGL 线程） */
typedef struct {
    void (*on_adapter_on)(const char *addr, const char *alias);
    void (*on_adapter_off)(void);
    void (*on_conn_state)(const char *addr, int connected); /* 1=已连接 0=断开 */
    void (*on_play_state)(const char *addr, int play_state); /* 1=播放 2=暂停（BTMG 定义） */
    void (*on_track)(const char *addr, const char *title, const char *artist,
                     const char *album, int duration_ms);
    void (*on_play_pos)(const char *addr, int song_len_ms, int song_pos_ms);
    void (*on_volume)(const char *addr, unsigned int vol_0_127);
} bt_speaker_observer_t;

int  bt_speaker_init(const char *alias, const bt_speaker_observer_t *obs);
void bt_speaker_deinit(void);
/* 进入可连接可发现模式（手机能搜到并配对） */
int  bt_speaker_set_discoverable(bool on);
int  bt_speaker_query_state(void);
/* 播放控制（AVRCP）: 0=play 1=pause 2=stop 3=forward 4=backward */
int  bt_speaker_avrcp_cmd(int cmd);

#endif
