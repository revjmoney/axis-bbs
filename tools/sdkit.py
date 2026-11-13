"""The SD card kit for a first install of the AXIS Micro BBS: sdcard-kit.zip.

    python -I tools/sdkit.py OUT.zip [--with-lord]

Stages the card files in local/sdkit-stage the way tools/install_cyd.py --stage does (theme
packs found in ~/Downloads, the /scripts programs from the repo, and with --with-lord the LORD
game data) and zips them in the card's own layout, so the zip unpacks straight onto the card:
themes/<look>/, doors/lord/, scripts/. Downloads (staged under cache/) are never zipped.
LORD is left out unless asked for: it is a third party game, and whether its data may be
republished on the public site is Rev's call.
"""
import argparse
import os
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# staged folder -> folder on the card (see install_cyd.install_card)
CARD = (("themes", "themes"), ("lord", "doors/lord"), ("scripts", "scripts"))

README = """AXIS Micro BBS SD card kit
by Rev. J. Money / jmscnc.com

For a first install. Copy everything in this zip to the root of the board's SD card
(FAT32), keeping the folders:
  themes/      screen sets the board can show
  doors/lord/  LORD game data, when this kit has it
  scripts/     programs for the Scripts door and SCRIPT: menu commands
Nothing here holds users, messages or settings; those live on the board.
"""


def write_kit(stage_dir, out_zip):
    """Zip the staged card folders into out_zip; return the names. Refuses an empty kit."""
    entries = []
    for staged, card in CARD:
        root = os.path.join(stage_dir, staged)
        if not os.path.isdir(root):
            continue
        for dirpath, dirs, files in os.walk(root):
            dirs.sort()
            for name in sorted(files):
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, root).replace(os.sep, "/")
                entries.append((full, card + "/" + rel))
    if not entries:
        raise ValueError("nothing staged in %s: no themes, LORD data or scripts" % stage_dir)
    tmp = out_zip + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("README.txt", README)
        for full, arc in entries:
            z.write(full, arc)
    os.replace(tmp, out_zip)
    return ["README.txt"] + [a for _f, a in entries]


def main(argv=None):
    ap = argparse.ArgumentParser(prog="sdkit")
    ap.add_argument("out")
    ap.add_argument("--with-lord", action="store_true")
    args = ap.parse_args(argv)
    import install_cyd
    # A stage of its own, so local/sdstage (the one the install menu copies) is left alone.
    stage = os.path.join(install_cyd.ROOT, "local", "sdkit-stage")
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    install_cyd.STAGE = stage
    install_cyd.stage_themes([i for i, _f, _l in install_cyd.PACKS], {})
    if args.with_lord:
        install_cyd.stage_lord()
    install_cyd.stage_scripts()
    try:
        names = write_kit(stage, args.out)
    except ValueError as e:
        print("sdkit: %s" % e, file=sys.stderr)
        return 1
    tops = sorted({n.split("/")[0] for n in names if "/" in n})
    print("%s: %d files (%s)" % (args.out, len(names), ", ".join(tops)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
