import asyncio
import hashlib
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from axispanel import board as boardmod
from axispanel.board import BoardLink
from axispanel.discovery import board_ip
from axispanel.hub import Hub


def sha(t):
    return hashlib.sha256(t.encode()).hexdigest()


class FakeBoard:
    """Speaks the panel protocol like the firmware (bbs/panelproto.h)."""

    def __init__(self, role='main', password='sysop1'):
        self.role, self.salt = role, 'abc'
        self.hash = sha(self.salt + password)
        self.commands = []
        self.server = None

    async def start(self):
        self.server = await asyncio.start_server(self.client, '127.0.0.1', 0)
        return self.server.sockets[0].getsockname()[1]

    async def client(self, r, w):
        nonce = 'n1'
        w.write(b'AXISPANEL 1 n1\n')
        authed = False
        while True:
            raw = await r.readline()
            if not raw:
                return
            line = raw.decode().strip()
            if line.startswith('SALT '):
                w.write(('SALT %s\n' % (self.salt if line[5:] == 'Rev' else '-')).encode())
            elif line.startswith('AUTH '):
                authed = line.split()[2] == sha(nonce + self.hash)
                w.write(b'OK\n' if authed else b'NO\n')
                if authed:
                    w.write(('STAT role=%s nodes=2\n' % self.role).encode())
            elif line.startswith('CMD ') and authed:
                self.commands.append(line[4:])
                w.write(('did %s\nEND\n' % line[4:]).encode())
            await w.drain()


def link_to(port):
    return lambda ip: BoardLink('127.0.0.1', port)


def test_announcements_name_the_board():
    n = int.from_bytes(bytes([192, 168, 1, 164]), 'little')
    assert board_ip(('AXIS! %d 3 1 1' % n).encode()) == '192.168.1.164'
    assert board_ip(b'hello') is None


def test_login_and_commands_reach_the_main():
    async def go():
        fb = FakeBoard()
        port = await fb.start()
        hub = Hub(link_factory=link_to(port))
        hub.seen('board1')
        await asyncio.sleep(0)
        assert await hub.login('Rev', 'sysop1')
        await asyncio.sleep(0.1)
        assert hub.main() is not None
        assert await hub.command('@USERS') == ['did @USERS']
    asyncio.run(go())


def test_a_wrong_password_is_reported_not_retried():
    async def go():
        fb = FakeBoard()
        port = await fb.start()
        hub = Hub(link_factory=link_to(port))
        hub.seen('board1')
        await asyncio.sleep(0)
        assert not await hub.login('Rev', 'nope')
        assert fb.commands == []
    asyncio.run(go())


def test_settings_go_to_whichever_board_is_main_now():
    async def go():
        a, b = FakeBoard('follower'), FakeBoard('main')
        pa, pb = await a.start(), await b.start()
        ports = {'A': pa, 'B': pb}
        hub = Hub(link_factory=lambda ip: BoardLink('127.0.0.1', ports[ip]))
        hub.seen('A'), hub.seen('B')
        await asyncio.sleep(0)
        await hub.login('Rev', 'sysop1')
        await asyncio.sleep(0.1)
        await hub.command('@SET theme=oblivion')
        own = lambda fb: [c for c in fb.commands if c != '@NODES']  # (the app reads each node list to find boards)
        assert own(b) == ['@SET theme=oblivion'] and own(a) == []
        await hub.command('@KICK 3', board='A')
        assert own(a) == ['@KICK 3']
    asyncio.run(go())


def test_a_board_that_goes_quiet_is_dropped_and_comes_back():
    async def go():
        fb = FakeBoard()
        port = await fb.start()
        hub = Hub(link_factory=link_to(port))
        hub.seen('board1')
        await asyncio.sleep(0)
        await hub.login('Rev', 'sysop1')
        await asyncio.sleep(0.1)
        link = hub.links['board1']
        assert link.alive
        link.last_seen -= boardmod.QUIET_S + 1
        assert not link.alive
        hub.seen('board1')  # it announces itself again
        await asyncio.sleep(0.2)
        assert hub.links['board1'].alive
    asyncio.run(go())


def test_screen_bytes_reach_the_hub_and_console_lines_get_answers():
    async def go():
        fb = FakeBoard()
        port = await fb.start()
        got = []
        orig = fb.client

        async def client(r, w):  # after login, answer CON and push a screen line
            w.write(b'AXISPANEL 1 n1\n')
            while True:
                raw = await r.readline()
                if not raw:
                    return
                line = raw.decode().strip()
                if line.startswith('SALT '):
                    w.write(b'SALT abc\n')
                elif line.startswith('AUTH '):
                    w.write(b'OK\nSTAT role=main\nSCR 3 aGk=\n')
                elif line == 'CON NEW':
                    w.write(b'CON 3\n')
                await w.drain()
        fb.client = client
        fb.server.close()
        fb.server = await asyncio.start_server(client, '127.0.0.1', 0)
        port = fb.server.sockets[0].getsockname()[1]
        hub = Hub(link_factory=link_to(port))
        hub.on_screen = lambda ip, node, data: got.append((node, data))
        hub.seen('b')
        await asyncio.sleep(0)
        assert await hub.login('Rev', 'x')
        await asyncio.sleep(0.1)
        assert got == [(3, 'aGk=')]
        assert await hub.console('b', 'new') == 'CON 3'
    asyncio.run(go())
