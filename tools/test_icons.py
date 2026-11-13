"""Tests for tools/icons.py. Run: python -I tools/test_icons.py"""
import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icons  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class IconsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        with open(os.path.join(ROOT, "installer", "icons.json"), encoding="utf-8") as fh:
            self.grids = json.load(fh)
        self.features, self.presets = icons.load_lists(ROOT)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_every_id_is_drawn_and_written(self):
        written = icons.write(self.grids, self.tmp, self.features, self.presets)
        for i in icons.required_ids(self.features, self.presets):
            self.assertIn(i, written)
            self.assertTrue(os.path.isfile(os.path.join(self.tmp, i + ".svg")), i)

    def test_svg_is_one_rect_per_run(self):
        grid = ["." * 16] * 16
        grid = list(grid)
        grid[0] = "aaa..cc........w"
        svg = icons.svg(grid)
        self.assertIn('<rect x="0" y="0" width="3" height="1" fill="#f5a623"/>', svg)
        self.assertIn('<rect x="5" y="0" width="2" height="1" fill="#55ffff"/>', svg)
        self.assertIn('<rect x="15" y="0" width="1" height="1" fill="#ffffff"/>', svg)
        self.assertEqual(svg.count("<rect"), 3)
        self.assertIn('viewBox="0 0 16 16"', svg)
        self.assertIn('shape-rendering="crispEdges"', svg)

    def test_wrong_size_fails(self):
        bad = dict(self.grids)
        bad["lord"] = bad["lord"][:15]
        with self.assertRaisesRegex(ValueError, "lord: 15 rows"):
            icons.check(bad, self.features, self.presets)
        bad["lord"] = [r for r in self.grids["lord"]]
        bad["lord"][3] = bad["lord"][3] + "."
        with self.assertRaisesRegex(ValueError, "lord: row 4 is 17 wide"):
            icons.check(bad, self.features, self.presets)

    def test_unknown_palette_letter_fails(self):
        bad = dict(self.grids)
        bad["sd"] = list(self.grids["sd"])
        bad["sd"][0] = "r" + bad["sd"][0][1:]
        with self.assertRaisesRegex(ValueError, "sd: row 1 has 'r'"):
            icons.check(bad, self.features, self.presets)

    def test_missing_id_fails(self):
        bad = dict(self.grids)
        del bad["no-doors"]
        with self.assertRaisesRegex(ValueError, "no icon for: no-doors"):
            icons.check(bad, self.features, self.presets)
        bad = dict(self.grids)
        del bad["transfers"]
        with self.assertRaisesRegex(ValueError, "no icon for: transfers"):
            icons.check(bad, self.features, self.presets)

    def test_cli_fails_with_the_reason(self):
        bad = dict(self.grids)
        del bad["step2"]
        path = os.path.join(self.tmp, "icons.json")
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(bad, fh)
        self.assertEqual(icons.main(["--icons", path, "--out", os.path.join(self.tmp, "out"), "--quiet"]), 1)

    def test_the_site_icons_are_current(self):
        """site/icons/ is committed: it must be what tools/icons.py writes from installer/icons.json."""
        icons.write(self.grids, self.tmp, self.features, self.presets)
        site = os.path.join(ROOT, "site", "icons")
        want = sorted(os.listdir(self.tmp))
        self.assertEqual(sorted(os.listdir(site)), want)
        for name in want:
            with open(os.path.join(self.tmp, name), encoding="utf-8") as a, open(os.path.join(site, name), encoding="utf-8") as b:
                self.assertEqual(a.read(), b.read(), name + " is stale: run python -I tools/icons.py")


if __name__ == "__main__":
    unittest.main()
