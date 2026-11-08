"""Start AXIS sysop panel; remote access requires explicit TLS certificate and key."""
import argparse
import asyncio
import os
import socket
import ssl
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
    remote = args.host not in ('127.0.0.1', 'localhost', '::1')
    if remote and not (args.tls_cert and args.tls_key):
        raise SystemExit('Refusing non-loopback bind without --tls-cert and --tls-key')
    tls = None
    if args.tls_cert or args.tls_key:
        if not (args.tls_cert and args.tls_key):
            raise SystemExit('Both --tls-cert and --tls-key are required')
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.minimum_version = ssl.TLSVersion.TLSv1_2
        tls.load_cert_chain(args.tls_cert, args.tls_key)

    hub = Hub()
    for ip in args.board or []:
        hub.seen(ip)
    listener = Listener(hub.seen)
    await listener.start()
    store = Store()
    if store.creds():
        asyncio.ensure_future(hub.login(*store.creds()))
    runner = web.AppRunner(make_app(hub, store))
    await runner.setup()
    await web.TCPSite(runner, args.host, args.port, ssl_context=tls).start()
    shown_host = args.host if args.host not in ('0.0.0.0', '::') else lan_ip()
    scheme = 'https' if tls else 'http'
    print('AXIS sysop panel on %s://%s:%d/' % (scheme, shown_host, args.port))
    if remote:
        print('Remote panel access is enabled; use a trusted certificate and restrict network access.')
    stop = asyncio.Event()
    if not args.no_tray:
        from axispanel import tray
        loop = asyncio.get_running_loop()
        tray.start('%s://%s:%d/' % (scheme, shown_host, args.port), lambda: loop.call_soon_threadsafe(stop.set))
    await stop.wait()
    listener.stop()
    await runner.cleanup()


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=PORT)
    ap.add_argument('--host', default='127.0.0.1', help='bind address (remote bind requires TLS certificate and key)')
    ap.add_argument('--tls-cert', help='TLS certificate PEM; required for a non-loopback bind')
    ap.add_argument('--tls-key', help='TLS private key PEM; required for a non-loopback bind')
    ap.add_argument('--board', action='append', help='a board IP to add without waiting for its announcement')
    ap.add_argument('--no-tray', action='store_true')
    asyncio.run(main(ap.parse_args()))
