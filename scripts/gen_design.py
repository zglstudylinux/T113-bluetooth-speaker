#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_design.py — 蓝牙音箱 UI 重设计：三套 480x640 设计稿 + 背景图生成器

三套方案（同一信息架构，不同视觉皮肤）：
  A  midnight  深空玻璃  — 深蓝黑 + 青色霓虹 + 玻璃卡片 + 唱盘
  B  sunset    落日唱盘  — 紫粉橙渐变 + 黑胶唱盘 + 底部毛玻璃操作带
  C  cloud     云白极简  — 浅色苹果风 + 产品实拍圆窗 + 蓝色点缀

用法：
  python3 scripts/gen_design.py            # 生成三张设计稿 + 对比大图
  python3 scripts/gen_design.py --bg A     # 只输出某方案的背景图（实现阶段用）

原理：所有静态视觉（渐变/光晕/唱盘/玻璃卡片）全部烘进一张 480x640 背景 PNG，
LVGL 只在其上画动态元素（文字/进度条/按钮）。所以本脚本渲出的设计稿
（背景 + 模拟 LVGL 控件）与最终上屏效果基本一致。
2x 超采样抗锯齿后缩到 480x640。
"""
import math
import os
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 2                       # 超采样倍率
W, H = 480 * S, 640 * S
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_PATH = os.path.join(ROOT, "assets/fonts/SOURCEHANSANSCN_REGULAR.OTF")
PHOTO_PATH = os.path.join(ROOT, "assets/image/bt.png")   # C 方案产品实拍
OUT_DIR = os.path.join(ROOT, "assets/design")

random.seed(42)

# 演示数据（设计稿里的示例内容，与真机数据字段一致）
SAMPLE = dict(song="晴天", artist="周杰伦 - 叶惠美", status="已连接",
              addr="A4:C1:38:6C:AA:22", cur="1:24", total="4:02",
              prog=0.35, vol=46, vol_pct=46 / 127)

_fcache = {}


def F(px):
    """按 1x 像素值取字体（内部放大 S 倍）"""
    k = max(6, int(round(px * S)))
    if k not in _fcache:
        _fcache[k] = ImageFont.truetype(FONT_PATH, k)
    return _fcache[k]


def C(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def lerp_c(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


# ---------------- 背景基底（numpy 渐变 / 光晕） ----------------

def base_np(color):
    a = np.zeros((H, W, 3), np.float32)
    a[:, :] = C(color)
    return a


def radial(arr, cx, cy, r, col, strength):
    """往 numpy 画布上叠一个径向光晕（cx/cy/r 单位 1x 像素）"""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    d = np.sqrt((xx - cx * S) ** 2 + (yy - cy * S) ** 2) / (r * S)
    m = (np.clip(1 - d, 0, 1) ** 2) * strength
    c = np.array(C(col), np.float32)
    for i in range(3):
        arr[:, :, i] = arr[:, :, i] * (1 - m) + c[i] * m


def grad_np(stops, deg):
    """沿 deg 方向的多段线性渐变。stops=[(0,'#..'),(0.5,'#..')...]"""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    th = math.radians(deg)
    proj = xx * math.cos(th) + yy * math.sin(th)
    proj = (proj - proj.min()) / (proj.max() - proj.min())
    pos = np.array([p for p, _ in stops], np.float32)
    cols = np.array([C(c) for _, c in stops], np.float32)
    out = np.zeros((H, W, 3), np.float32)
    for i in range(3):
        out[:, :, i] = np.interp(proj, pos, cols[:, i])
    return out


def add_noise(arr, amp=1.2):
    """轻微噪声抖动，防渐变色带"""
    arr += np.random.uniform(-amp, amp, arr.shape).astype(np.float32)
    return arr


def np_to_img(arr):
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def np_to_rgb(arr):
    """RGB canvas: ImageDraw.Draw(im,"RGBA") 真正按 alpha 混合
    （PIL 的 RGBA canvas draw 是原样替换像素，半透明色会直接写死）"""
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")


def composite(img, ov, pos=None):
    """ov 合成到 img：RGBA canvas 用 alpha_composite，RGB canvas 用 paste+alpha"""
    if img.mode == "RGBA":
        img.alpha_composite(ov, pos)
    else:
        img.paste(ov, pos, ov)


# ---------------- 绘制辅助（坐标一律 1x 像素） ----------------

def overlay():
    return Image.new("RGBA", (W, H), (0, 0, 0, 0))


def rrect(d, box, r, fill=None, outline=None, width=1):
    x0, y0, x1, y1 = [v * S for v in box]
    r = min(r * S, (x1 - x0) / 2 - 1, (y1 - y0) / 2 - 1)
    if r < 1:   # 太小退化为直角矩形（PIL rounded_rectangle 极小矩形会报错）
        d.rectangle([x0, y0, x1, y1], fill=fill, outline=outline,
                    width=max(1, int(width * S)))
    else:
        d.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=fill,
                            outline=outline, width=max(1, int(width * S)))


def circle(d, cx, cy, r, fill=None, outline=None, width=1):
    d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S],
              fill=fill, outline=outline, width=max(1, int(width * S)))


def text(d, xy, s, px, col, anchor="mm"):
    d.text((xy[0] * S, xy[1] * S), s, font=F(px), fill=col, anchor=anchor)


def rgba(h, a):
    return C(h) + (a,)


# ---- 图标（矢量手绘，缩放后锐利） ----

def ico_pause(d, cx, cy, h, col, bw=None):
    bw = bw or h * 0.26
    gap = h * 0.34
    rrect(d, (cx - gap / 2 - bw, cy - h / 2, cx - gap / 2, cy + h / 2), bw * 0.45, fill=col)
    rrect(d, (cx + gap / 2, cy - h / 2, cx + gap / 2 + bw, cy + h / 2), bw * 0.45, fill=col)


def ico_play(d, cx, cy, h, col, off=None):
    off = h * 0.10 if off is None else off
    x0 = cx - h * 0.38 + off
    pts = [(x0, cy - h / 2), (x0, cy + h / 2), (cx + h * 0.46 + off, cy)]
    d.polygon([(x * S, y * S) for x, y in pts], fill=col)


def ico_prev(d, cx, cy, h, col):
    bw = h * 0.13
    rrect(d, (cx - h * 0.42, cy - h / 2, cx - h * 0.42 + bw, cy + h / 2), 1, fill=col)
    pts = [(cx + h * 0.36, cy - h / 2), (cx + h * 0.36, cy + h / 2), (cx - h * 0.26, cy)]
    d.polygon([(x * S, y * S) for x, y in pts], fill=col)


def ico_next(d, cx, cy, h, col):
    bw = h * 0.13
    rrect(d, (cx + h * 0.42 - bw, cy - h / 2, cx + h * 0.42, cy + h / 2), 1, fill=col)
    pts = [(cx - h * 0.36, cy - h / 2), (cx - h * 0.36, cy + h / 2), (cx + h * 0.26, cy)]
    d.polygon([(x * S, y * S) for x, y in pts], fill=col)


def bt_rune(d, cx, cy, r, col, wd=1.6):
    """蓝牙符号：竖线 + 右侧蝶形折线"""
    T, B = (cx, cy - r), (cx, cy + r)
    R1, R2, M = (cx + 0.62 * r, cy - 0.45 * r), (cx + 0.62 * r, cy + 0.45 * r), (cx, cy)
    seg = [T, B], [T, R1], [R1, M], [M, R2], [R2, B]
    for a, b in seg:
        d.line([a[0] * S, a[1] * S, b[0] * S, b[1] * S],
               fill=col, width=max(2, int(wd * S)), joint="curve")


# ---- 唱盘（返回 RGBA 贴片，含沟槽 / 高光楔 / 中心标） ----

def disc_tile(r, base, groove, label_a, label_b=None, hole=None,
              rim=None, sheen=26, groove_px=2.2):
    r = float(r)
    pad = 6
    d2 = int((2 * r + 2 * pad) * S)
    img = Image.new("RGBA", (d2, d2), (0, 0, 0, 0))
    dd = ImageDraw.Draw(img)
    cx = cy = d2 // 2
    R = r * S

    dd.ellipse([cx - R, cy - R, cx + R, cy + R], fill=C(base) + (255,))
    # 沟槽：一圈圈细圆环，亮度按伪随机起伏
    i = 0
    rr = (r * 0.30)
    while rr < r - 3:
        t = 0.5 + 0.5 * math.sin(i * 0.83)
        col = lerp_c(C(base), C(groove), 0.25 + 0.75 * t)
        dd.ellipse([cx - rr * S, cy - rr * S, cx + rr * S, cy + rr * S],
                   outline=col + (210,), width=1)
        rr += groove_px
        i += 1
    # 高光楔（左上 90° 扇形，模糊后像唱片反光）
    wedge = Image.new("RGBA", (d2, d2), (0, 0, 0, 0))
    wd = ImageDraw.Draw(wedge)
    wd.pieslice([2 * S, 2 * S, d2 - 2 * S, d2 - 2 * S], 185, 285,
                fill=(255, 255, 255, sheen))
    wedge = wedge.filter(ImageFilter.GaussianBlur(9 * S))
    # 楔形只留在盘面内
    mask = Image.new("L", (d2, d2), 0)
    ImageDraw.Draw(mask).ellipse([cx - R + 1, cy - R + 1, cx + R - 1, cy + R - 1], fill=255)
    img.paste(Image.alpha_composite(img.crop((0, 0, d2, d2)), wedge), (0, 0), mask)

    if rim:
        dd.ellipse([cx - R + 1, cy - R + 1, cx + R - 1, cy + R - 1],
                   outline=C(rim) + (160,), width=max(2, int(1.6 * S)))
    # 中心标（1~2 层色环 + 中孔）
    lr = r * 0.32
    dd.ellipse([cx - lr * S, cy - lr * S, cx + lr * S, cy + lr * S], fill=C(label_a) + (255,))
    if label_b:
        lr2 = r * 0.20
        dd.ellipse([cx - lr2 * S, cy - lr2 * S, cx + lr2 * S, cy + lr2 * S],
                   fill=C(label_b) + (255,))
    hr = (hole if hole else r * 0.06)
    dd.ellipse([cx - hr * S, cy - hr * S, cx + hr * S, cy + hr * S],
               fill=C(base) + (255,))
    return img


def paste_tile(img, tile, cx, cy):
    """tile 是 S 倍超采样像素，画布也是 S 倍 → 中心对齐直接用 cx*S"""
    composite(img, tile, (int(cx * S - tile.width / 2), int(cy * S - tile.height / 2)))


def glow_disc(img, cx, cy, r, col, alpha=70, blur=16):
    """盘后的柔光"""
    gl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(gl)
    gd.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S],
               fill=C(col) + (alpha,))
    composite(img, gl.filter(ImageFilter.GaussianBlur(blur * S)))


# ---- 通用 UI 行（状态栏 / 曲目信息 / 进度 / 控制 / 音量） ----

def draw_topbar(d, th):
    bt_rune(d, 34, 44, 9, th["accent"])
    text(d, (48, 44), "蓝牙音箱", 25, th["title"], anchor="lm")
    # 右侧状态胶囊（宽度按 22px 汉字估算，和板上 FreeType 22 一致）
    label = SAMPLE["status"]
    pw = 48 + len(label) * 22 + 10
    x1 = 454
    box = (x1 - pw, 29, x1, 59)
    rrect(d, box, 15, fill=th["pill_bg"], outline=th.get("pill_line"))
    circle(d, box[0] + 18, 44, 4, fill=th["accent"])
    text(d, (box[0] + 30, 45), label, 16, th["pill_txt"], anchor="lm")
    text(d, (x1, 73), SAMPLE["addr"], 12, th["sub"], anchor="rm")


def draw_track_info(d, th):
    text(d, (240, th["song_y"]), SAMPLE["song"], th.get("song_px", 44), th["song"])
    text(d, (240, th["artist_y"]), SAMPLE["artist"], 23, th["artist"])


def draw_progress(d, th):
    x0, x1, y = th["prog"]
    h = 6
    rrect(d, (x0, y - h / 2, x1, y + h / 2), h / 2, fill=th["bar_track"])
    xf = x0 + (x1 - x0) * SAMPLE["prog"]
    rrect(d, (x0, y - h / 2, xf, y + h / 2), h / 2, fill=th["bar_fill"])
    if th.get("knob") == "white":
        circle(d, xf, y, 7, fill=(255, 255, 255, 255), outline=th["bar_fill"], width=3)
    elif th.get("knob") == "accent":
        circle(d, xf, y, 6, fill=th["bar_fill"])
    text(d, (x0, y + 17), SAMPLE["cur"], 14, th["sub"], anchor="lm")
    text(d, (x1, y + 17), SAMPLE["total"], 14, th["sub"], anchor="rm")


def draw_volume(d, th):
    x, y0, y1, w = th["vol"]
    rrect(d, (x, y0, x + w, y1), w / 2, fill=th["bar_track"])
    yf = y1 - (y1 - y0) * SAMPLE["vol_pct"]
    rrect(d, (x, yf, x + w, y1), w / 2, fill=th["bar_fill"])
    text(d, (x + w / 2, y1 + 15), str(SAMPLE["vol"]), 14, th["sub"])


def draw_controls(img, d, th):
    cy = th["ctl_cy"]
    pr, sr = th["play_r"], th["side_r"]
    px, sx = 240, th.get("side_dx", 82)
    # 播放键投影/发光
    if th.get("play_glow"):
        glow_disc(img, px, cy, pr + 6, th["accent"], alpha=th["play_glow"], blur=10)
    circle(d, px, cy, pr, fill=th["play_bg"])
    ico_pause(d, px, cy, pr * 0.72, th["play_ico"])
    for x0, fn in ((px - sx, ico_prev), (px + sx, ico_next)):
        if th.get("side_fill"):
            circle(d, x0, cy, sr, fill=th["side_fill"], outline=th.get("side_line"), width=2)
        fn(d, x0, cy, sr * 0.78, th["side_ico"])


# ---------------- 方案 A：深空玻璃 ----------------

def bg_A():
    a = base_np("#0A0F1A")
    radial(a, 130, 170, 430, "#0E3A54", 0.62)
    radial(a, 440, 560, 480, "#251A4D", 0.55)
    radial(a, 240, 240, 260, "#0E4A5E", 0.25)
    return add_noise(a)


def scheme_A():
    img = np_to_img(bg_A())
    d = ImageDraw.Draw(img)

    # 玻璃卡片 + 唱盘
    glow_disc(img, 240, 230, 120, "#22D3EE", alpha=46, blur=22)
    card = overlay()
    cd = ImageDraw.Draw(card)
    rrect(cd, (30, 88, 450, 384), 26, fill=(255, 255, 255, 20), outline=(255, 255, 255, 40), width=1)
    img.alpha_composite(card)
    tile = disc_tile(92, base="#131E33", groove="#31446B", label_a="#22D3EE",
                     label_b="#0E7490", rim="#5EEAFF", sheen=40, groove_px=2.6)
    paste_tile(img, tile, 240, 226)
    d = ImageDraw.Draw(img)
    # 盘下均衡器小条（静态装饰）
    heights = [12, 22, 30, 18, 26, 14, 9]
    bw, gap = 8, 8
    x0 = 240 - (len(heights) * bw + (len(heights) - 1) * gap) / 2
    for i, hgt in enumerate(heights):
        x = x0 + i * (bw + gap)
        rrect(d, (x, 350 - hgt, x + bw, 350), 3, fill=rgba("#22D3EE", 120))

    th = dict(
        accent="#22D3EE",
        title=(234, 242, 255, 255), song=(242, 247, 255, 255),
        artist=(159, 178, 204, 255), sub=(124, 141, 166, 255),
        pill_bg=(255, 255, 255, 26), pill_txt=(223, 246, 255, 255),
        bar_track=(255, 255, 255, 36), bar_fill=rgba("#22D3EE", 255),
        pill_line=(255, 255, 255, 46), knob="accent",
        play_bg=rgba("#22D3EE", 255), play_ico=(7, 12, 22, 255), play_glow=70,
        side_fill=(255, 255, 255, 26), side_ico=(236, 244, 255, 255),
        side_line=(255, 255, 255, 52),
        song_y=420, artist_y=463, prog=(56, 360, 502), vol=(424, 462, 556, 10),
        ctl_cy=582, play_r=41, side_r=29,
    )
    draw_topbar(d, th)
    draw_track_info(d, th)
    draw_progress(d, th)
    draw_volume(d, th)
    draw_controls(img, d, th)
    return img


# ---------------- 方案 B：落日唱盘 ----------------

def bg_B():
    a = grad_np([(0.0, "#2A0A54"), (0.35, "#7A1F7E"), (0.62, "#D64980"),
                 (0.82, "#FF7854"), (1.0, "#FFA45C")], 115)
    radial(a, 440, 70, 320, "#FFB56B", 0.38)
    radial(a, 50, 430, 340, "#FF6FA5", 0.30)
    return add_noise(a, 1.5)


def scheme_B():
    img = np_to_img(bg_B())
    d = ImageDraw.Draw(img)
    # 漂浮光斑
    bok = overlay()
    bd = ImageDraw.Draw(bok)
    for _ in range(16):
        x = random.uniform(10, 470)
        y = random.uniform(10, 250)
        r = random.uniform(1.5, 4.5)
        alpha = random.randint(20, 60)
        circle(bd, x, y, r, fill=(255, 255, 255, alpha))
    img.alpha_composite(bok.filter(ImageFilter.GaussianBlur(1 * S)))
    d = ImageDraw.Draw(img)

    glow_disc(img, 240, 226, 118, "#FF8A5C", alpha=60, blur=24)
    tile = disc_tile(100, base="#221530", groove="#3A2752", label_a="#FF8A5C",
                     label_b="#E0559B", sheen=36, groove_px=2.4)
    paste_tile(img, tile, 240, 226)
    d = ImageDraw.Draw(img)

    # 底部毛玻璃操作带
    band = overlay()
    bd2 = ImageDraw.Draw(band)
    rrect(bd2, (24, 462, 456, 618), 24, fill=(255, 255, 255, 52), outline=(255, 255, 255, 96), width=1)
    img.alpha_composite(band.filter(ImageFilter.GaussianBlur(1 * S)))
    d = ImageDraw.Draw(img)

    th = dict(
        accent="#FFC94A",
        title=(255, 255, 255, 255), song=(255, 255, 255, 255),
        artist=(255, 224, 238, 235), sub=(255, 226, 238, 200),
        pill_bg=(10, 6, 20, 110), pill_txt=(255, 255, 255, 235),
        bar_track=(255, 255, 255, 90), bar_fill=rgba("#FFC94A", 255),
        pill_line=(255, 255, 255, 80), knob="white",
        play_bg=(255, 255, 255, 255), play_ico=rgba("#D64980", 255),
        side_fill=(255, 255, 255, 64), side_ico=(255, 255, 255, 255),
        side_line=None,
        song_y=372, artist_y=418, prog=(48, 368, 498), vol=(424, 480, 548, 10),
        ctl_cy=576, play_r=38, side_r=27,
    )
    draw_topbar(d, th)
    draw_track_info(d, th)
    draw_progress(d, th)
    draw_volume(d, th)
    draw_controls(img, d, th)
    return img


# ---------------- 方案 C：云白极简（产品实拍圆窗） ----------------

def photo_tile(r):
    """bt.png 产品照裁成圆形贴片。
    实测：网罩深色像素 bbox x158-294 y287-448 → 球心约 x238, y368，直径~175。"""
    ph = Image.open(PHOTO_PATH).convert("RGB")
    side = 250
    cx, cy = 238, 368
    box = (cx - side // 2, cy - side // 2, cx + side // 2, cy + side // 2)
    ph = ph.crop(box).resize((int(2 * r * S), int(2 * r * S)), Image.LANCZOS)
    mask = Image.new("L", ph.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, ph.size[0] - 1, ph.size[1] - 1], fill=255)
    out = Image.new("RGBA", ph.size, (0, 0, 0, 0))
    out.paste(ph, (0, 0), mask)
    return out


def bg_C():
    a = base_np("#F5F7FA")
    # 顶部一点点冷色
    radial(a, 240, -80, 420, "#DCE9F5", 0.75)
    return add_noise(a, 0.8)


def scheme_C():
    img = np_to_img(bg_C())
    d = ImageDraw.Draw(img)
    # 装饰同心圆环
    for r, wd in ((150, 2), (188, 1)):
        circle(d, 240, 238, r, outline=rgba("#DFE6EF", 255), width=wd * 0.7)
    # 产品照投影
    sh = overlay()
    sd = ImageDraw.Draw(sh)
    sd.ellipse([140 * S, 322 * S, 340 * S, 356 * S], fill=(60, 80, 110, 70))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(10 * S)))
    # 产品实拍圆窗 + 白描边
    paste_tile(img, photo_tile(100), 240, 238)
    d = ImageDraw.Draw(img)
    circle(d, 240, 238, 100, outline=(255, 255, 255, 255), width=3)
    circle(d, 240, 238, 102, outline=rgba("#E2E8F0", 255), width=1)

    th = dict(
        accent="#0A84FF",
        title=(15, 23, 42, 255), song=(15, 23, 42, 255),
        artist=(100, 116, 139, 255), sub=(148, 163, 184, 255),
        pill_bg=(255, 255, 255, 255), pill_txt=(71, 85, 105, 255),
        bar_track=rgba("#E2E8F0", 255), bar_fill=rgba("#0A84FF", 255),
        pill_line=rgba("#E2E8F0", 255), knob="white",
        play_bg=rgba("#0A84FF", 255), play_ico=(255, 255, 255, 255), play_glow=46,
        side_fill=(255, 255, 255, 255), side_ico=(51, 65, 85, 255),
        side_line=rgba("#E2E8F0", 255),
        song_y=390, artist_y=434, prog=(56, 360, 502), vol=(424, 462, 556, 10),
        ctl_cy=582, play_r=41, side_r=29,
    )
    draw_topbar(d, th)
    draw_track_info(d, th)
    draw_progress(d, th)
    draw_volume(d, th)
    draw_controls(img, d, th)
    return img


# ======================= V2 五套潮流方案 =======================
# 2025-26 趋势：液态玻璃 / 极光 mesh 渐变 / 黏土拟物 / Y2K 铬金属 / Bento 网格

# ---- 通用辅助（V2 新增） ----

def glass_card(img, box, r, tint=(255, 255, 255, 0), tint_opa=0, line=(255, 255, 255, 0),
               top_hi=0, blur=2):
    """iOS 液态玻璃卡片：底色 tint + 1px 高光边 + 顶部 1px 高光，可选模糊底。
    2025 液态玻璃精髓：边框比内部亮、顶部有条细高光。"""
    card = overlay()
    cd = ImageDraw.Draw(card)
    rrect(cd, box, r, fill=(tint[0], tint[1], tint[2], tint_opa or tint[3]),
          outline=line, width=1)
    composite(img, card.filter(ImageFilter.GaussianBlur(blur * S)))
    if top_hi:
        hl = overlay()
        hd = ImageDraw.Draw(hl)
        rrect(hd, (box[0] + 6, box[1] + 2, box[2] - 6, box[1] + 4), 1,
              fill=(255, 255, 255, top_hi))
        composite(img, hl)
    return ImageDraw.Draw(img, "RGBA")


def clay_rrect(d, box, r, fill, hi_opa=110, sh_opa=70):
    """黏土拟物按钮：底色 + 底部厚投影 + 顶部内高光（三层近似 3D）"""
    x0, y0, x1, y1 = box
    sh = (0, 0, 0, sh_opa)
    hi = (255, 255, 255, hi_opa)
    rrect(d, (x0, y0 + 5, x1, y1 + 8), r, fill=sh)          # 落影
    rrect(d, (x0, y0, x1, y1), r, fill=fill)                 # 本体
    rrect(d, (x0 + 5, y0 + 4, x1 - 5, y0 + r), max(2, r // 2), fill=hi)  # 顶部高光


def chrome_rrect(img, box, r, dark="#8A94A6", light="#F4F7FB", mid="#C9D2DE"):
    """铬金属渐变圆角条（竖向）：暗-亮-暗 模拟镜面拉丝"""
    x0, y0, x1, y1 = [v * S for v in box]
    w, h = int(x1 - x0), int(y1 - y0)
    g = np.zeros((h, w, 3), np.float32)
    pos = np.array([0.0, 0.28, 0.5, 0.75, 1.0])
    cols = np.array([C(dark), C(light), C(mid), C(light), C(dark)], np.float32)
    t = np.linspace(0, 1, h)
    for i in range(3):
        g[:, :, i] = np.interp(t, pos, cols[:, i])[:, None]
    strip = Image.fromarray(np.clip(g, 0, 255).astype(np.uint8), "RGB").convert("RGBA")
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([1, 1, w - 2, h - 2], radius=max(1, int(r * S)), fill=255)
    img.paste(strip, (int(x0), int(y0)), mask)
    return ImageDraw.Draw(img, "RGBA")


# ---- D1 液态玻璃 Liquid Glass（iOS 26 风：浅银白 + 透明层叠） ----

def bg_D1():
    a = base_np("#E8ECF2")
    radial(a, 120, 90, 380, "#F7FAFD", 0.9)
    radial(a, 430, 300, 420, "#D3DCE8", 0.7)
    radial(a, 140, 560, 400, "#C9D6E6", 0.6)
    return add_noise(a, 0.8)


def scheme_D1():
    img = np_to_rgb(bg_D1())
    d = ImageDraw.Draw(img, "RGBA")
    # 大圆玻璃层（背景装饰：折射圆）
    for cx, cy, r, opa in ((395, 140, 120, 90), (60, 470, 140, 70)):
        gl = overlay()
        gd = ImageDraw.Draw(gl)
        circle(gd, cx, cy, r, fill=(255, 255, 255, opa), outline=(255, 255, 255, 160), width=2)
        composite(img, gl)
    d = ImageDraw.Draw(img, "RGBA")
    # 唱盘玻璃卡片
    d = glass_card(img, (36, 96, 444, 376), 34, tint=(255, 255, 255), tint_opa=110,
                   line=(255, 255, 255, 210), top_hi=150)
    tile = disc_tile(92, base="#2E3A4E", groove="#55677F", label_a="#FFFFFF",
                     label_b="#B9C6D6", rim="#FFFFFF", sheen=52, groove_px=2.6)
    paste_tile(img, tile, 240, 226)
    d = ImageDraw.Draw(img, "RGBA")

    th = dict(
        accent="#0A84FF",
        title=(38, 48, 66, 255), song=(24, 32, 48, 255),
        artist=(96, 108, 128, 255), sub=(140, 152, 170, 255),
        pill_bg=(255, 255, 255, 150), pill_txt=(48, 58, 76, 255),
        bar_track=(38, 48, 66, 46), bar_fill=rgba("#0A84FF", 255),
        pill_line=(255, 255, 255, 230), knob="white",
        play_bg=(28, 32, 44, 255), play_ico=(255, 255, 255, 255), play_glow=0,
        side_fill=(255, 255, 255, 190), side_ico=(48, 58, 76, 255),
        side_line=(255, 255, 255, 235),
        song_y=420, artist_y=463, prog=(56, 360, 502), vol=(424, 462, 556, 10),
        ctl_cy=582, play_r=41, side_r=29,
    )
    draw_topbar(d, th)
    draw_track_info(d, th)
    draw_progress(d, th)
    draw_volume(d, th)
    draw_controls(img, d, th)
    return img


# ---- D2 极光流彩 Aurora Mesh（多色柔光 mesh + 白玻璃卡 + 流光） ----

def bg_D2():
    a = base_np("#BFA8F0")
    radial(a, 90, 120, 420, "#7FD4FF", 0.85)
    radial(a, 420, 180, 380, "#FF9AD5", 0.8)
    radial(a, 240, 520, 460, "#8F7BFF", 0.85)
    radial(a, 460, 560, 300, "#7CFFD4", 0.55)
    return add_noise(a, 1.4)


def scheme_D2():
    img = np_to_rgb(bg_D2())
    # 磨砂白玻璃卡片（唱盘+均衡器共用底）
    d = glass_card(img, (28, 88, 452, 384), 32, tint=(255, 255, 255), tint_opa=72,
                   line=(255, 255, 255, 190), top_hi=140, blur=3)
    glow_disc(img, 240, 222, 104, "#FFFFFF", alpha=90, blur=26)
    tile = disc_tile(88, base="#241B3A", groove="#453463", label_a="#FF9AD5",
                     label_b="#7FD4FF", rim="#FFFFFF", sheen=44, groove_px=2.5)
    paste_tile(img, tile, 240, 222)
    d = ImageDraw.Draw(img, "RGBA")
    # 流光弧线（卡片下缘）
    arc = overlay()
    ad = ImageDraw.Draw(arc)
    ad.arc([40 * S, 330 * S, 440 * S, 470 * S], 200, 340, fill=(255, 255, 255, 190),
           width=int(2.4 * S))
    _t = arc.filter(ImageFilter.GaussianBlur(1.2 * S))
    composite(img, _t)
    d = ImageDraw.Draw(img, "RGBA")

    th = dict(
        accent="#6C5CE7",
        title=(255, 255, 255, 255), song=(255, 255, 255, 255),
        artist=(255, 245, 252, 235), sub=(255, 255, 255, 185),
        pill_bg=(255, 255, 255, 110), pill_txt=(70, 50, 110, 255),
        bar_track=(255, 255, 255, 120), bar_fill=(255, 255, 255, 255),
        pill_line=(255, 255, 255, 220), knob="white",
        play_bg=(255, 255, 255, 255), play_ico=rgba("#7C4DFF", 255), play_glow=80,
        side_fill=(255, 255, 255, 110), side_ico=(255, 255, 255, 255),
        side_line=(255, 255, 255, 200),
        song_y=420, artist_y=463, prog=(56, 360, 502), vol=(424, 462, 556, 10),
        ctl_cy=582, play_r=41, side_r=29,
    )
    draw_topbar(d, th)
    draw_track_info(d, th)
    draw_progress(d, th)
    draw_volume(d, th)
    draw_controls(img, d, th)
    return img


# ---- D3 软糖黏土 Claymorphism（粉彩 + 3D 软控件） ----

def bg_D3():
    a = base_np("#FDF0EC")
    radial(a, 110, 110, 400, "#FFE8F0", 0.9)
    radial(a, 430, 280, 400, "#E8F0FF", 0.85)
    radial(a, 240, 580, 420, "#FFF4E0", 0.9)
    return add_noise(a, 0.7)


def scheme_D3():
    img = np_to_rgb(bg_D3())
    d = ImageDraw.Draw(img, "RGBA")
    # 黏土大卡片
    clay_rrect(d, (34, 92, 446, 380), 36, (255, 255, 255, 255), hi_opa=140, sh_opa=60)
    # 黏土唱盘座（圆 + 唱盘）
    cd_sh = overlay()
    csd = ImageDraw.Draw(cd_sh)
    circle(csd, 240, 236, 100, fill=(210, 160, 150, 60))
    _t = cd_sh.filter(ImageFilter.GaussianBlur(4 * S))
    composite(img, _t)
    d = clay_circle(img, 240, 230, 100, (255, 224, 178, 255))
    tile = disc_tile(80, base="#5C4B8A", groove="#7A68A8", label_a="#FFB7C5",
                     label_b="#FFD8A8", rim="#FFFFFF", sheen=34, groove_px=2.4)
    paste_tile(img, tile, 240, 230)
    d = ImageDraw.Draw(img, "RGBA")

    th = dict(
        accent="#FF8FAB",
        title=(94, 78, 118, 255), song=(84, 66, 108, 255),
        artist=(150, 132, 168, 255), sub=(180, 164, 192, 255),
        pill_bg=(255, 255, 255, 255), pill_txt=(122, 96, 150, 255),
        bar_track=(94, 78, 118, 40), bar_fill=rgba("#FF8FAB", 255),
        pill_line=(255, 214, 224, 255), knob="accent",
        play_bg=rgba("#FF8FAB", 255), play_ico=(255, 255, 255, 255), play_glow=0,
        side_fill=(255, 255, 255, 255), side_ico=(150, 110, 160, 255),
        side_line=(255, 224, 232, 255),
        song_y=420, artist_y=463, prog=(56, 360, 502), vol=(424, 462, 556, 10),
        ctl_cy=582, play_r=41, side_r=29,
    )
    # 黏土风控制按钮单独画（draw_controls 是平面的）
    draw_topbar(d, th)
    draw_track_info(d, th)
    draw_progress(d, th)
    draw_volume(d, th)
    cy = 582
    # 播放键
    sh = overlay(); sd = ImageDraw.Draw(sh)
    circle(sd, 240, cy + 5, 41, fill=(210, 120, 140, 90))
    _t = sh.filter(ImageFilter.GaussianBlur(3 * S))
    composite(img, _t)
    d = clay_circle(img, 240, cy, 41, (255, 143, 171, 255))
    ico_pause(d, 240, cy, 41 * 0.62, (255, 255, 255, 255))
    for x0, fn in ((158, ico_prev), (322, ico_next)):
        sh2 = overlay(); sd2 = ImageDraw.Draw(sh2)
        circle(sd2, x0, cy + 4, 29, fill=(190, 160, 180, 80))
        _t = sh2.filter(ImageFilter.GaussianBlur(3 * S))
        composite(img, _t)
        d = clay_circle(img, x0, cy, 29, (255, 255, 255, 255))
        fn(d, x0, cy, 29 * 0.7, (170, 120, 150, 255))
    return img


def clay_circle(img, cx, cy, r, fill, hi_opa=130):
    """黏土圆：落影 + 本体 + 弧顶高光。img 兼容 RGB/RGBA 画布。返回新 Draw。"""
    d = ImageDraw.Draw(img, "RGBA")
    circle(d, cx, cy + 5, r, fill=(0, 0, 0, 55))
    circle(d, cx, cy, r, fill=fill)
    arc = overlay()
    ad = ImageDraw.Draw(arc)
    ad.arc([(cx - r + 7) * S, (cy - r + 5) * S, (cx + r - 7) * S, (cy + r - 3) * S],
           190, 350, fill=(255, 255, 255, hi_opa), width=int(3 * S))
    composite(img, arc)
    return ImageDraw.Draw(img, "RGBA")


# ---- D4 液态铬 Y2K Chrome（银镜面 + 冷蓝 + keynote 风） ----

def bg_D4():
    a = base_np("#0E1420")
    radial(a, 120, 140, 460, "#2A3A52", 0.7)
    radial(a, 420, 520, 440, "#3A4E6E", 0.6)
    radial(a, 240, 620, 320, "#4C6284", 0.45)
    return add_noise(a)


def scheme_D4():
    img = np_to_rgb(bg_D4())
    # 铬边唱盘卡片
    chrome_rrect(img, (30, 90, 450, 382), 30, dark="#5E6C82", light="#EAF1FA", mid="#9FADC2")
    d = ImageDraw.Draw(img, "RGBA")
    rrect(d, (36, 96, 444, 376), 26, fill=(10, 16, 28, 235))
    glow_disc(img, 240, 228, 112, "#8FB8E8", alpha=60, blur=22)
    # 铬环唱盘
    tile = disc_tile(90, base="#1A2434", groove="#3C4E68", label_a="#D9E6F5",
                     label_b="#7FA8D9", rim="#EAF1FA", sheen=58, groove_px=2.5)
    paste_tile(img, tile, 240, 228)
    d = ImageDraw.Draw(img, "RGBA")
    # 铬条：进度条底
    chrome_rrect(img, (52, 496, 364, 506), 5, dark="#5E6C82", light="#F4F8FD", mid="#AAB8CC")

    th = dict(
        accent="#7FB2E8",
        title=(233, 240, 250, 255), song=(244, 248, 253, 255),
        artist=(168, 182, 202, 255), sub=(130, 146, 168, 255),
        pill_bg=(233, 241, 250, 40), pill_txt=(226, 236, 248, 255),
        bar_track=(120, 136, 160, 70), bar_fill=rgba("#D9E6F5", 255),
        pill_line=(168, 184, 204, 160), knob="white",
        play_bg=rgba("#D9E6F5", 255), play_ico=(16, 24, 38, 255), play_glow=0,
        side_fill=(233, 241, 250, 36), side_ico=(226, 236, 248, 255),
        side_line=(168, 184, 204, 150),
        song_y=420, artist_y=463, prog=(56, 360, 502), vol=(424, 462, 556, 10),
        ctl_cy=582, play_r=41, side_r=29,
    )
    draw_topbar(d, th)
    draw_track_info(d, th)
    draw_progress(d, th)   # 进度画在铬条上（fill 覆盖）
    draw_volume(d, th)
    draw_controls(img, d, th)
    return img


# ---- D5 Bento 便当盒（分格卡片 + 中性底 + 单强调色） ----

def bg_D5():
    a = base_np("#F2F3F5")
    radial(a, 240, -60, 380, "#E4E9F0", 0.8)
    return add_noise(a, 0.6)


def scheme_D5():
    img = np_to_rgb(bg_D5())
    d = ImageDraw.Draw(img, "RGBA")

    GRID = "#FFFFFF"     # 卡片色
    LINE = (17, 24, 39, 26)
    # bento 格子：封面大格 / 歌名格 / 进度格 / 控制格 / 音量竖格
    boxes = {
        "cover":  (24, 84, 344, 356),
        "vol":    (356, 84, 456, 356),
        "song":   (24, 368, 456, 452),
        "prog":   (24, 464, 456, 528),
        "ctrl":   (24, 540, 456, 620),
    }
    for key, b in boxes.items():
        sh = overlay()
        sd = ImageDraw.Draw(sh)
        rrect(sd, (b[0], b[1] + 4, b[2], b[3] + 6), 26, fill=(15, 23, 42, 22))
        _t = sh.filter(ImageFilter.GaussianBlur(2 * S))
        composite(img, _t)
        rrect(d, b, 26, fill=GRID, outline=LINE, width=1)
    # 封面格：产品照片方形圆角（贴合产品）
    ph = Image.open(PHOTO_PATH).convert("RGB")
    side = 250
    cx, cy = 238, 368
    box = (cx - side // 2, cy - side // 2, cx + side // 2, cy + side // 2)
    ph = ph.crop(box).resize(((boxes["cover"][2] - boxes["cover"][0] - 16) * S,
                              (boxes["cover"][3] - boxes["cover"][1] - 16) * S), Image.LANCZOS)
    mask = Image.new("L", ph.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, ph.size[0] - 1, ph.size[1] - 1],
                                           radius=20 * S, fill=255)
    img.paste(ph, ((boxes["cover"][0] + 8) * S, (boxes["cover"][1] + 8) * S), mask)
    d = ImageDraw.Draw(img, "RGBA")
    # 音量格：竖条 + 数字
    vx = (boxes["vol"][0] + boxes["vol"][2]) // 2
    rrect(d, (vx - 6, boxes["vol"][1] + 26, vx + 6, boxes["vol"][3] - 44), 6,
          fill=(17, 24, 39, 22))
    vh = int((boxes["vol"][3] - 44) - (boxes["vol"][1] + 26))
    fh = int(vh * SAMPLE["vol_pct"])
    rrect(d, (vx - 6, boxes["vol"][3] - 44 - fh, vx + 6, boxes["vol"][3] - 44), 6,
          fill=rgba("#0A84FF", 255))
    text(d, (vx, boxes["vol"][3] - 26), str(SAMPLE["vol"]), 15, (100, 116, 139, 255))

    th = dict(
        accent="#0A84FF",
        title=(17, 24, 39, 255), song=(17, 24, 39, 255),
        artist=(100, 116, 139, 255), sub=(148, 163, 184, 255),
        pill_bg=(255, 255, 255, 255), pill_txt=(71, 85, 105, 255),
        bar_track=(17, 24, 39, 26), bar_fill=rgba("#0A84FF", 255),
        pill_line=(226, 232, 240, 255), knob="accent",
        play_bg=rgba("#0A84FF", 255), play_ico=(255, 255, 255, 255), play_glow=0,
        side_fill=(241, 245, 249, 255), side_ico=(51, 65, 85, 255),
        side_line=(226, 232, 240, 255),
        song_y=398, artist_y=436, prog=(44, 300, 500), vol=(424, 462, 556, 10),
        ctl_cy=580, play_r=34, side_r=25,
    )
    # 自绘 bento 各格内容（不用通用 draw_*，坐标不同）
    draw_topbar(d, th)
    # 歌名格
    text(d, (40, 402), SAMPLE["song"], 34, th["song"], anchor="lm")
    text(d, (40, 434), SAMPLE["artist"], 17, th["artist"], anchor="lm")
    # 状态点（歌名格右上）
    circle(d, 428, 396, 5, fill=rgba("#34C759", 255))
    text(d, (416, 396), SAMPLE["status"], 14, (100, 116, 139, 255), anchor="rm")
    # 进度格
    px0, px1, py = 44, 436, 488
    rrect(d, (px0, py - 4, px1, py + 4), 4, fill=(17, 24, 39, 26))
    xf = px0 + (px1 - px0) * SAMPLE["prog"]
    rrect(d, (px0, py - 4, xf, py + 4), 4, fill=rgba("#0A84FF", 255))
    circle(d, xf, py, 8, fill=(255, 255, 255, 255), outline=rgba("#0A84FF", 255), width=3)
    text(d, (px0, py + 20), SAMPLE["cur"], 14, th["sub"], anchor="lm")
    text(d, (px1, py + 20), SAMPLE["total"], 14, th["sub"], anchor="rm")
    # 控制格（横排三键，居中）
    cy = 580
    circle(d, 150, cy, 25, fill=(241, 245, 249, 255), outline=(226, 232, 240, 255), width=1)
    ico_prev(d, 150, cy, 25 * 0.72, (51, 65, 85, 255))
    circle(d, 240, cy, 34, fill=rgba("#0A84FF", 255))
    ico_pause(d, 240, cy, 34 * 0.56, (255, 255, 255, 255))
    circle(d, 330, cy, 25, fill=(241, 245, 249, 255), outline=(226, 232, 240, 255), width=1)
    ico_next(d, 330, cy, 25 * 0.72, (51, 65, 85, 255))
    return img

def save_1x(img, path):
    img.convert("RGB").resize((480, 640), Image.LANCZOS).save(path)


def compare_sheet(paths, out):
    """三张 360x480 并排 + 标题条"""
    cols = []
    for p, label in zip(paths, ["A · 深空玻璃", "B · 落日唱盘", "C · 云白极简"]):
        im = Image.open(p).resize((360, 480), Image.LANCZOS)
        col = Image.new("RGB", (360, 480 + 56), (24, 26, 32))
        col.paste(im, (0, 56))
        cd = ImageDraw.Draw(col)
        cd.text((180 * S // 2, 28), label, font=F(24), fill=(255, 255, 255, 255), anchor="mm")
        cols.append(col)
    gap = 20
    sheet = Image.new("RGB", (360 * 3 + gap * 4, 480 + 56 + gap * 2), (24, 26, 32))
    x = gap
    for col in cols:
        sheet.paste(col, (x, gap))
        x += 360 + gap
    sheet.save(out)


def gen_assets_D1():
    """D1 液态玻璃上屏素材：bg.png（静态全烘入）+ disc.png（184x184 透明唱盘）。"""
    img = np_to_rgb(bg_D1())
    # 背景装饰玻璃折射圆
    for cx, cy, r, opa in ((395, 140, 120, 90), (60, 470, 140, 70)):
        gl = overlay()
        gd = ImageDraw.Draw(gl)
        circle(gd, cx, cy, r, fill=(255, 255, 255, opa), outline=(255, 255, 255, 160), width=2)
        composite(img, gl)
    # 唱盘玻璃卡片（含顶高光）
    glass_card(img, (36, 96, 444, 376), 34, tint=(255, 255, 255), tint_opa=110,
               line=(255, 255, 255, 210), top_hi=150)

    out_bg = os.path.join(OUT_DIR, "bg_liquidglass.png")
    save_1x(img, out_bg)

    # 唱盘：深灰盘面 + 白标（与设计稿 disc_tile 参数一致）
    disc = disc_tile(92, base="#2E3A4E", groove="#55677F", label_a="#FFFFFF",
                     label_b="#B9C6D6", rim="#FFFFFF", sheen=52, groove_px=2.6)
    out_disc = os.path.join(OUT_DIR, "disc.png")
    disc.convert("RGBA").resize((184, 184), Image.LANCZOS).save(out_disc)
    print(out_bg)
    print(out_disc)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    schemes = {"A": scheme_A, "B": scheme_B, "C": scheme_C,
               "D1": scheme_D1, "D2": scheme_D2, "D3": scheme_D3,
               "D4": scheme_D4, "D5": scheme_D5}
    names = {"A": "midnight", "B": "sunset", "C": "cloud",
             "D1": "liquidglass", "D2": "aurora", "D3": "clay",
             "D4": "chrome", "D5": "bento"}

    if len(sys.argv) > 1 and sys.argv[1] == "--bg":
        key = sys.argv[2].upper()
        bgf = {"A": bg_A, "B": bg_B, "C": bg_C,
               "D1": bg_D1, "D2": bg_D2, "D3": bg_D3, "D4": bg_D4, "D5": bg_D5}[key]
        out = os.path.join(OUT_DIR, f"bg_{names[key]}.png")
        save_1x(np_to_img(bgf()), out)
        print(out)
        return

    if len(sys.argv) > 1 and sys.argv[1] == "--assets":
        key = sys.argv[2].upper() if len(sys.argv) > 2 else "A"
        if key == "A":
            gen_assets_A()
        elif key == "D1":
            gen_assets_D1()
        else:
            print("暂无该方案的 assets 生成器:", key)
            sys.exit(1)
        return

    if len(sys.argv) > 1 and sys.argv[1] == "--v2":
        paths = []
        for key in ("D1", "D2", "D3", "D4", "D5"):
            p = os.path.join(OUT_DIR, f"mockup2_{names[key]}.png")
            save_1x(schemes[key](), p)
            paths.append(p)
            print("V2 设计稿:", p)
        # 5 列对比图（每列 300x400 + 标题条）
        labels = ["D1 液态玻璃", "D2 极光流彩", "D3 软糖黏土", "D4 液态铬", "D5 Bento"]
        cols = []
        for p, label in zip(paths, labels):
            im = Image.open(p).resize((300, 400), Image.LANCZOS)
            col = Image.new("RGB", (300, 400 + 52), (28, 30, 36))
            col.paste(im, (0, 52))
            cd = ImageDraw.Draw(col)
            cd.text((150 * S // 2, 26), label, font=F(22), fill=(255, 255, 255), anchor="mm")
            cols.append(col)
        gap = 14
        sheet = Image.new("RGB", (300 * 5 + gap * 6, 400 + 52 + gap * 2), (28, 30, 36))
        x = gap
        for col in cols:
            sheet.paste(col, (x, gap))
            x += 300 + gap
        out = os.path.join(OUT_DIR, "compare2.png")
        sheet.save(out)
        print("V2 对比图:", out)
        return

    paths = []
    for key, fn in schemes.items():
        p = os.path.join(OUT_DIR, f"mockup_{names[key]}.png")
        save_1x(fn(), p)
        paths.append(p)
        print("设计稿:", p)
    sheet = os.path.join(OUT_DIR, "compare.png")
    compare_sheet(paths, sheet)
    print("对比图:", sheet)


if __name__ == "__main__":
    main()
