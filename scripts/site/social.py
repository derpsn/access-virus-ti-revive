#!/usr/bin/env python3
# social.py - the link preview image of the website (site/src/social.png, 1200x630, Open Graph): the site's own
# colours and text, no Access screenshot (rule 1: screenshots only next to the "not affiliated" notice, never as
# branding). Run again after changing the text; needs Pillow and the Liberation Sans fonts.
#
#   python3 scripts/site/social.py
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONTS = "/usr/share/fonts/truetype/liberation/"
BG, TEXT, SOFT, ACCENT = "#0d0e10", "#ebe8e1", "#8e8b84", "#e5402e" # site/src/style.css, dark theme


def main():
    img = Image.new("RGB", (1200, 630), BG)
    d = ImageDraw.Draw(img)
    bold = lambda n: ImageFont.truetype(FONTS + "LiberationSans-Bold.ttf", n)
    regular = lambda n: ImageFont.truetype(FONTS + "LiberationSans-Regular.ttf", n)
    d.rectangle((80, 150, 92, 330), fill=ACCENT)
    d.text((124, 140), "TI Revive", font=bold(112), fill=TEXT)
    d.text((128, 272), "The Access Virus TI on Windows 11, macOS and Linux,", font=regular(40), fill=TEXT)
    d.text((128, 322), "without the old drivers.", font=regular(40), fill=TEXT)
    d.text((128, 420), "Free and open source. VST3 and Audio Unit.", font=regular(32), fill=SOFT)
    d.text((128, 520), "Not affiliated with Access Music or Kemper.", font=regular(24), fill=SOFT)
    img.save(os.path.join(ROOT, "site", "src", "social.png"), optimize=True)


if __name__ == "__main__":
    main()
