"""Writes installer/test-fixtures/flash-plans.json, the shared flash decisions.

The page (site/installer-logic.js planFlash, node --test) and the PC scripts (Task 9, Python) both test
against that file, so they decide the same way for the same board. Everything here is self-contained:
partition tables are built from the layouts below (the same bytes gen_esp32part writes: 32-byte
entries, then the MD5 entry), app headers are minimal ESP image headers with esp_app_desc_t. The
expected results are written by hand; this script never computes them.

Run: python -I tools/gen_flashplans.py   (then node --test site/installer-logic.test.mjs)
"""
import hashlib
import json
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "installer", "test-fixtures", "flash-plans.json")
MB = 1024 * 1024

TYPES = {"app": 0, "data": 1}
SUB = {"factory": 0, "ota_0": 0x10, "ota_1": 0x11, "ota_2": 0x12, "ota_3": 0x13, "ota": 0, "nvs": 2,
       "coredump": 3, "spiffs": 0x82, "littlefs": 0x83, "0x40": 0x40}


def table(rows):
    b = b""
    for name, t, s, off, size in rows:
        b += struct.pack("<2sBBII16sI", b"\xaa\x50", TYPES[t], SUB[s], off, size, name.encode(), 0)
    return b + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(b).digest()


def app_header(project, version):
    """ESP image header (magic 0xE9) and esp_app_desc_t: magic at 0x20, version at 0x30, project at 0x50."""
    b = bytearray(b"\xff" * 256)
    b[0:8] = bytes([0xE9, 0x05, 0x02, 0x2F, 0, 0, 0, 0])
    b[0x18:0x20] = b"\x00" * 8
    b[0x20:0x24] = struct.pack("<I", 0xABCD5432)
    b[0x24:0x30] = b"\x00" * 12
    b[0x30:0x50] = version.encode().ljust(32, b"\x00")
    b[0x50:0x70] = project.encode().ljust(32, b"\x00")
    return bytes(b)


# the AXIS board (partitions.csv)
AXIS_ROWS = [("nvs", "data", "nvs", 0x9000, 0x5000), ("otadata", "data", "ota", 0xE000, 0x2000),
             ("app0", "app", "ota_0", 0x10000, 0x240000), ("spiffs", "data", "spiffs", 0x250000, 0x1B0000)]
# the LFD3K-SM single-mode modem (lfd3k-sm firmware/partitions.csv)
SINGLE_ROWS = [("nvs", "data", "nvs", 0x9000, 0x5000), ("otadata", "data", "ota", 0xE000, 0x2000),
               ("ota_0", "app", "ota_0", 0x10000, 0x180000), ("ota_1", "app", "ota_1", 0x190000, 0x180000),
               ("storage", "data", "littlefs", 0x310000, 0x100000)]
# the LFD3K-SM multimode layout (lfd3k-sm multimode/partitions-multimode.csv); the modem is ota_1
MM_ROWS = [("nvs", "data", "nvs", 0x9000, 0xF000), ("otadata", "data", "ota", 0x18000, 0x2000),
           ("picker_nvs", "data", "nvs", 0x1A000, 0x6000), ("factory", "app", "factory", 0x20000, 0x80000),
           ("ota_0", "app", "ota_0", 0xA0000, 0x400000), ("ota_1", "app", "ota_1", 0x4A0000, 0x180000),
           ("ota_2", "app", "ota_2", 0x620000, 0x180000), ("ota_3", "app", "ota_3", 0x7A0000, 0x200000),
           ("lfd3k_nvs", "data", "nvs", 0x9A0000, 0x10000), ("spiffs", "data", "spiffs", 0x9B0000, 0x200000),
           ("littlefs", "data", "spiffs", 0xBB0000, 0x100000), ("camdb", "data", "0x40", 0xCB0000, 0x320000),
           ("coredump", "data", "coredump", 0xFD0000, 0x10000)]
# an older multimode layout: the modem slot is smaller
OLD_MM_ROWS = [r if r[0] != "ota_1" else ("ota_1", "app", "ota_1", 0x4A0000, 0x100000) for r in MM_ROWS]
ARDUINO_ROWS = [("nvs", "data", "nvs", 0x9000, 0x5000), ("otadata", "data", "ota", 0xE000, 0x2000),
                ("app0", "app", "ota_0", 0x10000, 0x140000), ("app1", "app", "ota_1", 0x150000, 0x140000),
                ("spiffs", "data", "spiffs", 0x290000, 0x160000), ("coredump", "data", "coredump", 0x3F0000, 0x10000)]
OLD_AXIS_ROWS = [("nvs", "data", "nvs", 0x9000, 0x5000), ("otadata", "data", "ota", 0xE000, 0x2000),
                 ("app0", "app", "ota_0", 0x10000, 0x1E0000), ("door", "data", "spiffs", 0x1F0000, 0x60000),
                 ("spiffs", "data", "spiffs", 0x250000, 0x1B0000)]

AXIS_TABLE, SINGLE_TABLE, MM_TABLE = table(AXIS_ROWS), table(SINGLE_ROWS), table(MM_ROWS)
OLD_MM_TABLE, ARDUINO_TABLE, OLD_AXIS_TABLE = table(OLD_MM_ROWS), table(ARDUINO_ROWS), table(OLD_AXIS_ROWS)
AXIS_APP = app_header("axis-bbs", "v0.61")
MODEM_APP = app_header("lfd3k_sm", "4be192f")
FOREIGN_APP = app_header("arduino-lib-builder", "esp-idf: v4.4.7")
MESH_APP = app_header("meshtastic", "2.7.26")
BLANK = b"\xff" * 64
BLANK_APP = b"\xff" * 256

h = lambda b: b.hex()
BL_SHA = "20b967a0a5c276f79d2e1fe999d4afa607bfa46abf6f4ca0c08496f779d11b66"


def single_radio(seed, flash_size):
    return [
        {"role": "bootloader", "file": "bootloader.bin", "offset": 0x0, "size": 20000, "sha256": seed[0] * 64},
        {"role": "partitions", "file": "partitions.bin", "offset": 0x8000, "size": 3072, "sha256": seed[1] * 64},
        {"role": "otadata", "file": "ota_data_initial.bin", "offset": 0xE000, "size": 8192, "sha256": seed[2] * 64},
        {"role": "app", "file": "firmware.bin", "offset": 0x10000, "size": 1000000, "sha256": seed[3] * 64}]


TARGETS = {
    "axis-board": {
        "about": "AXIS Micro BBS board image (a Task 6 manifest; table = its partitions.bin)",
        "kind": "board", "chip": "ESP32", "project": "axis-bbs", "layout": "single",
        "part_table_hex": h(AXIS_TABLE),
        "images": [
            {"role": "bootloader", "file": "bootloader.bin", "offset": 0x1000, "size": 27248, "sha256": BL_SHA},
            {"role": "partitions", "file": "partitions.bin", "offset": 0x8000, "size": 3072, "sha256": "0e737991d33b2886bd0e71a1c6a7584a6b61ebf98dce41093915d726a31e2eb6"},
            {"role": "otadata", "file": "ota_data_initial.bin", "offset": 0xE000, "size": 8192, "sha256": "7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f"},
            {"role": "app", "file": "firmware.bin", "offset": 0x10000, "size": 1904544, "sha256": "815d4c829d2e8e11dca83abf7a051ff98d99fd7f4bbe7c11f4fcede17e55fd71"}]},
    "axis-board-too-big": {
        "about": "the same board image with an app larger than app0 (0x240000)",
        "kind": "board", "chip": "ESP32", "project": "axis-bbs", "layout": "single",
        "part_table_hex": h(AXIS_TABLE),
        "images": [
            {"role": "bootloader", "file": "bootloader.bin", "offset": 0x1000, "size": 27248, "sha256": BL_SHA},
            {"role": "partitions", "file": "partitions.bin", "offset": 0x8000, "size": 3072, "sha256": "00" * 32},
            {"role": "app", "file": "firmware.bin", "offset": 0x10000, "size": 0x240001, "sha256": "00" * 32}]},
    "radio-v4-multimode": {
        "about": "LFD3K-SM heltec_v4: the modem slot (slot ota_1) of the multimode layout; only the app image is written",
        "kind": "radio", "chip": "ESP32-S3", "project": "lfd3k_sm", "layout": "multimode", "slot": "ota_1",
        "flash_size": 16 * MB, "part_table_hex": h(MM_TABLE),
        "images": [{"role": "app", "file": "firmware.bin", "offset": 0x4A0000, "size": 1059504, "sha256": "810cc6fc005519796545ec09d1668c481416bd99848b83c237062072b7c68232"}]},
    "radio-v4-single": {
        "about": "LFD3K-SM heltec_v4_single (firmware/partitions.csv), a 16 MB Heltec V4",
        "kind": "radio", "chip": "ESP32-S3", "project": "lfd3k_sm", "layout": "single", "flash_size": 16 * MB,
        "part_table_hex": h(SINGLE_TABLE), "images": single_radio("1234", 16 * MB)},
    "radio-v3-single": {
        "about": "LFD3K-SM heltec_v3_single (firmware/partitions.csv), an 8 MB Heltec V3",
        "kind": "radio", "chip": "ESP32-S3", "project": "lfd3k_sm", "layout": "single", "flash_size": 8 * MB,
        "part_table_hex": h(SINGLE_TABLE), "images": single_radio("5678", 8 * MB)},
}

ALL_BOARD = [["bootloader", 0x1000], ["partitions", 0x8000], ["otadata", 0xE000], ["app", 0x10000]]
ALL_RADIO = [["bootloader", 0x0], ["partitions", 0x8000], ["otadata", 0xE000], ["app", 0x10000]]
# first installs erase the data partitions of the target's table (hand-written from the layouts above)
ERASE_BOARD = [["nvs", 0x9000, 0x5000], ["otadata", 0xE000, 0x2000], ["spiffs", 0x250000, 0x1B0000]]
ERASE_RADIO = [["nvs", 0x9000, 0x5000], ["otadata", 0xE000, 0x2000], ["storage", 0x310000, 0x100000]]


def case(name, about, chip, flash, table_b, app_b, target, kind, regions, code, erase=(), bl=None):
    d = {"chip": chip, "flash_size": flash, "part_table_hex": h(table_b), "app_header_hex": h(app_b)}
    if bl:
        d["bootloader_sha256"] = bl
    return {"name": name, "about": about, "device": d, "target": target,
            "expect": {"kind": kind, "code": code,
                       "erase": [{"label": l, "offset": o, "size": s} for l, o, s in erase],
                       "regions": [{"role": r, "offset": o} for r, o in regions]}}


CYD, V4, V3 = 4 * MB, 16 * MB, 8 * MB
CASES = [
    case("board-update", "a CYD already on AXIS with the same table: the app only", "ESP32", CYD, AXIS_TABLE, AXIS_APP,
         "axis-board", "update", [["app", 0x10000]], "update"),
    case("board-first-blank", "a brand-new ESP32 (blank flash)", "ESP32", CYD, BLANK, BLANK_APP,
         "axis-board", "first", ALL_BOARD, "first", ERASE_BOARD),
    case("board-first-stock", "a CYD on a stock Arduino sketch and table", "ESP32", CYD, ARDUINO_TABLE, FOREIGN_APP,
         "axis-board", "first", ALL_BOARD, "first", ERASE_BOARD),
    case("board-first-old-layout", "AXIS on an older partition layout: a different table is a first install",
         "ESP32", CYD, OLD_AXIS_TABLE, AXIS_APP, "axis-board", "first", ALL_BOARD, "first", ERASE_BOARD),
    case("board-first-same-table-foreign-app", "the AXIS table but another program in app0: first install; the identical table is not rewritten",
         "ESP32", CYD, AXIS_TABLE, FOREIGN_APP, "axis-board", "first",
         [["bootloader", 0x1000], ["otadata", 0xE000], ["app", 0x10000]], "first", ERASE_BOARD),
    case("board-first-same-bootloader", "a different table but the boot loader on the board is already this one: it is not rewritten",
         "ESP32", CYD, ARDUINO_TABLE, FOREIGN_APP, "axis-board", "first",
         [["partitions", 0x8000], ["otadata", 0xE000], ["app", 0x10000]], "first", ERASE_BOARD, bl=BL_SHA),
    case("board-image-on-radio", "a radio (ESP32-S3) offered the board image", "ESP32-S3", V4, MM_TABLE, MODEM_APP,
         "axis-board", "refuse", [], "chip"),
    case("board-too-big", "an app larger than app0", "ESP32", CYD, AXIS_TABLE, AXIS_APP,
         "axis-board-too-big", "refuse", [], "too-big"),
    case("radio-image-on-cyd", "a CYD (ESP32) offered a radio image", "ESP32", CYD, AXIS_TABLE, AXIS_APP,
         "radio-v3-single", "refuse", [], "chip"),
    case("radio-multimode-on-cyd", "a CYD (ESP32) offered the multimode modem slot", "ESP32", CYD, AXIS_TABLE, AXIS_APP,
         "radio-v4-multimode", "refuse", [], "chip"),
    case("v4-multimode-update", "a V4 with the multimode table: only the modem slot", "ESP32-S3", V4, MM_TABLE, MODEM_APP,
         "radio-v4-multimode", "update", [["app", 0x4A0000]], "update"),
    case("v4-multimode-empty-slot", "a V4 with the multimode table and nothing in the modem slot yet: still only the slot",
         "ESP32-S3", V4, MM_TABLE, BLANK_APP, "radio-v4-multimode", "update", [["app", 0x4A0000]], "update"),
    case("v4-multimode-needs-layout", "the multimode slot image on a V4 without the multimode layout", "ESP32-S3", V4,
         SINGLE_TABLE, MODEM_APP, "radio-v4-multimode", "refuse", [], "no-multimode"),
    case("v4-multimode-other-slot", "a multimode layout whose modem slot differs from the image's", "ESP32-S3", V4,
         OLD_MM_TABLE, MODEM_APP, "radio-v4-multimode", "refuse", [], "slot"),
    case("v3-on-multimode", "the V3 image on a 16 MB multimode V4: the flash size says it is a V4",
         "ESP32-S3", V4, MM_TABLE, MODEM_APP, "radio-v3-single", "refuse", [], "flash-size"),
    case("v4-single-on-multimode", "the single-mode V4 image on a board with the multimode layout: never a first install",
         "ESP32-S3", V4, MM_TABLE, MODEM_APP, "radio-v4-single", "refuse", [], "multimode-layout"),
    case("v4-single-update", "a V4 already on the single-mode modem", "ESP32-S3", V4, SINGLE_TABLE, MODEM_APP,
         "radio-v4-single", "update", [["app", 0x10000]], "update"),
    case("v3-image-on-v4-single", "the V3 image on a V4 running the single-mode modem: same chip and table, 16 MB says V4",
         "ESP32-S3", V4, SINGLE_TABLE, MODEM_APP, "radio-v3-single", "refuse", [], "flash-size"),
    case("v4-image-on-v3-single", "the V4 image on a V3 running the single-mode modem: 8 MB says V3",
         "ESP32-S3", V3, SINGLE_TABLE, MODEM_APP, "radio-v4-single", "refuse", [], "flash-size"),
    case("v3-update", "a V3 already on the single-mode modem", "ESP32-S3", V3, SINGLE_TABLE, MODEM_APP,
         "radio-v3-single", "update", [["app", 0x10000]], "update"),
    case("v3-first-meshtastic", "a V3 on Meshtastic (its own table): first install, boot loader at 0x0", "ESP32-S3", V3,
         ARDUINO_TABLE, MESH_APP, "radio-v3-single", "first", ALL_RADIO, "first", ERASE_RADIO),
    case("v3-first-blank", "a blank V3", "ESP32-S3", V3, BLANK, BLANK_APP, "radio-v3-single", "first", ALL_RADIO,
         "first", ERASE_RADIO),
    case("radio-unknown-flash-size", "a radio whose flash size could not be read", "ESP32-S3", None, SINGLE_TABLE,
         MODEM_APP, "radio-v4-single", "refuse", [], "flash-size"),
]

DOC = {
    "about": ("Shared flash decisions: site/installer-logic.js planFlash (node --test) and Task 9's axis_flash.py read "
              "this file; regenerate with python -I tools/gen_flashplans.py. device: what was read from the board "
              "(chip name as esptool reports it, the flash size in bytes esptool detects or null, the bytes at 0x8000, "
              "the first bytes at the target app image's offset, optionally the sha256 of the boot loader region). "
              "target: a key of targets (a manifest plus its partition table bytes, the project name in the app "
              "header, flash_size for radios, slot for the multimode modem). expect: kind update|first|refuse, a "
              "reason code, the data partitions a first install erases (label, offset, size), the images written "
              "in order (role, offset)."),
    "first_install_warning": "This replaces everything on this board. Whatever it runs now, and its settings, will be gone.",
    "multimode_marker": "picker_nvs",
    "codes": ["update", "first", "chip", "flash-size", "too-big", "no-multimode", "slot", "multimode-layout"],
    "targets": TARGETS,
    "cases": CASES,
}

if __name__ == "__main__":
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(DOC, fh, indent=1)
        fh.write("\n")
    print("flash-plans: %d targets, %d cases -> %s" % (len(TARGETS), len(CASES), OUT))
