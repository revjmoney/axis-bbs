# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Get a LORD 4.07 folder ready for the board, on the PC.

usage: python -I tools/door_prepare.py LORD_FOLDER OUT_FOLDER

Copies the game, removes any old player data, writes NODE1.DAT (node 1, FOSSIL, DOOR.SYS in C:\\),
runs LORDCFG once through build/doorhost.exe to create a fresh lord.dat, then drops the docs,
batch files and LORDCFG the board doesn't need.
"""
import os
import shutil
import subprocess
import sys

NODE1 = (
    "**Seth Able's Node Data System, for node 1\r\n"
    "BBSNAME Axis Underground CNC\r\n"
    "BBSTYPE DOORSYS\r\n"
    ";COMPORT 0\r\n"
    "FOSSIL\r\n"
    "LOCKBAUD 0\r\n"
    "STATUS ON\r\n"
    "STATFORE 15\r\n"
    "STATBACK 1\r\n"
    ";NODE1\r\n"
    "NODIRECT\r\n"
    ";UNUSED\r\n"
    "BBSDROP C:\\\r\n"
    "NOOPEN\r\n"
    "NORESET\r\n"
    ";UNUSED\r\n"
    "TASKER 4\r\n"
    "XTRATASK 0\r\n"
)  # the same text tests/test_lord_telnet.cpp proves on the PC

FRESH = {"player.dat", "lord.dat", "playext.dax"}
DROP = {"lord.doc", "lady.doc", "register.doc", "readme.1st", "install.txt", "upgrade.txt", "whatsnew.txt",
        "trouble.txt", "bugrpt.frm", "lordstrc.pas", "local.bat", "start.bat", "doattrib.bat", "noattrib.bat",
        "dstart", "start1", "start2", "start3", "start4", "start5", "lordcfg.exe", "node0.dat", "door.sys"}


def main(src, out):
    here = os.path.dirname(os.path.abspath(__file__))
    doorhost = os.path.join(here, "..", "build", "doorhost.exe")
    if os.path.exists(out):
        shutil.rmtree(out)
    shutil.copytree(src, out)
    for n in os.listdir(out):
        if n.lower() in FRESH:
            os.remove(os.path.join(out, n))
    with open(os.path.join(out, "NODE1.DAT"), "w", newline="") as f:
        f.write(NODE1)
    script = os.path.join(out, "_cfg.txt")
    with open(script, "w") as f:
        f.write("Create a NEW LORD.DAT\tA\t\nExit the program\tx\t\n")
    r = subprocess.run([doorhost, "--dir", out, "--exe", "LORDCFG.EXE", "--script", script],
                       capture_output=True, text=True, timeout=120)
    os.remove(script)
    print(r.stdout.strip())
    if not os.path.exists(os.path.join(out, "lord.dat")) and not os.path.exists(os.path.join(out, "LORD.DAT")):
        sys.exit("LORDCFG did not create lord.dat")
    for n in os.listdir(out):
        if n.lower() in DROP:
            os.remove(os.path.join(out, n))
    total = sum(os.path.getsize(os.path.join(out, n)) for n in os.listdir(out))
    print(f"ready: {out} ({len(os.listdir(out))} files, {total // 1024} KB)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
