# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Write src/platform/cyd/web_roots.h: the few root certificates the board needs to check the TLS
servers it talks to (Discord webhooks and api.ipify.org: Google Trust Services R1/R4; api.godaddy.com:
Go Daddy G2), taken from the Mozilla bundle that Python's certifi ships.

usage: python -I tools/make_web_roots.py
"""
import re
import certifi

WANT = ['GTS Root R1', 'GTS Root R4', 'Go Daddy Root Certificate Authority - G2']
text = open(certifi.where(), encoding='utf-8').read()
blocks = re.findall(r'# Label: "([^"]+)"\n.*?(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)', text, re.S)
found = {label: pem for label, pem in blocks if label in WANT}
missing = [w for w in WANT if w not in found]
assert not missing, missing
out = ['// SPDX-License-Identifier: GPL-3.0-or-later', '// Copyright (C) 2026 Rev. J. Money',
       '// Root certificates for the board\'s HTTPS calls (made by tools/make_web_roots.py from the Mozilla bundle):',
       '// ' + ', '.join(WANT) + '.', '#pragma once', '', 'static const char kWebRoots[] =']
for w in WANT:
    out.append(f'    // {w}')
    for line in found[w].splitlines():
        out.append(f'    "{line}\\n"')
out[-1] += ';'
open('src/platform/cyd/web_roots.h', 'w', newline='\n').write('\n'.join(out) + '\n')
print('wrote', len(WANT), 'roots')
