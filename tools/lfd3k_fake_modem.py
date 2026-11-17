#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""A pretend LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM on a pty, for trying axis --modem without a radio.

usage: lfd3k_fake_modem.py LINK [one|stop|strand MARKDIR]      (then: axis --modem LINK ...)

LINK becomes a symlink to the pty's slave side, so axis can be pointed at it before it exists and finds the new one
after the pretend modem is "unplugged". It answers as the real firmware does: in a call, +++ with the guard time is
answered OK and ATH then NO CARRIER; in command mode +++ is only text (the CR before ATH makes it ERROR) and ATH is
OK. Every open of the port must bring guard, +++, guard, ATH before AT&V (checked and logged). It plays three calls:
  1. RING, waits for ATA, CONNECT LORA SF9 ..., shows the screen, then NO CARRIER (the caller hangs up);
     (before that the caller types "NO CARRIER" + CR, which must not end the call);
  2. the same, then wrong setup PINs until AXIS says goodbye, and NO CARRIER during the guard time: expects +++
     after the guard time all the same, then ATH;
  3. the same, then the pty is closed mid-call (the modem unplugged) for 12 s and opened again (a new pty), where
     it answers the open's ATH and AT&V and plays one more RING/CONNECT/NO CARRIER.
With "one" it plays a single 25 s call instead, for a set-up board (AXIS logs it as a RADIO SF9 caller).
With "stop" it plays a single call and keeps it up, printing what axis sends, for stopping axis mid-call: axis
must send +++ (answered OK) and then ATH (answered NO CARRIER) before it exits.
With "strand" (a board that is not set up) it ignores AXIS's +++ so the hang-up fails, and touches marker files in
MARKDIR for a driver script: stranded (a typed RING and CONNECT must do nothing; the minute's retry is answered),
stranded2 (recovered by a framed NO CARRIER), callup (kill axis now and start it again: the new start must hang the
call up before AT&V) and stranded3 (stop axis now: one more +++, no ATH).
Only the Python standard library; Linux only.
"""
import os
import pty
import select
import sys
import time
import tty

CONNECT = b'\r\nCONNECT LORA SF9 BW125 1758/ARQ/LZ\r\n'
OK = b'\r\nOK\r\n'
NO_CARRIER = b'\r\nNO CARRIER\r\n'
CMD_ERROR_OK = b'\r\nERROR\r\n\r\nOK\r\n'   # command mode: the CR ends the "+++" line (ERROR), then ATH is OK
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

    def wait_probe(self, in_call=False):
        """axis opening the port: guard, +++, guard, CR ATH CR, then AT&V. With in_call the pretend modem is still
        in a call left up from before (+++ is answered OK, ATH NO CARRIER); else it is in command mode."""
        got = self.read(20, b'+++')
        if b'+++' in got and in_call:
            time.sleep(0.5)
            self.send(OK)
            log('answered +++ with OK (a call was still up)')
        got += self.read(6, b'ATH\r')
        if b'ATH\r' in got:
            self.send(NO_CARRIER if in_call else CMD_ERROR_OK)
            log('answered ATH with ' + ('NO CARRIER' if in_call else 'ERROR, OK (command mode)'))
        got += self.read(6, b'AT&V\r')
        log(f'axis sent {show(got)!r}')
        at = [got.find(x) for x in (b'+++', b'ATH\r', b'AT&V\r')]
        good = -1 not in at and at == sorted(at)
        log('open: ' + ('+++, then ATH, then AT&V' if good else f'WRONG ORDER {at}'))
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

    def wrong_pins(self):
        """Wrong setup PINs until AXIS says goodbye; returns what came and when its last screen byte came."""
        got, last = b'', time.time()
        for _ in range(8):
            self.send(b'1\r')
            end = time.time() + 1
            while time.time() < end:
                chunk = self.read(0.05)
                got += chunk
                if chunk and b'+++' not in got:
                    last = time.time()
            if b'Goodbye' in got:
                break
        return got, last

    def hangup_answered(self, in_call=True, secs=30):
        """axis hanging up: +++ (answered OK after half a second when in_call), then CR ATH CR."""
        got = self.read(secs, b'+++')
        if b'+++' not in got:
            log('no +++ from axis')
            return
        t = time.time()
        if in_call:
            time.sleep(0.5)
            self.send(OK)
            log('answered +++ with OK')
        got = self.read(5, b'ATH\r')
        log(f'{time.time() - t:.2f} s after +++ axis sent {show(got)!r}')
        if b'ATH\r' in got:
            self.send(NO_CARRIER if in_call else CMD_ERROR_OK)
            log('answered ATH with ' + ('NO CARRIER' if in_call else 'ERROR, OK (command mode)'))
        else:
            log('no ATH from axis')

    def ignore_escapes(self, tries=3):
        """axis hanging up while the pretend modem ignores +++ (it stays in data mode): counts the +++ tries, checks
        that no ATH is sent as call bytes, and waits out the last try's guard."""
        got, t0, n = b'', time.time(), 0
        while n < tries and time.time() - t0 < 60:
            got += self.read(0.1)
            while got.count(b'+++') > n:
                n += 1
                log(f'+++ number {n} ignored, {time.time() - t0:.2f} s in')
        got += self.read(10)   # the last try's guard (4x modem_guard_ms) and the stranded log line
        log('no ATH sent as call bytes' if b'ATH' not in got else 'ATH SENT AS CALL BYTES')
        return n


def mark(markdir, name):
    with open(os.path.join(markdir, name), 'w') as f:
        f.write(f'{time.time()}\n')
    log(f'marker {name}')


def strand(m, markdir):
    log('--- strand 1: AXIS hangs up, +++ ignored three times, then the minute\'s retry is answered')
    m.ring()
    got, _ = m.wrong_pins()
    log(f'PIN replies: {show(got)!r}')
    m.ignore_escapes()
    mark(markdir, 'stranded')
    m.send(b'\r\nRING\r\n')            # the caller typing result codes: nothing may come of them
    m.send(b'\r\nCONNECT LORA SF5 BW500 62500/ARQ/LZ\r\n')
    m.send(b'RING\rCONNECT 9600\r')
    got = m.read(4)
    log('typed RING and CONNECT: ' + ('axis sent nothing' if not got else f'axis sent {show(got)!r}'))
    log('waiting for the retry (+++ about a minute after the strand)')
    t = time.time()
    m.hangup_answered(in_call=True, secs=80)
    log(f'retry came {time.time() - t:.1f} s after the typed codes')
    m.read(2)
    m.ring()                            # the line answers calls again
    m.send(NO_CARRIER)
    log('sent NO CARRIER')
    m.read(2)

    log('--- strand 2: the same, then the far end drops (framed NO CARRIER)')
    m.ring()
    m.wrong_pins()
    m.ignore_escapes()
    mark(markdir, 'stranded2')
    m.send(NO_CARRIER)
    log('sent NO CARRIER')
    got = m.read(3)
    log(f'after NO CARRIER axis sent {show(got)!r}')
    m.ring()
    m.send(NO_CARRIER)
    log('sent NO CARRIER')
    m.read(2)

    log('--- restart: a call is up when axis dies; the next start must hang it up before AT&V')
    m.ring()
    mark(markdir, 'callup')
    m.wait_probe(in_call=True)
    m.ring()
    m.send(NO_CARRIER)
    log('sent NO CARRIER')
    m.read(2)

    log('--- stop while stranded: one more +++ (ignored), no ATH, axis exits in time')
    m.ring()
    m.wrong_pins()
    m.ignore_escapes()
    mark(markdir, 'stranded3')
    got = m.read(8)
    log(f'during the stop axis sent {show(got)!r}: ' + ('no ATH' if b'ATH' not in got else 'ATH SENT AS CALL BYTES'))
    log('done')


def main():
    link = sys.argv[1]
    m = Modem(link)
    m.wait_probe()
    if sys.argv[2:3] == ['strand']:
        strand(m, sys.argv[3])
        return
    if sys.argv[2:] == ['one']:   # one 25 s call ended by NO CARRIER (a set-up board tags it RADIO SFn)
        m.ring()
        m.read(25)
        m.send(NO_CARRIER)
        log('sent NO CARRIER')
        m.read(2)
        return
    if sys.argv[2:] == ['stop']:  # one call kept up while axis is stopped (SIGTERM): expects +++, then ATH
        m.ring()
        log('the call is up: stop axis now')
        m.hangup_answered(in_call=True, secs=30)
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
    got, last = m.wrong_pins()
    log(f'PIN replies: {show(got)!r}')
    m.send(NO_CARRIER)   # the caller drops during the hang-up guard: +++ and ATH must come all the same
    log('sent NO CARRIER during the guard time')
    if b'+++' not in got:
        got += m.read(6, b'+++')
    log(f'+++ came {time.time() - last:.2f} s after the last screen byte')
    t = time.time()
    got = m.read(4, b'ATH\r')
    log(f'{time.time() - t:.2f} s after +++ axis sent {show(got)!r}')
    m.send(CMD_ERROR_OK)   # the call is over, so the modem is in command mode
    log('answered ATH with ERROR, OK (command mode)')
    m.read(2)

    log('--- call 3: the modem is unplugged mid-call')
    m.ring()
    m.close()
    time.sleep(12)
    m = Modem(link)
    m.wait_probe()
    m.ring()
    m.send(NO_CARRIER)
    log('sent NO CARRIER')
    m.read(2)
    log('done')


if __name__ == '__main__':
    main()
