# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Capture the documentation's screenshots and animations from the live boards.

Callers log in over telnet (an 80x25 ANSI/CP437 terminal runs in here, see bbs_shots.py) and play
scripted scenes; each scene saves PNGs and an animated GIF of what the caller saw. Logins come from
the git-ignored local/ files (test-credentials.txt for the sysop, test-users.txt for test callers).
A password is only ever typed after its prompt is on screen.

usage: python -I tools/screenshots/docs_capture.py OUTDIR [scene ...]   (no scenes: all of them)
"""
import os
import re
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bbs_shots import Term, strip_telnet  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
BOARD1, BOARD2 = '192.168.1.164', '192.168.1.165'


def creds():
    t = open(os.path.join(ROOT, 'local', 'test-credentials.txt')).read()
    out = {'sysop': (re.search(r'^handle:\s*(.+)$', t, re.M).group(1).strip(), re.search(r'^password:\s*(.+)$', t, re.M).group(1).strip())}
    for line in open(os.path.join(ROOT, 'local', 'test-users.txt')):
        if not line.startswith('#') and line.count('|') == 2:
            h, _, pw = line.rstrip('\n').split('|')
            out[h] = (h, pw)
    return out


class Caller:
    def __init__(self, out, host, who, tag):
        self.out, self.host, self.who, self.tag = out, host, who, tag
        self.s = socket.create_connection((host, 23), 10)
        self.s.settimeout(0.05)
        self.term = Term()
        self.frames = None  # recording a GIF: (images, durations)
        self.last = 0

    def text(self):
        return self.term.text()

    def pump(self, secs, until=None, frame_ms=140):
        end = time.time() + secs
        while time.time() < end:
            try:
                d = self.s.recv(4096)
                if not d:
                    return False
                self.term.feed(strip_telnet(d).decode('cp437', 'replace'))
            except socket.timeout:
                pass
            if self.frames is not None and time.time() - self.last >= frame_ms / 1000:
                self.last = time.time()
                self.frames[0].append(self.term.image(self.title()).convert('P', palette=1, colors=32))
                self.frames[1].append(frame_ms)
            if until and re.search(until, self.text(), re.I):
                self.pump(0.6) if until else None
                return True
        return until is None

    def title(self):
        return f'AXIS UNDERGROUND CNC   telnet {self.host}   {self.tag}'

    def keys(self, k, secs=1.5, until=None, slow=0):
        if slow:  # typed like a person, for the animations
            for ch in k:
                self.s.sendall(ch.encode('latin1'))
                self.pump(slow)
        else:
            self.s.sendall(k.encode('latin1'))
        return self.pump(secs, until)

    def shot(self, name):
        self.term.image(self.title()).save(os.path.join(self.out, name + '.png'))
        print('  ', name + '.png')

    def gif_start(self):
        self.frames = ([], [])
        self.last = 0

    def gif_save(self, name, hold_ms=2500):
        imgs, durs = self.frames
        self.frames = None
        if not imgs:
            return
        # drop repeated frames (merge their time) so the files stay small
        keep_i, keep_d = [imgs[0]], [durs[0]]
        for im, d in zip(imgs[1:], durs[1:]):
            if im.tobytes() == keep_i[-1].tobytes():
                keep_d[-1] += d
            else:
                keep_i.append(im)
                keep_d.append(d)
        keep_d[-1] += hold_ms
        keep_i[0].save(os.path.join(self.out, name + '.gif'), save_all=True, append_images=keep_i[1:], duration=keep_d, loop=0, optimize=True)
        print('  ', name + '.gif', len(keep_i), 'frames')

    def login(self, gif=None):
        if gif:
            self.gif_start()
        self.pump(20, r'picks|login|Handle')
        if re.search(r'picks|login', self.text()) and 'Handle' not in self.text():
            self.keys('l', 2, 'Handle')
        self.keys(self.who[0] + '\r', 2, 'assword')
        if 'assword' not in self.text():
            raise SystemExit('no password prompt for ' + self.who[0] + ': stopping (never type a password blind)')
        self.keys(self.who[1] + '\r', 4, r'y/N|Main Menu|Command')
        if gif:
            self.gif_save(gif)

    def menu(self):
        if re.search(r'y/N', self.text()):
            self.keys('n', 3, r'Main Menu|Command')

    def bye(self):
        try:
            self.s.sendall(b'g')
            self.pump(1)
            self.s.close()
        except OSError:
            pass


def scene_login(out, c):
    print('scene: login')
    a = Caller(out, BOARD1, c['sysop'], 'login')
    a.gif_start()
    a.pump(9)  # the RotatoCAM splash: typed in like a 9600 baud modem, then its moving parts
    a.gif_save('splash', 1000)
    a.shot('splash')
    a.pump(12, r'picks|login|Handle')
    a.gif_start()
    a.shot('matrix')
    for k in ['\x1b[C', '\x1b[C', '\x1b[C', '\x1b[D', '\x1b[D', '\x1b[D']:
        a.keys(k, 0.8)
    a.gif_save('matrix', 1500)
    a.keys('\r', 2, 'Handle')
    a.keys(a.who[0] + '\r', 2, 'assword')
    if 'assword' not in a.text():
        raise SystemExit('no password prompt: stopping')
    a.keys(a.who[1] + '\r', 4, r'y/N|Main Menu')
    a.shot('oneliners')
    a.keys('n', 3, 'Main Menu')
    a.shot('main-menu')
    a.bye()


def scene_files(out, c):
    print('scene: files')
    a = Caller(out, BOARD2, c['C4MH34D'], 'files (board 2, over the link)')
    a.login()
    a.menu()
    a.gif_start()
    a.keys('t', 5, 'Area number')
    a.shot('files-areas')
    a.keys('20\r', 6, r'\[Q\]uit')
    a.shot('files-list')
    a.keys('i', 1)
    a.keys('2048gb.gb\r', 4, r'\[Q\]uit')
    a.shot('files-info')
    a.keys('d', 1)
    a.keys('2048gb.gb\r', 3, r'\[Q\]uit')
    a.shot('files-download-gate')
    a.gif_save('files')
    a.keys('q', 3, 'Area number')
    a.gif_start()
    a.keys('/', 1)
    a.keys('tobu\r', 20, 'found', )
    a.shot('files-search')
    a.keys('n', 15, r'last call\.|new file')
    a.shot('files-new')
    a.gif_save('files-search')
    a.keys('10\r', 6, r'\[Q\]uit')
    a.keys('i', 1)
    a.keys('ACID-50A.ZIP\r', 6, r'\[Q\]uit')
    a.shot('files-archive')
    a.bye()


def scene_messages(out, c):
    print('scene: messages')
    a = Caller(out, BOARD1, c['sysop'], 'messages')
    a.login()
    a.menu()
    a.gif_start()
    a.keys('m', 4, 'Area number')
    a.shot('msgs-areas')
    a.keys('1\r', 3)
    a.shot('msgs-list')
    a.keys('r1\r', 3)
    a.shot('msgs-read')
    a.keys('a', 1.5)
    a.keys('\r', 1)
    a.keys('\r', 1.5)
    a.keys('screenshots for the docs, ignore me\r', 1, slow=0.03)
    a.keys('/s\r', 3)
    a.shot('msgs-reply')
    a.gif_save('messages')
    a.keys('q', 2)
    a.keys('\r', 2)
    b = Caller(out, BOARD2, c['C4MH34D'], 'new scan')
    b.login()
    b.menu()
    b.gif_start()
    b.keys('!', 6)
    b.shot('msgs-scan')
    for _ in range(6):
        if 'caught up' in b.text():
            break
        b.keys('n', 2.5)
    b.gif_save('msgs-scan')
    b.bye()
    a.bye()


def scene_email(out, c):
    print('scene: email')
    a = Caller(out, BOARD2, c['CheesyFingersCNC'], 'email (CheesyFingersCNC)')
    a.login()
    a.menu()
    a.gif_start()
    a.keys('e', 3, 'Email')
    a.keys('s', 1)
    a.keys('C4MH34D\r', 1.5, slow=0.04)
    a.keys('jig files\r', 1.5, slow=0.04)
    a.keys('the 4th axis jig files are in the vault\r', 1, slow=0.02)
    a.keys('/s\r', 3)
    a.shot('email-send')
    a.gif_save('email-send')
    a.bye()
    b = Caller(out, BOARD2, c['C4MH34D'], 'email (C4MH34D)')
    b.login()
    b.menu()
    b.shot('email-notice')
    b.gif_start()
    b.keys('e', 3, 'Email')
    b.shot('email-inbox')
    m = re.findall(r'(\d+)\s+\*?\s*CheesyFingersCNC', b.text())
    b.keys((m[-1] if m else '1') + '\r', 3)
    b.shot('email-read')
    b.gif_save('email-read')
    b.bye()


def scene_multinode(out, c):
    print('scene: multinode')
    a = Caller(out, BOARD1, c['sysop'], 'multinode (Rev)')
    b = Caller(out, BOARD2, c['C4MH34D'], 'multinode (C4MH34D)')
    a.login()
    a.menu()
    b.login()
    b.menu()
    a.keys('=', 2, 'MultiNode')
    a.shot('multinode-menu')
    a.keys('w', 2.5)
    a.shot('multinode-who')
    nodes = dict((h, n) for n, h in re.findall(r'node (\d+)\s+(\S+)', a.text()))
    to = nodes.get('C4MH34D', '3')
    b.gif_start()
    a.keys('m', 1)
    a.keys(to + '\r', 1.5)
    a.keys('nice work on the jig\r', 2, slow=0.03)
    b.pump(2)
    b.shot('multinode-note')
    b.gif_save('multinode-note')
    # split screen chat: Rev asks, C4 accepts, both type
    a.keys('s', 1)
    a.keys(to + '\r', 2)
    b.pump(3, 'Accept')
    b.shot('split-ask')
    b.gif_start()
    b.keys('y', 2)
    a.pump(1)
    for ka, kb in [('hey C4, how is alabama', 'hot and humid, chips flying'), ('good. LORD tonight?', 'only if you bring a red dragon')]:
        a.keys(ka + '\r', 0.2, slow=0.05)
        b.pump(0.5)
        b.keys(kb + '\r', 0.2, slow=0.05)
        a.pump(0.5)
    b.pump(1)
    b.shot('split-chat')
    b.gif_save('split-chat', 3000)
    a.keys('\x1b', 2)
    b.pump(2)
    # teleconference
    a.keys('c', 2)
    b.keys('c', 2)
    b.gif_start()
    a.keys('evening, underground\r', 1, slow=0.04)
    b.pump(1)
    b.keys('yo rev, the board looks sick\r', 1, slow=0.04)
    b.pump(1.5)
    b.shot('teleconference')
    b.gif_save('teleconference')
    a.keys('/q\r', 2)
    b.keys('/q\r', 2)
    a.bye()
    b.bye()


def scene_extras(out, c):
    print('scene: extras')
    a = Caller(out, BOARD2, c['C4MH34D'], 'extras')
    a.login()
    a.menu()
    a.keys('r', 2)
    a.keys('l', 2.5)
    a.shot('rumors')
    a.keys('q', 3, 'Main Menu')
    a.keys('b', 4)
    a.shot('bbs-list')
    a.keys('q', 3, 'Main Menu')
    a.keys('-', 4)
    a.shot('top-ten')
    a.keys('v', 3)
    a.keys('1', 3)
    a.shot('voting')
    a.keys('x', 2)
    a.keys('q', 3, 'Main Menu')
    a.keys('w', 3)
    a.shot('last-callers')
    a.keys('o', 1)
    a.keys('\r', 3)
    a.gif_start()
    a.keys('a', 3, 'Change')
    a.shot('account')
    a.keys('t', 1)
    a.keys('chips over alabama\r', 3, slow=0.04)
    a.gif_save('account')
    a.keys('q', 3)
    a.bye()


def scene_sysop(out, c):
    print('scene: sysop')
    a = Caller(out, BOARD1, c['sysop'], 'sysop')
    a.login()
    a.menu()
    a.keys('*', 3)
    a.shot('user-editor')
    m = re.search(r'(\d+)\s+C4MH34D', a.text())
    a.keys((m.group(1) if m else '1') + '\r', 3)
    a.shot('user-editor-caller')
    a.keys('q', 2)
    a.keys('q', 3)
    a.bye()


def scene_doors(out, c):
    print('scene: doors')
    a = Caller(out, BOARD1, c['sysop'], 'doors')
    a.login()
    a.menu()
    a.keys('o', 3, 'Door number')
    a.shot('doors')
    doors = dict((name.strip(), k) for k, name in re.findall(r'\[(\w)\]\s+([^\n]+?)\s{2,}', a.text()))
    viewer = next((k for n, k in doors.items() if 'ANSI' in n), None)
    if viewer:
        a.gif_start()
        a.keys(viewer, 6)
        a.shot('ansi-viewer')
        a.keys('1\r', 5)
        a.shot('ansi-viewer-pieces')
        a.keys('1\r', 8)
        a.shot('ansi-viewer-art')
        a.gif_save('ansi-viewer')
        for _ in range(4):
            a.keys('q', 2)
    a.bye()


def scene_dos(out, c):
    print('scene: dos')
    pw = open(os.path.join(ROOT, 'local', 'dospass.txt')).read().strip().splitlines()[-1].strip()
    a = Caller(out, BOARD1, c['sysop'], 'DOS prompt door')
    a.login()
    a.menu()
    a.keys('o', 3, 'Door number')
    m = re.search(r'\[(\w)\]\s+DOS Prompt', a.text())
    if not m:
        print('   (no DOS Prompt door listed: skipped)')
        a.bye()
        return
    a.keys(m.group(1), 5, 'DOS prompt password')
    if 'DOS prompt password' not in a.text():
        print('   (no password prompt: skipped)')
        a.bye()
        return
    a.keys(pw + '\r', 3, r'C:\\>')
    a.gif_start()
    for cmd in ['ver', 'help', 'md demo', 'tree', 'dir']:
        a.keys(cmd + '\r', 1.5, slow=0.06)
    a.shot('dos-prompt')
    a.keys('\x1b[A', 1)
    a.keys('\r', 1.5)
    a.keys('rd demo\r', 1.5, slow=0.06)
    a.gif_save('dos-prompt')
    a.keys('exit\r', 3)
    a.bye()


def scene_axis(out, c):
    """The Axis look: the theme is set board-wide, so this switches it over the serial console and back."""
    print('scene: axis look (needs the serial console on COM18)')
    import serial

    def setting(v):
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = 'COM18', 115200, 0.5
        s.dtr = s.rts = False
        s.open()
        s.write(('@SET theme=' + v + '\n').encode())
        time.sleep(2)
        s.close()

    setting('axis')
    try:
        a = Caller(out, BOARD1, c['sysop'], 'Axis look')
        a.pump(20, 'Handle')
        a.shot('axis-login')
        a.keys(a.who[0] + '\r', 2, 'assword')
        if 'assword' not in a.text():
            raise SystemExit('no password prompt: stopping')
        a.keys(a.who[1] + '\r', 4, r'y/N|Command')
        a.menu()
        a.shot('axis-main-menu')
        a.keys('t', 4, 'Area number')
        a.shot('axis-files')
        a.bye()
    finally:
        setting('oblivion')


SCENES = {'login': scene_login, 'files': scene_files, 'messages': scene_messages, 'email': scene_email, 'multinode': scene_multinode,
          'extras': scene_extras, 'sysop': scene_sysop, 'doors': scene_doors, 'dos': scene_dos, 'axis': scene_axis}

if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    c = creds()
    for name in (sys.argv[2:] or list(SCENES)):
        SCENES[name](out, c)
        time.sleep(1)
