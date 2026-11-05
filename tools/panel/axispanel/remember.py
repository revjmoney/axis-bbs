"""Remember me: long-lived browser tokens, and the sysop login kept on this PC so the panel can log in to
the boards by itself (after the app restarts too). The password is sealed with Windows DPAPI (this Windows
user only); only hashes of the tokens are stored. Elsewhere there is no DPAPI, so nothing is remembered."""
import base64
import ctypes
import hashlib
import json
import os
import secrets
import sys


def _dir():
    d = os.path.join(os.environ.get('APPDATA') or os.path.expanduser('~'), 'axispanel')
    os.makedirs(d, exist_ok=True)
    return d


class _Blob(ctypes.Structure):
    _fields_ = [('cbData', ctypes.c_uint32), ('pbData', ctypes.POINTER(ctypes.c_char))]


def _dpapi(data, seal):
    crypt, kernel = ctypes.windll.crypt32, ctypes.windll.kernel32
    buf = ctypes.create_string_buffer(data, len(data))
    src, out = _Blob(len(data), ctypes.cast(buf, ctypes.POINTER(ctypes.c_char))), _Blob()
    fn = crypt.CryptProtectData if seal else crypt.CryptUnprotectData
    if not fn(ctypes.byref(src), None, None, None, None, 0x1, ctypes.byref(out)):  # 0x1: no UI
        raise OSError('DPAPI failed')
    try:
        return ctypes.string_at(out.pbData, out.cbData)
    finally:
        kernel.LocalFree(out.pbData)


def _hash(token):
    return hashlib.sha256(token.encode()).hexdigest()


class Store:
    def __init__(self, folder=None):
        self.path = os.path.join(folder or _dir(), 'remember.json')
        self.ok = sys.platform == 'win32'
        try:
            with open(self.path) as f:
                self.data = json.load(f)
        except (OSError, ValueError):
            self.data = {'tokens': [], 'login': None}

    def _save(self):
        tmp = self.path + '.tmp'
        with open(tmp, 'w') as f:
            json.dump(self.data, f)
        os.replace(tmp, self.path)

    def issue(self, handle, password):
        """A new token for a browser (None where the login cannot be kept safely)."""
        if not self.ok:
            return None
        sealed = _dpapi(json.dumps([handle, password]).encode(), True)
        token = secrets.token_urlsafe(32)
        self.data['login'] = base64.b64encode(sealed).decode()
        self.data['tokens'].append(_hash(token))
        self._save()
        return token

    def valid(self, token):
        return bool(token) and _hash(token) in self.data['tokens']

    def creds(self):
        """(handle, password), or None."""
        if not self.ok or not self.data.get('login'):
            return None
        try:
            h, p = json.loads(_dpapi(base64.b64decode(self.data['login']), False))
            return h, p
        except (OSError, ValueError):
            return None

    def revoke(self, token):
        h = _hash(token or '')
        if h in self.data['tokens']:
            self.data['tokens'].remove(h)
            if not self.data['tokens']:
                self.data['login'] = None  # nobody remembered: forget the login too
            self._save()
