"""Tests for the search-engine and link-preview parts of scripts/site/build.py: title, description, canonical URL,
Open Graph, structured data, sitemap, image loading. Run: python3 -I -m unittest discover -s scripts/tests"""
import json
import os
import re
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "site"))
import build  # noqa: E402

SITE = "https://derpsn.github.io/access-virus-ti-revive/"


def release(tag, when):
    names = ["-setup.exe", ".msi", "-mac.zip", "-linux.tar.gz"]
    return {"tag_name": tag, "published_at": when + "T12:00:00Z", "body": "", "prerelease": False, "draft": False,
            "html_url": build.GH + "/releases/tag/" + tag,
            "assets": [{"name": "TIRevive-" + tag[1:] + n, "size": 1000000,
                        "browser_download_url": build.GH + "/releases/download/" + tag + "/x" + n} for n in names]}


class Seo(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = tempfile.mkdtemp()
        build.render(cls.out, [release("v0.8.34", "2026-10-25"), release("v0.8.33", "2026-10-24")], {})
        cls.pages = {}
        for slug in ("index", "changelog"):
            with open(os.path.join(cls.out, slug + ".html")) as f:
                cls.pages[slug] = f.read()

    def head(self, slug):
        return self.pages[slug].split("</head>")[0]

    def test_titles_name_the_synth_and_the_systems(self):
        t = re.search(r"<title>(.*?)</title>", self.head("index")).group(1)
        for word in ("TI Revive", "Virus TI", "Windows", "macOS", "Linux"):
            self.assertIn(word, t)
        self.assertNotEqual(t, re.search(r"<title>(.*?)</title>", self.head("changelog")).group(1))

    def test_descriptions_differ_and_fit_a_search_result(self):
        d = [re.search(r'<meta name="description" content="([^"]*)"', self.head(s)).group(1)
             for s in ("index", "changelog")]
        self.assertNotEqual(d[0], d[1])
        for x in d:
            self.assertLessEqual(len(x), 170, x)

    def test_canonical_and_open_graph(self):
        for slug, url in (("index", SITE), ("changelog", SITE + "changelog.html")):
            h = self.head(slug)
            self.assertIn(f'<link rel="canonical" href="{url}">', h)
            self.assertIn(f'<meta property="og:url" content="{url}">', h)
            self.assertIn(f'<meta property="og:image" content="{SITE}social.png">', h)
            self.assertIn('<meta name="twitter:card" content="summary_large_image">', h)
        self.assertTrue(os.path.isfile(os.path.join(self.out, "social.png")))

    def test_favicon(self):
        self.assertIn('<link rel="icon" href="favicon.svg"', self.head("index"))
        self.assertTrue(os.path.isfile(os.path.join(self.out, "favicon.svg")))

    def test_structured_data_on_the_landing_page(self):
        m = re.search(r'<script type="application/ld\+json">(.*?)</script>', self.head("index"), re.S)
        data = json.loads(m.group(1))
        self.assertEqual(data["@type"], "SoftwareApplication")
        self.assertEqual(data["softwareVersion"], "0.8.34")
        self.assertEqual(data["offers"]["price"], "0")
        self.assertNotIn("ld+json", self.head("changelog"))

    def test_sitemap_lists_both_pages(self):
        with open(os.path.join(self.out, "sitemap.xml")) as f:
            s = f.read()
        self.assertIn(f"<loc>{SITE}</loc>", s)
        self.assertIn(f"<loc>{SITE}changelog.html</loc>", s)
        self.assertIn("<lastmod>2026-10-25</lastmod>", s)

    def test_only_the_first_screenshot_loads_eagerly(self):
        imgs = re.findall(r"<img [^>]*>", self.pages["index"])
        self.assertNotIn('loading="lazy"', imgs[0])
        for i in imgs[1:]:
            self.assertIn('loading="lazy"', i)

    def test_h1_is_the_first_heading(self):
        body = self.pages["index"].split("<main>")[1]
        self.assertEqual(re.search(r"<h[1-6]", body).group(0), "<h1")


if __name__ == "__main__":
    unittest.main()
