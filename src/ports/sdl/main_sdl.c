/*
 * main_sdl.c — host/x86 模拟器入口（ports 层，SDL 窗口版 main_linux.c）
 *
 * 与板上 main_linux.c 同一组装顺序：
 *   1. LVGL + SDL 显示/鼠标 + FreeType 字体（字体端口复用 src/ports/lv_port_font.c，
 *      经 BOARD_RES_PATH 宏指向 repo assets/）
 *   2. UI 主题 init（同一份 liquidglass 主题，与板上零差别）
 *   3. app_player_start：队列先建 → **sim 模拟后端**灌事件 → drain timer 消费
 *   4. lv_timer_handler 主循环
 *
 * 与板上唯一差异 = 业务后端（player_backend_sim vs player_backend_btmg）+
 * 显示/输入端口（SDL vs fb/evdev）——这正是 UI/业务解耦的直接演示。
 *
 * 用法：bt_speaker_sim [-t 秒]
 *   -t N  跑 N 秒后自动退出（CI 冒烟用，配合 SDL_VIDEODRIVER=dummy）；缺省一直跑
 */
#include "lvgl/lvgl.h"
#include "lv_freetype.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "../lv_port_font.h"
#include "../../ui/ui_backend.h"
#include "../../apps/app_player.h"
#include "../../services/player_backend.h"

#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* 模拟器无对外广播名（set_alias 为 NULL，组装层自动忽略） */
#define SIM_ALIAS  ""

/* LVGL tick：与 main_linux.c 相同（LV_TICK_CUSTOM=1 时 custom_tick_get 供时基） */
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if (start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = ((uint64_t)tv_start.tv_sec * 1000000
                    + (uint64_t)tv_start.tv_usec) / 1000;
    }
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms = ((uint64_t)tv_now.tv_sec * 1000000
                       + (uint64_t)tv_now.tv_usec) / 1000;
    return (uint32_t)(now_ms - start_ms);
}

/* UI 主题的命令请求 → 业务后端（LVGL 线程直调） */
static void ui_cmd_request(player_cmd_t c)
{
    app_player_cmd(c);
}

int main(int argc, char *argv[])
{
    /* -t <秒>：冒烟模式，N 秒后自动退出 0 */
    int smoke_s = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "-t") == 0)
            smoke_s = atoi(argv[i + 1]);
    }

    /* ===== 1. 平台端口（SDL） ===== */
    if (lv_port_disp_init() != 0)
        return 1;
    lv_port_indev_init();

    /* ===== 2. 字体 + UI 主题 + 业务（组装层一并接线）===== */
    const lv_font_t *font_large = NULL, *font_small = NULL;
    if (lv_port_font_init(&font_large, &font_small) != 0)
        fprintf(stderr, "warn: CN font load fail, theme may render fallback\n");

    ui_env_t env = {
        .scr         = lv_scr_act(),
        .font_large  = font_large,
        .font_small  = font_small,
        .cmd_request = ui_cmd_request,
    };

    /* 队列先建 → UI init → sim 后端灌事件 → drain timer 消费（与板上同序） */
    if (app_player_start(&player_backend_sim, SIM_ALIAS,
                         &ui_backend_liquidglass, &env) != 0) {
        fprintf(stderr, "app_player start fail\n");
        lv_port_disp_exit();
        return 1;
    }
    app_player_query_state();

    /* ===== 4. 主循环（-t 模式计时退出） ===== */
    uint32_t deadline_ms = smoke_s ? custom_tick_get() + (uint32_t)smoke_s * 1000 : 0;
    while (1) {
        uint32_t time_till_next = lv_timer_handler();
        usleep((time_till_next > 0 ? time_till_next : 1) * 1000);
        if (smoke_s && custom_tick_get() >= deadline_ms)
            break;
    }

    printf("sim: exit after %ds\n", smoke_s);
    app_player_stop();
    lv_port_font_exit();
    lv_port_disp_exit();
    return 0;
}
