"""Tests for tools/buildcost.py. Run: python -I tools/test_buildcost.py"""
import hashlib
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import buildcost  # noqa: E402

# Cut from a real `xtensa-esp32-elf-size -A firmware.elf` (env cyd, column padding shortened).
SIZE_A = """.pio/build/cyd/firmware.elf  :
section                    size         addr
.rtc.text                     0   1074528256
.rtc_noinit                  88   1342177280
.iram0.vectors             1027   1074266112
.iram0.text               60399   1074267140
.dram0.data               14288   1073414144
.noinit                       0   1073428432
.dram0.bss                34080   1073428432
.flash.appdesc              256   1061158944
.flash.rodata            479340   1061159200
.flash.rodata_noload       8173   1061638540
.flash.text             1656763   1074593824
Total                   2254414
"""

# Cut from the real linker map (axis-bbs.map) of the same build: 0x37d0 = 14288, 0x8520 = 34080.
MAP = """ *fill*         0x000000004008dea2        0x0
.dram0.data     0x000000003ffb0000     0x37d0
                0x000000003ffb0000                _data_start = ABSOLUTE (.)
 .dram0.data.foo
                0x000000003ffb0010       0x40 foo.o
.dram0.bss      0x000000003ffb37d0     0x8520
                0x000000003ffb37d0                . = ALIGN (0x8)
.flash.rodata_noload
                0x000000003f47518c     0x1fed
"""

PRESETS = [
    {"id": "everything", "label": "Everything", "off": []},
    {"id": "no-doors", "label": "No doors", "off": ["lord", "scripts", "dosprompt"]},
    {"id": "no-files", "label": "No file areas", "off": ["files", "ansiview", "transfers"]},
]
PEAKS = {
    "lord": {"peak_kb": 20, "what": "LORD"},
    "ansiview": {"peak_kb": 44, "what": "ZIP window"},
    "transfers": {"peak_kb": 8, "what": "XMODEM and ZMODEM buffers"},
    "web": {"peak_kb": 48, "what": "TLS"},
}
PARTS = """# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
otadata,  data, ota,     0xe000,   0x2000
app0,     app,  ota_0,   0x10000,  0x240000
spiffs,   data, spiffs,  0x250000, 0x1B0000
"""


def fake_build(d, fw=b"\xe9" * 1000, ota=True, map_text=MAP):
    os.makedirs(d, exist_ok=True)
    files = {"firmware.bin": fw, "bootloader.bin": b"B" * 300, "partitions.bin": b"P" * 3072}
    if ota:
        files["ota_data_initial.bin"] = b"\xff" * 8192
    for name, data in files.items():
        with open(os.path.join(d, name), "wb") as fh:
            fh.write(data)
    if map_text is not None:
        with open(os.path.join(d, "axis-bbs.map"), "w", encoding="utf-8") as fh:
            fh.write(map_text)


class MeasureTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_dram_from_size_output(self):
        self.assertEqual(buildcost.dram_from_size(SIZE_A), 14288 + 34080)

    def test_dram_from_map(self):
        self.assertEqual(buildcost.dram_from_map(MAP), 14288 + 34080)

    def test_map_without_sections_is_refused(self):
        with self.assertRaises(ValueError):
            buildcost.dram_from_map("nothing here\n")

    def test_measure_uses_bin_size_and_map(self):
        fake_build(self.tmp)
        got = buildcost.measure(self.tmp, size_tool="")
        self.assertEqual(got, {"flash": 1000, "dram_static": 48368})

    def test_measure_without_map_or_elf_is_refused(self):
        fake_build(self.tmp, map_text=None)
        with self.assertRaises(ValueError):
            buildcost.measure(self.tmp, size_tool="")


class TableTest(unittest.TestCase):
    RESULTS = {
        "everything": {"flash": 2211817, "dram_static": 48368},
        "no-doors": {"flash": 2130829, "dram_static": 47000},
        "no-files": {"flash": 2092757, "dram_static": 48000},
    }

    def test_freed_is_everything_minus_preset(self):
        t = buildcost.table(self.RESULTS, PEAKS, PRESETS, release="v1.0")
        rows = {r["id"]: r for r in t["presets"]}
        self.assertEqual(rows["everything"]["flash_freed"], 0)
        self.assertEqual(rows["no-doors"]["flash_freed"], 2211817 - 2130829)
        self.assertEqual(rows["no-files"]["flash_freed"], 2211817 - 2092757)
        self.assertEqual(rows["no-doors"]["dram_static_freed"], 48368 - 47000)
        self.assertEqual(t["release"], "v1.0")
        self.assertEqual(t["app_partition"], 0x240000)
        self.assertEqual([r["id"] for r in t["presets"]], ["everything", "no-doors", "no-files"])

    def test_spikes_gone_come_from_peaks(self):
        t = buildcost.table(self.RESULTS, PEAKS, PRESETS)
        rows = {r["id"]: r for r in t["presets"]}
        self.assertEqual(rows["everything"]["spikes_gone"], [])
        self.assertEqual([s["id"] for s in rows["no-doors"]["spikes_gone"]], ["lord"])
        self.assertEqual([s["id"] for s in rows["no-files"]["spikes_gone"]], ["ansiview", "transfers"])
        self.assertEqual(rows["no-files"]["spikes_gone_kb"], 52)

    def test_flash_percent_of_app_partition(self):
        t = buildcost.table(self.RESULTS, PEAKS, PRESETS)
        self.assertEqual(t["presets"][0]["flash_pct"], round(100.0 * 2211817 / 0x240000, 1))

    def test_missing_everything_is_refused(self):
        with self.assertRaises(ValueError):
            buildcost.table({"no-doors": self.RESULTS["no-doors"]}, PEAKS, PRESETS)

    def test_missing_preset_is_refused(self):
        r = dict(self.RESULTS)
        del r["no-files"]
        with self.assertRaises(ValueError):
            buildcost.table(r, PEAKS, PRESETS)


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.build = os.path.join(self.tmp, "build")
        self.out = os.path.join(self.tmp, "out", "no-doors")
        self.parts = os.path.join(self.tmp, "partitions.csv")
        with open(self.parts, "w", encoding="utf-8") as fh:
            fh.write(PARTS)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_manifest_lists_images_with_offsets_and_hashes(self):
        fake_build(self.build)
        m = buildcost.manifest(self.build, self.out, "no-doors", ["lord", "scripts", "dosprompt"],
                               "v1.0", partitions_csv=self.parts, size_tool="")
        self.assertEqual(m["chip"], "ESP32")
        self.assertEqual(m["preset"], "no-doors")
        self.assertEqual(m["release"], "v1.0")
        self.assertEqual(m["off"], ["dosprompt", "lord", "scripts"])
        self.assertEqual(m["app_partition"], {"offset": 0x10000, "size": 0x240000})
        self.assertEqual(m["cost"], {"flash": 1000, "dram_static": 48368})
        by_role = {i["role"]: i for i in m["images"]}
        self.assertEqual(by_role["bootloader"]["offset"], 0x1000)
        self.assertEqual(by_role["partitions"]["offset"], 0x8000)
        self.assertEqual(by_role["otadata"]["offset"], 0xE000)
        self.assertEqual(by_role["otadata"]["file"], "ota_data_initial.bin")
        self.assertEqual(by_role["app"]["offset"], 0x10000)
        app = by_role["app"]
        self.assertEqual(app["size"], 1000)
        self.assertEqual(app["sha256"], hashlib.sha256(b"\xe9" * 1000).hexdigest())
        for img in m["images"]:
            self.assertTrue(os.path.isfile(os.path.join(self.out, img["file"])))
        with open(os.path.join(self.out, "manifest.json"), encoding="utf-8") as fh:
            self.assertEqual(json.load(fh), m)

    def test_boot_app0_is_used_when_there_is_no_ota_data(self):
        fake_build(self.build, ota=False)
        with open(os.path.join(self.build, "boot_app0.bin"), "wb") as fh:
            fh.write(b"\x00" * 8192)
        m = buildcost.manifest(self.build, self.out, "everything", [], "v1.0",
                               partitions_csv=self.parts, size_tool="")
        ota = [i for i in m["images"] if i["role"] == "otadata"]
        self.assertEqual(ota[0]["file"], "boot_app0.bin")

    def test_image_bigger_than_app_partition_is_refused(self):
        fake_build(self.build, fw=b"\xe9" * (0x240000 + 1))
        with self.assertRaises(ValueError):
            buildcost.manifest(self.build, self.out, "everything", [], "v1.0",
                               partitions_csv=self.parts, size_tool="")

    def test_table_from_manifests(self):
        for pid, size in (("everything", 3000), ("no-doors", 2000), ("no-files", 2500)):
            b = os.path.join(self.tmp, "b-" + pid)
            fake_build(b, fw=b"\xe9" * size)
            off = next(p["off"] for p in PRESETS if p["id"] == pid)
            buildcost.manifest(b, os.path.join(self.tmp, "rel", pid), pid, off, "v1.0",
                               partitions_csv=self.parts, size_tool="")
        results = buildcost.results_from_dir(os.path.join(self.tmp, "rel"))
        self.assertEqual(results["no-doors"]["flash"], 2000)
        t = buildcost.table(results, PEAKS, PRESETS, release="v1.0")
        self.assertEqual({r["id"]: r["flash_freed"] for r in t["presets"]},
                         {"everything": 0, "no-doors": 1000, "no-files": 500})


if __name__ == "__main__":
    unittest.main()
