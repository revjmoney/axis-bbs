"""Start the AXIS sysop panel: finds the boards, serves http://<this pc>:8326/ on the LAN, tray icon."""
import argparse
import asyncio
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from aiohttp import web

from axispanel.discovery import Listener
from axispanel.hub import Hub
from axispanel.remember import Store
from axispanel.web import PORT, make_app


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('10.255.255.255', 1))
        return s.getsockname()[0]
    except OSError:
        return '127.0.0.1'
    finally:
        s.close()


async def main(args):
    hub = Hub()
    for ip in args.board or []:  # boards named on the command line, besides the ones that announce themselves
        hub.seen(ip)
    listener = Listener(hub.seen)
    await listener.start()
    store = Store()
    if store.creds():  # remembered: log in to the boards right away, before any browser opens
        asyncio.ensure_future(hub.login(*store.creds()))
    runner = web.AppRunner(make_app(hub, store))
    await runner.setup()
    host = args.host or '127.0.0.1'
    await web.TCPSite(runner, host, args.port).start()
    url = 'http://%s:%d/' % (lan_ip() if host in ('0.0.0.0', '::') else host, args.port)
    print('AXIS sysop panel on', url, '(phone or PC on this network)' if host in ('0.0.0.0', '::') else '(local browser only)')
    stop = asyncio.Event()
    if not args.no_tray:
        from axispanel import tray
        loop = asyncio.get_running_loop()
        tray.start(url, lambda: loop.call_soon_threadsafe(stop.set))
    await stop.wait()
    listener.stop()
    await runner.cleanup()


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=PORT)
    ap.add_argument('--host', default='127.0.0.1', help='bind address; use 0.0.0.0 to expose the panel on the LAN')
    ap.add_argument('--board', action='append', help='a board IP to add without waiting for its announcement')
    ap.add_argument('--no-tray', action='store_true')
    asyncio.run(main(ap.parse_args()))
