"""Tests for tools/buildopts.py. Run: python -I -m unittest tools/test_buildopts.py"""
import json
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import buildopts  # noqa: E402

FEATURES, PRESETS = buildopts.load(ROOT)
SELECTABLE = {f["id"] for f in FEATURES if not f.get("reserved")}


class BuildOptsTest(unittest.TestCase):
    def test_every_preset_resolves(self):
        for p in PRESETS:
            with self.subTest(preset=p["id"]):
                self.assertEqual(buildopts.resolve(set(p["off"])), set(p["off"]))

    def test_expected_presets_exist(self):
        ids = {p["id"] for p in PRESETS}
        self.assertEqual(ids, {"everything", "no-doors", "no-files", "no-radio", "just-bbs", "no-sd"})

    def test_just_bbs_turns_off_every_selectable_id(self):
        jb = next(p for p in PRESETS if p["id"] == "just-bbs")
        self.assertEqual(set(jb["off"]), SELECTABLE)

    def test_no_sd_turns_off_exactly_the_sd_features(self):
        ns = next(p for p in PRESETS if p["id"] == "no-sd")
        want = {f["id"] for f in FEATURES if f.get("needs_sd") and not f.get("reserved")}
        self.assertEqual(set(ns["off"]), want)
        self.assertEqual(want, {"files", "ansiview", "transfers", "lord", "scripts", "nets"})

    def test_nets_is_selectable_and_in_the_off_presets(self):
        self.assertIn("nets", SELECTABLE)
        for pid in ("just-bbs", "no-sd"):
            p = next(p for p in PRESETS if p["id"] == pid)
            self.assertIn("nets", p["off"], pid)
        ev = next(p for p in PRESETS if p["id"] == "everything")
        self.assertEqual(ev["off"], [])
        self.assertEqual(buildopts.build_flags({"nets"}), ["-DAXIS_NETS=0"])

    def test_every_feature_has_needs_sd_flag(self):
        for f in FEATURES:
            self.assertIn(f["needs_sd"], (True, False), f["id"])

    def test_features_h_needs_sd_matches_the_list(self):
        # AXIS_NEEDS_SD in src/bbs/features.h (the SD card notice) names exactly the needs_sd features' defines
        with open(os.path.join(ROOT, "src", "bbs", "features.h"), encoding="utf-8") as fh:
            line = next(l for l in fh if l.startswith("#define AXIS_NEEDS_SD "))
        named = set(re.findall(r"AXIS_[A-Z]+", line.split(" ", 2)[2]))
        want = {f["define"] for f in FEATURES if f.get("needs_sd") and not f.get("reserved")}
        self.assertEqual(named, want)

    def test_files_off_alone_names_dependents(self):
        with self.assertRaises(ValueError) as cm:
            buildopts.resolve({"files"})
        self.assertIn("ansiview", str(cm.exception))
        self.assertIn("transfers", str(cm.exception))

    def test_reserved_and_unknown_ids_raise(self):
        for bad in ("gpio", "nope", "AXIS_LORD", "a;b"):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    buildopts.resolve({bad})

    def test_fingerprint_stable_and_release_sensitive(self):
        a = buildopts.fingerprint({"lord", "scripts"}, "v1.0")
        b = buildopts.fingerprint({"scripts", "lord"}, "v1.0")
        self.assertEqual(a, b)
        self.assertEqual(len(a), 12)
        self.assertNotEqual(a, buildopts.fingerprint({"lord", "scripts"}, "v1.1"))
        self.assertNotEqual(a, buildopts.fingerprint({"lord"}, "v1.0"))

    def test_fingerprint_fixture(self):
        path = os.path.join(ROOT, "installer", "test-fixtures", "fingerprints.json")
        with open(path, encoding="utf-8") as fh:
            cases = json.load(fh)
        self.assertGreaterEqual(len(cases), 3)
        for c in cases:
            with self.subTest(case=c):
                self.assertEqual(buildopts.fingerprint(set(c["off"]), c["release"]), c["fingerprint"])

    def test_build_flags(self):
        self.assertEqual(buildopts.build_flags({"lord"}), ["-DAXIS_LORD=0"])
        self.assertEqual(buildopts.build_flags(set()), [])
        self.assertEqual(buildopts.build_flags({"scripts", "lord"}), ["-DAXIS_LORD=0", "-DAXIS_SCRIPTS=0"])

    def test_everything_gives_no_flags(self):
        ev = next(p for p in PRESETS if p["id"] == "everything")
        self.assertEqual(buildopts.build_flags(set(ev["off"])), [])

    def test_cli(self):
        self.assertEqual(buildopts.main(["flags", "--preset", "no-doors"]), 0)
        self.assertEqual(buildopts.main(["check", "--off", "files"]), 1)
        self.assertEqual(buildopts.main(["check", "--off", "lord"]), 0)
        self.assertEqual(buildopts.main(["flags", "--preset", "nope"]), 1)


if __name__ == "__main__":
    unittest.main()
