# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Load the BBS board with busy callers while recording its free heap.

  python -I tools/busytest.py --main 192.168.1.164 --follower 192.168.1.165 \
      --port COM20 --follower-port COM21 --minutes 15 --out busy.csv

Callers (each a thread with a telnet socket, logged in the way tools/seven_login.py does, then
"2" on the fast-login menu). Keys tuned on board 1 with the crt theme and the card's menus:
  A (main)      Doors > LORD, forest fights (F, then A until "You win"), Q out of the game
  B (main)      Doors > ANSI Pack Viewer, first area, opens each pack (number + Enter) and plays
                its first piece non-stop for a few seconds
  C (main)      Messages: areas 1, 14, 2 (reads the first post); Transfer: file areas 1, 7, 9,
                and one ZMODEM download of NET1NY09.ZIP from area 9 (received and discarded)
  D (follower)  Messages, Transfer, Doors and back, so the main serves theme screens through T
--callers picks some of them (e.g. --callers B).

Events: at 3 minutes the follower is reset (@RESTART force on --follower-port), at 5 minutes
the main is told to check DNS now (@DDNS now on --port). @HEAPWIN is sent first, so wmin/wlarge
cover the test only.

Every 30 s @STATUS is polled on --port (DTR and RTS held low, like tools/sdput.py), plus one row
before anyone dials and one after they hang up. CSV columns: see CSV_HEAD. Every serial line
goes to a raw log next to the CSV (it can hold the public hostname: keep it local), and the
board's @LOG is added at the end. Ends with what each caller did, PASS or FAIL and the lowest
numbers seen. FAIL on: a reset (up= goes down or the ROM's rst: line), a line with ALLOC FAILED,
wmin < 61440, wlarge < 32768, @STATUS without wmin/wlarge, or a caller that never did its job
(A no fight or town screen, B no pack, C no ZMODEM download, D no screen). Stops early after --max-resets
resets, 90 s without @STATUS, or the main coming back without its role or card.
BUSY_TRACE=1 prints every key whose expected answer never came.

Passwords come from --users (default local/test-users.txt, handle|location|password). They are
never printed and never put on a command line. RevJMoney is skipped. Needs four callers there.
"""
from __future__ import annotations

import argparse
import os
import re
import socket
import sys
import threading
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WMIN_FLOOR = 61440
WLARGE_FLOOR = 32768
POLL_S = 30
TRACE = bool(os.environ.get("BUSY_TRACE"))  # print every key whose answer never came
RESTART_AT_S = 3 * 60
DDNS_AT_S = 5 * 60

# @STATUS fields, in CSV order (after t). "min" is the board's lowest since boot; wmin/wlarge the
# lowest seen at the top of loop() since @HEAPWIN.
CSV_KEYS = ("up", "on", "heap", "largest", "min", "wmin", "wlarge", "wdoing", "stack", "stk_web", "stk_mdns",
            "stk_timer", "outmax", "artmax")
CSV_HEAD = "t," + ",".join(CSV_KEYS) + ",resets"


def say(line):
    print(line, flush=True)


# --- login: reuse tools/seven_login.py -------------------------------------

def load_login():
    """(Call class, load_users function) from seven_login.py, or None when it cannot load."""
    try:
        import seven_login
    except Exception as e:  # pragma: no cover
        say("cannot load tools/seven_login.py: %s" % e)
        return None
    class BusyCall(seven_login.Call):
        """seven_login's Call, but past a matrix whose login row is "Journey onward" (key J):
        seven_login sends "l", which that matrix ignores."""

        def login(self):
            self.pump(18, r"picks|login|Handle|busy|Too many failed")
            if re.search(r"Journey onward", self.text(), re.I) and "Handle" not in self.text():
                self.send("j")
                self.pump(8, r"Handle")
            super().login()

    return BusyCall, seven_login.load_users


def plain(data):
    """Screen bytes as matchable text: ANSI codes out, then every run of spaces, line ends and
    other bytes (the art moves the cursor between words) becomes one space."""
    text = re.sub(r"\x1b\[[0-9;?]*m", "", bytes(data).decode("cp437", "replace"))  # colours sit inside words
    text = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", " ", text)  # cursor moves stand between them
    return " ".join(re.sub(r"[^ -~]", " ", text).split())


class Stop(Exception):
    pass


# The main menu's prompt, "[16:41] Handle ->" (it has no "Command"). Wait for the prompt, not the
# art: a key sent while the menu is still drawing is lost.
MAIN_MENU = r"\[\d\d:\d\d\][^\[\n]{1,40}->"


class Worker(threading.Thread):
    """One caller. run_caller() loops until the shared stop event is set; failures reconnect.
    self.stats counts what the caller really did (shown in the summary)."""

    def __init__(self, name, host, handle, password, call_cls, stop, ledger):
        super().__init__(name=name, daemon=True)
        self.host = host
        self.handle = handle
        self.password = password
        self.call_cls = call_cls
        self.stop = stop
        self.ledger = ledger
        self.call = None
        self.index = 0
        self.rounds = 0
        self.stats = {"logins": 0}

    def count(self, key, n=1):
        self.stats[key] = self.stats.get(key, 0) + n

    # helpers -----------------------------------------------------------
    def check(self):
        if self.stop.is_set():
            raise Stop()

    def keys(self, s, until=None, seconds=4.0):
        """Send keys, then read until the pattern shows in what arrived after them (or the time
        runs out). Returns that fresh text, ANSI codes removed."""
        self.check()
        self.call.pump(0.05)  # what was still on its way belongs to the last keys
        self.call.app.clear()  # (Call.pump trims a long buffer, so a saved length would go stale)
        mark = 0
        self.call.send(s)
        end = time.time() + seconds
        fresh = ""
        while time.time() < end:
            self.check()
            if not self.call.pump(0.3) and self.call.sock is None:
                raise ConnectionError("dropped")
            fresh = plain(self.call.app[mark:])
            if until and re.search(until, fresh, re.I):
                self.call.pump(0.3)
                return plain(self.call.app[mark:])
        if TRACE and until:
            say("trace %s: %r wanted %r, got: %s" % (self.name, s, until, fresh[-150:]))
        return fresh

    def to_menu(self):
        """Back to the main menu: Q backs out of every menu, door and list here (and answers a
        "press a key")."""
        for _ in range(6):
            if re.search(MAIN_MENU, self.keys("q", MAIN_MENU, 3.0), re.I):
                return True
        return False

    def after_login(self):
        """The fast-login menu comes first ("1. Normal Login  2. Main Menu"): take the main menu."""
        self.count("logins")
        tail = plain(self.call.app[-3000:])
        if re.search(MAIN_MENU, tail, re.I):
            return
        if not re.search(MAIN_MENU, self.keys("2", MAIN_MENU, 6.0), re.I) and not self.to_menu():
            raise RuntimeError("no main menu after login")

    def door(self, key):
        """From the main menu: Doors, then the door's key."""
        if not re.search(r"Doors\] Command", self.keys("d", r"Doors\] Command", 6.0), re.I):
            raise RuntimeError("no Doors menu")
        return key

    # lifecycle ---------------------------------------------------------
    def run(self):
        while not self.stop.is_set():
            self.call = self.call_cls(self.host, self.handle, self.password)
            try:
                self.call.connect()
                self.call.login()
                self.after_login()
                while not self.stop.is_set():
                    self.run_caller()
                    self.rounds += 1
            except Stop:
                pass
            except Exception as e:
                self.count("errors")
                why = str(e)[:200]
                self.ledger.note("%s: %s (reconnecting)" % (self.name, why))
                # each caller waits its own time, so a reboot does not bring all of them back at once;
                # a call the board turned away waits longer
                self.stop.wait((30 if "turned" in why else 8) + 7 * self.index)
            finally:
                self.call.close()

    def run_caller(self):
        raise NotImplementedError


class CallerA(Worker):
    """LORD: forest fights until the day's fights run out, then the stats screen and the town."""

    def run_caller(self):
        self.door("l")
        text = self.keys("l", r"Town Square", 20.0)
        for _ in range(4):  # the day's news and "press a key" screens come first on a new day
            if re.search(r"Town Square", text, re.I):
                break
            text = self.keys("\r", r"Town Square", 6.0)
        else:
            raise RuntimeError("LORD did not reach the Town Square")
        self.count("lord_entries")
        for _ in range(8):
            text = self.keys("f", r"\(A\)ttack|tired|no more|fights? left|Town Square", 6.0)
            if not re.search(r"\(A\)ttack", text, re.I):  # out of fights today: walk the town instead
                if not self.stats.get("town_screens"):
                    tail = " ".join(text.split())[-120:]
                    self.ledger.note("%s: no fight on F, the game said: %s" % (self.name, tail))
                self.count("town_screens")
                self.keys("v", r"\(press|Town|key|fights \d+", 4.0)
                self.keys("q" if re.search(r"Armor|\(B\)uy", text) else "\r", None, 1.0)
                continue
            for _ in range(20):  # attack until the fight ends
                text = self.keys("a", r"You win|killed|dies|Town Square|\(A\)ttack|hits with", 4.0)
                if re.search(r"You win", text, re.I):
                    self.count("fights_won")
                    break
                if re.search(r"killed|you have died|dead", text, re.I):
                    self.count("deaths")
                    break
            else:
                self.keys("r", None, 1.5)  # still going: run
        if not re.search(r"Back from LORD", self.keys("q", r"Back from LORD", 8.0), re.I):
            self.keys("q", None, 2.0)
        self.keys("q", r"Doors\] Command", 4.0)  # the "press a key"
        self.to_menu()


class CallerB(Worker):
    """ANSI Pack Viewer: open each pack of the first area in turn and play a piece."""

    def run_caller(self):
        self.door("a")
        if not re.search(r"pick a file area", self.keys("a", r"pick a file area", 8.0), re.I):
            raise RuntimeError("no ANSI viewer")
        listing = self.keys("\r", r"Esc or Q back", 10.0)
        npacks = max(1, len(re.findall(r"\.ZIP\s+\d+K", listing, re.I)))
        for p in range(1, min(npacks, 12) + 1):
            text = self.keys("%d\r" % p, r"pieces|won't open|Esc or Q back", 15.0)
            if not re.search(r"pieces", text, re.I):
                self.count("pack_fail")
                continue
            self.count("packs_opened")
            shown = self.keys("\r", r"short on memory", 3.0)  # view the first piece
            if re.search(r"short on memory", shown, re.I):  # the viewer limit refused it: still on the piece list
                self.count("pieces_refused")
                self.keys("q", r"Esc or Q back", 6.0)  # back to the packs
                continue
            shown = self.keys("n", r"short on memory", 5.0)  # non-stop for a few seconds
            if re.search(r"short on memory", shown, re.I):  # stopped part way by the limit: back on the list
                self.count("pieces_stopped")
                self.keys("q", r"Esc or Q back", 6.0)
                continue
            self.count("pieces_played")
            self.keys("q", r"Enter views", 6.0)  # back to the pieces
            self.keys("q", r"Esc or Q back", 6.0)  # back to the packs
        self.keys("q", r"pick a file area", 4.0)
        self.keys("q", None, 3.0)  # leave the viewer
        self.to_menu()


class CallerC(Worker):
    """Message areas, then the file areas, then one ZMODEM download (bytes thrown away)."""

    FILE_AREA = "9"  # Text Files: two small zips
    FILE_NAME = "NET1NY09.ZIP"

    def run_caller(self):
        if not re.search(r"Messages\] Command", self.keys("m", r"Messages\] Command", 6.0), re.I):
            raise RuntimeError("no Messages menu")
        for area in ("1", "14", "2"):  # General Chatter, RotatoCAM (both have posts), an empty one
            self.keys("*", r"uit or Esc", 6.0)  # the area list
            text = self.keys(area + "\r", r"Enter reads|\[Q\]uit", 6.0)
            if re.search(r"\d+ msg", text, re.I):
                self.count("msg_areas")
            if re.search(r"Enter reads", text, re.I):  # (an empty area has no list, only the prompt)
                if re.search(r"reply", self.keys("\r", r"\[Q\]uit:", 6.0), re.I):
                    self.count("msgs_read")
            for _ in range(4):  # reader > area list > Messages menu
                if re.search(r"Messages\] Command", self.keys("q", r"Messages\] Command", 3.0), re.I):
                    break
        self.to_menu()
        text = self.keys("t", r"Transfer\] Command", 6.0)
        if not re.search(r"Transfer\] Command", text, re.I):
            raise RuntimeError("no Transfer menu: " + plain(self.call.app)[-120:])
        for area in ("1", "7", self.FILE_AREA):
            self.keys("f", r"uit or Esc", 6.0)  # the area list
            if re.search(r"\[D\]ownload", self.keys(area + "\r", r"\[D\]ownload", 6.0), re.I):
                self.count("file_areas")
                if area == self.FILE_AREA and not self.stats.get("zmodem_ok"):  # one download per run
                    self.download(self.FILE_NAME)
            for _ in range(4):  # file list > area list > Transfer menu
                if re.search(r"Transfer\] Command", self.keys("q", r"Transfer\] Command", 3.0), re.I):
                    break
        self.to_menu()

    def download(self, name):
        if not re.search(r"MODEM", self.keys("d" + name + "\r", r"\[Z\]MODEM", 6.0)):
            self.count("zmodem_fail")
            return False
        self.keys("z", r"\brz\b|Ctrl-X", 10.0)  # the sender's "rz" and its ZRQINIT
        ok, got, size = zmodem_discard(self.call, self.stop, 120)
        self.count("zmodem_ok" if ok else "zmodem_fail")
        self.count("zmodem_bytes", got)
        self.ledger.note("%s: ZMODEM %s %s, %d of %s bytes" % (self.name, name, "ok" if ok else "FAILED", got, size))
        self.keys("q", None, 2.0)  # the protocol prompt comes back after a transfer
        return ok


class CallerD(Worker):
    """Follower: move between menus so the main serves the theme screens."""

    def run_caller(self):
        for k, prompt in (("m", r"Messages\] Command"), ("t", r"Transfer\] Command"), ("d", r"Doors\] Command")):
            if re.search(prompt, self.keys(k, prompt, 8.0), re.I):
                self.count("screens")
            if re.search(MAIN_MENU, self.keys("q", MAIN_MENU, 8.0), re.I):
                self.count("screens")
            else:
                self.to_menu()
        self.stop.wait(2)


# --- ZMODEM receive, bytes discarded ---------------------------------------

ZDLE = 0x18
ZPAD = 0x2A


def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def hex_header(ftype, p0=0, p1=0, p2=0, p3=0):
    body = bytes((ftype, p0, p1, p2, p3))
    c = crc16(body)
    h = (body + bytes((c >> 8, c & 255))).hex()
    tail = b"\r\x8a" + (b"" if ftype in (3, 8) else b"\x11")  # no XON after ZACK/ZFIN
    return b"**" + bytes((ZDLE,)) + b"B" + h.encode() + tail


ZRQINIT, ZRINIT, ZACK, ZFILE, ZFIN, ZRPOS, ZDATA, ZEOF = 0, 1, 3, 4, 8, 9, 10, 11
ZCRCE, ZCRCG, ZCRCQ, ZCRCW = 0x68, 0x69, 0x6A, 0x6B  # h i j k
RINIT_FLAGS = 0x03  # CANFDX | CANOVIO. No CANFC32: every frame then carries CRC-16, one less case here.


def pos4(n):
    return n & 255, (n >> 8) & 255, (n >> 16) & 255, (n >> 24) & 255


class ZRx:
    """ZMODEM receive state machine. feed() takes bytes after telnet stripping and returns the
    replies to send. Counts ZDLE-decoded data bytes; ZACK carries that count."""

    def __init__(self):
        self.mode = "seek"  # seek, kind, hex, bin, data, crc
        self.zdle = False
        self.hdr = bytearray()
        self.data = bytearray()
        self.crc_left = 0
        self.after = "seek"  # where the crc of a subpacket goes next
        self.in_file_info = False
        self.received = 0  # data bytes of the file, ZDLE-decoded
        self.size = None  # from the ZFILE subpacket
        self.done = False
        self.got_zfin = False
        self.last_end = 0
        self.pads = 0

    def _unescape(self, b):
        """(byte, end marker or None) or None while a ZDLE waits for its pair."""
        if self.zdle:
            self.zdle = False
            if b in (ZCRCE, ZCRCG, ZCRCQ, ZCRCW):
                return None, b
            if b == 0x6C:
                return 0x7F, None
            if b == 0x6D:
                return 0xFF, None
            return b ^ 0x40, None
        if b == ZDLE:
            self.zdle = True
            return None
        if b in (0x11, 0x13, 0x91, 0x93):  # XON/XOFF are noise in a stream
            return None
        return b, None

    def _header(self, ftype, p):
        out = bytearray()
        if ftype == ZRQINIT:
            out += hex_header(ZRINIT, 0, 0, 0, RINIT_FLAGS)
        elif ftype == ZFILE:
            self.in_file_info = True
            self.mode, self.data = "data", bytearray()
        elif ftype == ZDATA:
            self.received = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24
            self.mode, self.data = "data", bytearray()
        elif ftype == ZEOF:
            out += hex_header(ZRINIT, 0, 0, 0, RINIT_FLAGS)
        elif ftype == ZFIN:
            self.got_zfin = True
            self.done = True
            out += hex_header(ZFIN) + b"OO"
        return out

    def _subpacket_end(self, end):
        out = bytearray()
        if self.in_file_info:
            self.in_file_info = False
            parts = bytes(self.data).split(b"\0")
            if len(parts) > 1:
                try:
                    self.size = int(parts[1].split()[0])
                except (ValueError, IndexError):
                    self.size = None
            self.received = 0
            out += hex_header(ZRPOS, 0, 0, 0, 0)
        else:
            self.received += len(self.data)
            if end in (ZCRCQ, ZCRCW):
                out += hex_header(ZACK, *pos4(self.received))
        self.last_end = end
        self.data = bytearray()
        return out

    def feed(self, chunk):
        out = bytearray()
        for b in chunk:
            m = self.mode
            if m == "seek":
                if b == ZPAD:
                    self.pads += 1
                elif b == ZDLE and self.pads:
                    self.mode, self.pads = "kind", 0
                else:
                    self.pads = 0
            elif m == "kind":
                if b == ord("B"):
                    self.mode, self.hdr = "hex", bytearray()
                elif b == ord("A"):
                    self.mode, self.hdr, self.zdle = "bin", bytearray(), False
                else:  # 'C' (CRC-32) is never asked for; anything else is noise
                    self.mode = "seek"
            elif m == "hex":
                self.hdr.append(b)
                if len(self.hdr) == 14:
                    self.mode = "seek"
                    try:
                        raw = bytes.fromhex(self.hdr.decode("ascii"))
                    except ValueError:
                        continue
                    out += self._header(raw[0], raw[1:5])
            elif m == "bin":
                r = self._unescape(b)
                if r is None or r[0] is None:
                    continue
                self.hdr.append(r[0])
                if len(self.hdr) == 7:  # type, 4 position bytes, CRC-16
                    self.mode = "seek"
                    out += self._header(self.hdr[0], self.hdr[1:5])
            elif m == "data":
                r = self._unescape(b)
                if r is None:
                    continue
                byte, end = r
                if end is not None:
                    self.mode, self.crc_left, self.pending_end = "crc", 2, end
                elif len(self.data) < 16384:
                    self.data.append(byte)
                else:  # runaway: lost a frame end
                    self.mode, self.data = "seek", bytearray()
            elif m == "crc":
                r = self._unescape(b)
                if r is None or r[0] is None:
                    continue
                self.crc_left -= 1
                if self.crc_left == 0:
                    end = self.pending_end
                    out += self._subpacket_end(end)
                    self.mode = "data" if end in (ZCRCG, ZCRCQ) else "seek"
        return bytes(out)


def zmodem_discard(call, stop, seconds):
    """Receive one ZMODEM file and throw the bytes away. Returns (ok, bytes received, size the
    sender gave). ok means the sender said ZFIN and the byte count matches the size."""
    sock = call.sock
    sock.settimeout(0.3)
    end = time.time() + seconds
    rx = ZRx()
    # The board already sent "rz\r" and ZRQINIT; some of that may be in call.app. Answer with ZRINIT.
    sock.sendall(hex_header(ZRINIT, 0, 0, 0, RINIT_FLAGS))
    quiet = 0
    while time.time() < end and not stop.is_set() and not rx.done:
        try:
            data = sock.recv(8192)
        except socket.timeout:
            quiet += 1
            if rx.mode == "seek" and quiet % 10 == 0 and rx.size is None:
                sock.sendall(hex_header(ZRINIT, 0, 0, 0, RINIT_FLAGS))
            continue
        except OSError:
            break
        if not data:
            break
        quiet = 0
        reply = rx.feed(call.strip.feed(data))
        if reply:
            sock.sendall(reply)
    ok = rx.got_zfin and rx.size is not None and rx.received == rx.size
    return ok, rx.received, rx.size


# --- serial ----------------------------------------------------------------

def open_serial(port):
    import serial
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 2
    s.dtr = s.rts = False  # set before opening, or opening resets the board
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def parse_status(line):
    """key=value tokens of an @STATUS line into a dict (strings)."""
    out = {}
    for tok in line.split():
        k, eq, v = tok.partition("=")
        if eq:
            out[k] = v
    return out


def to_int(v, default=0):
    try:
        return int(v)
    except (TypeError, ValueError):
        return default


class Ledger:
    def __init__(self):
        self.lock = threading.Lock()
        self.notes = []

    def note(self, s):
        with self.lock:
            self.notes.append(s)
        say("note: " + s)


class Monitor:
    """Polls @STATUS and reads every other serial line: ALLOC FAILED, boots (the ROM's rst: line),
    panics. Every line goes to the raw log when one is given (it can hold the public hostname:
    keep it local)."""

    BOOT = re.compile(r"rst:0x[0-9a-f]+ \(")
    PANIC = re.compile(r"Guru Meditation|abort\(\) was called|Backtrace:|CORRUPT HEAP|assert failed", re.I)

    def __init__(self, ser, raw_log=None):
        self.ser = ser
        self.raw = raw_log
        self.alloc_failed = 0
        self.last_up = None
        self.up_drops = 0
        self.boots = 0
        self.panic_lines = []
        self.t0 = time.time()

    @property
    def resets(self):
        return max(self.up_drops, self.boots)

    def _line(self, raw):
        line = raw.decode("ascii", "replace").rstrip()
        if self.raw:
            self.raw.write("%7.1f %s\n" % (time.time() - self.t0, line))
            self.raw.flush()
        if "ALLOC FAILED" in line:
            self.alloc_failed += 1
        if self.BOOT.search(line):
            self.boots += 1
        if self.PANIC.search(line) and len(self.panic_lines) < 40:
            self.panic_lines.append(line[:160])
        return line

    def poll(self):
        self.ser.write(b"\n@STATUS\n")
        self.ser.flush()
        end = time.time() + 6
        found = None
        while time.time() < end:
            raw = self.ser.readline()
            if not raw:
                continue
            line = self._line(raw)
            if "up=" in line and "heap=" in line:
                found = parse_status(line[line.index("id=") :] if "id=" in line else line)
                if self.last_up is not None and to_int(found.get("up")) < self.last_up:
                    self.up_drops += 1
                self.last_up = to_int(found.get("up"))
                break
        self.drain()
        return found

    def drain(self):
        try:
            while self.ser.in_waiting:
                self._line(self.ser.readline())
        except OSError:
            pass

    def command(self, cmd, seconds=1.0):
        self.ser.write(("\n" + cmd + "\n").encode())
        self.ser.flush()
        end = time.time() + seconds
        while time.time() < end:
            raw = self.ser.readline()
            if raw:
                self._line(raw)


def serial_send(port, cmd):
    s = open_serial(port)
    try:
        s.write(("\n" + cmd + "\n").encode())
        s.flush()
        time.sleep(0.5)
    finally:
        s.close()


# --- main ------------------------------------------------------------------

def parse_args(argv):
    ap = argparse.ArgumentParser(description="Busy-callers memory test for the BBS board.")
    ap.add_argument("--main", required=True, help="main board IP")
    ap.add_argument("--follower", required=True, help="follower board IP")
    ap.add_argument("--port", required=True, help="serial port of the main (status and @DDNS now)")
    ap.add_argument("--follower-port", default="", help="serial port of the follower (@RESTART force at 3 min)")
    ap.add_argument("--minutes", type=float, default=15.0)
    ap.add_argument("--out", required=True, help="CSV file to write")
    ap.add_argument("--users", default=os.path.join(ROOT, "local", "test-users.txt"),
                    help="callers file, handle|location|password (default local/test-users.txt)")
    ap.add_argument("--callers", default="ABCD", help="which callers to run, e.g. A or ABC (default ABCD)")
    ap.add_argument("--max-resets", type=int, default=5, help="stop the run after this many resets of the main")
    ap.add_argument("--serial-log", default="", help="raw serial log (default: next to --out, .serial.log)")
    return ap.parse_args(argv)


def selftest():
    st = parse_status("id=a role=main heap=100 largest=50 up=7 wmin=9 wdoing=lord_x")
    assert st["heap"] == "100" and st["wdoing"] == "lord_x"
    h = hex_header(1, 0, 0, 0, 0x23)
    assert h.startswith(b"**\x18B01") and h.endswith(b"\r\x8a\x11")
    assert crc16(b"123456789") == 0x31C3

    def esc(bs):  # the board escapes every control byte, 0x7f and 0xff
        o = bytearray()
        for b in bs:
            if b < 0x20 or b in (0x7F, 0xFF, ZDLE) or (b & 0x7F) < 0x20:
                o += bytes((ZDLE, b ^ 0x40)) if b not in (0x7F, 0xFF) else bytes((ZDLE, 0x6C if b == 0x7F else 0x6D))
            else:
                o.append(b)
        return bytes(o)

    def bin_hdr(t, p):
        body = bytes((t,) + p)
        c = crc16(body)
        return b"*\x18A" + esc(body + bytes((c >> 8, c & 255)))

    def sub(data, end):
        c = crc16(data + bytes((end,)))
        return esc(data) + bytes((ZDLE, end)) + esc(bytes((c >> 8, c & 255)))

    payload = bytes(range(256)) * 5
    rx = ZRx()
    replies = rx.feed(b"rz\r" + hex_header(ZRQINIT))
    assert hex_header(ZRINIT, 0, 0, 0, RINIT_FLAGS) in replies
    replies = rx.feed(bin_hdr(ZFILE, (0, 0, 0, 1)) + sub(b"T.BIN\x00%d 0 0 0 1 %d\x00" % (len(payload), len(payload)), ZCRCW))
    assert rx.size == len(payload) and hex_header(ZRPOS) in replies
    replies = rx.feed(bin_hdr(ZDATA, (0, 0, 0, 0)) + sub(payload[:1000], ZCRCQ))
    assert hex_header(ZACK, *pos4(1000)) in replies, replies
    rx.feed(sub(payload[1000:], ZCRCE) + hex_header(ZEOF, *pos4(len(payload))) + hex_header(ZFIN))
    assert rx.got_zfin and rx.received == len(payload)
    say("selftest ok")
    return 0


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if argv == ["--selftest"]:
        return selftest()
    args = parse_args(argv)
    login = load_login()
    if login is None:
        return 2
    call_cls, load_users = login
    users = load_users(args.users)
    if len(users) < 4:
        say("Not running. %s needs 4 callers with a password (handle|location|password)." % args.users)
        return 2
    stop = threading.Event()
    ledger = Ledger()
    plan = [("A", CallerA, args.main), ("B", CallerB, args.main), ("C", CallerC, args.main), ("D", CallerD, args.follower)]
    workers = []
    try:
        ser = open_serial(args.port)
    except Exception as e:
        say("cannot open the main's serial port: %s" % e)
        return 2
    raw_path = args.serial_log or os.path.splitext(args.out)[0] + ".serial.log"
    raw = open(raw_path, "w", encoding="ascii", errors="replace")
    mon = Monitor(ser, raw)
    low = {}
    reasons = []
    seen_window = False
    last_answer = time.time()
    start = time.time()

    def record(f, st, now):
        nonlocal seen_window
        vals = [int(now)] + [to_int(st.get(k)) if k not in ("wdoing", "role") else st.get(k, "") for k in CSV_KEYS] + [mon.resets]
        f.write(",".join(str(v) for v in vals) + "\n")
        f.flush()
        for k in ("heap", "largest", "min", "wmin", "wlarge", "stack", "stk_web", "stk_mdns", "stk_timer"):
            if k in st and (k not in low or to_int(st[k]) < low[k]):
                low[k] = to_int(st[k])
        say("t=%ds up=%s on=%s free=%s largest=%s min=%s wmin=%s wlarge=%s wdoing=%s stk_web=%s resets=%d" % (
            int(now), st.get("up"), st.get("on"), st.get("heap"), st.get("largest"), st.get("min"), st.get("wmin"),
            st.get("wlarge"), st.get("wdoing"), st.get("stk_web"), mon.resets))
        if "wmin" in st and "wlarge" in st:
            seen_window = True
        if "wmin" in st and to_int(st["wmin"]) < WMIN_FLOOR and "wmin" not in reasons:
            reasons.append("wmin")
        if "wlarge" in st and to_int(st["wlarge"]) < WLARGE_FLOOR and "wlarge" not in reasons:
            reasons.append("wlarge")

    with open(args.out, "w", encoding="ascii", newline="") as f:
        f.write(CSV_HEAD + "\n")
        st = mon.poll()  # t=0: before anyone dials
        if st is None:
            say("no @STATUS answer from the main before the test: not running")
            return 2
        record(f, st, 0)
        try:  # the windowed low (wmin wlarge wdoing) then covers this run only
            mon.command("@HEAPWIN", 0.5)
            ledger.note("@HEAPWIN sent: window starts with the test")
        except Exception as e:
            ledger.note("@HEAPWIN failed: %s" % e)
        start = time.time()
        for idx, (name, cls, host) in enumerate(plan):
            if name not in args.callers:
                continue
            handle, pw = users[idx]
            w = cls("caller-" + name, host, handle, pw, call_cls, stop, ledger)
            w.index = idx
            workers.append(w)
            w.start()
            stop.wait(8)  # the main takes one call at a time: stagger
            mon.drain()
        total = args.minutes * 60
        did_restart = did_ddns = False
        next_poll = time.time()
        while True:
            now = time.time() - start
            if now >= total:
                break
            if not did_restart and now >= RESTART_AT_S:
                did_restart = True
                if args.follower_port:
                    try:
                        serial_send(args.follower_port, "@RESTART force")
                        ledger.note("follower reset at %ds" % now)
                    except Exception as e:
                        ledger.note("follower reset failed: %s" % e)
                else:
                    ledger.note("no --follower-port: follower not reset")
            if not did_ddns and now >= DDNS_AT_S:
                did_ddns = True
                try:
                    mon.command("@DDNS now", 0.5)
                    ledger.note("DNS check forced at %ds" % now)
                except Exception as e:
                    ledger.note("@DDNS now failed: %s" % e)
            if time.time() >= next_poll:
                next_poll = max(next_poll + POLL_S, time.time() + 1)  # (never two rows for one late poll)
                st = mon.poll()
                if st is None:
                    ledger.note("no @STATUS answer at %ds" % now)
                    if time.time() - last_answer > 90:
                        reasons.append("no @STATUS for 90 s")
                        ledger.note("stopping: the main stopped answering")
                        break
                else:
                    last_answer = time.time()
                    record(f, st, now)
                    if st.get("role") != "main" or to_int(st.get("sd_mb")) == 0:
                        reasons.append("main came back as %s with sd_mb=%s" % (st.get("role"), st.get("sd_mb")))
                        ledger.note("stopping: the main lost its role or its card")
                        break
            if mon.resets >= args.max_resets:
                ledger.note("stopping: %d resets" % mon.resets)
                reasons.append("cut short after %d resets" % mon.resets)
                break
            mon.drain()
            time.sleep(0.5)
        stop.set()
        for w in workers:
            w.join(timeout=10)
        mon.drain()
        st = mon.poll()  # a last row, after the callers hung up
        if st is not None:
            record(f, st, time.time() - start)
    try:
        mon.command("@LOG", 4.0)  # the board's own event log goes into the raw log too
        ser.close()
    except OSError:
        pass
    raw.close()
    if not seen_window:
        reasons.append("@STATUS never gave wmin/wlarge (old firmware?)")
    if mon.resets:
        reasons.append("reset x%d" % mon.resets)
    if mon.alloc_failed:
        reasons.append("ALLOC FAILED x%d" % mon.alloc_failed)
    for w in workers:  # what each caller really did, so a caller stuck at a menu shows
        say("%s %s rounds=%d %s" % (w.name, w.host, w.rounds, " ".join("%s=%d" % kv for kv in sorted(w.stats.items()))))
    for w in workers:
        want = {"caller-A": "fights_won", "caller-B": "packs_opened", "caller-C": "zmodem_ok", "caller-D": "screens"}[w.name]
        if not w.stats.get(want) and not (want == "fights_won" and w.stats.get("town_screens")):
            ledger.note("%s never did its job (%s=0)" % (w.name, want))
            reasons.append("%s made no progress" % w.name)  # a caller stuck at a menu put no load on the board
    for line in mon.panic_lines[:10]:
        say("panic: " + line)
    verdict = "FAIL" if reasons else "PASS"
    say("%s lowest free=%s largest=%s min=%s wmin=%s wlarge=%s stk_web=%s stk_mdns=%s stk_timer=%s resets=%d "
        "alloc_failed=%d rounds=%s%s" % (
            verdict, low.get("heap"), low.get("largest"), low.get("min"), low.get("wmin"), low.get("wlarge"),
            low.get("stk_web"), low.get("stk_mdns"), low.get("stk_timer"), mon.resets, mon.alloc_failed,
            "/".join(str(w.rounds) for w in workers), (" because " + ", ".join(reasons)) if reasons else ""))
    say("serial log (local only, may hold the public hostname): " + raw_path)
    return 1 if reasons else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        say("stopped")
        sys.exit(1)
