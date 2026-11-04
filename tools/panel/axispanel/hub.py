"""Every board the panel knows: links, who is main, and where each command goes."""
import asyncio

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

    def seen(self, ip):
        """A board announced itself: link to it once we can log in."""
        if ip not in self.links or not self.links[ip].alive:
            asyncio.ensure_future(self.connect(ip))

    async def login(self, handle, password):
        """The sysop's login, tried on every known board; True if any board took it."""
        self.handle, self.password = handle, password
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
        self._changed()
        return ok

    async def _watch(self, ip, link):
        while link.writer is not None:
            try:
                kind, data = await asyncio.wait_for(link.events.get(), 20)
            except asyncio.TimeoutError:
                continue
            if kind == 'evt' and self.on_event:
                self.on_event(ip, data)
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

    async def command(self, cmd, board=None):
        if any(cmd.startswith(m) for m in MAIN_COMMANDS) or board is None:
            link = self.main()
        else:
            link = self.links.get(board)
        if not link or not link.alive:
            return ['axis: that board is not connected']
        return await link.command(cmd)
