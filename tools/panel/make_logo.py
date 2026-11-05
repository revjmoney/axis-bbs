"""Make the panel's ANSI logo (static/logo.ans) with the BBS's own artpack generator: the drippy axis look."""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'art'))

import axis_packs  # noqa: E402

art = axis_packs.header('PANEL', 'AXIS', 'axis', 7)
art = re.sub(r'panel.*?%NN', 'panel', art.replace('%BN', 'sysop panel'), flags=re.S)  # the BBS fills these live; the panel is static
open(os.path.join(HERE, 'axispanel', 'static', 'logo.ans'), 'wb').write(art.encode('latin-1'))
print(len(art), 'bytes')
