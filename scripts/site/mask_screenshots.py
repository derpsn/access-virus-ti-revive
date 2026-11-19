"""Cover Access' logos in the README/site screenshots and put the TI Revive name where the top-left logo was.

The plug-in and the Control Center draw Access' look at runtime from the user's own installer, logos included; only
our screenshots of it are published, and those carry our name instead (branding cleanup, user 2026-11-19).
Run once on a fresh capture, from the repository root:
    python3 -I scripts/site/mask_screenshots.py [file ...]      (default: every file in JOBS, in place)
A new screenshot needs its boxes in JOBS. The plug-in's logos sit at the same place in every page of the editor:
at 100 % size, top logo (24, 10, 163, 38) and the bottom bar's "VIRUS|TI" (66, 482, 163, 496); other sizes scale.
Tests: scripts/tests/test_mask_screenshots.py."""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONTS = ["/usr/share/fonts/truetype/lato/Lato-Black.ttf",  # chosen by the user 2026-11-19
         "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"]
ORANGE = (229, 64, 46)  # #e5402e, the site's accent and favicon


def plug(s, top, bottom):
    """a plug-in page: the top logo becomes the name, the bottom bar's lettering is covered with its own wood"""
    return [(top, ("close", 15, "logo")), (bottom, ("wood", round(490 * s)))]


# box -> how: close = dark logo on light ground, open = light logo on dark ground, tile = brushed metal (repeat the
# strip right of the box), wood = copy the bar's wood from k pixels further right, flat = the panel's colour;
# "logo" = draw the name into the cleaned box
JOBS = {
    "docs/images/plugin-edit-menu.png": plug(1, (24, 10, 163, 38), (66, 482, 163, 496)),
    "docs/images/plugin-filter-page.png": plug(1, (24, 10, 163, 38), (66, 482, 163, 496)),
    "docs/images/plugin-help-inactive.png": plug(1, (24, 10, 163, 38), (66, 482, 163, 496)),
    "docs/images/plugin-help-menu.png": plug(1, (24, 10, 163, 38), (66, 482, 163, 496)),
    "docs/images/plugin-library-drop.png": plug(1, (24, 10, 163, 38), (66, 482, 163, 496)),
    "docs/images/plugin-menus.png": plug(1, (24, 10, 163, 38), (66, 482, 163, 496)),
    "docs/images/plugin-section-lock.png": plug(1, (24, 10, 163, 38), (66, 482, 163, 496)),
    # the Easy page also has the logos on its big knob's ring (the tooltip's edge stays out of the box)
    "docs/images/plugin-help-mode.png": plug(1.49, (38, 15, 244, 57), (100, 723, 243, 744)) + [
        ((860, 125, 1016, 165), ("close", 21)), ((898, 380, 1020, 431), ("close", 21))],
    "site/src/plugin-filter.png": plug(1.43, (34, 14, 231, 54), (92, 680, 229, 700)),
    # the Control Center: logo on brushed metal, and Access' product photo of the Virus
    "docs/images/control-center.png": [((8, 8, 148, 39), ("tile", 18, "logo")), ((518, 196, 646, 258), ("flat", 0))],
    "docs/images/control-center-nerd-mode.png": [((6, 40, 185, 77), ("tile", 20, "logo"))],
}


def font(size):
    for path in FONTS:
        if os.path.isfile(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size)


def apply(im, box, how):
    kind, k = how[:2]
    x0, y0, x1, y1 = box
    px = sorted(im.crop(box).get_flattened_data(), key=sum)
    ink = px[len(px) // 50] if kind == "close" else px[-len(px) // 50]  # the old logo's own colour
    if kind == "tile":
        strip = im.crop((x1 + 1, y0, x1 + 1 + k, y1))
        for x in range(x0, x1, k):
            im.paste(strip.crop((0, 0, min(k, x1 - x), y1 - y0)), (x, y0))
    elif kind == "wood":
        im.paste(im.crop((x0 + k, y0, x1 + k, y1)), (x0, y0))
    elif kind == "flat":
        ring = sorted((im.getpixel((x, y)) for x in range(x0 - 3, x1 + 3) for y in (y0 - 3, y1 + 2)), key=sum)
        im.paste(ring[len(ring) // 2], box)
    else:  # close/open: a morphological filter wider than the strokes takes them out, edges stay
        big = im.crop((x0 - k, y0 - k, x1 + k, y1 + k))
        a, b = ImageFilter.MaxFilter(k), ImageFilter.MinFilter(k)
        if kind == "open":
            a, b = b, a
        clean = big.filter(a).filter(b).filter(ImageFilter.GaussianBlur(1.2))
        im.paste(clean.crop((k, k, k + x1 - x0, k + y1 - y0)), (x0, y0))
    if len(how) > 2:
        draw_logo(im, box, ink)


def draw_logo(im, box, ink):
    """'TI Revive': drawn 4x and scaled down (soft edges like the skin's art), partly transparent so the surface
    shows through, with a faint edge under it like the original logo's"""
    x0, y0, x1, y1 = box
    S = 4
    w, h = (x1 - x0) * S, (y1 - y0) * S
    size = round(h * 0.95)
    while True:
        f = font(size)
        l, t, r, b = f.getbbox("TI Revive")
        if (r - l <= w - 6 * S and b - t <= h * 0.78) or size < 10:
            break
        size -= 2
    ox, oy = 3 * S - l, (h - (b - t)) / 2 - t
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    edge = (0, 0, 0, 70) if sum(ink[:3]) > 380 else (255, 255, 255, 110)  # light logo on dark metal: a dark edge
    for dy, ti, rv in ((S, edge, edge), (0, ORANGE + (205,), tuple(ink[:3]) + (215,))):
        d.text((ox, oy + dy), "TI", font=f, fill=ti)
        d.text((ox + f.getlength("TI "), oy + dy), "Revive", font=f, fill=rv)
    layer = layer.resize((x1 - x0, y1 - y0), Image.LANCZOS)
    im.paste(layer, (x0, y0), layer)


def main(paths):
    for path in paths or list(JOBS):
        src = Image.open(path)
        im = src.convert("RGB")
        for box, how in JOBS[path]:
            apply(im, box, how)
        if src.mode == "P":  # keep a 256-colour PNG small (the site's pictures); median cut lost the orange
            try:
                im = im.quantize(256, method=Image.Quantize.LIBIMAGEQUANT)
            except ValueError:  # a Pillow built without libimagequant
                im = im.quantize(256, method=Image.Quantize.FASTOCTREE)
        im.save(path, optimize=True)
        print("masked", path)


if __name__ == "__main__":
    main(sys.argv[1:])
