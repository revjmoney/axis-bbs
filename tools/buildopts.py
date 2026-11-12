"""Feature selection helper for AXIS Micro BBS build options.

Reads installer/features.json and installer/presets.json (the one source of
truth). Standard library only. Run with: python -I tools/buildopts.py ...
"""
import argparse
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(root):
    """Return (features, presets) read from <root>/installer."""
    base = os.path.join(root, "installer")
    with open(os.path.join(base, "features.json"), encoding="utf-8") as fh:
        features = json.load(fh)
    with open(os.path.join(base, "presets.json"), encoding="utf-8") as fh:
        presets = json.load(fh)
    return features, presets


def resolve(off, features=None):
    """Validate a set of switched-off ids; return it. Raises ValueError."""
    if features is None:
        features = load(ROOT)[0]
    by_id = {f["id"]: f for f in features}
    off = set(off)
    for i in sorted(off):
        if i not in by_id:
            raise ValueError("unknown feature: %r" % (i,))
        if by_id[i].get("reserved"):
            raise ValueError("feature %s is reserved and cannot be selected yet" % i)
    broken = []
    for f in features:
        if f["id"] in off or f.get("reserved"):
            continue
        for need in f.get("needs", []):
            if need in off:
                broken.append((f["id"], need))
    if broken:
        raise ValueError("; ".join(
            "%s needs %s, which is off (turn %s off too, or keep %s on)" % (a, b, a, b)
            for a, b in broken))
    return off


def fingerprint(off, release):
    data = release + "|" + ",".join(sorted(off))
    return hashlib.sha256(data.encode("utf-8")).hexdigest()[:12]


def build_flags(off, features=None):
    if features is None:
        features = load(ROOT)[0]
    defs = {f["id"]: f["define"] for f in features}
    return ["-D%s=0" % defs[i] for i in sorted(off, key=lambda k: defs[k])]


def _selection(args, features, presets):
    if args.preset:
        for p in presets:
            if p["id"] == args.preset:
                off = set(p["off"])
                break
        else:
            raise ValueError("unknown preset: %r" % (args.preset,))
    else:
        off = {s for s in (args.off or "").split(",") if s}
    return resolve(off, features), off


def main(argv=None):
    ap = argparse.ArgumentParser(prog="buildopts")
    ap.add_argument("cmd", choices=["flags", "fingerprint", "check"])
    ap.add_argument("--preset")
    ap.add_argument("--off")
    ap.add_argument("--release", default="")
    args = ap.parse_args(argv)
    features, presets = load(ROOT)
    try:
        off, _ = _selection(args, features, presets)
    except ValueError as e:
        print("refused: %s" % e, file=sys.stderr)
        return 1
    if args.cmd == "flags":
        print(" ".join(build_flags(off, features)))
    elif args.cmd == "fingerprint":
        print(fingerprint(off, args.release))
    return 0


if __name__ == "__main__":
    sys.exit(main())
