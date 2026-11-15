# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Flash AXIS Micro BBS or the LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM from a PC.

The build scripts (site/install.sh, site/install.ps1) call this after a build. It decides the
same way as the installer page (site/installer-logic.js planFlash and guardRegions): an update
writes the app only and erases nothing; a first install happens only after the warning and a
typed confirm, erases the data partitions of the image's table (never one a written image covers,
never below 0x9000) and writes the boot loader, the table, otadata and the app. Both are tested
against installer/test-fixtures/flash-plans.json (tools/test_axis_flash.py).

    python -I tools/axis_flash.py ports
    python -I tools/axis_flash.py probe --kind board|radio [--radio v3|v4] [--port P] --state FILE
    python -I tools/axis_flash.py manifest --kind board|radio --build DIR --out DIR [--env E]
    python -I tools/axis_flash.py flash --kind board|radio --images DIR [--port P] [--headless]
    (probe and flash also take --dry-run, with --device-info FILE for a pretend board)

Talks to the board through esptool 4.9.0 (the build scripts install it). Never erases the chip.
Standard library only, apart from esptool and pyserial on the real path.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

BOARD_NAME = "AXIS Micro BBS"
MODEM_NAME = "LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM"
FIRST_INSTALL_WARNING = ("This replaces everything on this board. "
                         "Whatever it runs now, and its settings, will be gone.")
CONFIRM_WORD = "REPLACE"
MULTIMODE_MARKER = "picker_nvs"  # only the LFD3K-SM multimode layout has this partition
STAGES = ["BL4Z1NG BYT3S", "FL0WTR0N ENGAGED", "S0N1C B00M", "DATAMASTER WRITING"]
FIRST_ROLES = ["bootloader", "partitions", "otadata", "app"]
PROJECT = {"board": "axis-bbs", "radio": "lfd3k_sm"}
CHIP = {"board": "ESP32", "radio": "ESP32-S3"}
MB = 1024 * 1024
RADIO_FLASH = {"v3": 8 * MB, "v4": 16 * MB}
ENV_FLASH = {"heltec_v4": 16 * MB, "heltec_v4_single": 16 * MB, "heltec_v3_single": 8 * MB}
BOOTLOADER_OFFSET = {"ESP32": 0x1000, "ESP32-S3": 0x0}
ESPTOOL_VERSION = "4.9.0"
FLASH_BAUD = 460800
CONSOLE_BAUD = 115200
HEADLESS_ANSWER = re.compile(r"axis: (saved, restarting|headless SAVE FAILED)")


class Refused(Exception):
    """guard_regions found something it will not write."""


class Stopped(Exception):
    """The run stops with this message (nothing more is done)."""


# == what is on the board (same as installer-logic.js) ==

def _u32(b, i):
    return b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)


def _cstring(b, start, length):
    s = ""
    for i in range(start, min(start + length, len(b))):
        if b[i] == 0:
            break
        if b[i] < 32 or b[i] > 126:
            return None
        s += chr(b[i])
    return s


def parse_table(data):
    """The partition table (the bytes at 0x8000) as entries, or None when there is none."""
    if not data:
        return None
    out = []
    for i in range(0, len(data) - 31, 32):
        if data[i] != 0xAA or data[i + 1] != 0x50:  # 0xEBEB (the MD5 entry) or 0xFFFF ends it
            break
        out.append({"type": data[i + 2], "subtype": data[i + 3], "offset": _u32(data, i + 4),
                    "size": _u32(data, i + 8), "label": _cstring(data, i + 12, 16) or "",
                    "flags": _u32(data, i + 28)})
    return out or None


def tables_equal(a, b):
    if not a or not b or len(a) != len(b):
        return False
    keys = ("type", "subtype", "offset", "size", "label")
    return all(x[k] == y[k] for x, y in zip(a, b) for k in keys)


def is_multimode(entries):
    return bool(entries) and any(e["label"] == MULTIMODE_MARKER for e in entries)


def parse_app_header(data):
    """The program in an app image: {project, version} from esp_app_desc_t, or None."""
    if not data or len(data) < 0x70 or data[0] != 0xE9:
        return None
    if _u32(data, 0x20) != 0xABCD5432:
        return None
    project, version = _cstring(data, 0x50, 32), _cstring(data, 0x30, 32)
    if not project:
        return None
    return {"project": project, "version": version or ""}


def radio_env(model, table_bytes):
    """Which LFD3K-SM image a radio gets: the multimode slot only on a V4 that has that layout."""
    if model == "v3":
        return "heltec_v3_single"
    return "heltec_v4" if is_multimode(parse_table(table_bytes)) else "heltec_v4_single"


# == the flash decision (same as planFlash) ==

def _chip_what(chip):
    if chip == "ESP32":
        return "a CYD or bare ESP32 BBS board"
    if chip == "ESP32-S3":
        return "a Heltec V3 or V4 radio"
    return "another kind of board"


def _refuse(code, reason):
    return {"kind": "refuse", "code": code, "erase": [], "regions": [], "reason": reason + " Nothing was written."}


def _radio_model(size):
    return {8 * MB: "Heltec V3", 16 * MB: "Heltec V4"}.get(size)


def _size_text(size):
    return ("%g MB" % (size / MB)) if size else "an unknown size"


def _by_label(entries, label):
    for e in entries or []:
        if e["label"] == label:
            return e
    return None


def _app_image(images):
    for img in images:
        if img["role"] == "app":
            return img
    return None


def _data_partitions(entries):
    return [{"label": e["label"], "offset": e["offset"], "size": e["size"]} for e in entries if e["type"] == 1]


def _overlaps(a, b):
    return a["offset"] < b["offset"] + b["size"] and b["offset"] < a["offset"] + a["size"]


def _inside(r, p):
    return r["offset"] >= p["offset"] and r["offset"] + r["size"] <= p["offset"] + p["size"]


def plan_flash(device, target):
    """device: {chip, flash_size (bytes or None), part_table (bytes at 0x8000), app_header (bytes at the
    target's app offset), bootloader_sha256 (optional)}. target: {kind "board"|"radio", chip, project,
    layout "single"|"multimode", slot, flash_size, part_table (the image's own table, bytes), images}.
    Returns {kind "update"|"first"|"refuse", code, erase, regions, reason}, as planFlash does."""
    name = MODEM_NAME if target["kind"] == "radio" else BOARD_NAME
    if device["chip"] != target["chip"]:
        return _refuse("chip", "This is an %s (%s). The %s image is for an %s (%s)." % (
            device["chip"], _chip_what(device["chip"]), name, target["chip"], _chip_what(target["chip"])))
    if target["kind"] == "radio" and not target.get("flash_size"):
        return _refuse("flash-size", "This radio image does not say which flash size it is for, so the installer "
                       "cannot tell a Heltec V3 (8 MB) from a Heltec V4 (16 MB).")
    if target.get("flash_size") and device.get("flash_size") != target["flash_size"]:
        is_model = _radio_model(device.get("flash_size"))
        for_model = _radio_model(target["flash_size"]) or _size_text(target["flash_size"])
        return _refuse("flash-size", "This radio has %s of flash%s. The image you picked is for the %s (%s). Pick %s "
                       "(a Heltec V3 has 8 MB, a Heltec V4 has 16 MB)." % (
                           _size_text(device.get("flash_size")), (", so it is a " + is_model) if is_model else "",
                           for_model, _size_text(target["flash_size"]), is_model or "the radio you have"))
    want = parse_table(target["part_table"])
    if not want:
        raise ValueError("the image has no partition table")
    app = _app_image(target["images"])
    if not app:
        raise ValueError("the image has no app")
    part = None
    for e in want:
        if e["type"] == 0 and e["offset"] == app["offset"]:
            part = e
    if not part:
        raise ValueError("the app offset is not an app partition in the image's table")
    if target.get("layout") == "multimode" and (not target.get("slot") or part["label"] != target["slot"]):
        raise ValueError("the modem slot named by the image is not where its app goes")
    if app["size"] > part["size"]:
        return _refuse("too-big", "The firmware is %d bytes and its partition holds %d." % (app["size"], part["size"]))
    have = parse_table(device.get("part_table"))
    same = tables_equal(have, want)

    if target.get("layout") == "multimode":
        if same:
            return {"kind": "update", "code": "update", "erase": [], "regions": [dict(app)],
                    "reason": "This V4 has the multimode layout: only the modem slot is written. The picker, the other "
                              "modes and the modem's settings stay."}
        if is_multimode(have):
            return _refuse("slot", "This V4's multimode layout puts the modem slot somewhere other than this image "
                           "expects. Set up the current multimode layout with the steps in the lfd3k-sm repo first.")
        return _refuse("no-multimode", "This V4 does not have the multimode layout, so the modem slot image cannot go "
                       "on it. Pick the single-mode modem, or set up the multimode layout with the steps in the "
                       "lfd3k-sm repo first.")

    if is_multimode(have):
        return _refuse("multimode-layout", "This radio has the multimode layout (the picker and its modes). A "
                       "single-mode image would wipe it. Pick Heltec V4 and the installer writes only the modem slot.")

    hdr = parse_app_header(device.get("app_header"))
    if same and hdr and hdr["project"] == target["project"]:
        return {"kind": "update", "code": "update", "erase": [], "regions": [dict(app)],
                "reason": "This board already runs %s (%s) with the same partition layout: only the firmware is "
                          "written. %s" % (name, hdr["version"] or "unknown version",
                                           "Its settings stay." if target["kind"] == "radio"
                                           else "Users, messages and settings stay.")}
    if not have:
        why = "This board has no partition table (it is blank or new)."
    elif not same:
        why = "This board has a different partition layout%s." % ((" and runs " + hdr["project"]) if hdr else "")
    else:
        why = ("This board runs %s, not %s." % (hdr["project"], name)) if hdr else \
            "This board has the same partition layout but no %s program in it." % name
    regions = []
    for img in sorted(target["images"], key=lambda i: i["offset"]):
        if img["role"] not in FIRST_ROLES:
            continue
        if img["role"] == "partitions" and same:
            continue  # already this table
        if img["role"] == "bootloader" and device.get("bootloader_sha256") and \
                str(device["bootloader_sha256"]).lower() == str(img["sha256"]).lower():
            continue  # already this boot loader
        regions.append(dict(img))
    # erase the data partitions a written image does not already replace (the otadata image covers otadata)
    erase = [e for e in _data_partitions(want) if not any(_overlaps(e, r) for r in regions)]
    return {"kind": "first", "code": "first", "erase": erase, "regions": regions,
            "reason": why + " " + FIRST_INSTALL_WARNING}


def _refused(why):
    raise Refused("refused: " + why)


def guard_regions(plan, device_table, target):
    """Last check right before anything is erased or written, independent of plan_flash (as guardRegions).
    device_table: the board's parsed table (entries) or None. Raises Refused."""
    if not plan or plan.get("kind") not in ("update", "first"):
        _refused("nothing to write")
    want = parse_table(target["part_table"])
    if not want:
        _refused("the image has no partition table")
    app = _app_image(target["images"])
    if not app:
        _refused("the image has no app")
    app_part = None
    for e in want:
        if e["type"] == 0 and e["offset"] == app["offset"]:
            app_part = e
    if not app_part:
        _refused("the app is not in an app partition of the image's table")
    erase = plan.get("erase") or []

    if plan["kind"] == "update":
        if erase:
            _refused("an update never erases")
        if len(plan["regions"]) != 1:
            _refused("an update writes one image")
        r = plan["regions"][0]
        if r["role"] != "app" or r["offset"] != app["offset"]:
            _refused("an update writes the app only, at its own offset")
        if target.get("layout") == "multimode":
            slot, mine = _by_label(want, target.get("slot")), _by_label(device_table, target.get("slot"))
            if not slot or slot is not app_part:
                _refused("the modem slot is not the image's app partition")
            if not mine or mine["type"] != 0 or mine["offset"] != slot["offset"] or mine["size"] != slot["size"]:
                _refused("this board's modem slot differs from the image's")
            if not _inside(r, slot):
                _refused("the modem does not fit its slot")
            return True
        if not any(p["type"] == 0 and p["offset"] == r["offset"] and _inside(r, p) for p in device_table or []):
            _refused("the app does not fit an app partition on this board")
        return True

    if target.get("layout") == "multimode":
        _refused("the multimode layout is never installed from here")
    data = _data_partitions(want)
    for e in erase:
        if not any(d["offset"] == e["offset"] and d["size"] == e["size"] for d in data):
            _refused("erase at 0x%x is not a data partition of the image's table" % e["offset"])
        # the erase rule on its own: never the boot loader or the table, never a written image, never an app
        if e["offset"] < 0x9000:
            _refused("erase at 0x%x is below 0x9000" % e["offset"])
        for r in plan["regions"]:
            if _overlaps(e, r):
                _refused("erase at 0x%x overlaps the %s being written" % (e["offset"], r["role"]))
        for p in want:
            if p["type"] == 0 and _overlaps(e, p):
                _refused("erase at 0x%x overlaps the app partition %s" % (e["offset"], p["label"]))
    ota = None
    for p in want:
        if p["type"] == 1 and p["subtype"] == 0:
            ota = p
    for r in plan["regions"]:
        if r["role"] == "bootloader" and r["offset"] + r["size"] <= 0x8000:
            continue
        if r["role"] == "partitions" and r["offset"] == 0x8000 and r["size"] <= 0xC00:
            continue
        if r["role"] == "otadata" and ota and r["offset"] == ota["offset"] and _inside(r, ota):
            continue
        if r["role"] == "app" and r["offset"] == app_part["offset"] and _inside(r, app_part):
            continue
        _refused("%s at 0x%x is not where the image's table puts it" % (r["role"], r["offset"]))
    return True


# == progress ==

def stage_name(percent):
    return STAGES[max(0, min(3, int(percent // 25)))]


def block_bar(percent, width=30, ascii_only=False):
    p = max(0, min(100, int(percent)))
    full = int(p * width / 100.0 + 0.5)  # halves round up, as Math.round does on the page
    on, off = ("#", ".") if ascii_only else (u"█", u"░")
    return "[" + on * full + off * (width - full) + "] %3d%%" % p


def hex_offset(n):
    return "0x%X" % n


def kb(n):
    return "%d KB" % ((n + 1023) // 1024)


# == manifests: what is written, from a local build or a downloaded release ==

def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(65536), b""):
            h.update(block)
    return h.hexdigest()


def make_manifest(kind, build_dir, out_dir, env=None, release=""):
    """Copy a local build's images to out_dir and write out_dir/manifest.json, in the shape of the
    release manifests (Task 6 for the board, Task 10 for the radio). Offsets come from the build's
    own partitions.bin; the boot loader sits where the chip's ROM loads it."""
    chip = CHIP[kind]
    with open(os.path.join(build_dir, "partitions.bin"), "rb") as fh:
        table_bytes = fh.read()
    table = parse_table(table_bytes)
    if not table:
        raise Stopped("%s has no partition table" % os.path.join(build_dir, "partitions.bin"))
    layout = "multimode" if kind == "radio" and is_multimode(table) else "single"
    apps = sorted((e for e in table if e["type"] == 0), key=lambda e: e["offset"])
    if not apps:
        raise Stopped("the build's partition table has no app partition")
    slot = None
    if layout == "multimode":
        slot = "ota_1"  # lfd3k-sm firmware/platformio.ini: board_build.app_partition_name = ota_1
        app_part = _by_label(table, slot)
        if not app_part or app_part["type"] != 0:
            raise Stopped("the multimode table has no app partition %s" % slot)
    else:
        app_part = apps[0]
    ota = next((e for e in table if e["type"] == 1 and e["subtype"] == 0), None)
    ota_file = next((f for f in ("ota_data_initial.bin", "boot_app0.bin")
                     if os.path.isfile(os.path.join(build_dir, f))), None)
    plan = []
    if layout == "single":
        plan += [("bootloader", "bootloader.bin", BOOTLOADER_OFFSET[chip]), ("partitions", "partitions.bin", 0x8000)]
        if ota and ota_file:
            plan.append(("otadata", ota_file, ota["offset"]))
    plan.append(("app", "firmware.bin", app_part["offset"]))
    os.makedirs(out_dir, exist_ok=True)
    images = []
    for role, name, offset in plan:
        src = os.path.join(build_dir, name)
        if not os.path.isfile(src):
            raise Stopped("the build has no %s" % src)
        shutil.copyfile(src, os.path.join(out_dir, name))
        images.append({"role": role, "file": name, "offset": offset, "size": os.path.getsize(src),
                       "sha256": _sha256(src)})
    if layout == "multimode":
        shutil.copyfile(os.path.join(build_dir, "partitions.bin"), os.path.join(out_dir, "partitions.bin"))
    app = images[-1]
    if app["size"] > app_part["size"]:
        raise Stopped("firmware.bin is %d bytes, %s holds %d" % (app["size"], app_part["label"], app_part["size"]))
    m = {"release": release, "chip": chip, "layout": layout, "table": "partitions.bin",
         "app_partition": {"offset": app_part["offset"], "size": app_part["size"]}, "images": images,
         "built": "locally"}
    if kind == "radio":
        m["env"] = env or ""
        m["flash_size"] = ENV_FLASH.get(env)  # None (refused later) for an env it does not know
        if slot:
            m["slot"] = slot
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(m, fh, indent=1)
        fh.write("\n")
    return m


def load_target(kind, images_dir):
    """The target for plan_flash from images_dir/manifest.json; every image's size and sha256 are checked."""
    with open(os.path.join(images_dir, "manifest.json"), encoding="utf-8") as fh:
        m = json.load(fh)
    images = []
    for img in m["images"]:
        path = os.path.join(images_dir, img["file"])
        if not os.path.isfile(path):
            raise Stopped("%s is missing" % img["file"])
        if os.path.getsize(path) != img["size"]:
            raise Stopped("%s is %d bytes, the manifest says %d" % (img["file"], os.path.getsize(path), img["size"]))
        if _sha256(path) != str(img["sha256"]).lower():
            raise Stopped("%s does not match its sha256" % img["file"])
        images.append(dict(img, path=path))
    table_name = m.get("table") or next((i["file"] for i in m["images"] if i["role"] == "partitions"), None)
    if not table_name:
        raise Stopped("the manifest names no partition table")
    with open(os.path.join(images_dir, table_name), "rb") as fh:
        part_table = fh.read()
    app = _app_image(images)
    if not app:
        raise Stopped("the manifest has no app image")
    if m.get("app_partition") and m["app_partition"]["offset"] != app["offset"]:
        raise Stopped("the manifest's app offset and app partition disagree")
    return {"kind": kind, "chip": m["chip"], "project": PROJECT[kind], "layout": m.get("layout") or "single",
            "slot": m.get("slot"), "flash_size": m.get("flash_size"), "part_table": part_table, "images": images,
            "env": m.get("env", "")}


# == the board, through esptool ==

def parse_flash_id(text):
    """chip, MAC and flash size (bytes) from esptool 4.9 `flash_id` output; unknown values are None."""
    chips = re.findall(r"Detecting chip type\.\.\.\s*(ESP32(?:-[A-Z0-9]+)?)\b", text)
    mac = re.findall(r"^MAC:\s*([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})\s*$", text, re.M)
    size = re.findall(r"Detected flash size:\s*(\d+)MB", text)
    return {"chip": chips[-1] if chips else None, "mac": mac[0].lower() if mac else None,
            "flash_size": int(size[-1]) * MB if size else None}


def _run_esptool(argv, on_line):
    """Run `python -m esptool argv`, passing each output line to on_line; returns (exit code, all output)."""
    env = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUNBUFFERED="1")
    p = subprocess.Popen([sys.executable, "-m", "esptool"] + argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         stdin=subprocess.DEVNULL, env=env)
    lines = []
    for raw in iter(p.stdout.readline, b""):
        line = raw.decode("utf-8", "replace").rstrip("\r\n")
        for part in line.split("\r"):
            lines.append(part)
            on_line(part)
    return p.wait(), "\n".join(lines)


class Board(object):
    """The board on a serial port. Every call is one esptool run; runner(argv, on_line) -> (code, text)."""

    def __init__(self, port, out, runner=None, dry_run=False):
        self.port, self.out, self.dry_run = port, out, dry_run
        self.runner = runner or _run_esptool
        self.commands = []

    def esptool(self, args, before="default_reset", after="no_reset", on_line=None):
        argv = ["--chip", "auto", "--port", self.port, "--baud", str(FLASH_BAUD), "--before", before,
                "--after", after] + args
        self.commands.append(argv)
        if self.dry_run:
            self.out.line("DRY RUN, would run: esptool " + " ".join(argv))
            return ""
        code, text = self.runner(argv, on_line or (lambda s: None))
        if code != 0:
            tail = [s for s in text.splitlines() if s.strip()][-3:]
            raise Stopped("esptool stopped (exit %d): %s" % (code, " / ".join(tail) or "no output"))
        return text

    def identify(self):
        return parse_flash_id(self.esptool(["flash_id"]))

    def read(self, offset, size):
        fd, path = tempfile.mkstemp(prefix="axis-read-", suffix=".bin")
        os.close(fd)
        try:
            self.esptool(["read_flash", "0x%x" % offset, "0x%x" % size, path])
            with open(path, "rb") as fh:
                return fh.read()
        finally:
            os.remove(path)

    def reset(self, mode):
        """Leave the boot loader: hard_reset, watchdog_reset (native USB) or no_reset (replug)."""
        if mode != "no_reset":
            self.esptool(["read_mac"], after=mode)


def reset_mode(target, device):
    """How the board leaves the boot loader after a write, as each board's own build sets it."""
    if target["kind"] == "radio" and target.get("layout") == "multimode":
        return "no_reset"  # lfd3k-sm notes: a reset can count as a crash and open the picker; replug instead
    if device.get("chip") == "ESP32-S3" and device.get("flash_size") == 16 * MB:
        return "watchdog_reset"  # the V4's native USB (lfd3k-sm firmware/platformio.ini after_reset)
    return "hard_reset"


def leave_boot_loader(board, mode, out):
    """After a refusal or a cancel: reset the board out of the boot loader. A failure here is only logged, so the
    reason the run stopped stays the error the person sees."""
    try:
        board.reset(mode)
    except Exception as e:  # the port may be gone; nothing was written either way
        out.line("Could not reset the board (%s). Unplug it and plug it back in." % e)


def read_device(board, target, out):
    """Read the chip, the table at 0x8000, the app header and the boot loader, as the page does.
    Returns (device, plan or None): a wrong chip or radio model is refused before reading more."""
    info = board.identify()
    out.line("Chip: %s, MAC %s, flash %s" % (info["chip"] or "unknown", info["mac"] or "unknown",
                                             _size_text(info["flash_size"]) if info["flash_size"] else "size unknown"))
    device = {"chip": info["chip"], "flash_size": info["flash_size"], "part_table": None, "app_header": None,
              "bootloader_sha256": None, "mac": info["mac"]}
    out.line("Reading the partition table at 0x8000 ...")
    device["part_table"] = board.read(0x8000, 0xC00)
    size_ok = (not target.get("flash_size") or device["flash_size"] == target["flash_size"]) \
        if target["kind"] != "radio" else bool(target.get("flash_size")) and device["flash_size"] == target["flash_size"]
    if device["chip"] != target["chip"] or not size_ok:
        return device, plan_flash(device, target)
    app = _app_image(target["images"])
    device["app_header"] = board.read(app["offset"], 0x100)
    hdr = parse_app_header(device["app_header"])
    out.line("At %s: %s" % (hex_offset(app["offset"]), ("%s %s" % (hdr["project"], hdr["version"])) if hdr
                           else "no program"))
    bl = next((i for i in target["images"] if i["role"] == "bootloader"), None)
    if bl:
        device["bootloader_sha256"] = hashlib.sha256(board.read(bl["offset"], bl["size"])).hexdigest()
    return device, None


def device_from_info(path):
    """A pretend board for --dry-run and the tests: the fixture's device shape."""
    with open(path, encoding="utf-8") as fh:
        d = json.load(fh)
    h = lambda k: bytes.fromhex(d[k]) if d.get(k) else None
    return {"chip": d.get("chip"), "flash_size": d.get("flash_size"), "part_table": h("part_table_hex"),
            "app_header": h("app_header_hex"), "bootloader_sha256": d.get("bootloader_sha256"),
            "mac": d.get("mac", "pretend")}


# == talking to the person ==

class Out(object):
    def __init__(self, stream=None):
        self.stream = stream or sys.stdout
        enc = getattr(self.stream, "encoding", None) or "ascii"
        try:
            u"█░".encode(enc)
            self.ascii_only = False
        except (UnicodeEncodeError, LookupError):
            self.ascii_only = True
        self.tty = hasattr(self.stream, "isatty") and self.stream.isatty()

    def line(self, text):
        self.stream.write(text + "\n")
        self.stream.flush()

    def bar(self, percent, done, total, last=False):
        text = "%s %s  %s of %s" % (block_bar(percent, 30, self.ascii_only), stage_name(percent), kb(done), kb(total))
        if self.tty:
            self.stream.write("\r" + text + ("\n" if last else ""))
            self.stream.flush()
        else:
            self.line(text)


def ask_terminal(prompt):
    """Ask on the person's terminal and read one typed line from it, never from stdin or stdout (under
    `curl | sh` stdin is the script itself, and the build scripts pass stdout through). None when there
    is no terminal."""
    try:
        if os.name == "nt":
            with open("CONOUT$", "w") as con_out, open("CONIN$", "r") as con_in:
                con_out.write(prompt)
                con_out.flush()
                return con_in.readline().strip()
        with open("/dev/tty", "r+") as tty:
            tty.write(prompt)
            tty.flush()
            return tty.readline().strip()
    except OSError:
        return None


def confirm_first(plan, kind, out, ask):
    out.line("")
    out.line("FIRST INSTALL")
    out.line(FIRST_INSTALL_WARNING)
    out.line(plan["reason"].replace(FIRST_INSTALL_WARNING, "").strip())
    out.line("It erases " + ", ".join("%s at %s" % (e["label"], hex_offset(e["offset"])) for e in plan["erase"]) +
             ", then writes " + ", ".join("%s at %s" % (r["role"], hex_offset(r["offset"])) for r in plan["regions"]) + ".")
    if kind == "board":
        out.line("After it boots, the board runs its setup wizard. For full functions it needs an SD card: the SD card "
                 "kit on the installer page has the starting files.")
    answer = ask("Type %s and press Enter to go ahead, or just press Enter to stop: " % CONFIRM_WORD)
    if answer is None:
        out.line("There is no terminal to type the confirm in, so the first install does not go ahead.")
        return False
    return answer == CONFIRM_WORD


# == the write ==

def _write_with_progress(board, files, after, out):
    """One esptool write_flash for every file; the bar, the stage names and the log follow its output."""
    total = sum(f["size"] for f in files) or 1
    before = []
    acc = 0
    for f in files:
        before.append(acc)
        acc += f["size"]
    state = {"current": -1, "tenth": -1, "verified": 0}

    def which(addr):
        best = 0
        for i, f in enumerate(files):
            if f["offset"] <= addr:
                best = i
        return best

    def on_line(line):
        m = re.search(r"Writing at 0x([0-9a-fA-F]+)\.\.\. \((\d+) ?%\)", line)
        if m:
            i = which(int(m.group(1), 16))
            if i != state["current"]:
                state["current"] = i
                f = files[i]
                out.line("Writing %s (%s, %s) at %s ..." % (f["role"], f["file"], kb(f["size"]), hex_offset(f["offset"])))
            done = before[i] + files[i]["size"] * min(100, int(m.group(2))) // 100
            pct = 100.0 * done / total
            tenth = int(pct // 10)
            if out.tty or tenth != state["tenth"]:
                state["tenth"] = tenth
                out.bar(pct, done, total)
            return
        if "Hash of data verified" in line:
            state["verified"] += 1
            if out.tty:
                out.line("")
            out.line("Hash of data verified.")
        elif line.startswith("Wrote ") or "Compressed " in line:
            out.line(line.strip())

    argv = ["write_flash", "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"]
    for f in files:
        argv += ["0x%x" % f["offset"], f["path"]]
    board.esptool(argv, after=after, on_line=on_line)
    if not board.dry_run:
        out.bar(100, total, total, last=True)
        if state["verified"] != len(files):
            raise Stopped("esptool checked %d of %d images" % (state["verified"], len(files)))


def send_headless(port, out, opener=None, seconds=45):
    """Bare ESP32: send @HEADLESS on at 115200 until the board answers (as the page does)."""
    if opener is None:
        import serial  # pyserial, installed with esptool
        def opener():
            s = serial.Serial()  # DTR and RTS low before the port opens, so opening it does not reset the board
            s.port, s.baudrate, s.timeout = port, CONSOLE_BAUD, 0.2
            s.dtr = False
            s.rts = False
            s.open()
            return s
    s = opener()
    try:
        try:
            s.dtr = False
            s.rts = False
        except Exception:
            pass
        seen, end, next_send = "", time.time() + seconds, 0
        while time.time() < end:
            if time.time() >= next_send:
                s.write(b"\r\n@HEADLESS on\r\n")
                next_send = time.time() + 3
            seen += s.read(256).decode("utf-8", "replace")
            m = HEADLESS_ANSWER.search(seen)
            if m:
                return m.group(0)
            seen = seen[-4000:]
        return None
    finally:
        s.close()


def flash(kind, images_dir, port, out, headless=False, dry_run=False, device_info=None, runner=None,
          ask=ask_terminal, headless_opener=None):
    """Decide and write, as the page's flashJob does. Returns 0 done, 2 refused or cancelled; raises Stopped."""
    name = MODEM_NAME if kind == "radio" else BOARD_NAME
    target = load_target(kind, images_dir)
    out.line("FLASHING: %s%s" % (name, (", " + target["env"]) if target.get("env") else ""))
    for img in target["images"]:
        out.line("Image %s (%s) at %s, sha256 checked" % (img["file"], kb(img["size"]), hex_offset(img["offset"])))
    if device_info and not dry_run:
        raise Stopped("a pretend board (the device-info option) works with a dry run only")
    board = Board(port or "PORT", out, runner=runner, dry_run=dry_run)
    if device_info:
        device = device_from_info(device_info)
        out.line("Pretend board from %s: %s, flash %s" % (device_info, device["chip"], _size_text(device["flash_size"])))
        plan = None
    elif dry_run:
        out.line("DRY RUN: would read the chip, its MAC and flash size, the table at 0x8000, the app header and the "
                 "boot loader on %s, then decide update or first install as the installer page does." % board.port)
        board.identify()
        return 0
    else:
        out.line("Connecting to the boot loader on %s (hold BOOT if it does not answer) ..." % port)
        try:
            device, plan = read_device(board, target, out)
        except Stopped as e:
            raise Stopped("%s. Nothing was written. Unplug the board, plug it back in and run this again." % e)
    if plan is None:
        plan = plan_flash(device, target)
    mode = reset_mode(target, device)
    out.line(plan["reason"])
    if plan["kind"] == "refuse":
        out.line("REFUSED: nothing was written.")
        if not device_info:
            leave_boot_loader(board, reset_mode(target, device), out)  # back to what it ran before
        return 2
    if plan["kind"] == "first":
        if dry_run:
            out.line("")
            out.line("FIRST INSTALL")
            out.line(FIRST_INSTALL_WARNING)
            out.line("DRY RUN: the typed confirm (%s) is asked here; a dry run never writes." % CONFIRM_WORD)
        elif not confirm_first(plan, kind, out, ask):
            out.line("Cancelled. Nothing was written.")
            if not device_info:
                leave_boot_loader(board, reset_mode(target, device), out)
            return 2
        else:
            out.line("Confirmed: replacing everything on this board.")
    try:
        guard_regions(plan, parse_table(device.get("part_table")), target)
    except (Refused, ValueError):
        if not device_info and not dry_run:
            leave_boot_loader(board, reset_mode(target, device), out)  # as after a declined confirm
        raise
    by_key = {(i["role"], i["offset"]): i for i in target["images"]}
    files = [by_key[(r["role"], r["offset"])] for r in plan["regions"]]
    wrote = False
    try:
        for e in plan["erase"]:
            out.line("Erasing %s (%s) at %s ..." % (e["label"], kb(e["size"]), hex_offset(e["offset"])))
            wrote = True
            board.esptool(["erase_region", "0x%x" % e["offset"], "0x%x" % e["size"]])
        wrote = True
        _write_with_progress(board, files, mode, out)
    except Stopped as e:
        if wrote and not dry_run:
            raise Stopped("%s. The write did not finish, so the board may not start. Unplug it, plug it back in and "
                          "run this again." % e)
        raise
    if dry_run:
        out.line("DRY RUN: nothing was written.")
        return 0
    if mode == "no_reset":
        out.line("Unplug the radio and plug it back in to start the modem.")
    else:
        out.line("Resetting the %s ..." % ("radio" if kind == "radio" else "board"))
    if headless:
        out.line("Bare ESP32: sending @HEADLESS on at %d baud once it boots ..." % CONSOLE_BAUD)
        try:
            answer = send_headless(port, out, opener=headless_opener)
        except Exception as e:  # the firmware is written; only this step failed
            answer = None
            out.line("Could not open %s: %s" % (port, e))
        if answer and "saved" in answer:
            out.line("The board answered: %s. It stays headless through later flashes." % answer)
        else:
            out.line("The board did not confirm headless. Open a serial terminal at 115200 baud and type @HEADLESS on")
    if kind == "board":
        out.line("DONE. Your AXIS Micro BBS restarts with its users, messages and settings." if plan["kind"] == "update"
                 else "DONE. Your AXIS Micro BBS starts its setup wizard: join its AXIS-SETUP hotspot to set it up.")
    else:
        out.line("DONE. The %s is restarting." % MODEM_NAME)
    return 0


def probe(kind, port, out, radio=None, dry_run=False, device_info=None, runner=None):
    """Before a build: the right chip (and for a radio the right model)? Returns (code, state dict).
    The state holds the port and, for a radio, which image it gets (radio_env)."""
    want = {"kind": kind, "chip": CHIP[kind], "flash_size": RADIO_FLASH.get(radio) if kind == "radio" else None}
    if device_info and not dry_run:
        raise Stopped("a pretend board (the device-info option) works with a dry run only")
    board = Board(port or "PORT", out, runner=runner, dry_run=dry_run)
    if device_info:
        device = device_from_info(device_info)
    elif dry_run:
        out.line("DRY RUN: would read the chip, its MAC, its flash size and the table at 0x8000 on %s." % board.port)
        board.identify()
        return 0, {"port": board.port, "env": radio_env(radio, None) if kind == "radio" else "cyd"}
    else:
        out.line("Connecting to the boot loader on %s (hold BOOT if it does not answer) ..." % port)
        info = board.identify()
        device = {"chip": info["chip"], "flash_size": info["flash_size"], "mac": info["mac"]}
        out.line("Chip: %s, MAC %s, flash %s" % (info["chip"] or "unknown", info["mac"] or "unknown",
                                                 _size_text(info["flash_size"]) if info["flash_size"] else "size unknown"))
        device["part_table"] = board.read(0x8000, 0xC00)
    state = {"port": board.port}
    code = 0
    plan = None
    if device.get("chip") != want["chip"] or (kind == "radio" and device.get("flash_size") != want["flash_size"]):
        plan = plan_flash(device, dict(want, project=PROJECT[kind], images=[], part_table=None))
        out.line(plan["reason"])
        out.line("REFUSED: nothing was built or written.")
        code = 2
    elif kind == "radio":
        state["env"] = radio_env(radio, device.get("part_table"))
        out.line("Radio image: %s%s" % (state["env"], " (this V4 has the multimode layout: the modem slot only)"
                                        if state["env"] == "heltec_v4" else ""))
    else:
        state["env"] = "cyd"
    if not device_info and not dry_run:
        # a multimode V4 (its table has the picker) is left in the boot loader, never reset into the picker
        layout = "multimode" if kind == "radio" and is_multimode(parse_table(device.get("part_table"))) else "single"
        leave_boot_loader(board, reset_mode({"kind": kind, "layout": layout}, device), out)
        if layout == "multimode":
            out.line("Unplug the radio and plug it back in before the flash step.")
    return code, state


def list_ports():
    try:
        from serial.tools import list_ports as lp
    except ImportError:
        return []
    return sorted(p.device for p in lp.comports())


def pick_port(out, ask=ask_terminal):
    ports = list_ports()
    if len(ports) == 1:
        out.line("Using the only serial port: %s" % ports[0])
        return ports[0]
    if not ports:
        raise Stopped("No serial port found. Plug the board in with a data USB cable, or name it with the port option.")
    out.line("Serial ports: " + ", ".join(ports))
    answer = ask("Which port is the board on? ")
    if answer not in ports:
        raise Stopped("No port picked. Run again and name it with the port option.")
    return answer


def _check_esptool(out):
    try:
        import esptool
    except ImportError:
        raise Stopped("esptool is not installed in this Python. The build scripts install esptool %s." % ESPTOOL_VERSION)
    if getattr(esptool, "__version__", "") != ESPTOOL_VERSION:
        raise Stopped("esptool %s is needed here, this Python has %s." % (ESPTOOL_VERSION, esptool.__version__))


def _write_state(path, state):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        for k in sorted(state):
            fh.write("%s=%s\n" % (k, state[k]))


def main(argv=None, out=None, runner=None, ask=ask_terminal):
    try:
        sys.stdout.reconfigure(errors="replace")
    except (AttributeError, ValueError):
        pass
    out = out or Out()
    ap = argparse.ArgumentParser(prog="axis_flash", description="Flash AXIS Micro BBS or the " + MODEM_NAME + ".")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ports")
    p = sub.add_parser("probe")
    p.add_argument("--kind", choices=["board", "radio"], required=True)
    p.add_argument("--radio", choices=["v3", "v4"])
    p.add_argument("--port")
    p.add_argument("--state", required=True)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--device-info")
    m = sub.add_parser("manifest")
    m.add_argument("--kind", choices=["board", "radio"], required=True)
    m.add_argument("--build", required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--env")
    m.add_argument("--release", default="")
    f = sub.add_parser("flash")
    f.add_argument("--kind", choices=["board", "radio"], required=True)
    f.add_argument("--images", required=True)
    f.add_argument("--port")
    f.add_argument("--headless", action="store_true")
    f.add_argument("--dry-run", action="store_true")
    f.add_argument("--device-info")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "ports":
            for name in list_ports():
                out.line(name)
            return 0
        if args.cmd == "manifest":
            got = make_manifest(args.kind, args.build, args.out, env=args.env, release=args.release)
            out.line("Images for the %s in %s: %s" % (MODEM_NAME if args.kind == "radio" else BOARD_NAME, args.out,
                                                      ", ".join("%s at %s" % (i["file"], hex_offset(i["offset"]))
                                                                for i in got["images"])))
            return 0
        if args.device_info and not args.dry_run:
            # a pretend board must never decide what is written to a real one
            raise Stopped("a pretend board (the device-info option) works with a dry run only")
        live = not args.dry_run
        if live:
            _check_esptool(out)
        port = args.port
        if live and not port:
            port = pick_port(out, ask)
        if args.cmd == "probe":
            if args.kind == "radio" and not args.radio:
                raise Stopped("A radio needs its model: v3 or v4.")
            code, state = probe(args.kind, port, out, radio=args.radio, dry_run=args.dry_run,
                                device_info=args.device_info, runner=runner)
            _write_state(args.state, state)
            return code
        return flash(args.kind, args.images, port, out, headless=args.headless, dry_run=args.dry_run,
                     device_info=args.device_info, runner=runner, ask=ask)
    except (Stopped, Refused, ValueError, OSError) as e:
        out.line("STOPPED: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
