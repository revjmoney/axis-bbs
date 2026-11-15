"""The installer's 8-bit icons: installer/icons.json (16x16 grids) to site/icons/<id>.svg.

Each grid row is 16 characters over a fixed palette:
  .  transparent   a  amber #f5a623   c  cyan #55ffff   g  green #55ff55   w  white   k  grey
The SVG has one rect per run of same-coloured pixels in a row. The page scales them by whole
multiples with image-rendering: pixelated. Every feature, every preset and the page's own ids
must have an icon; a wrong size, an unknown letter or a missing id stops with the reason.

Run: python -I tools/icons.py   (writes site/icons/; the SVGs are committed, tools/test_icons.py
checks they are current)
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PALETTE = {"a": "#f5a623", "c": "#55ffff", "g": "#55ff55", "w": "#ffffff", "k": "#8a8a95"}
PAGE_IDS = ["cyd", "esp32", "heltec_v4", "heltec_v3", "both", "sd", "release", "install", "custom",
            "step1", "step2", "step3"]
SIZE = 16


def load_lists(root):
    with open(os.path.join(root, "installer", "features.json"), encoding="utf-8") as fh:
        features = json.load(fh)
    with open(os.path.join(root, "installer", "presets.json"), encoding="utf-8") as fh:
        presets = json.load(fh)
    return features, presets


def required_ids(features, presets):
    return [f["id"] for f in features] + [p["id"] for p in presets] + PAGE_IDS


def check(grids, features, presets):
    """Raise ValueError naming the first problem."""
    missing = [i for i in required_ids(features, presets) if i not in grids]
    if missing:
        raise ValueError("no icon for: " + ", ".join(missing))
    for name in sorted(grids):
        rows = grids[name]
        if not isinstance(rows, list) or len(rows) != SIZE:
            raise ValueError("%s: %s rows, want %d" % (name, len(rows) if isinstance(rows, list) else "no", SIZE))
        for n, row in enumerate(rows, 1):
            if not isinstance(row, str) or len(row) != SIZE:
                raise ValueError("%s: row %d is %s wide, want %d" % (name, n, len(row) if isinstance(row, str) else "not", SIZE))
            for ch in row:
                if ch != "." and ch not in PALETTE:
                    raise ValueError("%s: row %d has %r, not a palette letter (. a c g w k)" % (name, n, ch))


def svg(rows):
    parts = []
    for y, row in enumerate(rows):
        x = 0
        while x < len(row):
            ch = row[x]
            end = x
            while end < len(row) and row[end] == ch:
                end += 1
            if ch != ".":
                parts.append('<rect x="%d" y="%d" width="%d" height="1" fill="%s"/>' % (x, y, end - x, PALETTE[ch]))
            x = end
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16" '
            'shape-rendering="crispEdges">' + "".join(parts) + "</svg>\n")


def write(grids, out_dir, features, presets):
    check(grids, features, presets)
    os.makedirs(out_dir, exist_ok=True)
    for old in os.listdir(out_dir):
        if old.endswith(".svg") and old[:-4] not in grids:
            os.remove(os.path.join(out_dir, old))
    for name in sorted(grids):
        with open(os.path.join(out_dir, name + ".svg"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(svg(grids[name]))
    return sorted(grids)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="icons")
    ap.add_argument("--icons", default=os.path.join(ROOT, "installer", "icons.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "site", "icons"))
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)
    features, presets = load_lists(ROOT)
    try:
        with open(args.icons, encoding="utf-8") as fh:
            grids = json.load(fh)
        names = write(grids, args.out, features, presets)
    except ValueError as e:
        if not args.quiet:
            print("icons: %s" % e, file=sys.stderr)
        return 1
    if not args.quiet:
        print("icons: wrote %d SVGs to %s" % (len(names), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
