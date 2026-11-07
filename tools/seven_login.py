# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Log seven callers in and leave a window on each. This process then exits.

The cluster has seven nodes. Callers can dial the main and be handed off, but
the main keeps only its own two sessions and relays at most four more, and it
will not hand a second call to the same board inside ten seconds. Dialing each
board for the nodes it actually holds is what fills all seven:

  192.168.1.164   main    2 callers    nodes 1 and 2
  192.168.1.165   cyd2    2 callers    nodes 3 and 4
  192.168.1.202   ring    3 callers    nodes 5, 6, and 7

Passwords come from local/test-users.txt as handle|location|password.
They are never printed and never put on a command line. RevJMoney is skipped.

Each session is logged in from this script (splash, matrix, handle, password
only after the password prompt, then "n" on a y/N question). A SyncTERM
window attaches after that and shows the screen. Closing a window drops that
caller. This script never sends goodbye and never picks a door.

  python tools/seven_login.py
"""
from __future__ import annotations

import os
import re
import socket
import subprocess
import sys
import time
from urllib.parse import quote

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
USERS = os.path.join(ROOT, "local", "test-users.txt")
HOST = "192.168.1.164"
# (host, how many of that board's own sessions). Order is the seat order.
SEATS = (
    ("192.168.1.164", 2),
    ("192.168.1.202", 3),
    ("192.168.1.165", 2),
)
NEED = sum(n for _, n in SEATS)
STAGGER_S = 8  # same board, so the accept loop is not hit all at once
PORT0 = 2311
SKIP = {"revjmoney"}
# Live callers that are not RevJMoney. Used only for the "file is short" hint.
KNOWN = (
    "C4MH34D",
    "CheesyFingersCNC",
    "RotaryRick",
    "LatheLarry",
    "ChipBreaker",
    "EndmillEd",
    "Spindle Kid",
)
SYNCTERM_CANDIDATES = (
    r"F:\axis-bbs-work\tools\syncterm\syncterm.exe",
    os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "SyncTERM", "syncterm.exe"),
)

IAC, SE, SB = 255, 240, 250
WILL, WONT, DO, DONT = 251, 252, 253, 254
OPT_BINARY, OPT_ECHO, OPT_SGA = 0, 1, 3


def say(line):
    print(line, flush=True)


def find_syncterm():
    for p in SYNCTERM_CANDIDATES:
        if p and os.path.isfile(p):
            return p
    return ""


def load_users(path):
    """(handle, password) in file order. Lines that are not handle|location|password are skipped."""
    if not os.path.isfile(path):
        return []
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            s = raw.strip()
            if not s or s.startswith("#") or s.count("|") != 2:
                continue
            handle, _location, password = s.split("|")
            handle = handle.strip()
            if not handle or not password or handle.lower() in SKIP:
                continue
            out.append((handle, password))
    return out


def seat_plan(users):
    plan = []
    i = 0
    for host, count in SEATS:
        for _ in range(count):
            plan.append((users[i][0], host, 23))
            i += 1
    return plan


def explain_short(users):
    handles = [h for h, _pw in users]
    say("Not dialing. local/test-users.txt has %d caller%s with a password%s." % (
        len(handles), "" if len(handles) == 1 else "s",
        ": " + ", ".join(handles) if handles else ""))
    say("Seven logins need %d lines in that file, each shaped handle|location|password." % NEED)
    missing = [h for h in KNOWN if h not in handles]
    if missing:
        say("Other live callers, besides RevJMoney: " + ", ".join(missing) + ".")
    say("This script does not create accounts. Put the lines in the file, then run it again.")
    exe = find_syncterm()
    say("SyncTERM: " + (exe if exe else "not found, so the windows would be consoles."))


# --- telnet -----------------------------------------------------------------

class TelnetStrip:
    """Drop IAC from the BBS. Application bytes, including a doubled IAC, stay."""

    def __init__(self):
        self.state = 0  # 0 data, 1 after IAC, 2 option byte, 3 sub, 4 sub after IAC

    def feed(self, data):
        out = bytearray()
        for b in data:
            st = self.state
            if st == 0:
                if b == IAC:
                    self.state = 1
                else:
                    out.append(b)
            elif st == 1:
                if b == IAC:
                    out.append(IAC)
                    self.state = 0
                elif b == SB:
                    self.state = 3
                elif WILL <= b <= DONT:
                    self.state = 2
                else:
                    self.state = 0
            elif st == 2:
                self.state = 0
            elif st == 3:
                self.state = 4 if b == IAC else 3
            else:
                self.state = 0 if b == SE else 3
        return bytes(out)


def telnet_encode(data):
    return data.replace(b"\xff", b"\xff\xff")


class TelnetPeer:
    """Talk telnet to SyncTERM. feed() returns the keys. take_reply() is our answers."""

    def __init__(self):
        self.state = 0
        self.reply = bytearray()

    def hello(self):
        # We echo (the board does). Binary so CP437 lives. Suppress go-ahead.
        opts = (OPT_ECHO, OPT_SGA, OPT_BINARY)
        buf = bytearray()
        for opt in opts:
            buf += bytes((IAC, WILL, opt))
        buf += bytes((IAC, DO, OPT_BINARY))
        return bytes(buf)

    def feed(self, data):
        app = bytearray()
        for b in data:
            st = self.state
            if st == 0:
                if b == IAC:
                    self.state = 1
                else:
                    app.append(b)
            elif st == 1:
                if b == IAC:
                    app.append(IAC)
                    self.state = 0
                elif b == SB:
                    self.state = 3
                elif WILL <= b <= DONT:
                    self._cmd = b
                    self.state = 2
                else:
                    self.state = 0
            elif st == 2:
                self._answer(self._cmd, b)
                self.state = 0
            elif st == 3:
                self.state = 4 if b == IAC else 3
            else:
                self.state = 0 if b == SE else 3
        return bytes(app)

    def _answer(self, cmd, opt):
        if cmd == DO and opt in (OPT_BINARY, OPT_ECHO, OPT_SGA):
            ans = WILL
        elif cmd == DO:
            ans = WONT
        elif cmd == WILL and opt in (OPT_BINARY, OPT_SGA):
            ans = DO
        elif cmd == WILL:
            ans = DONT
        elif cmd == WONT:
            ans = DONT
        else:
            ans = WONT
        self.reply += bytes((IAC, ans, opt))

    def take_reply(self):
        r = bytes(self.reply)
        self.reply.clear()
        return r


# --- one caller -------------------------------------------------------------

class Call:
    def __init__(self, host, handle, password):
        self.host = host
        self.handle = handle
        self.password = password
        self.sock = None
        self.strip = TelnetStrip()
        self.app = bytearray()

    def text(self):
        return self.app.decode("cp437", "replace")

    def connect(self):
        self.sock = socket.create_connection((self.host, 23), 12)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

    def pump(self, seconds, until=None):
        end = time.time() + seconds
        self.sock.settimeout(0.2)
        while time.time() < end:
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                data = None
            except OSError:
                return False
            if data is None:
                pass
            elif data == b"":
                return False
            else:
                self.app += self.strip.feed(data)
                if len(self.app) > 250000:
                    del self.app[: len(self.app) - 200000]
            if until and re.search(until, self.text(), re.I):
                self.pump(0.4)  # drain a little more, without matching again
                return True
        return until is None

    def send(self, s):
        self.sock.sendall(s.encode("latin1"))

    def login(self):
        # The splash finishes on its own. A key is only sent if that wait expires.
        self.pump(18, r"picks|login|Handle|busy|Too many failed")
        if not re.search(r"picks|login|Handle|busy|Too many failed", self.text(), re.I):
            self.send("\r")
            self.pump(12, r"picks|login|Handle|busy|Too many failed")
        low = self.text()
        if re.search(r"busy|Too many failed", low, re.I):
            raise RuntimeError("the board turned this call away")
        if re.search(r"picks|login", low, re.I) and "Handle" not in low:
            self.send("l")
            self.pump(8, r"Handle")
        if "Handle" not in self.text():
            raise RuntimeError("no handle prompt")
        self.send(self.handle + "\r")
        self.pump(10, r"assword|Wrong")
        if "assword" not in self.text():
            raise RuntimeError("no password prompt, password was not sent")
        sent = len(self.app)
        self.send(self.password + "\r")
        self.pump(15, r"y/N|Main Menu|Command|Wrong password|Too many failed")

        def fresh():
            return self.app[sent:].decode("cp437", "replace")

        if re.search(r"Wrong password|Too many failed", fresh(), re.I):
            raise RuntimeError("login was refused")
        for _ in range(4):
            if re.search(r"Main Menu|Command", fresh(), re.I):
                self.pump(1.2)
                return
            if re.search(r"y/N", fresh(), re.I):
                self.send("n")
                self.pump(8, r"Main Menu|Command|y/N")
                continue
            if re.search(r"press a key|\(press", fresh(), re.I):
                self.send("\r")
                self.pump(6, r"Main Menu|Command|y/N")
                continue
            break
        if not re.search(r"Main Menu|Command", fresh(), re.I):
            raise RuntimeError("logged in but the main menu did not come up")
        self.pump(1.2)

    def close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


def public_tail(call):
    """Last screen line or two, with the password removed, for the log only."""
    text = call.text()
    if call.password:
        text = text.replace(call.password, "")
    text = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", " ", text)
    text = "".join(ch if 32 <= ord(ch) < 127 else " " for ch in text)
    text = re.sub(r" +", " ", text).strip()
    return text[-140:]


def bridge(bbs, replay):
    """Accept one local SyncTERM and copy bytes until either side closes."""
    import select
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", int(os.environ["AXIS_SEVEN_PORT"])))
    srv.listen(1)
    srv.settimeout(180)
    status_ready()
    try:
        term, _addr = srv.accept()
    except socket.timeout:
        raise RuntimeError("SyncTERM did not connect")
    finally:
        srv.close()
    term.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    term.settimeout(0.0)
    bbs.settimeout(0.0)
    peer = TelnetPeer()
    strip = TelnetStrip()
    term.sendall(peer.hello() + telnet_encode(replay))
    while True:
        try:
            readable, _, _ = select.select([bbs, term], [], [], 0.5)
        except OSError:
            break
        if bbs in readable:
            try:
                data = bbs.recv(4096)
            except OSError:
                break
            if not data:
                break
            clean = strip.feed(data)
            if clean:
                try:
                    term.sendall(telnet_encode(clean))
                except OSError:
                    break
        if term in readable:
            try:
                data = term.recv(4096)
            except OSError:
                break
            if not data:
                break
            keys = peer.feed(data)
            reply = peer.take_reply()
            try:
                if reply:
                    term.sendall(reply)
                if keys:
                    bbs.sendall(keys)
            except OSError:
                break
    try:
        term.close()
    except OSError:
        pass


def status_path():
    return os.environ["AXIS_SEVEN_STATUS"]


def status_ready():
    _write_status("ready")


def _write_status(text):
    path = status_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="ascii", errors="replace") as f:
        f.write(text)
    os.replace(tmp, path)


def child_main():
    index = int(sys.argv[2])
    delay = float(sys.argv[3])
    users = load_users(USERS)
    if index >= len(users):
        _write_status("fail: no caller at this seat")
        return 1
    handle, password = users[index]
    host = seat_plan(users)[index][1]
    log_path = os.environ.get("AXIS_SEVEN_LOG", "")
    if log_path:
        sys.stdout = open(log_path, "a", encoding="ascii", errors="replace")
        sys.stderr = sys.stdout
    say("%s dialing %s" % (handle, host))
    if delay:
        time.sleep(delay)
    call = Call(host, handle, password)
    try:
        call.connect()
        call.login()
    except Exception as e:
        say("%s fail: %s" % (handle, e))
        _write_status("fail: " + str(e)[:160])
        call.close()
        return 1
    say("%s at the menu on %s" % (handle, host))
    say(public_tail(call))
    try:
        if os.environ.get("AXIS_SEVEN_CONSOLE") == "1":
            status_ready()
            console_hold(call)
        else:
            bridge(call.sock, bytes(call.app))
    except Exception as e:
        say("%s window closed: %s" % (handle, e))
    finally:
        call.close()
    say("%s hung up (window closed)" % handle)
    return 0


def console_hold(call):
    """No SyncTERM: this console is the terminal. CP437 out, keys through."""
    import msvcrt
    import ctypes
    ctypes.windll.kernel32.SetConsoleOutputCP(437)
    ctypes.windll.kernel32.SetConsoleCP(437)
    ctypes.windll.kernel32.SetConsoleTitleW(call.handle + "  " + call.host)
    sys.stdout.buffer.write(bytes(call.app))
    sys.stdout.buffer.flush()
    sock = call.sock
    sock.settimeout(0.05)
    strip = TelnetStrip()
    while True:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            data = None
        except OSError:
            break
        if data is None:
            pass
        elif data == b"":
            break
        else:
            sys.stdout.buffer.write(strip.feed(data))
            sys.stdout.buffer.flush()
        while msvcrt.kbhit():
            ch = msvcrt.getch()
            if ch == b"\x00" or ch == b"\xe0":
                msvcrt.getch()
                continue
            try:
                sock.sendall(ch)
            except OSError:
                return


# --- parent -----------------------------------------------------------------

def work_dir():
    d = os.path.join(os.environ.get("TEMP", r"C:\Users\rev\AppData\Local\Temp"), "axis-seven")
    os.makedirs(d, exist_ok=True)
    return d


def launch_windows(ready):
    """ready: list of (index, handle, host, port). Returns how many windows opened."""
    exe = find_syncterm()
    opened = 0
    for index, handle, host, port in ready:
        if exe:
            url = "telnet://%s@127.0.0.1:%d" % (quote(handle), port)
            argv = [exe, "-q", "-4", "-iGW", "-l25", url]
            say("  window %s  %s  (%s)" % (handle, url, host))
            spawn(argv, subprocess.DEVNULL, os.path.dirname(exe), console=False)
            opened += 1
        else:
            # The child is already the bridge. A console child is started instead, below.
            pass
        time.sleep(0.3)
    if not exe:
        say("SyncTERM was not found. Use the console windows that are already open.")
    return opened if exe else len(ready)


def parent_main():
    users = load_users(USERS)
    seen = {}
    for handle, _pw in users:
        if handle.lower() in seen:
            say("Not dialing. %s is in local/test-users.txt more than once." % handle)
            return 2
        seen[handle.lower()] = handle
    if len(users) < NEED:
        explain_short(users)
        return 2
    users = users[:NEED]
    plan = seat_plan(users)
    exe = find_syncterm()
    say("Seven callers. The script exits after the windows are up. It does not hang anyone up.")
    say("SyncTERM: " + (exe if exe else "not found, consoles instead."))
    for i, (handle, host, port) in enumerate(plan):
        say("  %d  %-18s  %s:%d" % (i + 1, handle, host, port))
    folder = work_dir()
    children = []
    for i, (handle, host, _port) in enumerate(plan):
        # How many seats on this same host are ahead of this one.
        ahead = sum(1 for h, hh, _p in plan[:i] if hh == host)
        status = os.path.join(folder, "seat%d.status" % i)
        log_path = os.path.join(folder, "seat%d.log" % i)
        for stale in (status, log_path):
            if os.path.isfile(stale):
                os.remove(stale)
        env = os.environ.copy()
        env["AXIS_SEVEN_PORT"] = str(PORT0 + i)
        env["AXIS_SEVEN_STATUS"] = status
        env["AXIS_SEVEN_LOG"] = log_path
        if not exe:
            env["AXIS_SEVEN_CONSOLE"] = "1"
        argv = [sys.executable, os.path.abspath(__file__), "--child", str(i), str(ahead * STAGGER_S)]
        log = open(log_path, "a", encoding="ascii", errors="replace")
        # Environment for the child. Popen env replaces the whole block.
        try:
            flags_note = env
            proc = _spawn_env(argv, log, None, console=not exe, env=flags_note)
        except OSError as e:
            say("%s did not start: %s" % (handle, e))
            log.close()
            continue
        log.close()
        children.append((i, handle, host, PORT0 + i, status, proc))

    deadline = time.time() + 40 + STAGGER_S * 3 + 90
    pending = {i: (handle, host, port, status) for i, handle, host, port, status, _p in children}
    ready = []
    failed = []
    while pending and time.time() < deadline:
        for i, (handle, host, port, status) in list(pending.items()):
            if not os.path.isfile(status):
                continue
            text = open(status, encoding="ascii", errors="replace").read().strip()
            if not text:
                continue
            del pending[i]
            if text == "ready":
                say("%s is at the menu on %s" % (handle, host))
                ready.append((i, handle, host, port))
            else:
                say("%s: %s" % (handle, text))
                failed.append(handle)
        time.sleep(0.4)
    for i, (handle, host, _port, _status) in pending.items():
        say("%s timed out dialing %s. See %s" % (handle, host, os.path.join(folder, "seat%d.log" % i)))
        failed.append(handle)
    if exe:
        launch_windows(ready)
    say("")
    if len(ready) == NEED and not failed:
        say("All seven are up. This script is done. Close a window to drop that caller.")
        return 0
    say("%d of %d stayed up. This script is done. The ones that connected are still connected." % (len(ready), NEED))
    return 1 if ready else 1


def _spawn_env(argv, log, cwd, console, env):
    flags_sets = []
    base = 0x00000200
    away = 0x01000000
    if console:
        flags_sets = [base | away | 0x00000010, base | 0x00000010]
    else:
        quiet = 0x00000008 | 0x08000000
        flags_sets = [base | away | quiet, base | quiet, base]
    last = None
    for flags in flags_sets:
        try:
            return subprocess.Popen(
                argv,
                cwd=cwd,
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=log,
                stderr=subprocess.STDOUT,
                creationflags=flags,
                close_fds=True,
            )
        except OSError as e:
            last = e
    raise last


def selftest():
    s = TelnetStrip()
    assert s.feed(bytes((IAC, WILL, OPT_ECHO, 65))) == b"A"
    assert s.feed(bytes((IAC, IAC, 66))) == bytes((IAC, 66))
    assert telnet_encode(bytes((65, IAC, 66))) == bytes((65, IAC, IAC, 66))
    peer = TelnetPeer()
    assert peer.feed(bytes((IAC, DO, OPT_ECHO, 67))) == b"C"
    assert peer.take_reply() == bytes((IAC, WILL, OPT_ECHO))
    # A split IAC across feeds must not leak the option byte.
    s = TelnetStrip()
    assert s.feed(bytes((IAC,))) == b""
    assert s.feed(bytes((DO, OPT_SGA, 68))) == b"D"
    users = [("A", "x"), ("B", "x"), ("C", "x"), ("D", "x"), ("E", "x"), ("F", "x"), ("G", "x")]
    plan = seat_plan(users)
    assert [p[1] for p in plan] == ["192.168.1.164"] * 2 + ["192.168.1.202"] * 3 + ["192.168.1.165"] * 2
    say("selftest ok")
    return 0


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--selftest":
        return selftest()
    if len(sys.argv) >= 2 and sys.argv[1] == "--child":
        return child_main()
    return parent_main()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        say("stopped")
        sys.exit(1)
