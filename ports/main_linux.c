/*
 * main_linux.c — Linux/T113 入口 + 组装 + 主循环（ports 层）
 *
 * 组装顺序（顺序很重要，见 project-guide §5.1/§5.4）：
 *   1. LVGL + 显示 + 触摸 + 字体（ports）
 *   2. UI 主题 init（liquidglass）
 *   3. app_player_start：队列先建 → btmg 后端开始灌事件 → drain timer 消费
 *      （早到事件天然缓冲，query_state 兜底补发）
 *   4. lv_timer_handler 主循环
 *
 * 替代原 src/main.c；本层是唯一认识所有模块的地方（组装层 apps/ 供它调用）。
 */
#include "lvgl/lvgl.h"
#include "lv_freetype.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_port_font.h"
#include "../ui/ui_backend.h"
#include "../apps/app_player.h"
#include "../services/player_backend.h"

#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>

#define BT_ALIAS  "ZGL_BT_SPEAKER"

/* LVGL tick：LV_TICK_CUSTOM=1 时 custom_tick_get 直接供时基 */
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
    (void)argc; (void)argv;

    /* ===== 1. 平台端口 ===== */
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

    /* 队列先建 → UI init → btmg 后端灌事件 → drain timer 消费 */
    if (app_player_start(BT_ALIAS, &ui_backend_liquidglass, &env) != 0) {
        fprintf(stderr, "app_player start fail\n");
        lv_port_disp_exit();
        return 1;
    }
    app_player_query_state();

    /* ===== 4. 主循环 ===== */
    while (1) {
        uint32_t time_till_next = lv_timer_handler();
        usleep((time_till_next > 0 ? time_till_next : 1) * 1000);
    }

    /* 不可达（保留正常退出路径的完整清理顺序） */
    app_player_stop();
    lv_port_font_exit();
    lv_port_disp_exit();
    return 0;
}
