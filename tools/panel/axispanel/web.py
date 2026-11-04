"""The panel's web side: the page, and one WebSocket per browser (phone or PC on the LAN)."""
import json
import os

from aiohttp import web, WSMsgType

STATIC = os.path.join(os.path.dirname(__file__), 'static')
PORT = 8326


def make_app(hub):
    app = web.Application()
    sockets = set()

    async def broadcast(msg):
        for ws in list(sockets):
            if ws.get('authed') if isinstance(ws, dict) else getattr(ws, 'authed', False):
                try:
                    await ws.send_str(json.dumps(msg))
                except ConnectionError:
                    sockets.discard(ws)

    def changed():
        import asyncio
        asyncio.ensure_future(broadcast({'boards': hub.boards()}))

    def event(ip, line):
        import asyncio
        asyncio.ensure_future(broadcast({'evt': {'board': ip, 'line': line}}))

    hub.on_change, hub.on_event = changed, event

    async def index(request):
        return web.FileResponse(os.path.join(STATIC, 'index.html'))

    async def socket(request):
        ws = web.WebSocketResponse(heartbeat=20)
        await ws.prepare(request)
        ws.authed = False
        sockets.add(ws)
        await ws.send_str(json.dumps({'need': 'login'}))
        async for msg in ws:
            if msg.type != WSMsgType.TEXT:
                continue
            try:
                m = json.loads(msg.data)
            except ValueError:
                continue
            if 'login' in m:
                ok = await hub.login(m['login'].get('handle', ''), m['login'].get('password', ''))
                ws.authed = ok
                await ws.send_str(json.dumps({'login': ok}))
                if ok:
                    await ws.send_str(json.dumps({'boards': hub.boards()}))
            elif not ws.authed:
                await ws.send_str(json.dumps({'need': 'login'}))
            elif 'cmd' in m:
                lines = await hub.command(m['cmd'], m.get('board'))
                await ws.send_str(json.dumps({'reply': {'cmd': m['cmd'], 'board': m.get('board'), 'lines': lines}}))
        sockets.discard(ws)
        return ws

    app.router.add_get('/', index)
    app.router.add_get('/ws', socket)
    app.router.add_static('/static/', STATIC)
    return app
