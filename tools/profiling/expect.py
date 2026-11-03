# usage: expect.py script.txt outfile prog args...
# script lines: <regex>\t<keys>   (keys use \r etc; empty regex = just wait 1s)
import os, pty, sys, time, select, re, signal
script, out, argv = sys.argv[1], sys.argv[2], sys.argv[3:]
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm'; os.execvp(argv[0], argv)
buf = b''; mark = 0; alive = True
def pump(t):
    global buf, alive
    end = time.time() + t
    while time.time() < end and alive:
        r,_,_ = select.select([fd],[],[],0.05)
        if r:
            try:
                d = os.read(fd, 65536)
                if not d: alive = False
                buf += d
            except OSError: alive = False
def text(b): return re.sub(r'\s+', ' ', re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]', b' ', b).decode('latin1'))
for ln in open(script):
    ln = ln.rstrip('\n')
    if not ln or ln.startswith('#'): continue
    pat, keys = ln.split('\t', 1)
    t0 = time.time()
    while alive and pat and not re.search(pat, text(buf[mark:]), re.I) and time.time() - t0 < 25:
        pump(0.2)
    if pat and not re.search(pat, text(buf[mark:]), re.I):
        print('TIMEOUT waiting for', repr(pat)); break
    pump(0.4)
    mark = len(buf)
    for ch in keys.encode().decode('unicode_escape').encode('latin1'):
        try: os.write(fd, bytes([ch]))
        except OSError: break
        pump(0.12)
    print('ok', pat, '->', repr(keys))
pump(4)
if alive:
    os.kill(pid, signal.SIGTERM); time.sleep(0.5)
    try: os.kill(pid, signal.SIGKILL)
    except ProcessLookupError: pass
open(out, 'wb').write(buf)
print('--- tail ---'); print(text(buf)[-1500:])
