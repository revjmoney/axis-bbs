"""Tests for tools/sdkit.py. Run: python -I tools/test_sdkit.py"""
import os
import shutil
import sys
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import sdkit  # noqa: E402


def write(path, data=b"x"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(data)


class KitTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.stage = os.path.join(self.tmp, "sdstage")
        self.out = os.path.join(self.tmp, "sdcard-kit.zip")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def names(self):
        with zipfile.ZipFile(self.out) as z:
            return sorted(z.namelist())

    def test_staged_folders_land_where_the_card_wants_them(self):
        write(os.path.join(self.stage, "themes", "crt", "MAIN.ANS"))
        write(os.path.join(self.stage, "lord", "lordtxt.lrd"))
        write(os.path.join(self.stage, "scripts", "hello.axs"))
        write(os.path.join(self.stage, "cache", "lord407.zip"))
        write(os.path.join(self.stage, "cache", "lord407", "LORD.EXE"))
        sdkit.write_kit(self.stage, self.out)
        self.assertEqual(self.names(), ["README.txt", "doors/lord/lordtxt.lrd", "scripts/hello.axs",
                                        "themes/crt/MAIN.ANS"])

    def test_readme_has_no_em_dash_or_double_hyphen(self):
        write(os.path.join(self.stage, "scripts", "hello.axs"))
        sdkit.write_kit(self.stage, self.out)
        with zipfile.ZipFile(self.out) as z:
            text = z.read("README.txt").decode("utf-8")
        self.assertIn("AXIS Micro BBS", text)
        self.assertNotIn("—", text)
        self.assertNotIn("--", text)

    def test_empty_stage_is_refused(self):
        os.makedirs(os.path.join(self.stage, "cache"))
        write(os.path.join(self.stage, "cache", "junk"))
        with self.assertRaises(ValueError):
            sdkit.write_kit(self.stage, self.out)
        self.assertFalse(os.path.exists(self.out))


if __name__ == "__main__":
    unittest.main()
