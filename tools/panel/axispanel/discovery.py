"""Finds AXIS boards by the cluster's own UDP announcements ("AXIS! <ip> <epoch> <node> <sd>", port 2325)."""
import asyncio
import ipaddress
import socket

DISCOVERY_PORT = 2325


def board_ip(datagram: bytes, sender=None):
    """The board's IP from an announcement, or None. With the sender's address given, an announcement that names
    another address is ignored: anyone can send one, but only the board itself sends from its own IP."""
    parts = datagram.decode('ascii', 'replace').split()
    if len(parts) < 2 or parts[0] != 'AXIS!':
        return None
    try:
        n = int(parts[1])  # the firmware sends the IPv4 address as a little-endian number
        ip = str(ipaddress.IPv4Address(n.to_bytes(4, 'little')))
    except (ValueError, OverflowError):
        try:
            ip = str(ipaddress.IPv4Address(parts[1]))
        except ValueError:
            return None
    if sender is not None and ip != sender:
        return None
    return ip


class _Proto(asyncio.DatagramProtocol):
    def __init__(self, on_board):
        self.on_board = on_board

    def datagram_received(self, data, addr):
        ip = board_ip(data, addr[0])
        if ip:
            self.on_board(ip)


class Listener:
    def __init__(self, on_board, port=DISCOVERY_PORT):
        self.on_board, self.port, self.transport = on_board, port, None

    async def start(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(('', self.port))
        loop = asyncio.get_running_loop()
        self.transport, _ = await loop.create_datagram_endpoint(lambda: _Proto(self.on_board), sock=s)

    def stop(self):
        if self.transport:
            self.transport.close()
