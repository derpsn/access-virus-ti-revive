#!/usr/bin/env python3
"""make_art.py - generate the TI Revive installer artwork (own design, no Access/Kemper material).

WiX UI bitmaps:
  dialog.bmp 493x312  Welcome/Finish pages: WiX draws its text on the white right part (x >= 164),
                      so the art lives in the left 164 px panel.
  banner.bmp 493x58   inner pages: WiX draws the title black on the left, so the art sits on the right.
Font: Noto Sans (SIL Open Font License). Run: python3 src/installer/art/make_art.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"
FONT_REG = "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"

BG_TOP = (14, 18, 27)      # near-black blue
BG_BOTTOM = (28, 36, 52)
ACCENT = (46, 196, 182)    # teal
WHITE = (255, 255, 255)


def gradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return img


def wave_layer(w, h, y0, amp, cycles, color, width, phase=0.0, saw=0.0):
    """A glowing oscillator trace: sine morphing into a saw towards the right."""
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    pts = []
    for x in range(w + 1):
        t = x / w
        a = 2 * math.pi * cycles * t + phase
        sine = math.sin(a)
        sawv = 2 * ((a / (2 * math.pi)) % 1) - 1
        m = saw * t
        v = (1 - m) * sine + m * sawv
        env = math.sin(math.pi * min(1, t * 1.1)) ** 0.6
        pts.append((x, y0 - amp * env * v))
    d.line(pts, fill=color + (255,), width=width, joint="curve")
    glow = layer.filter(ImageFilter.GaussianBlur(width * 2.5))
    return Image.alpha_composite(glow, layer)


def dialog():
    w, h, panel = 493, 312, 164
    img = Image.new("RGB", (w, h), WHITE)
    art = gradient(panel, h, BG_TOP, BG_BOTTOM).convert("RGBA")
    art = Image.alpha_composite(art, wave_layer(panel, h, 196, 30, 2.2, (120, 70, 90), 2, 1.2, 0.0))
    art = Image.alpha_composite(art, wave_layer(panel, h, 196, 38, 1.6, ACCENT, 3, 0.0, 0.85))
    d = ImageDraw.Draw(art)
    big = ImageFont.truetype(FONT_BOLD, 62)
    word = ImageFont.truetype(FONT_BOLD, 26)
    small = ImageFont.truetype(FONT_REG, 10)
    d.text((18, 36), "TI", font=big, fill=WHITE)
    d.text((20, 112), "Revive", font=word, fill=ACCENT)
    d.text((20, 254), "The Access Virus TI", font=small, fill=(170, 182, 200))
    d.text((20, 268), "on Windows 11, without", font=small, fill=(170, 182, 200))
    d.text((20, 282), "the old drivers", font=small, fill=(170, 182, 200))
    img.paste(art.convert("RGB"), (0, 0))
    # thin accent edge between art panel and text area
    ImageDraw.Draw(img).line([(panel - 1, 0), (panel - 1, h)], fill=ACCENT, width=2)
    img.save(os.path.join(HERE, "dialog.bmp"))


def banner():
    w, h, art_w = 493, 58, 150
    img = Image.new("RGB", (w, h), WHITE)
    art = gradient(art_w, h, BG_TOP, BG_BOTTOM).convert("RGBA")
    art = Image.alpha_composite(art, wave_layer(art_w, h, 29, 15, 2.0, ACCENT, 2, 0.0, 0.85))
    d = ImageDraw.Draw(art)
    f = ImageFont.truetype(FONT_BOLD, 15)
    tw = d.textlength("TI Revive", font=f)
    d.text((art_w - tw - 8, 3), "TI Revive", font=f, fill=WHITE)
    # alpha: 0 at the left edge of the art (white title area) -> opaque from 45 % of its width on
    mask = Image.new("L", (art_w, h))
    md = ImageDraw.Draw(mask)
    for x in range(art_w):
        md.line([(x, 0), (x, h)], fill=min(255, int(255 * x / (0.45 * art_w))))
    img.paste(art.convert("RGB"), (w - art_w, 0), mask)
    ImageDraw.Draw(img).line([(0, h - 1), (w, h - 1)], fill=ACCENT, width=1)
    img.save(os.path.join(HERE, "banner.bmp"))


if __name__ == "__main__":
    dialog()
    banner()
    print("wrote", os.path.join(HERE, "dialog.bmp"), os.path.join(HERE, "banner.bmp"))
