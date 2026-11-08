"""One connection to one board's panel port (2326): challenge login, commands, the event and status feed."""
import asyncio
import hashlib
import time

PANEL_PORT = 2326
QUIET_S = 15  # no STAT this long: the board is gone


def sha(text: str) -> str:
    return hashlib.sha256(text.encode('latin-1')).hexdigest()


def parse_stat(text: str) -> dict:
    out = {}
    for kv in text.split():
        k, _, v = kv.partition('=')
        out[k] = v
    return out


class LoginFailed(Exception):
    pass


class BoardLink:
    def __init__(self, ip, port=PANEL_PORT):
        self.ip, self.port = ip, port
        self.events = asyncio.Queue()
        self.stat = {}
        self.last_seen = 0.0
        self.reader = self.writer = None
        self._lines = asyncio.Queue()  # replies to our own lines (not EVT / STAT)
        self._pump = None
        self._lock = asyncio.Lock()

    @property
    def alive(self):
        return self.writer is not None and time.monotonic() - self.last_seen < QUIET_S

    async def login(self, handle, password, timeout=5):
        self.reader, self.writer = await asyncio.wait_for(asyncio.open_connection(self.ip, self.port), timeout)
        hello = (await asyncio.wait_for(self.reader.readline(), timeout)).decode('latin-1').strip()
        if hello == 'BUSY' or not hello.startswith('AXISPANEL '):
            await self.close()
            raise LoginFailed(hello or 'no greeting')
        nonce = hello.split()[2]
        self.last_seen = time.monotonic()
        self._pump = asyncio.create_task(self._read())
        salt_reply = await self._ask('SALT ' + handle)
        if salt_reply == 'SALT -':
            await self.close()
            return False
        parts = salt_reply.split()
        if len(parts) < 2 or parts[0] != 'SALT':
            await self.close()
            raise LoginFailed('invalid salt response')
        salt = parts[1]
        scheme = parts[2] if len(parts) > 2 else 'SHA256'  # compatibility with older boards
        if scheme == 'PBKDF2-SHA256-200000':
            stored_hash = 'pbkdf2-sha256$200000$' + hashlib.pbkdf2_hmac(
                'sha256', password.encode('latin-1'), salt.encode('latin-1'), 200000, dklen=32
            ).hex()
        elif scheme == 'SHA256':
            stored_hash = sha(salt + password)
        else:
            await self.close()
            raise LoginFailed('unsupported password hash scheme')
        proof = sha(nonce + stored_hash)
        ok = await self._ask('AUTH %s %s' % (handle, proof)) == 'OK'
        if not ok:
            await self.close()
        return ok

    async def command(self, cmd, timeout=10):
        async with self._lock:
            self.writer.write(('CMD %s\n' % cmd).encode('latin-1'))
            await self.writer.drain()
            out = []
            while True:
                line = await asyncio.wait_for(self._lines.get(), timeout)
                if line == 'END':
                    return out
                out.append(line)

    async def ask(self, line, timeout=5):
        """A screen wall or console line (WALL / CON); returns the board's one-line answer."""
        async with self._lock:
            return await self._ask(line, timeout)

    async def send(self, line):
        """A line with no answer (KEY)."""
        self.writer.write((line + '\n').encode('latin-1'))
        await self.writer.drain()

    async def _ask(self, line, timeout=5):
        self.writer.write((line + '\n').encode('latin-1'))
        await self.writer.drain()
        return await asyncio.wait_for(self._lines.get(), timeout)

    async def _read(self):
        try:
            while True:
                raw = await self.reader.readline()
                if not raw:
                    break
                line = raw.decode('latin-1').rstrip('\r\n')
                self.last_seen = time.monotonic()
                if line.startswith('SCR '):  # a node's screen bytes (base64): the wall and the console
                    _, node, data = (line.split(' ', 2) + [''])[:3]
                    await self.events.put(('scr', (int(node), data)))
                elif line.startswith('EVT '):
                    await self.events.put(('evt', line[4:]))
                elif line.startswith('STAT '):
                    self.stat = parse_stat(line[5:])
                    await self.events.put(('stat', self.stat))
                else:
                    await self._lines.put(line)
        except (ConnectionError, asyncio.CancelledError):
            pass
        finally:
            self.writer = None

    async def close(self):
        if self._pump:
            self._pump.cancel()
        if self.writer:
            self.writer.close()
        self.writer = None
