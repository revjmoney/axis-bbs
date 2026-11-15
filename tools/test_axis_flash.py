"""Tests for tools/axis_flash.py. Run: python -I tools/test_axis_flash.py

The flash decision must match the installer page (site/installer-logic.js planFlash and guardRegions):
both run every case in installer/test-fixtures/flash-plans.json. The write path runs against a fake
esptool that records what it was asked to do; no serial port is opened.
"""
import contextlib
import hashlib
import io
import json
import os
import re
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import axis_flash as F  # noqa: E402

with open(os.path.join(ROOT, "installer", "test-fixtures", "flash-plans.json"), encoding="utf-8") as _fh:
    PLANS = json.load(_fh)

MB = 1024 * 1024


def target_from(t):
    return {"kind": t["kind"], "chip": t["chip"], "project": t["project"], "layout": t.get("layout", "single"),
            "slot": t.get("slot"), "flash_size": t.get("flash_size"), "part_table": bytes.fromhex(t["part_table_hex"]),
            "images": t["images"]}


def device_from(d):
    return {"chip": d["chip"], "flash_size": d.get("flash_size"), "part_table": bytes.fromhex(d["part_table_hex"]),
            "app_header": bytes.fromhex(d["app_header_hex"]), "bootloader_sha256": d.get("bootloader_sha256")}


def case(name):
    return next(c for c in PLANS["cases"] if c["name"] == name)


class Capture(F.Out):
    def __init__(self):
        F.Out.__init__(self, io.StringIO())

    @property
    def text(self):
        return self.stream.getvalue()


# == the decision: the shared fixture ==

class DecisionTest(unittest.TestCase):
    def test_every_fixture_case_matches_the_page(self):
        self.assertGreaterEqual(len(PLANS["cases"]), 23)
        for c in PLANS["cases"]:
            t = target_from(PLANS["targets"][c["target"]])
            p = F.plan_flash(device_from(c["device"]), t)
            e = c["expect"]
            self.assertEqual(p["kind"], e["kind"], c["name"])
            self.assertEqual(p["code"], e["code"], c["name"])
            self.assertIn(p["code"], PLANS["codes"], c["name"])
            self.assertEqual([[x["label"], x["offset"], x["size"]] for x in p["erase"]],
                             [[x["label"], x["offset"], x["size"]] for x in e["erase"]], c["name"])
            self.assertEqual([[r["role"], r["offset"]] for r in p["regions"]],
                             [[r["role"], r["offset"]] for r in e["regions"]], c["name"])
            self.assertTrue(p["reason"], c["name"])
            if p["kind"] == "refuse":
                self.assertTrue(p["reason"].endswith("Nothing was written."), c["name"])
            else:
                self.assertTrue(F.guard_regions(p, F.parse_table(device_from(c["device"])["part_table"]), t), c["name"])
            if p["kind"] == "first":
                self.assertTrue(p["reason"].endswith(F.FIRST_INSTALL_WARNING), c["name"])

    def test_the_warning_and_names_are_the_shared_text(self):
        self.assertEqual(F.FIRST_INSTALL_WARNING, PLANS["first_install_warning"])
        self.assertEqual(F.FIRST_INSTALL_WARNING,
                         "This replaces everything on this board. Whatever it runs now, and its settings, will be gone.")
        self.assertEqual(F.MULTIMODE_MARKER, PLANS["multimode_marker"])
        self.assertEqual(F.MODEM_NAME, "LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM")
        self.assertEqual(F.BOARD_NAME, "AXIS Micro BBS")
        logic = readable("site/installer-logic.js")
        for value in (F.FIRST_INSTALL_WARNING, F.MODEM_NAME, F.BOARD_NAME, F.MULTIMODE_MARKER):
            self.assertIn('"%s"' % value, logic)
        for stage in F.STAGES:
            self.assertIn('"%s"' % stage, logic)

    def test_flash_size_refusals_name_both_models(self):
        p = F.plan_flash(device_from(case("v3-image-on-v4-single")["device"]), target_from(PLANS["targets"]["radio-v3-single"]))
        self.assertIn("so it is a Heltec V4", p["reason"])
        self.assertIn("for the Heltec V3 (8 MB)", p["reason"])
        p = F.plan_flash(device_from(case("radio-manifest-no-flash-size")["device"]),
                         target_from(PLANS["targets"]["radio-v4-single-no-size"]))
        self.assertIn("does not say which flash size", p["reason"])

    def test_radio_env(self):
        mm = bytes.fromhex(PLANS["targets"]["radio-v4-multimode"]["part_table_hex"])
        single = bytes.fromhex(PLANS["targets"]["radio-v4-single"]["part_table_hex"])
        self.assertEqual(F.radio_env("v4", mm), "heltec_v4")
        self.assertEqual(F.radio_env("v4", single), "heltec_v4_single")
        self.assertEqual(F.radio_env("v4", b"\xff" * 64), "heltec_v4_single")
        self.assertEqual(F.radio_env("v3", single), "heltec_v3_single")
        self.assertEqual(F.radio_env("v3", mm), "heltec_v3_single")

    def test_progress_matches_the_page(self):
        self.assertEqual(F.stage_name(0), "BL4Z1NG BYT3S")
        self.assertEqual(F.stage_name(30), "FL0WTR0N ENGAGED")
        self.assertEqual(F.stage_name(60), "S0N1C B00M")
        self.assertEqual(F.stage_name(100), "DATAMASTER WRITING")
        self.assertEqual(F.block_bar(50, 10), u"[█████░░░░░]  50%")
        self.assertEqual(F.block_bar(100, 4, ascii_only=True), "[####] 100%")


class GuardTest(unittest.TestCase):
    def setUp(self):
        self.axis_t = target_from(PLANS["targets"]["axis-board"])
        self.axis = F.parse_table(self.axis_t["part_table"])
        self.mm_t = target_from(PLANS["targets"]["radio-v4-multimode"])
        self.mm = F.parse_table(self.mm_t["part_table"])
        self.first = F.plan_flash(device_from(case("board-first-blank")["device"]), self.axis_t)

    @staticmethod
    def upd(r):
        return {"kind": "update", "erase": [], "regions": [r]}

    def test_update_never_touches_nvs_otadata_littlefs_or_another_slot(self):
        self.assertTrue(F.guard_regions(self.upd({"role": "app", "offset": 0x10000, "size": 0x200000}), self.axis, self.axis_t))
        for bad in ({"role": "app", "offset": 0x9000, "size": 0x1000}, {"role": "otadata", "offset": 0xE000, "size": 0x2000},
                    {"role": "app", "offset": 0x250000, "size": 0x1000}, {"role": "app", "offset": 0x10000, "size": 0x240001},
                    {"role": "partitions", "offset": 0x8000, "size": 0xC00}):
            with self.assertRaises(F.Refused, msg=bad):
                F.guard_regions(self.upd(bad), self.axis, self.axis_t)
        with self.assertRaises(F.Refused):
            F.guard_regions({"kind": "update", "erase": [{"label": "nvs", "offset": 0x9000, "size": 0x5000}],
                             "regions": [{"role": "app", "offset": 0x10000, "size": 1000}]}, self.axis, self.axis_t)
        self.assertTrue(F.guard_regions(self.upd({"role": "app", "offset": 0x4A0000, "size": 1059504}), self.mm, self.mm_t))
        with self.assertRaises(F.Refused):
            F.guard_regions(self.upd({"role": "app", "offset": 0x20000, "size": 1000}), self.mm, self.mm_t)
        other = dict(self.mm_t, images=[{"role": "app", "offset": 0xA0000, "size": 1000}])
        with self.assertRaises(F.Refused):
            F.guard_regions(self.upd({"role": "app", "offset": 0xA0000, "size": 1000}), self.mm, other)
        old_mm = F.parse_table(bytes.fromhex(case("v4-multimode-other-slot")["device"]["part_table_hex"]))
        with self.assertRaises(F.Refused):
            F.guard_regions(self.upd({"role": "app", "offset": 0x4A0000, "size": 1000}), old_mm, self.mm_t)
        with self.assertRaises(F.Refused):
            F.guard_regions({"kind": "refuse", "erase": [], "regions": []}, self.axis, self.axis_t)

    def test_every_erase_is_checked_on_its_own(self):
        def with_erase(e):
            return dict(self.first, erase=self.first["erase"] + [e])
        self.assertTrue(F.guard_regions(self.first, None, self.axis_t))
        for e in ({"label": "x", "offset": 0x8000, "size": 0x1000}, {"label": "otadata", "offset": 0xE000, "size": 0x2000},
                  {"label": "app0", "offset": 0x10000, "size": 0x240000}):
            with self.assertRaises(F.Refused, msg=e):
                F.guard_regions(with_erase(e), None, self.axis_t)
        # a malformed table: a data partition that overlaps the app partition
        import struct
        rows = [(0x9000, 0x5000, 1, 2, "nvs"), (0xE000, 0x2000, 1, 0, "otadata"), (0x10000, 0x240000, 0, 0x10, "app0"),
                (0x200000, 0x100000, 1, 0x82, "spiffs")]
        table = b"".join(struct.pack("<2sBBII16sI", b"\xaa\x50", t, s, o, z, l.encode(), 0) for o, z, t, s, l in rows)
        bad = dict(self.axis_t, part_table=table + b"\xff" * 32)
        plan = F.plan_flash(device_from(case("board-first-blank")["device"]), bad)
        with self.assertRaises(F.Refused):
            F.guard_regions(plan, None, bad)

    def test_first_install_regions_sit_where_the_table_puts_them(self):
        moved = json.loads(json.dumps(self.first))
        next(r for r in moved["regions"] if r["role"] == "app")["offset"] = 0x20000
        bad_ota = json.loads(json.dumps(self.first))
        next(r for r in bad_ota["regions"] if r["role"] == "otadata")["offset"] = 0xF000
        for plan in (moved, bad_ota):
            with self.assertRaises(F.Refused):
                F.guard_regions(plan, None, self.axis_t)
        with self.assertRaises(F.Refused):
            F.guard_regions(dict(self.first), None, dict(self.axis_t, layout="multimode"))


# == esptool output ==

# Built from esptool 4.9.0's own print statements (esptool/cmds.py detect_chip, detect_flash_id; loader
# MAC line), including the ESP32's "switching and trying again" path. Not captured from a board (Task 11).
ESP32_FLASH_ID = """esptool.py v4.9.0
Serial port COM20
Connecting....
Detecting chip type... Unsupported detection protocol, switching and trying again...
Connecting....
Detecting chip type... ESP32
Chip is ESP32-D0WD-V3 (revision v3.1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
Crystal is 40MHz
MAC: 8c:94:df:4f:1c:4c
Uploading stub...
Running stub...
Stub running...
Changing baud rate to 460800
Changed.
Manufacturer: 20
Device: 4016
Detected flash size: 4MB
Staying in bootloader.
"""
S3_FLASH_ID = """esptool.py v4.9.0
Serial port COM11
Connecting...
Detecting chip type... ESP32-S3
Chip is ESP32-S3 (QFN56) (revision v0.2)
Features: WiFi, BLE, Embedded PSRAM 2MB (AP_3v3)
Crystal is 40MHz
USB mode: USB-Serial/JTAG
MAC: 3c:0f:02:ec:e4:b0
Uploading stub...
Running stub...
Stub running...
Changing baud rate to 460800
Changed.
Manufacturer: c8
Device: 4018
Detected flash size: 16MB
Flash type set in eFuse: quad (4 data lines)
Staying in bootloader.
"""


class EsptoolOutputTest(unittest.TestCase):
    def test_flash_id(self):
        self.assertEqual(F.parse_flash_id(ESP32_FLASH_ID), {"chip": "ESP32", "mac": "8c:94:df:4f:1c:4c", "flash_size": 4 * MB})
        self.assertEqual(F.parse_flash_id(S3_FLASH_ID), {"chip": "ESP32-S3", "mac": "3c:0f:02:ec:e4:b0", "flash_size": 16 * MB})

    def test_unknown_values_fail_closed(self):
        got = F.parse_flash_id(S3_FLASH_ID.replace("Detected flash size: 16MB", "Detected flash size: Unknown"))
        self.assertIsNone(got["flash_size"])
        self.assertEqual(F.parse_flash_id("garbage"), {"chip": None, "mac": None, "flash_size": None})
        dev = {"chip": "ESP32-S3", "flash_size": None, "part_table": None, "app_header": None}
        self.assertEqual(F.plan_flash(dev, target_from(PLANS["targets"]["radio-v4-single"]))["code"], "flash-size")


# == the write path, against a fake esptool ==

class FakeBoard(object):
    """Pretend board behind esptool: answers flash_id, read_flash, erase_region and write_flash."""

    def __init__(self, device, flash_id_text):
        self.flash = {}  # offset: bytes
        self.flash[0x8000] = bytes.fromhex(device["part_table_hex"])
        self.app_header = bytes.fromhex(device["app_header_hex"])
        self.flash_id_text = flash_id_text
        self.calls = []
        self.fail_write = False

    def __call__(self, argv, on_line):
        self.calls.append(argv)
        cmd_at = next(i for i, a in enumerate(argv) if a in ("flash_id", "read_flash", "erase_region", "write_flash",
                                                              "read_mac", "erase_flash"))
        cmd, rest = argv[cmd_at], argv[cmd_at + 1:]
        out = []
        if cmd == "flash_id":
            out = self.flash_id_text.splitlines()
        elif cmd == "read_flash":
            off, size, path = int(rest[0], 16), int(rest[1], 16), rest[2]
            data = self.flash.get(off, self.app_header if off != 0x8000 and off >= 0x10000 else b"")
            data = (data + b"\xff" * size)[:size]
            with open(path, "wb") as fh:
                fh.write(data)
            out = ["Read %d bytes at 0x%x" % (size, off)]
        elif cmd == "erase_region":
            out = ["Erasing region (may be slow depending on size)...", "Erase completed successfully in 0.1 seconds."]
        elif cmd == "write_flash":
            pairs = [a for a in rest if not a.startswith("--") and a != "keep"]
            for off, path in zip(pairs[0::2], pairs[1::2]):
                size = os.path.getsize(path)
                for pct in (0, 25, 50, 75, 100):
                    out.append("Writing at 0x%08x... (%d %%)" % (int(off, 16) + size * pct // 100, pct))
                    if self.fail_write and pct == 50:
                        for line in out:
                            on_line(line)
                        return 2, "\n".join(out + ["A fatal error occurred: Timed out"])
                out.append("Wrote %d bytes at %s in 1.0 seconds" % (size, off))
                out.append("Hash of data verified.")
        for line in out:
            on_line(line)
        return 0, "\n".join(out)

    def commands(self):
        return [next(a for a in argv if a in ("flash_id", "read_flash", "erase_region", "write_flash", "read_mac",
                                              "erase_flash")) for argv in self.calls]


def make_images(tmp, t):
    """An images folder with manifest.json for a fixture target (files of the declared sizes)."""
    os.makedirs(tmp, exist_ok=True)
    images = []
    for img in t["images"]:
        if img["role"] == "partitions":
            data = (bytes.fromhex(t["part_table_hex"]) + b"\xff" * img["size"])[:img["size"]]
        else:
            data = bytes([0xE9]) + b"\x00" * (img["size"] - 1)
        with open(os.path.join(tmp, img["file"]), "wb") as fh:
            fh.write(data)
        images.append(dict(img, sha256=hashlib.sha256(data).hexdigest()))
    m = {"chip": t["chip"], "layout": t.get("layout", "single"), "images": images}
    if t.get("flash_size"):
        m["flash_size"] = t["flash_size"]
    if t.get("slot"):
        m["slot"] = t["slot"]
    if not any(i["role"] == "partitions" for i in images):
        with open(os.path.join(tmp, "partitions.bin"), "wb") as fh:
            fh.write(bytes.fromhex(t["part_table_hex"]))
        m["table"] = "partitions.bin"
    with open(os.path.join(tmp, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump(m, fh)
    return tmp


class FlashPathTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="axis-flash-test-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_flash(self, case_name, target_name, flash_id_text, answer="", headless=False, kind=None):
        c = case(case_name)
        t = PLANS["targets"][target_name]
        images = make_images(os.path.join(self.tmp, "img"), t)
        fake = FakeBoard(c["device"], flash_id_text)
        out = Capture()
        asked = []

        def ask(prompt):
            asked.append(len(fake.calls))
            return answer

        opener = None
        if headless:
            class Port(object):
                dtr = rts = True
                sent = []

                def write(self, b):
                    Port.sent.append(b)

                def read(self, n):
                    return b"axis: saved, restarting\r\n" if Port.sent else b""

                def close(self):
                    pass
            opener = Port
        code = F.flash(kind or t["kind"], images, "COM99", out, headless=headless, runner=fake, ask=ask,
                       headless_opener=opener)
        return code, fake, out.text, asked

    def test_update_writes_the_app_only_and_erases_nothing(self):
        code, fake, text, asked = self.run_flash("board-update", "axis-board", ESP32_FLASH_ID)
        self.assertEqual(code, 0, text)
        self.assertEqual(asked, [])
        self.assertNotIn("erase_region", fake.commands())
        writes = [a for a in fake.calls if "write_flash" in a]
        self.assertEqual(len(writes), 1)
        self.assertEqual([a for a in writes[0] if a.startswith("0x")], ["0x10000"])
        self.assertEqual(writes[0][writes[0].index("--after") + 1], "hard_reset")
        for flag in ("--flash_mode", "--flash_freq", "--flash_size"):
            self.assertEqual(writes[0][writes[0].index(flag) + 1], "keep")
        self.assertIn("Users, messages and settings stay.", text)
        self.assertIn("DATAMASTER WRITING", text)
        self.assertIn("DONE. Your AXIS Micro BBS restarts", text)

    def test_first_install_waits_for_the_typed_confirm_then_erases_and_writes(self):
        code, fake, text, asked = self.run_flash("board-first-stock", "axis-board", ESP32_FLASH_ID, answer="REPLACE")
        self.assertEqual(code, 0, text)
        self.assertEqual(len(asked), 1)
        before = fake.commands()[:asked[0]]
        self.assertNotIn("erase_region", before)
        self.assertNotIn("write_flash", before)
        self.assertIn(F.FIRST_INSTALL_WARNING, text)
        erases = [(a[a.index("erase_region") + 1], a[a.index("erase_region") + 2]) for a in fake.calls if "erase_region" in a]
        self.assertEqual(erases, [("0x9000", "0x5000"), ("0x250000", "0x1b0000")])
        write = next(a for a in fake.calls if "write_flash" in a)
        self.assertEqual([a for a in write if a.startswith("0x")], ["0x1000", "0x8000", "0xe000", "0x10000"])
        self.assertIn("Erasing nvs (20 KB) at 0x9000 ...", text)
        self.assertIn("join its AXIS-SETUP hotspot", text)

    def test_a_declined_or_impossible_confirm_writes_nothing(self):
        for answer in ("", "yes", "replace", None):
            code, fake, text, asked = self.run_flash("board-first-blank", "axis-board", ESP32_FLASH_ID, answer=answer)
            self.assertEqual(code, 2, answer)
            self.assertNotIn("erase_region", fake.commands())
            self.assertNotIn("write_flash", fake.commands())
            self.assertIn("Nothing was written.", text)
            self.assertEqual(fake.commands()[-1], "read_mac")  # the board is reset out of the boot loader

    def test_a_refusal_writes_nothing(self):
        code, fake, text, asked = self.run_flash("board-image-on-radio", "axis-board", S3_FLASH_ID)
        self.assertEqual(code, 2)
        self.assertEqual(asked, [])
        self.assertEqual(fake.commands(), ["flash_id", "read_flash", "read_mac"])
        self.assertIn("REFUSED: nothing was written.", text)
        code, fake, text, asked = self.run_flash("v3-image-on-v4-single", "radio-v3-single", S3_FLASH_ID)
        self.assertEqual(code, 2)
        self.assertNotIn("write_flash", fake.commands())
        self.assertIn("so it is a Heltec V4", text)

    def test_multimode_v4_gets_only_its_slot(self):
        code, fake, text, asked = self.run_flash("v4-multimode-update", "radio-v4-multimode", S3_FLASH_ID)
        self.assertEqual(code, 0, text)
        write = next(a for a in fake.calls if "write_flash" in a)
        self.assertEqual([a for a in write if a.startswith("0x")], ["0x4a0000"])
        self.assertEqual(write[write.index("--after") + 1], "no_reset")
        self.assertIn("Unplug the radio and plug it back in", text)

    def test_never_erases_the_chip(self):
        for name, tname, fid, ans in (("board-first-blank", "axis-board", ESP32_FLASH_ID, "REPLACE"),
                                      ("v3-first-blank", "radio-v3-single", S3_FLASH_ID.replace("16MB", "8MB"), "REPLACE")):
            code, fake, text, asked = self.run_flash(name, tname, fid, answer=ans)
            self.assertEqual(code, 0, text)
            for argv in fake.calls:
                self.assertNotIn("erase_flash", argv)
                self.assertNotIn("--erase-all", argv)
                if "erase_region" in argv:
                    self.assertGreaterEqual(int(argv[argv.index("erase_region") + 1], 16), 0x9000)

    def test_a_failed_write_says_so(self):
        c = case("board-update")
        images = make_images(os.path.join(self.tmp, "img"), PLANS["targets"]["axis-board"])
        fake = FakeBoard(c["device"], ESP32_FLASH_ID)
        fake.fail_write = True
        out = Capture()
        with self.assertRaises(F.Stopped) as ctx:
            F.flash("board", images, "COM99", out, runner=fake, ask=lambda p: "")
        self.assertIn("may not start", str(ctx.exception))

    def test_bare_esp32_is_told_headless(self):
        code, fake, text, asked = self.run_flash("board-update", "axis-board", ESP32_FLASH_ID, headless=True)
        self.assertEqual(code, 0, text)
        self.assertIn("The board answered: axis: saved, restarting.", text)

    def test_tampered_image_is_refused_before_the_port_is_touched(self):
        images = make_images(os.path.join(self.tmp, "img"), PLANS["targets"]["axis-board"])
        with open(os.path.join(images, "firmware.bin"), "r+b") as fh:
            fh.write(b"\x00")
        fake = FakeBoard(case("board-update")["device"], ESP32_FLASH_ID)
        with self.assertRaises(F.Stopped):
            F.flash("board", images, "COM99", Capture(), runner=fake)
        self.assertEqual(fake.calls, [])


class ProbeTest(unittest.TestCase):
    def probe(self, case_name, kind, radio, fid):
        fake = FakeBoard(case(case_name)["device"], fid)
        out = Capture()
        code, state = F.probe(kind, "COM99", out, radio=radio, runner=fake)
        return code, state, out.text, fake

    def test_radio_env_from_the_board(self):
        code, state, text, fake = self.probe("v4-multimode-update", "radio", "v4", S3_FLASH_ID)
        self.assertEqual((code, state["env"]), (0, "heltec_v4"))
        self.assertNotIn("write_flash", fake.commands())
        code, state, text, fake = self.probe("v4-single-update", "radio", "v4", S3_FLASH_ID)
        self.assertEqual(state["env"], "heltec_v4_single")

    def test_wrong_model_or_chip_is_refused_before_the_build(self):
        code, state, text, fake = self.probe("v3-image-on-v4-single", "radio", "v3", S3_FLASH_ID)
        self.assertEqual(code, 2)
        self.assertIn("so it is a Heltec V4", text)
        self.assertIn("nothing was built or written", text)
        code, state, text, fake = self.probe("radio-image-on-cyd", "radio", "v3", ESP32_FLASH_ID)
        self.assertEqual(code, 2)
        code, state, text, fake = self.probe("board-image-on-radio", "board", None, S3_FLASH_ID)
        self.assertEqual(code, 2)
        code, state, text, fake = self.probe("board-update", "board", None, ESP32_FLASH_ID)
        self.assertEqual((code, state["env"]), (0, "cyd"))


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="axis-manifest-test-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def build(self, target_name, ota=True):
        d = os.path.join(self.tmp, target_name + "-build")
        os.makedirs(d)
        files = {"partitions.bin": bytes.fromhex(PLANS["targets"][target_name]["part_table_hex"]) + b"\xff" * 64,
                 "bootloader.bin": b"\xe9" + b"\x00" * 20000, "firmware.bin": b"\xe9" + b"\x00" * 100000}
        if ota:
            files["ota_data_initial.bin"] = b"\xff" * 8192
        for name, data in files.items():
            with open(os.path.join(d, name), "wb") as fh:
                fh.write(data)
        return d

    def test_board_build(self):
        out = os.path.join(self.tmp, "board")
        m = F.make_manifest("board", self.build("axis-board"), out)
        self.assertEqual([(i["role"], i["offset"]) for i in m["images"]],
                         [("bootloader", 0x1000), ("partitions", 0x8000), ("otadata", 0xE000), ("app", 0x10000)])
        t = F.load_target("board", out)
        p = F.plan_flash(device_from(case("board-first-blank")["device"]), t)
        self.assertEqual([r["role"] for r in p["regions"]], ["bootloader", "partitions", "otadata", "app"])
        p = F.plan_flash(device_from(case("board-update")["device"]), t)
        self.assertEqual(p["kind"], "update")

    def test_radio_builds(self):
        out = os.path.join(self.tmp, "mm")
        m = F.make_manifest("radio", self.build("radio-v4-multimode"), out, env="heltec_v4")
        self.assertEqual(([(i["role"], i["offset"]) for i in m["images"]], m["layout"], m["slot"], m["flash_size"]),
                         ([("app", 0x4A0000)], "multimode", "ota_1", 16 * MB))
        p = F.plan_flash(device_from(case("v4-multimode-update")["device"]), F.load_target("radio", out))
        self.assertEqual([(r["role"], r["offset"]) for r in p["regions"]], [("app", 0x4A0000)])
        out = os.path.join(self.tmp, "v3")
        m = F.make_manifest("radio", self.build("radio-v3-single"), out, env="heltec_v3_single")
        self.assertEqual([(i["role"], i["offset"]) for i in m["images"]],
                         [("bootloader", 0x0), ("partitions", 0x8000), ("otadata", 0xE000), ("app", 0x10000)])
        self.assertEqual(m["flash_size"], 8 * MB)
        p = F.plan_flash(device_from(case("v3-first-blank")["device"]), F.load_target("radio", out))
        self.assertEqual([e["label"] for e in p["erase"]], ["nvs", "storage"])
        out = os.path.join(self.tmp, "unknown")
        F.make_manifest("radio", self.build("radio-v4-single"), out, env="something_else")
        p = F.plan_flash(device_from(case("v4-single-update")["device"]), F.load_target("radio", out))
        self.assertEqual(p["code"], "flash-size")  # an env it does not know has no flash size: refused


class CliTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="axis-cli-test-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def no_esptool(self, argv, on_line):
        raise AssertionError("a dry run ran esptool: %r" % (argv,))

    def test_dry_run_with_a_pretend_board_prints_the_plan_and_writes_nothing(self):
        images = make_images(os.path.join(self.tmp, "img"), PLANS["targets"]["axis-board"])
        info = os.path.join(self.tmp, "device.json")
        with open(info, "w", encoding="utf-8") as fh:
            json.dump(case("board-first-stock")["device"], fh)
        out = Capture()
        code = F.main(["flash", "--kind", "board", "--images", images, "--port", "COM99", "--dry-run",
                       "--device-info", info], out=out, runner=self.no_esptool, ask=lambda p: self.fail("asked"))
        self.assertEqual(code, 0, out.text)
        self.assertIn(F.FIRST_INSTALL_WARNING, out.text)
        self.assertIn("DRY RUN, would run: esptool --chip auto --port COM99 --baud 460800 --before default_reset "
                      "--after no_reset erase_region 0x9000 0x5000", out.text)
        self.assertRegex(out.text, r"write_flash .* 0x1000 \S+bootloader.bin 0x8000 \S+partitions.bin 0xe000 "
                                   r"\S+ota_data_initial.bin 0x10000 \S+firmware.bin")
        self.assertIn("DRY RUN: nothing was written.", out.text)

    def test_dry_run_without_a_board_lists_the_steps(self):
        state = os.path.join(self.tmp, "state.txt")
        out = Capture()
        code = F.main(["probe", "--kind", "radio", "--radio", "v4", "--port", "/dev/ttyUSB0", "--state", state,
                       "--dry-run"], out=out, runner=self.no_esptool)
        self.assertEqual(code, 0)
        self.assertIn("flash_id", out.text)
        with open(state, encoding="utf-8") as fh:
            self.assertEqual(fh.read(), "env=heltec_v4_single\nport=/dev/ttyUSB0\n")


# == the build scripts, dry run (CI runs these on ubuntu with sh and on windows with PowerShell 5.1) ==

import subprocess  # noqa: E402

RADIO_WAIT = "Radio images come with the first LFD3K-SM release."


def posix(path):
    return path.replace("\\", "/")


class ScriptRunner(object):
    """Runs site/install.sh or site/install.ps1 with --dry-run. For sh, a fake uname (Linux, or Darwin)
    and a python3 that is this Python go first in PATH, so the same test runs under Git's sh on Windows."""

    def __init__(self, tmp):
        self.tmp = tmp

    def sh(self, args, home, system="Linux"):
        sh = shutil.which("sh")
        if not sh:
            raise unittest.SkipTest("no sh")
        fake = os.path.join(self.tmp, "fakebin-" + system)
        if not os.path.isdir(fake):
            os.makedirs(fake)
            with open(os.path.join(fake, "uname"), "w", newline="\n") as fh:
                fh.write('#!/bin/sh\nif [ "$1" = -m ]; then echo x86_64; else echo %s; fi\n' % system)
            with open(os.path.join(fake, "python3"), "w", newline="\n") as fh:
                fh.write('#!/bin/sh\nexec "%s" "$@"\n' % posix(sys.executable))
            for name in ("uname", "python3"):
                os.chmod(os.path.join(fake, name), 0o755)
        env = dict(os.environ, AXIS_BUILD_HOME=posix(home), PATH=fake + os.pathsep + os.environ.get("PATH", ""))
        p = subprocess.run([sh, posix(os.path.join(ROOT, "site", "install.sh"))] + [posix(a) for a in args],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env)
        return p.returncode, p.stdout.decode("utf-8", "replace")

    def ps(self, args, home):
        if os.name != "nt" or not shutil.which("powershell"):
            raise unittest.SkipTest("no Windows PowerShell")
        env = dict(os.environ, AXIS_BUILD_HOME=home)
        p = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                            os.path.join(ROOT, "site", "install.ps1")] + args,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env)
        return p.returncode, p.stdout.decode("utf-8", "replace")


class ScriptDryRunTest(unittest.TestCase):
    """Each check runs for both scripts: sh flags (--board) and PowerShell flags (-Board)."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="axis-dryrun-")
        self.run_ = ScriptRunner(self.tmp)
        self.n = 0
        # a source copy whose radio.json names a release, for the radio path
        self.radio_src = os.path.join(self.tmp, "src-radio")
        shutil.copytree(os.path.join(ROOT, "installer"), os.path.join(self.radio_src, "installer"))
        os.makedirs(os.path.join(self.radio_src, "tools"))
        for name in ("buildopts.py", "axis_flash.py"):
            shutil.copy(os.path.join(HERE, name), os.path.join(self.radio_src, "tools", name))
        with open(os.path.join(self.radio_src, "installer", "radio.json"), "w", encoding="utf-8") as fh:
            json.dump({"version": "v0.6.0", "site": "https://revjmoney.github.io/lfd3k-sm/releases/"}, fh)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def home(self):
        self.n += 1
        return os.path.join(self.tmp, "home%d" % self.n)

    def both(self, args):
        """[(shell, code, output)] for sh and PowerShell; args use sh spelling and are mapped for PowerShell."""
        ps_name = {"--board": "-Board", "--off": "-Off", "--target": "-Target", "--radio": "-Radio", "--flash": "-Flash",
                   "--port": "-Port", "--release": "-Release", "--source": "-Source", "--dry-run": "-DryRun"}
        got = []
        for shell in ("sh", "ps"):
            try:
                if shell == "sh":
                    code, out = self.run_.sh(args, self.home())
                else:
                    code, out = self.run_.ps([ps_name.get(a, a) for a in args], self.home())
            except unittest.SkipTest:
                continue
            got.append((shell, code, out))
        if not got:
            self.skipTest("neither sh nor Windows PowerShell")
        return got

    def test_board_plan(self):
        for shell, code, out in self.both(["--dry-run", "--source", ROOT, "--board", "no-doors", "--target", "cyd",
                                           "--release", "v0.61", "--flash", "--port", "COM7"]):
            self.assertEqual(code, 0, shell + out)
            for want in ("YOU NEED AN SD CARD FOR FULL FUNCTIONS", "no-sd", "by Rev. J. Money / jmscnc.com",
                         "platformio==6.1.18 esptool==4.9.0", "AXIS_OFF=dosprompt,lord,scripts",
                         "flash_id", "-m platformio run -d", "-e cyd", "manifest --kind board",
                         "flash --kind board", "--port COM7", "DRY RUN finished"):
                self.assertIn(want, out, shell)
            self.assertNotIn("--headless", out, shell)
            self.assertNotIn("Downloading", out, shell)

    def test_bare_esp32_and_radio_v3(self):
        for shell, code, out in self.both(["--dry-run", "--source", self.radio_src, "--board", "everything", "--target",
                                           "esp32", "--radio", "v3", "--release", "v0.61", "--flash", "--port", "COM7"]):
            self.assertEqual(code, 0, shell + out)
            self.assertIn("AXIS_OFF=\n", out.replace("\r", ""), shell)  # Everything switches nothing off
            self.assertIn("--headless", out, shell)
            for want in ("LFD3K-SM release v0.6.0 (from installer/radio.json)",
                         "https://static.rust-lang.org/rustup/dist/", "rustup-init", "espup",
                         "https://github.com/revjmoney/lfd3k-sm/archive/refs/tags/v0.6.0.tar.gz",
                         "cargo +esp build -p lfd3k-ffi", "-e heltec_v3_single", "flash --kind radio"):
                self.assertIn(want, out, shell)
            self.assertNotIn("archive/refs/tags/v0.61", out, shell)  # the radio version is not the board's release

    def test_v4_without_flash_builds_both_images(self):
        for shell, code, out in self.both(["--dry-run", "--source", self.radio_src, "--radio", "v4"]):
            self.assertEqual(code, 0, shell + out)
            self.assertIn("-e heltec_v4 ", out.replace("\r\n", " ").replace("\n", " ") + " ", shell)
            self.assertIn("-e heltec_v4_single", out, shell)
            self.assertNotIn("flash --kind", out, shell)
            self.assertNotIn("AXIS_OFF", out, shell)  # radio only: no board build

    def test_a_broken_dependency_is_refused_with_the_reason(self):
        for shell, code, out in self.both(["--dry-run", "--source", ROOT, "--off", "files", "--release", "v0.61"]):
            self.assertNotEqual(code, 0, shell)
            self.assertIn("ansiview needs files, which is off", out, shell)
            self.assertIn("STOPPED: that selection is refused", out, shell)
            self.assertNotIn("platformio run", out, shell)

    def test_radio_waits_for_the_first_release(self):
        for shell, code, out in self.both(["--dry-run", "--source", ROOT, "--board", "everything", "--radio", "v4",
                                           "--release", "v0.61"]):
            self.assertNotEqual(code, 0, shell)
            self.assertIn("STOPPED: " + RADIO_WAIT, out, shell)
            self.assertNotIn("platformio run", out, shell)  # nothing is built, the board neither
            self.assertNotIn("rustup", out, shell)

    def test_rerun_reuses_tools_and_deletes_partial_downloads(self):
        # the venv a first run left, and a download a first run did not finish
        for shell in ("sh", "ps"):
            home = self.home()
            venv_py = os.path.join(home, "venv", "Scripts" if shell == "ps" else "bin",
                                   "python.exe" if shell == "ps" else "python")
            os.makedirs(os.path.dirname(venv_py))
            with open(venv_py, "w", newline="\n") as fh:
                fh.write('#!/bin/sh\nexec "%s" "$@"\n' % posix(sys.executable))
            os.chmod(venv_py, 0o755)
            with open(os.path.join(home, "venv", ".axis-ok"), "w") as fh:
                fh.write("platformio==6.1.18 esptool==4.9.0")
            os.makedirs(os.path.join(home, "src"))
            part = os.path.join(home, "src", "axis-bbs-v0.61.tar.gz.part")
            with open(part, "w") as fh:
                fh.write("half")
            args = ["--dry-run", "--board", "no-sd", "--release", "v0.61"]
            try:
                if shell == "sh":
                    code, out = self.run_.sh(args, home)
                else:
                    code, out = self.run_.ps(["-DryRun", "-Board", "no-sd", "-Release", "v0.61"], home)
            except unittest.SkipTest:
                continue
            self.assertEqual(code, 0, shell + out)
            self.assertIn("would delete the partial download", out, shell)
            self.assertIn("axis-bbs-v0.61.tar.gz.part", out, shell)
            self.assertIn("Reusing", out, shell)
            self.assertIn("would download https://github.com/revjmoney/axis-bbs/archive/refs/tags/v0.61.tar.gz", out, shell)
            self.assertTrue(os.path.exists(part), shell)  # a dry run deletes nothing

    def test_sh_refuses_macos(self):
        code, out = self.run_.sh(["--board", "everything"], self.home(), system="Darwin")
        self.assertNotEqual(code, 0)
        self.assertIn("macOS is not supported", out)

    def test_ps_from_irm_never_closes_the_window(self):
        if os.name != "nt" or not shutil.which("powershell"):
            self.skipTest("no Windows PowerShell")
        cmd = ("& ([scriptblock]::Create((Get-Content -Raw '%s'))) -DryRun -Source '%s' -Off files; 'still here'"
               % (os.path.join(ROOT, "site", "install.ps1"), ROOT))
        p = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", cmd],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                           env=dict(os.environ, AXIS_BUILD_HOME=self.home()))
        out = p.stdout.decode("utf-8", "replace")
        self.assertIn("STOPPED: that selection is refused", out)
        self.assertIn("still here", out)


# == what a person reads ==

def readable(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as fh:
        return fh.read()


SCRIPTS = ["site/install.sh", "site/install.ps1"]


class TextTest(unittest.TestCase):
    def test_no_em_dash_no_double_hyphen_except_flags(self):
        for path in ["tools/axis_flash.py"] + SCRIPTS:
            text = readable(path)
            self.assertNotIn(u"—", text, path)
            self.assertNotIn(u"–", text, path)
            for n, line in enumerate(text.splitlines(), 1):
                # a flag (--port, --dry-run) or the end of sh's options in "sh -s -- --board"
                rest = re.sub(r"--[a-z][a-z0-9_-]*", "", line)
                rest = rest.replace("sh -s -- ", "")
                self.assertNotIn("--", rest, "%s:%d: %s" % (path, n, line))

    def test_full_names_and_no_old_spelling(self):
        for path in ["tools/axis_flash.py"] + SCRIPTS:
            text = readable(path)
            self.assertNotIn("LORA FLOWTRON", text.upper().replace("LRA FLOWTRON", ""), path)
            self.assertIn("LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM", text, path)
            self.assertIn("AXIS Micro BBS", text, path)

    def test_the_scripts_first_screen(self):
        for path in SCRIPTS:
            text = readable(path)
            self.assertIn("YOU NEED AN SD CARD FOR FULL FUNCTIONS", text, path)
            self.assertIn("no-sd", text, path)
            self.assertIn("by Rev. J. Money / jmscnc.com", text, path)
            self.assertIn("Radio images come with the first LFD3K-SM release.", text, path)
            self.assertIn("macOS is not supported", text, path)
            self.assertIn("platformio==6.1.18", text, path)
            self.assertIn("esptool==" + F.ESPTOOL_VERSION, text, path)

    def test_install_ps1_is_plain_ascii_without_bom(self):
        with open(os.path.join(ROOT, "site", "install.ps1"), "rb") as fh:
            data = fh.read()
        self.assertFalse(data.startswith(b"\xef\xbb\xbf"))
        data.decode("ascii")


if __name__ == "__main__":
    unittest.main()
