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
        self.assertIn("scripts/", text)
        self.assertNotIn("themes/", text)  # only the folders this kit has
        self.assertNotIn("\u2014", text)
        self.assertNotIn("--", text)

    def test_empty_stage_is_refused(self):
        os.makedirs(os.path.join(self.stage, "cache"))
        write(os.path.join(self.stage, "cache", "junk"))
        with self.assertRaises(ValueError):
            sdkit.write_kit(self.stage, self.out)
        self.assertFalse(os.path.exists(self.out))


class MainTest(unittest.TestCase):
    """sdkit.main with install_cyd pointed at a temp repo and a temp Downloads folder."""

    def setUp(self):
        import install_cyd
        self.ic = install_cyd
        self.saved = (install_cyd.ROOT, install_cyd.DOWNLOADS, install_cyd.STAGE)
        self.tmp = tempfile.mkdtemp()
        install_cyd.ROOT = os.path.join(self.tmp, "repo")
        install_cyd.DOWNLOADS = os.path.join(self.tmp, "Downloads")
        write(os.path.join(install_cyd.ROOT, "scripts", "hello.axs"))
        fn = install_cyd.PACKS[0][1]
        os.makedirs(install_cyd.DOWNLOADS)
        with zipfile.ZipFile(os.path.join(install_cyd.DOWNLOADS, fn), "w") as z:
            z.writestr("ans/04_main.ans", "screen")
        self.out = os.path.join(self.tmp, "kit.zip")

    def tearDown(self):
        self.ic.ROOT, self.ic.DOWNLOADS, self.ic.STAGE = self.saved
        shutil.rmtree(self.tmp)

    def names(self):
        with zipfile.ZipFile(self.out) as z:
            return sorted(z.namelist())

    def run_main(self, *args):
        import contextlib
        import io
        with contextlib.redirect_stdout(io.StringIO()):
            return sdkit.main([self.out, *args])

    def test_default_kit_holds_only_the_repo_files(self):
        self.assertEqual(self.run_main(), 0)
        self.assertEqual(self.names(), ["README.txt", "scripts/hello.axs"])

    def test_with_themes_adds_the_downloaded_packs(self):
        self.assertEqual(self.run_main("--with-themes"), 0)
        self.assertIn("themes/%s/MAIN.ANS" % self.ic.PACKS[0][0], self.names())


if __name__ == "__main__":
    unittest.main()
