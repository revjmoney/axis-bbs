# usage: react.py rules.py outfile seconds prog args...
# rules.py defines RULES = [(regex, keys_or_callable), ...] checked against the settled screen tail
import os, pty, sys, time, select, re, signal
rules_file, out, secs, argv = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4:]
ns = {}; exec(open(rules_file).read(), ns); RULES = ns['RULES']; state = {'n': {}}
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm'; os.execvp(argv[0], argv)
buf = b''; alive = True; last = time.time(); mark = 0
def text(b): return re.sub(r'\s+', ' ', re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]', b' ', b).decode('latin1'))
t0 = time.time()
while alive and time.time() - t0 < secs:
    r,_,_ = select.select([fd],[],[],0.05)
    if r:
        try:
            d = os.read(fd, 65536)
            if not d: alive = False
            buf += d; last = time.time()
        except OSError: alive = False
        continue
    if time.time() - last < 0.7 or len(buf) == mark: continue
    tail = text(buf[mark:])[-300:]
    for pat, keys in RULES:
        if re.search(pat, tail, re.I):
            k = keys(state, tail) if callable(keys) else keys
            break
    else:
        k = None
    print(f'[{time.time()-t0:5.1f}] ...{tail[-110:]!r} -> {k!r}')
    mark = len(buf)
    if k is None: continue
    for ch in k.encode('latin1'):
        os.write(fd, bytes([ch])); time.sleep(0.12)
    last = time.time()
if alive:
    os.kill(pid, signal.SIGTERM); time.sleep(0.5)
    try: os.kill(pid, signal.SIGKILL)
    except ProcessLookupError: pass
    print('KILLED (no clean exit)')
else:
    print('EXITED CLEANLY')
open(out, 'wb').write(buf)
