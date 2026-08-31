/*
 * theme.h — D1 液态玻璃主题：色板 / 素材路径 / 布局常量
 * （从原 src/ui/ui_main.c 拆出，数值与设计稿 assets/design/mockup2_liquidglass.png 一致）
 */
#ifndef LIQUIDGLASS_THEME_H
#define LIQUIDGLASS_THEME_H

/* ---------- 主题色（D1 液态玻璃：浅银白底 + 深色文字 + iOS 蓝） ---------- */
#define COL_BG        0xE8ECF2   /* 降级底色（无背景图时） */
#define COL_TITLE     0x26303C
#define COL_SONG      0x182030
#define COL_ARTIST    0x606C80
#define COL_SUB       0x8C98AA
#define COL_ACCENT    0x0A84FF   /* iOS 蓝 */
#define COL_PLAY_BG   0x1C202C   /* 播放大圆钮：近黑（玻璃上的深色锚点） */
#define COL_PLAY_ICO  0xFFFFFF
#define COL_SIDE_BG   0xFFFFFF   /* 前后曲玻璃白圆钮 */
#define COL_SIDE_ICO  0x303A4C
#define COL_PILL_BG   0xFFFFFF   /* 状态胶囊 */
#define COL_PILL_TXT  0x303A4C

/* ---------- 图片资源路径（deploy.sh 推到 /mnt/UDISK/speaker/image/） ---------- */
#define IMG_BG_FILE   "/mnt/UDISK/speaker/image/bg.png"
#define IMG_BG_PATH   "S:" IMG_BG_FILE
#define IMG_DISC_FILE "/mnt/UDISK/speaker/image/disc.png"
#define IMG_DISC_PATH "S:" IMG_DISC_FILE

/* ---------- 布局常量（与设计稿一致，单位 px） ---------- */
#define DISC_CX       240
#define DISC_CY       226
#define DISC_SIZE     184
#define DISC_TICK_MS  33        /* 旋转定时器周期 ~30fps */
#define DISC_SPIN_MS  30000     /* 播放时唱盘一圈周期（缓慢优雅） */

/* 状态胶囊下方的别名行（与 ports 层的 BT_ALIAS 一致） */
#define THEME_BT_ALIAS "ZGL_BT_SPEAKER"

#endif /* LIQUIDGLASS_THEME_H */
