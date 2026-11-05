import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

import pytest
from axispanel.web import make_app


class FakeHub:
    def __init__(self):
        self.on_change = self.on_event = None
        self.ran = []
        self.password = None  # as the real hub: set once a login works

    async def login(self, h, p):
        if p == 'sysop1':
            self.password = p
        return p == 'sysop1'

    def boards(self):
        return [{'ip': '10.0.0.2', 'alive': True, 'stat': {'role': 'main'}}]

    async def command(self, cmd, board=None):
        self.ran.append((cmd, board))
        return ['ok ' + cmd]


@pytest.fixture
def hub():
    return FakeHub()


async def recv(ws):
    return json.loads((await ws.receive()).data)


async def test_the_socket_asks_for_login_first(aiohttp_client, hub):
    c = await aiohttp_client(make_app(hub))
    ws = await c.ws_connect('/ws')
    assert await recv(ws) == {'need': 'login'}
    await ws.send_str(json.dumps({'cmd': '@USERS'}))
    assert await recv(ws) == {'need': 'login'}
    assert hub.ran == []


async def test_a_command_from_the_browser_reaches_the_hub_and_comes_back(aiohttp_client, hub):
    c = await aiohttp_client(make_app(hub))
    ws = await c.ws_connect('/ws')
    await recv(ws)
    await ws.send_str(json.dumps({'login': {'handle': 'Rev', 'password': 'sysop1'}}))
    assert await recv(ws) == {'login': True}
    assert 'boards' in await recv(ws)
    await ws.send_str(json.dumps({'cmd': '@KICK 3', 'board': '10.0.0.2'}))
    r = await recv(ws)
    assert r['reply']['lines'] == ['ok @KICK 3'] and hub.ran == [('@KICK 3', '10.0.0.2')]


async def test_board_changes_are_pushed_to_the_browser(aiohttp_client, hub):
    c = await aiohttp_client(make_app(hub))
    ws = await c.ws_connect('/ws')
    await recv(ws)
    await ws.send_str(json.dumps({'login': {'handle': 'Rev', 'password': 'sysop1'}}))
    await recv(ws), await recv(ws)
    hub.on_event('10.0.0.2', '12:00 Kid logged in')
    assert (await recv(ws))['evt']['line'] == '12:00 Kid logged in'


class FakeStore:
    """Remembered logins: tokens and the sysop login kept on the PC (DPAPI in the real one)."""
    def __init__(self):
        self.tokens, self.saved = set(), None

    def issue(self, handle, password):
        self.saved = (handle, password)
        t = 'tok%d' % len(self.tokens)
        self.tokens.add(t)
        return t

    def valid(self, token):
        return token in self.tokens

    def creds(self):
        return self.saved

    def revoke(self, token):
        self.tokens.discard(token)


async def test_remember_me_gives_a_token_and_the_cookie_logs_in_next_time(aiohttp_client, hub):
    store = FakeStore()
    c = await aiohttp_client(make_app(hub, store))
    ws = await c.ws_connect('/ws')
    await recv(ws)
    await ws.send_str(json.dumps({'login': {'handle': 'Rev', 'password': 'sysop1', 'remember': True}}))
    r = await recv(ws)
    assert r['login'] is True and store.valid(r['token']) and store.saved == ('Rev', 'sysop1')
    await ws.close()
    ws2 = await c.ws_connect('/ws', headers={'Cookie': 'axis_panel=' + r['token']})
    assert (await recv(ws2))['login'] is True  # no password typed
    assert 'boards' in await recv(ws2)


async def test_a_login_without_remember_me_gives_no_token(aiohttp_client, hub):
    store = FakeStore()
    c = await aiohttp_client(make_app(hub, store))
    ws = await c.ws_connect('/ws')
    await recv(ws)
    await ws.send_str(json.dumps({'login': {'handle': 'Rev', 'password': 'sysop1'}}))
    assert await recv(ws) == {'login': True} and not store.tokens


async def test_a_bad_cookie_still_asks_for_the_login(aiohttp_client, hub):
    c = await aiohttp_client(make_app(hub, FakeStore()))
    ws = await c.ws_connect('/ws', headers={'Cookie': 'axis_panel=forged'})
    assert await recv(ws) == {'need': 'login'}


async def test_logout_revokes_the_token(aiohttp_client, hub):
    store = FakeStore()
    c = await aiohttp_client(make_app(hub, store))
    ws = await c.ws_connect('/ws')
    await recv(ws)
    await ws.send_str(json.dumps({'login': {'handle': 'Rev', 'password': 'sysop1', 'remember': True}}))
    token = (await recv(ws))['token']
    await recv(ws)
    await ws.send_str(json.dumps({'logout': token}))
    assert await recv(ws) == {'need': 'login'}
    assert not store.valid(token)
