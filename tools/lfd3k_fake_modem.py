#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""A pretend LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM on a pty, for trying axis --modem without a radio.

usage: lfd3k_fake_modem.py LINK [one|stop]      (then: axis --modem LINK ...)

LINK becomes a symlink to the pty's slave side, so axis can be pointed at it before it exists and finds the new one
after the pretend modem is "unplugged". It plays three calls, printing what axis sends:
  1. RING, waits for ATA, CONNECT LORA SF9 ..., shows the screen, then NO CARRIER (the caller hangs up);
     (before that the caller types "NO CARRIER" + CR, which must not end the call);
  2. the same, then wrong setup PINs until AXIS says goodbye, and NO CARRIER during the guard time: expects +++
     after the guard time all the same, then ATH;
  3. the same, then the pty is closed mid-call (the modem unplugged) for 12 s and opened again (a new pty), where
     it answers AT&V and plays one more RING/CONNECT/NO CARRIER.
With "one" it plays a single 25 s call instead, for a set-up board (AXIS logs it as a RADIO SF9 caller).
With "stop" it plays a single call and keeps it up, printing what axis sends, for stopping axis mid-call: axis
must send +++ and then ATH (answered OK) before it exits.
Only the Python standard library; Linux only.
"""
import os
import pty
import select
import sys
import time
import tty

CONNECT = b'\r\nCONNECT LORA SF9 BW125 1758/ARQ/LZ\r\n'
T0 = time.time()


def log(msg):
    print(f'{time.time() - T0:7.2f} fake: {msg}', flush=True)


def show(data):
    out = ''.join(chr(b) if 32 <= b < 127 else '^[' if b == 27 else '\\r' if b == 13 else '\\n' if b == 10
                  else f'<{b:02x}>' for b in data)
    return out if len(out) < 600 else out[:600] + '...'


class Modem:
    def __init__(self, link):
        self.link = link
        self.master, self.slave = pty.openpty()
        tty.setraw(self.slave)   # no echo or line handling before axis sets it raw itself
        name = os.ttyname(self.slave)
        tmp = link + '.new'
        if os.path.lexists(tmp):
            os.unlink(tmp)
        os.symlink(name, tmp)
        os.replace(tmp, link)
        log(f'pty {name}, {link} points at it')

    def close(self):
        os.close(self.master)
        os.close(self.slave)
        log('pty closed (the modem is unplugged)')

    def send(self, b):
        os.write(self.master, b)

    def read(self, secs, until=None):
        """Everything axis writes for `secs` seconds (or until `until` is in it)."""
        got = b''
        end = time.time() + secs
        while time.time() < end:
            r, _, _ = select.select([self.master], [], [], 0.1)
            if r:
                try:
                    got += os.read(self.master, 4096)
                except OSError:
                    break
                if until and until in got:
                    break
        return got

    def wait_probe(self):
        got = self.read(15, b'AT&V\r')
        log(f'axis sent {show(got)!r}')
        if b'AT&V\r' in got:
            self.send(b'\r\nLFD3K-SM settings\r\nNAME TESTSTN\r\nOK\r\n')
            log('answered AT&V with NAME TESTSTN')

    def ring(self):
        self.send(b'\r\nRING\r\n')
        log('sent RING')
        got = self.read(5, b'ATA\r')
        log(f'axis answered {show(got)!r}')
        if b'ATA\r' not in got:
            sys.exit('fake: no ATA')
        time.sleep(0.3)
        self.send(CONNECT)
        log('sent ' + CONNECT.decode().strip())
        screen = self.read(4)
        log(f'screen from AXIS ({len(screen)} bytes, {screen.count(255)} IAC bytes): {show(screen)!r}')


def main():
    link = sys.argv[1]
    m = Modem(link)
    m.wait_probe()
    if sys.argv[2:] == ['one']:   # one 25 s call ended by NO CARRIER (a set-up board tags it RADIO SFn)
        m.ring()
        m.read(25)
        m.send(b'\r\nNO CARRIER\r\n')
        log('sent NO CARRIER')
        m.read(2)
        return
    if sys.argv[2:] == ['stop']:  # one call kept up while axis is stopped (SIGTERM): expects +++, then ATH
        m.ring()
        log('the call is up: stop axis now')
        got = b''
        end = time.time() + 30
        while time.time() < end and b'ATH\r' not in got:
            chunk = m.read(0.1)
            if chunk:
                got += chunk
                log(f'axis sent {show(chunk)!r}')
        if b'ATH\r' in got:
            m.send(b'\r\nOK\r\n')
            log('answered ATH with OK')
        else:
            log('no ATH from axis')
        m.read(1)
        return

    log('--- call 1: the caller hangs up (NO CARRIER)')
    m.ring()
    m.send(b'\r')
    log(f'after a CR: {show(m.read(12))!r}')   # long enough for AXIS's 10 s radio report to have gone twice
    m.send(b'NO CARRIER\r')                    # the caller typing it: not the modem's framed result code
    log(f'caller typed NO CARRIER + CR, the call is still up: {show(m.read(2))!r}')
    m.send(b'\r\nNO CAR')                       # the real one, split across two reads (20 ms apart)
    time.sleep(0.02)
    m.send(b'RIER\r\n')
    log('sent NO CARRIER (framed, in two pieces)')
    log(f'after NO CARRIER: {show(m.read(3))!r}')

    log('--- call 2: AXIS hangs up (wrong setup PINs)')
    time.sleep(1)
    m.ring()
    got, last = b'', time.time()
    for _ in range(8):
        m.send(b'1\r')
        end = time.time() + 1
        while time.time() < end:
            chunk = m.read(0.05)
            got += chunk
            if chunk and b'+++' not in got:
                last = time.time()
        if b'Goodbye' in got:
            break
    log(f'PIN replies: {show(got)!r}')
    m.send(b'\r\nNO CARRIER\r\n')   # the caller drops during the hang-up guard: +++ and ATH must come all the same
    log('sent NO CARRIER during the guard time')
    if b'+++' not in got:
        got += m.read(6, b'+++')
    log(f'+++ came {time.time() - last:.2f} s after the last screen byte')
    t = time.time()
    got = m.read(4, b'ATH\r')
    log(f'{time.time() - t:.2f} s after +++ axis sent {show(got)!r}')
    m.send(b'\r\nOK\r\n')
    log('answered ATH with OK')
    m.read(2)

    log('--- call 3: the modem is unplugged mid-call')
    m.ring()
    m.close()
    time.sleep(12)
    m = Modem(link)
    m.wait_probe()
    m.ring()
    m.send(b'\r\nNO CARRIER\r\n')
    log('sent NO CARRIER')
    m.read(2)
    log('done')


if __name__ == '__main__':
    main()
