"""What each AXIS Micro BBS release image costs, and the manifest the installer reads.

    python -I tools/buildcost.py measure .pio/build/cyd
    python -I tools/buildcost.py manifest .pio/build/cyd OUT_DIR --preset no-doors --release v0.60
    python -I tools/buildcost.py table RELEASE_DIR --release v0.60 --out RELEASE_DIR/costs.json

Flash is the size of firmware.bin (what is written to app0). Static RAM is .dram0.data plus
.dram0.bss, read with xtensa-esp32-elf-size from the PlatformIO toolchain when it is there,
otherwise from the linker map. Standard library only; runs on Windows and Linux.
"""
import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import buildopts  # noqa: E402

CHIP = "ESP32"
BOOTLOADER_OFFSET = 0x1000  # ESP32 second stage bootloader
PARTITIONS_OFFSET = 0x8000  # partition table
DRAM_SECTIONS = (".dram0.data", ".dram0.bss")


def find_size_tool():
    """Path of xtensa-esp32-elf-size, or "" when the toolchain is not installed."""
    exe = "xtensa-esp32-elf-size" + (".exe" if os.name == "nt" else "")
    core = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.join(os.path.expanduser("~"), ".platformio")
    cand = os.path.join(core, "packages", "toolchain-xtensa-esp32", "bin", exe)
    if os.path.isfile(cand):
        return cand
    return shutil.which(exe) or ""


def dram_from_size(text):
    """Static RAM from `size -A` output."""
    got = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] in DRAM_SECTIONS and parts[1].isdigit():
            got[parts[0]] = int(parts[1])
    if set(got) != set(DRAM_SECTIONS):
        raise ValueError("size output has no %s" % " and ".join(DRAM_SECTIONS))
    return sum(got.values())


def dram_from_map(text):
    """Static RAM from the linker map: the output section lines `.dram0.data  0xADDR  0xSIZE`."""
    total = 0
    for name in DRAM_SECTIONS:
        m = re.search(r"^%s\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)" % re.escape(name), text, re.M)
        if not m:
            raise ValueError("map has no %s section" % name)
        total += int(m.group(1), 16)
    return total


def measure(build_dir, size_tool=None):
    """{"flash": bytes of firmware.bin, "dram_static": .dram0.data + .dram0.bss}."""
    if size_tool is None:
        size_tool = find_size_tool()
    flash = os.path.getsize(os.path.join(build_dir, "firmware.bin"))
    elf = os.path.join(build_dir, "firmware.elf")
    if size_tool and os.path.isfile(elf):
        out = subprocess.run([size_tool, "-A", elf], check=True, capture_output=True, text=True).stdout
        return {"flash": flash, "dram_static": dram_from_size(out)}
    maps = sorted(glob.glob(os.path.join(build_dir, "*.map")))
    if not maps:
        raise ValueError("no size tool and no .map in %s" % build_dir)
    with open(maps[0], encoding="utf-8", errors="replace") as fh:
        return {"flash": flash, "dram_static": dram_from_map(fh.read())}


def partitions(csv_path):
    """{name: (offset, size)} from a partitions.csv."""
    out = {}
    with open(csv_path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) >= 5:
                out[cols[0]] = (int(cols[3], 0), int(cols[4], 0))
    return out


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(65536), b""):
            h.update(block)
    return h.hexdigest()


def manifest(build_dir, out_dir, preset, off, release, partitions_csv=None, size_tool=None):
    """Copy one preset's images into out_dir and write out_dir/manifest.json; return it.

    roles: bootloader, partitions and otadata are written on a first install only; app is
    the one image an update writes (app0)."""
    parts = partitions(partitions_csv or os.path.join(ROOT, "partitions.csv"))
    app_off, app_size = parts["app0"]
    ota_off = parts["otadata"][0]
    # This build (Arduino as an ESP-IDF component) makes ota_data_initial.bin; an
    # Arduino-only build makes boot_app0.bin. Either is the otadata image.
    ota_file = next((f for f in ("ota_data_initial.bin", "boot_app0.bin")
                     if os.path.isfile(os.path.join(build_dir, f))), None)
    plan = [("bootloader", "bootloader.bin", BOOTLOADER_OFFSET),
            ("partitions", "partitions.bin", PARTITIONS_OFFSET)]
    if ota_file:
        plan.append(("otadata", ota_file, ota_off))
    plan.append(("app", "firmware.bin", app_off))
    app_bytes = os.path.getsize(os.path.join(build_dir, "firmware.bin"))
    if app_bytes > app_size:
        raise ValueError("firmware.bin is %d bytes, app0 holds %d" % (app_bytes, app_size))
    os.makedirs(out_dir, exist_ok=True)
    images = []
    for role, name, offset in plan:
        src = os.path.join(build_dir, name)
        shutil.copyfile(src, os.path.join(out_dir, name))
        images.append({"role": role, "file": name, "offset": offset,
                       "size": os.path.getsize(src), "sha256": _sha256(src)})
    off = sorted(off)
    m = {
        "release": release,
        "preset": preset,
        "chip": CHIP,
        "off": off,
        "fingerprint": buildopts.fingerprint(set(off), release),
        "app_partition": {"offset": app_off, "size": app_size},
        "cost": measure(build_dir, size_tool=size_tool),
        "images": images,
    }
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(m, fh, indent=1)
        fh.write("\n")
    return m


def results_from_dir(release_dir):
    """{preset: cost} from <release_dir>/<preset>/manifest.json."""
    out = {}
    for path in sorted(glob.glob(os.path.join(release_dir, "*", "manifest.json"))):
        with open(path, encoding="utf-8") as fh:
            m = json.load(fh)
        out[m["preset"]] = m["cost"]
    return out


def table(results, peaks, presets, release="", app_size=None):
    """The cost table (costs.json): flash used, flash freed against Everything, spikes gone."""
    if app_size is None:
        app_size = partitions(os.path.join(ROOT, "partitions.csv"))["app0"][1]
    if "everything" not in results:
        raise ValueError("no Everything build to compare against")
    missing = [p["id"] for p in presets if p["id"] not in results]
    if missing:
        raise ValueError("no build for preset(s): %s" % ", ".join(missing))
    base = results["everything"]
    rows = []
    for p in presets:
        r = results[p["id"]]
        gone = [{"id": i, "peak_kb": peaks[i]["peak_kb"], "what": peaks[i]["what"]}
                for i in sorted(p["off"]) if i in peaks]
        rows.append({
            "id": p["id"],
            "label": p.get("label", p["id"]),
            "flash": r["flash"],
            "flash_pct": round(100.0 * r["flash"] / app_size, 1),
            "flash_freed": base["flash"] - r["flash"],
            "dram_static": r["dram_static"],
            "dram_static_freed": base["dram_static"] - r["dram_static"],
            "spikes_gone": gone,
            "spikes_gone_kb": sum(g["peak_kb"] for g in gone),
        })
    return {"release": release, "app_partition": app_size, "presets": rows}


def _load_json(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as fh:
        return json.load(fh)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="buildcost")
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("measure")
    m.add_argument("build_dir")
    mf = sub.add_parser("manifest")
    mf.add_argument("build_dir")
    mf.add_argument("out_dir")
    mf.add_argument("--preset", required=True)
    mf.add_argument("--release", required=True)
    t = sub.add_parser("table")
    t.add_argument("release_dir")
    t.add_argument("--release", required=True)
    t.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    try:
        if args.cmd == "measure":
            print(json.dumps(measure(args.build_dir)))
        elif args.cmd == "manifest":
            presets = _load_json("installer", "presets.json")
            match = [p for p in presets if p["id"] == args.preset]
            if not match:
                raise ValueError("unknown preset: %r" % args.preset)
            got = manifest(args.build_dir, args.out_dir, args.preset, match[0]["off"], args.release)
            print("%s: app %d bytes, static RAM %d bytes" % (args.preset, got["cost"]["flash"],
                                                             got["cost"]["dram_static"]))
        else:
            costs = table(results_from_dir(args.release_dir), _load_json("installer", "peaks.json"),
                          _load_json("installer", "presets.json"), release=args.release)
            with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
                json.dump(costs, fh, indent=1)
                fh.write("\n")
            for r in costs["presets"]:
                print("%-12s %9d bytes  %5.1f%%  freed %8d" % (r["id"], r["flash"], r["flash_pct"],
                                                               r["flash_freed"]))
    except (OSError, ValueError, KeyError) as e:
        print("buildcost: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
