# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Plug in one CYD and install Axis from this PC.

    python -I tools/install_cyd.py
    python -I tools/install_cyd.py --stage

The app image is the program (WiFi, telnet, the menus, native LORD, the script
engine). Screens, theme packs, LORD data, and /scripts programs are copied to
the SD card. A .axs file is a program: the Scripts door, a menu command
SCRIPT:name, and a login step SCRIPT run it. The ESP32 cannot run a native
binary loaded from the card, so the DOS 8086 build stays on the PC (doorhost).
It is not a second CYD image.

Steps the menu walks: pick the board, read its MAC before anything is written,
pick native LORD or the PC DOS host, pick themes, build, flash only the app,
copy files to board 1's card, then offer first-boot setup notes and a SyncTERM
dial entry.

Flash writes the bootloader, the partition table, otadata, and the app. It
does not erase NVS (0x9000), LittleFS (0x250000), or the SD card. The native
table has no door partition: the old 384 KB cache at 0x1F0000 is app space
(app0 is 0x240000, ending where LittleFS starts). Opening the console for the
copy sets DTR and RTS false first so the port open itself does not reset the
board.
The flash step does reset that one board into the bootloader. That is the
install, and it can renumber cluster nodes. The menu asks for the word FLASH
before it does that.
"""
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE = os.path.join(ROOT, "local", "sdstage")
DOWNLOADS = os.path.join(os.path.expanduser("~"), "Downloads")
LORD_SRC = os.path.join(os.path.dirname(ROOT), "axis-bbs-work", "doors", "lord407", "game")
APP_MAX = 0x240000  # partitions.csv app0. Native table has no door partition.
APP_ADDR = 0x10000

# Identity is the efuse MAC, not the COM number. Ports move.
BOARDS = [
    ("board1", "Board 1, main, the SD card", "8c:94:df:4f:1c:4c"),
    ("cyd2", "Board 2, follower, no card", "8c:94:df:4f:14:18"),
    ("ring", "Ring board, follower, no panel and no card", "30:76:f5:a7:03:6c"),
]

# Numbered screens in the Downloads packs, and the name the firmware opens.
ROLES = {
    "01": "PRELOGON.ANS",
    "03": "BULLETIN.ANS",
    "04": "MAIN.ANS",
    "05": "LOGIN.ANS",
    "06": "NEWUSER.ANS",
    "07": "DOORS.ANS",
    "08": "MSGS.ANS",
    "09": "FILES.ANS",
    "10": "MATRIX.ANS",
    "11": "FASTLOGN.ANS",
    "12": "SHOP.ANS",
    "13": "MILL.ANS",
}
# The main keeps 12 KB of a theme screen. A follower receives at most 8 KB (cluster frame).
THEME_CAP = 12288

PACKS = [
    ("crt", "axis-crt-hardcore-ansi-2026-11-07-v2.zip", "CRT hardcore"),
    ("sea", "axis-deep-sea-ansi-2026-11-07.zip", "Deep sea"),
    ("haunted", "axis-haunted-crt-ansi-2026-11-07.zip", "Haunted CRT"),
    ("cathedral", "axis-occult-cathedral-ansi-2026-11-07.zip", "Occult cathedral"),
    ("skulls", "axis-skulls-ansi-2026-11-07.zip", "Skulls"),
    ("underground", "axis-underground-ansi-2026-11-07.zip", "Underground (Saylor poster)"),
    ("wasteland", "axis-wasteland-ansi-2026-11-07.zip", "Wasteland"),
]

# Public Synchronet menu packs. Same @# names as lordtxt.lrd. Not copied into git.
LORD_PACKS = [
    ("ansilord", "ansilord"),
    ("al353", "al353"),
    ("al40", "al40"),
    ("visio003", "visio003"),
    ("toby", "toby"),
    ("warriors", "warriors"),
]
SBBS = "https://raw.githubusercontent.com/SynchronetBBS/sbbs/master/xtrn/lord/menus"
LORD_ZIP = "https://www.gameport.com/demos/lord407.zip"

SKIP_LORD = {
    "player.dat", "nativeday", "lord.dat", "playext.dax", "lord.doc", "lady.doc",
    "register.doc", "readme.1st", "install.txt", "upgrade.txt", "whatsnew.txt",
    "trouble.txt", "bugrpt.frm", "lordstrc.pas", "local.bat", "start.bat",
    "doattrib.bat", "noattrib.bat", "dstart", "node0.dat", "door.sys",
}


def say(text):
    print(text, flush=True)


def ask(prompt):
    return input(prompt).strip()


def choose(title, rows):
    """rows: list of (key, label). Returns the key."""
    say("")
    say(title)
    for i, (_k, label) in enumerate(rows, 1):
        say(f"  {i}. {label}")
    while True:
        raw = ask("Number: ")
        if raw.isdigit() and 1 <= int(raw) <= len(rows):
            return rows[int(raw) - 1][0]
        say("Type one of the numbers.")


def choose_many(title, rows):
    """Comma list, or 'all'. Returns keys."""
    say("")
    say(title)
    for i, (_k, label) in enumerate(rows, 1):
        say(f"  {i}. {label}")
    say("  all. every one of these")
    while True:
        raw = ask("Numbers (example: 1,3,5) or all: ").lower()
        if raw == "all":
            return [k for k, _l in rows]
        nums = [p.strip() for p in raw.split(",") if p.strip()]
        if nums and all(n.isdigit() and 1 <= int(n) <= len(rows) for n in nums):
            return [rows[int(n) - 1][0] for n in nums]
        say("Type numbers separated by commas, or all.")


def com_ports():
    try:
        from serial.tools import list_ports
        return [(p.device, p.description or "") for p in list_ports.comports()]
    except Exception:
        return []


def esptool_cmd():
    return [sys.executable, "-m", "esptool"]


def read_mac(port):
    cmd = esptool_cmd() + ["--chip", "esp32", "--port", port, "--baud", "115200",
                           "--after", "hard_reset", "read_mac"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    text = (r.stdout or "") + (r.stderr or "")
    m = re.search(r"MAC:\s*([0-9A-Fa-f:]{17})", text)
    return (m.group(1).lower() if m else ""), text


def pio():
    exe = shutil.which("pio")
    if exe:
        return [exe]
    guess = os.path.join(os.path.dirname(sys.executable), "Scripts", "pio.exe")
    if os.path.isfile(guess):
        return [guess]
    return ["pio"]


def curl(url, dest):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if os.path.isfile(dest) and os.path.getsize(dest) > 0:
        return True
    r = subprocess.run(["curl.exe", "-fL", "--retry", "2", "--retry-delay", "2", "-o", dest, url],
                       capture_output=True, text=True)
    if r.returncode != 0 or not os.path.isfile(dest) or os.path.getsize(dest) == 0:
        say(f"  download failed: {url}")
        if os.path.isfile(dest):
            os.remove(dest)
        return False
    return True


def shade(blob):
    return sum(1 for b in blob if 0xB0 <= b <= 0xDF)


def split_lrd(data):
    text = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    order, sections, cur, buf = [], {}, None, []
    for line in text.split(b"\n"):
        if line.startswith(b"@#") and b" " not in line and len(line) <= 24:
            if cur is not None:
                sections[cur] = b"\n".join(buf)
            cur = line[2:].decode("latin1")
            order.append(cur)
            buf = []
        elif cur is not None:
            buf.append(line)
    if cur is not None:
        sections[cur] = b"\n".join(buf)
    return order, sections


def join_lrd(order, sections):
    parts = []
    for name in order:
        parts.append(b"@#" + name.encode("latin1"))
        parts.append(sections.get(name, b""))
    return b"\n".join(parts) + b"\n"


def map_theme_zip(zip_path, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    written = []
    with zipfile.ZipFile(zip_path) as z:
        ans = []
        for n in z.namelist():
            norm = n.replace("\\", "/").lower()
            if norm.endswith(".ans") and (norm.startswith("ans/") or "/ans/" in norm):
                ans.append(n)
        by_num = {}
        for name in ans:
            base = os.path.basename(name)
            m = re.match(r"(\d{2})_", base)
            if m:
                by_num[m.group(1)] = name
        planned = dict(ROLES)
        # The tall Saylor screens are 37 KB and 45 KB. The board holds a theme
        # screen in RAM, so they stay on the card under their own names.
        if "14" in by_num:
            planned["14"] = "SAYLOR.ANS"
        if "15" in by_num:
            planned["15"] = "SAYLORLG.ANS"
        for num, dest_name in planned.items():
            src = by_num.get(num)
            if not src:
                continue
            data = z.read(src)
            path = os.path.join(out_dir, dest_name)
            with open(path, "wb") as f:
                f.write(data)
            written.append((dest_name, len(data)))
    return written


def stage_themes(ids, slot_for):
    """ids: folder names. slot_for: {axis|cyan|green: folder id or ''}."""
    root = os.path.join(STAGE, "themes")
    if os.path.isdir(root):
        shutil.rmtree(root)
    os.makedirs(root)
    by_id = {i: (fn, label) for i, fn, label in PACKS}
    made = []
    for tid in ids:
        fn, _label = by_id[tid]
        zp = os.path.join(DOWNLOADS, fn)
        if not os.path.isfile(zp):
            say(f"  missing {zp}")
            continue
        folder = os.path.join(root, tid)
        rows = map_theme_zip(zp, folder)
        made.append(tid)
        big = [n for n, sz in rows if sz > THEME_CAP and not n.startswith("SAYLOR")]
        follow = [n for n, sz in rows if 8192 < sz <= THEME_CAP and not n.startswith("SAYLOR")]
        extra = ""
        if big:
            extra += f", too big for the board: {', '.join(big)}"
        if follow:
            extra += f", a follower loses the bottom (8 KB frame): {', '.join(follow)}"
        say(f"  {tid}: {len(rows)} screens{extra}")
    for slot, tid in slot_for.items():
        src = os.path.join(root, tid)
        if tid and os.path.isdir(src):
            dst = os.path.join(root, slot)
            os.makedirs(dst, exist_ok=True)
            for name in os.listdir(src):
                shutil.copyfile(os.path.join(src, name), os.path.join(dst, name))
            say(f"  look '{slot}' shows the {tid} screens")
    return made


def lord_work_copy():
    if os.path.isfile(os.path.join(LORD_SRC, "lordtxt.lrd")) or os.path.isfile(os.path.join(LORD_SRC, "LORDTXT.DAT")):
        return LORD_SRC
    cache = os.path.join(STAGE, "cache")
    dest = os.path.join(cache, "lord407.zip")
    say("  local LORD folder is not there, downloading the shareware zip")
    if not curl(LORD_ZIP, dest):
        return ""
    unpack = os.path.join(cache, "lord407")
    if os.path.isdir(unpack):
        shutil.rmtree(unpack)
    with zipfile.ZipFile(dest) as z:
        z.extractall(unpack)
    for dirpath, _dirs, files in os.walk(unpack):
        low = {n.lower() for n in files}
        if "lordtxt.dat" in low or "lordtxt.lrd" in low:
            return dirpath
    return ""


def fetch_pack(key, cache):
    dest = os.path.join(cache, key + ".lrd")
    url = f"{SBBS}/{key}/lordtxt.lrd"
    if curl(url, dest):
        return dest
    return ""


def stage_lord():
    src = lord_work_copy()
    out = os.path.join(STAGE, "lord")
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    if not src:
        say("  no LORD data. The door will have nothing to read.")
        return out
    n = 0
    for name in os.listdir(src):
        low = name.lower()
        if low in SKIP_LORD or low.endswith((".exe", ".com", ".bat", ".zip")):
            continue
        full = os.path.join(src, name)
        if os.path.isfile(full):
            shutil.copyfile(full, os.path.join(out, name))
            n += 1
    say(f"  copied {n} LORD data files (no player file, no EXE)")
    base_name = "lordtxt.lrd" if os.path.isfile(os.path.join(out, "lordtxt.lrd")) else ""
    if not base_name:
        say("  no lordtxt.lrd in the game folder. Stock LORDTXT.DAT stays the fallback.")
        return out
    cache = os.path.join(STAGE, "cache")
    os.makedirs(cache, exist_ok=True)
    order, sections = split_lrd(open(os.path.join(out, base_name), "rb").read())
    notes = []
    for key, _folder in LORD_PACKS:
        path = fetch_pack(key, cache)
        if not path:
            continue
        shutil.copyfile(path, os.path.join(out, key + ".lrd"))
        _o, sect = split_lrd(open(path, "rb").read())
        for scene in ("ARTHUR", "LAIR"):
            if scene not in sections or scene not in sect:
                continue
            if shade(sections[scene]) >= 80:
                continue
            if shade(sect[scene]) > shade(sections[scene]) + 20:
                sections[scene] = sect[scene]
                notes.append(f"{scene} from {key}")
    house = join_lrd(order, sections)
    with open(os.path.join(out, "lordtxt.lrd"), "wb") as f:
        f.write(house)
    with open(os.path.join(out, "house.lrd"), "wb") as f:
        f.write(house)
    say("  house art: " + (", ".join(notes) if notes else "Ansi 1.00 as-is"))
    lines = ["house.lrd House (dressed)"]
    for key, _folder in LORD_PACKS:
        if os.path.isfile(os.path.join(out, key + ".lrd")):
            lines.append(f"{key}.lrd {key}")
    with open(os.path.join(out, "ARTS.TXT"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return out


def build_cyd():
    say("Building env cyd (one image for every board)...")
    r = subprocess.run(pio() + ["run", "-e", "cyd"], cwd=ROOT)
    if r.returncode != 0:
        say("Build failed.")
        return ""
    bin_path = os.path.join(ROOT, ".pio", "build", "cyd", "firmware.bin")
    if not os.path.isfile(bin_path):
        say("No firmware.bin after the build.")
        return ""
    size = os.path.getsize(bin_path)
    say(f"  app image {size} / {APP_MAX} bytes, {APP_MAX - size} free")
    if size > APP_MAX:
        say("  too big for the app partition. Not flashing.")
        return ""
    return bin_path


def build_doorhost():
    say("PC DOS door host. This is not flashed to a CYD.")
    say("The 8086 does not fit in the board image next to the BBS.")
    build = os.path.join(ROOT, "build")
    if not shutil.which("cmake"):
        say("cmake is not on PATH. From an MSYS2 shell:")
        say("  cmake -S . -B build -G Ninja && cmake --build build")
        return False
    r = subprocess.run(["cmake", "-S", ".", "-B", "build", "-G", "Ninja"], cwd=ROOT)
    if r.returncode != 0:
        return False
    r = subprocess.run(["cmake", "--build", "build"], cwd=ROOT)
    exe = os.path.join(build, "doorhost.exe")
    say("doorhost is ready." if os.path.isfile(exe) else "doorhost did not build.")
    return os.path.isfile(exe)


def flash_app(port, expect_mac):
    mac, text = read_mac(port)
    if not mac:
        say("Could not read the MAC. Nothing was flashed.")
        say(text[-500:])
        return False
    say(f"  port {port} MAC {mac}")
    if mac != expect_mac:
        say(f"  that is not {expect_mac}. Nothing was flashed.")
        return False
    if ask("Type FLASH to write the app on this board: ") != "FLASH":
        say("Skipped the flash.")
        return False
    say("Flashing the native table. NVS, LittleFS, and the card stay. The old door cache becomes app space.")
    r = subprocess.run(pio() + ["run", "-e", "cyd", "-t", "upload", "--upload-port", port], cwd=ROOT)
    if r.returncode != 0:
        say("Flash failed.")
        return False
    say("Flashed. This board rebooted. Other boards keep their node numbers unless this one was main.")
    return True


def put_tree(port, local, remote):
    """One folder, not nested. sdput sets DTR and RTS false before open."""
    script = os.path.join(ROOT, "tools", "sdput.py")
    r = subprocess.run([sys.executable, "-I", script, port, local, remote, "--baud=921600"])
    return r.returncode == 0


def install_card(port, mac):
    if mac != BOARDS[0][2]:
        say("This board has no SD card. Theme and LORD files stay on the main (board 1).")
        say("Plug board 1 in and run the card copy from this menu again.")
        return
    themes = os.path.join(STAGE, "themes")
    if os.path.isdir(themes):
        for name in sorted(os.listdir(themes)):
            folder = os.path.join(themes, name)
            if os.path.isdir(folder):
                say(f"Copying theme {name}")
                if not put_tree(port, folder, f"/themes/{name}"):
                    say(f"  theme {name} did not match. The card was not wiped.")
                    return
    lord = os.path.join(STAGE, "lord")
    if os.path.isdir(lord):
        say("Copying LORD data (player file is not in this folder)")
        if not put_tree(port, lord, "/doors/lord"):
            say("  LORD copy did not match.")
            return
    scripts = os.path.join(STAGE, "scripts")
    if os.path.isdir(scripts):
        say("Copying scripts")
        if not put_tree(port, scripts, "/scripts"):
            say("  Script copy did not match.")


def setup_notes(port):
    say("")
    say("First-boot setup is the touch screen, or telnet with the PIN the screen shows.")
    say("This script does not type a sysop password and does not rewrite users.")
    if ask("Read the boot lines on this port for 8 seconds? [y/N]: ").lower() != "y":
        return
    try:
        import serial
        import time
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, 0.4
        s.dtr = s.rts = False
        s.open()
        time.sleep(0.3)
        end = time.time() + 8
        while time.time() < end:
            line = s.readline()
            if line:
                say("  " + line.decode("ascii", "replace").rstrip())
        s.close()
    except Exception as e:
        say(f"  serial: {e}")
    say("If the screen is asking, tap SET UP ON THIS SCREEN, or SET UP OVER TELNET and dial the board.")
    say("Questions: handle, password, BBS name, location, board name, telnet port, timezone, FTN.")


def syncterm_entry():
    say("")
    if ask("Write a SyncTERM dial entry for axis.local port 23? [y/N]: ").lower() != "y":
        return
    body = (
        "; Axis Underground CNC\n"
        "[Axis Underground CNC]\n"
        "Address=axis.local\n"
        "Port=23\n"
        "ConnectionType=Telnet\n"
        "ScreenMode=80x25\n"
    )
    homes = [
        os.path.join(os.environ.get("APPDATA", ""), "SyncTERM"),
        os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "SyncTERM"),
    ]
    dest_dir = next((h for h in homes if h and os.path.isdir(h)), "")
    if not dest_dir:
        dest_dir = os.path.join(STAGE, "syncterm")
        os.makedirs(dest_dir, exist_ok=True)
        say("SyncTERM is not installed here. The dial entry is on disk.")
        say("Install SyncTERM, then copy this file into its directory.")
        say("Builds: https://sourceforge.net/projects/syncterm/files/")
    path = os.path.join(dest_dir, "axis-underground.lst")
    with open(path, "w", newline="\n") as f:
        f.write(body)
    say(f"  wrote {path}")
    say("  In SyncTERM: Dialing Directory, Insert, or open that list. Host axis.local, telnet, port 23.")


def banner():
    say("Axis install")
    say("1. Plug in the one board you are installing.")
    say("2. This script builds, flashes, and copies files.")
    say("The program stays in flash. Themes and LORD data go on board 1's SD card.")
    say("A follower has no card. It reads those files from the main.")


def run_menu():
    banner()
    job = choose("What do you want to do?", [
        ("native", "Native LORD on a CYD: build, flash, copy themes and game files to the card"),
        ("files", "Card only: themes and LORD data, do not rebuild or flash"),
        ("dos", "PC DOS door host (doorhost). Does not flash a board"),
    ])
    if job == "dos":
        build_doorhost()
        syncterm_entry()
        return

    ports = com_ports()
    if not ports:
        say("No COM ports. Plug the board in and run this again.")
        return
    port = choose("Which COM port is the board you just plugged in?", [(d, f"{d}  {desc}") for d, desc in ports])
    board = choose("Which board should that port be? The MAC is checked before a write.",
                   [(b[0], f"{b[1]}  {b[2]}") for b in BOARDS])
    expect = dict((b[0], b[2]) for b in BOARDS)[board]

    theme_ids = choose_many(
        "Which screensets go on the card? Each one is its own look, beside axis, cyan, and green.",
        [(i, f"{label}  ({fn})") for i, fn, label in PACKS])
    slots = {}

    say("")
    say("Plan")
    say(f"  port {port}, expect MAC {expect}")
    say(f"  job {job}")
    say(f"  themes {', '.join(theme_ids) or '(none)'}")
    for slot, tid in slots.items():
        if tid:
            say(f"  {slot} shows {tid}")
    if ask("Go ahead? [y/N]: ").lower() != "y":
        say("Stopped.")
        return

    stage_themes(theme_ids, slots)
    stage_lord()
    stage_scripts()
    if job == "native":
        if not build_cyd():
            return
        if not flash_app(port, expect):
            return
        mac = expect
    else:
        mac, _text = read_mac(port)
        if mac != expect:
            say(f"MAC is {mac or 'unknown'}, expected {expect}. Not writing the card.")
            return
    if board == "board1":
        install_card(port, mac)
    else:
        say("Files are staged in local/sdstage. Plug in board 1 to copy them. This board has no card.")
    setup_notes(port)
    syncterm_entry()
    say("Done.")


def stage_scripts():
    src = os.path.join(ROOT, "scripts")
    dst = os.path.join(STAGE, "scripts")
    if not os.path.isdir(src):
        say("No scripts folder in the repo.")
        return
    os.makedirs(dst, exist_ok=True)
    n = 0
    for name in os.listdir(src):
        if name.lower().endswith(".axs"):
            shutil.copy2(os.path.join(src, name), os.path.join(dst, name))
            n += 1
    say(f"Scripts: {n} file(s) in /scripts")


def stage_only():
    """Prepare local/sdstage without touching a board. Used to check the file copy."""
    banner()
    say("Stage only. No port is opened.")
    ids = [i for i, _f, _l in PACKS]
    slots = {}
    stage_themes(ids, slots)
    stage_lord()
    stage_scripts()
    say(f"Staged under {STAGE}")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--stage":
        stage_only()
    else:
        run_menu()
