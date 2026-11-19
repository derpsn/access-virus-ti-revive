"""Tests for scripts/site/mask_screenshots.py: Access' logos in the README/site screenshots are covered (and the top
one replaced by the TI Revive name); nothing outside a logo's box changes.
Run: python3 -I -m unittest discover -s scripts/tests"""
import importlib.util
import os
import unittest

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("mask", os.path.join(HERE, "..", "site", "mask_screenshots.py"))
mask = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mask)

BOX = (40, 20, 120, 44)


def panel(ground, stroke):
    """a 200 x 80 panel with a thin 'logo' in the box"""
    im = Image.new("RGB", (200, 80), ground)
    d = ImageDraw.Draw(im)
    for x in range(BOX[0] + 4, BOX[2] - 4, 9):
        d.rectangle((x, BOX[1] + 4, x + 3, BOX[3] - 4), fill=stroke)
    return im


def outside_unchanged(test, a, b):
    for y in range(a.height):
        for x in range(a.width):
            inside = BOX[0] <= x < BOX[2] and BOX[1] <= y < BOX[3]
            if not inside:
                test.assertEqual(a.getpixel((x, y)), b.getpixel((x, y)), (x, y))


class Mask(unittest.TestCase):
    def test_close_removes_a_dark_logo_on_light_ground(self):
        im = panel((190, 190, 192), (90, 90, 90))
        before = im.copy()
        mask.apply(im, BOX, ("close", 15))
        darkest = min(sum(p) for p in im.crop(BOX).get_flattened_data())
        self.assertGreater(darkest, 3 * 170)
        outside_unchanged(self, before, im)

    def test_open_removes_a_light_logo_on_dark_ground(self):
        im = panel((60, 62, 66), (210, 210, 210))
        mask.apply(im, BOX, ("open", 15))
        self.assertLess(max(sum(p) for p in im.crop(BOX).get_flattened_data()), 3 * 90)

    def test_tile_repeats_the_strip_right_of_the_box(self):
        im = panel((60, 62, 66), (210, 210, 210))
        ImageDraw.Draw(im).line((BOX[2] + 1, 30, BOX[2] + 18, 30), fill=(1, 2, 3))  # a groove right of the logo
        before = im.copy()
        mask.apply(im, BOX, ("tile", 18))
        self.assertEqual(im.getpixel((BOX[0] + 5, 30)), (1, 2, 3))
        self.assertEqual(im.getpixel((BOX[0] + 5, 24)), (60, 62, 66))
        outside_unchanged(self, before, im)

    def test_wood_copies_from_further_right(self):
        im = Image.new("RGB", (200, 80), (90, 60, 50))
        im.putpixel((BOX[0] + 100, BOX[1]), (7, 8, 9))
        mask.apply(im, BOX, ("wood", 100))
        self.assertEqual(im.getpixel(BOX[:2]), (7, 8, 9))

    def test_flat_takes_the_panel_colour(self):
        im = panel((80, 85, 90), (200, 200, 200))
        mask.apply(im, BOX, ("flat", 0))
        self.assertEqual(set(im.crop(BOX).get_flattened_data()), {(80, 85, 90)})

    def test_logo_draws_the_name_in_orange_and_the_old_logos_grey(self):
        im = panel((190, 190, 192), (90, 90, 90))
        mask.apply(im, BOX, ("close", 15, "logo"))
        px = list(im.crop(BOX).get_flattened_data())
        self.assertTrue(any(r > g + 60 and r > b + 60 for r, g, b in px))  # "TI" in the accent colour
        self.assertTrue(any(abs(r - g) < 8 and r < 150 for r, g, b in px))  # "Revive" in the old logo's grey

    def test_a_palette_png_stays_a_palette_png(self):  # the site's pictures are 256-colour PNGs (small to load)
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "shot.png")
            panel((190, 190, 192), (90, 90, 90)).convert("P").save(path)
            mask.JOBS[path] = [(BOX, ("close", 15, "logo"))]
            try:
                mask.main([path])
            finally:
                del mask.JOBS[path]
            out = Image.open(path)
            self.assertEqual(out.mode, "P")
            px = list(out.convert("RGB").get_flattened_data())  # the few orange pixels keep their colour
            self.assertTrue(any(r > 180 and g < 110 and b < 100 for r, g, b in px))

    def test_every_job_names_an_existing_screenshot(self):
        root = os.path.join(HERE, "..", "..")
        for path in mask.JOBS:
            self.assertTrue(os.path.isfile(os.path.join(root, path)), path)


if __name__ == "__main__":
    unittest.main()
