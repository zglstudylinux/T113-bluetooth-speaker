/*
 * lv_port_font.c — FreeType 中文字体端口
 * （由 src/main.c 的字体加载拆出，行为不变：lvgl 树旧 API lv_ft_font_init）
 */
#include "lv_port_font.h"
#include "lv_freetype.h"

#include <stdio.h>

/* 板上资源路径（deploy.sh 推送目标） */
#define BOARD_RES_PATH   "/mnt/UDISK/speaker"
#define FONT_CN_REGULAR  BOARD_RES_PATH "/fonts/SOURCEHANSANSCN_REGULAR.OTF"

static lv_ft_info_t ft_large;   /* 44 号：歌名 */
static lv_ft_info_t ft_small;   /* 22 号：标题/歌手/状态 */

int lv_port_font_init(const lv_font_t **large, const lv_font_t **small)
{
    lv_freetype_init(4, 4, 128 * 1024);

    static lv_ft_info_t f44 = {
        .name = FONT_CN_REGULAR,
        .weight = 44,
        .style = FT_FONT_STYLE_NORMAL,
    };
    static lv_ft_info_t f22 = {
        .name = FONT_CN_REGULAR,
        .weight = 22,
        .style = FT_FONT_STYLE_NORMAL,
    };

    if (lv_ft_font_init(&f44) && lv_ft_font_init(&f22)) {
        ft_large = f44;
        ft_small = f22;
        *large = f44.font;
        *small = f22.font;
        return 0;
    }
    printf("freetype font load FAIL: %s\n", FONT_CN_REGULAR);
    return -1;
}

void lv_port_font_exit(void)
{
    lv_ft_font_destroy(ft_large.font);
    lv_ft_font_destroy(ft_small.font);
}
