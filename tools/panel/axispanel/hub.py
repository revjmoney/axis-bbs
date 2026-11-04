"""Every board the panel knows: links, who is main, and where each command goes."""
import asyncio
import re

from .board import BoardLink

# These change the shared BBS (users, settings, lists): only the main's copy counts.
MAIN_COMMANDS = ('@USERS', '@USER ', '@VALIDATE', '@DENY', '@SEC ', '@PERM', '@TWIT', '@USERPASS', '@SYSOP', '@ADDUSER',
                 '@SET ', '@SETTINGS', '@SYSOPPASS', '@NUP', '@RUMOR', '@BBSLIST', '@VOTES', '@LINERS')


class Hub:
    def __init__(self, link_factory=BoardLink):
        self.links = {}  # ip -> BoardLink
        self.link_factory = link_factory
        self.handle = self.password = None  # held in memory only while the app runs
        self.on_change = None  # called when a board appears, changes or goes
        self.on_event = None   # called with (ip, line)
        self.on_screen = None  # called with (ip, node, base64 bytes)
        self.wall = False

    def seen(self, ip):
        """A board announced itself: link to it once we can log in."""
        if ip not in self.links or not self.links[ip].alive:
            asyncio.ensure_future(self.connect(ip))

    async def login(self, handle, password):
        """The sysop's login, tried on every known board; True if any board took it."""
        self.handle, self.password = handle, password
        for _ in range(16):  # just started: give the main a few announcements to be heard
            if self.links:
                break
            await asyncio.sleep(0.5)
        results = [await self.connect(ip) for ip in list(self.links)]
        return any(results)

    async def connect(self, ip):
        if self.handle is None:
            self.links.setdefault(ip, self.link_factory(ip))
            return False
        link = self.link_factory(ip)
        try:
            ok = await link.login(self.handle, self.password)
        except Exception:
            ok = False
        self.links[ip] = link
        if ok:
            asyncio.ensure_future(self._watch(ip, link))
            asyncio.ensure_future(self._learn(link))
            if self.wall:
                await link.ask('WALL ON')
        self._changed()
        return ok

    async def _learn(self, link):
        """Only the main announces itself: the other boards are found in its node list."""
        try:
            lines = await link.command('@NODES')
        except Exception:
            return
        for line in lines:
            m = re.search(r' ip (\d+\.\d+\.\d+\.\d+) ', line)
            if m and m.group(1) not in self.links:
                await self.connect(m.group(1))

    async def _watch(self, ip, link):
        while link.writer is not None:
            try:
                kind, data = await asyncio.wait_for(link.events.get(), 20)
            except asyncio.TimeoutError:
                continue
            if kind == 'evt' and self.on_event:
                self.on_event(ip, data)
            if kind == 'scr':
                if self.on_screen:
                    self.on_screen(ip, *data)
                continue
            self._changed()
        self._changed()

    def _changed(self):
        if self.on_change:
            self.on_change()

    def boards(self):
        return [{'ip': ip, 'alive': l.alive, 'stat': dict(l.stat)} for ip, l in sorted(self.links.items())]

    def main(self):
        for ip, l in self.links.items():
            if l.alive and l.stat.get('role') == 'main':
                return l
        return None

    async def set_wall(self, on):
        self.wall = on
        for l in self.links.values():
            if l.alive:
                await l.ask('WALL ON' if on else 'WALL OFF')

    async def console(self, board, action):
        """action: 'new', 'join <node>' or 'off'; returns the board's answer ('CON 3', 'CON NO ...')."""
        link = self.links.get(board)
        if not link or not link.alive:
            return 'CON NO that board is not connected'
        return await link.ask('CON ' + action.upper() if action in ('new', 'off') else 'CON JOIN ' + action.split()[-1])

    async def key(self, board, data):
        link = self.links.get(board)
        if link and link.alive:
            await link.send('KEY ' + data)

    async def command(self, cmd, board=None):
        if any(cmd.startswith(m) for m in MAIN_COMMANDS) or board is None:
            link = self.main()
        else:
            link = self.links.get(board)
        if not link or not link.alive:
            return ['axis: that board is not connected']
        return await link.command(cmd)
