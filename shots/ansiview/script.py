# bbs_shots.py steps: the built-in ANSI Pack Viewer door on the CYD
SCRIPT = [
    ('01-menu', b'Rev\r\n', 5, 'Command:'),
    ('02-doors', b'd', 5, 'Door number'),
    ('03-packs', b'2', 15, 'Pack number'),
    ('04-acid-pieces', b'1\r', 15, 'Piece number'),
    ('05-piece-1', b'1\r', 30, 'next'),
    ('06-piece-2', b'\r', 30, 'next'),
    ('07-piece-3', b'\r', 30, 'next'),
    ('08-packs', b'q', 15, 'Pack number'),
    ('09-blocktronics', b'12\r', 15, 'Piece number'),
    ('10-bt-piece', b'3\r', 30, 'next'),
    ('11-ice-pieces', b'q3\r', 15, 'Piece number'),
    ('12-ice-piece', b'2\r', 30, 'next'),
    ('13-back-at-bbs', b'qq', 15, 'Command:'),
]
