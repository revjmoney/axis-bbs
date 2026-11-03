# bbs_shots.py steps: BBS Black Jack 2.5 (Night Owl CD 5) on the CYD from the Doors menu
SCRIPT = [
    ('01-menu', b'Rev\r\n', 5, 'Command:'),
    ('02-doors', b'd', 5, 'Door number'),
    ('03-start', b'3', 40, 'Select'),
    ('04-color', b'C', 20, 'continue'),
    ('05-main', b'\r', 20, 'Choice'),
    ('06-play', b'P', 20, '.'),
    ('07-bet', b'50\r', 20, '.'),
    ('08-stand', b'S', 20, '.'),
    ('09-next', b'\r', 20, '.'),
    ('10-quit', b'Q', 20, '.'),
    ('11-more', b'Q\r', 20, '.'),
    ('12-back', b'\r', 30, 'Command:'),
]
