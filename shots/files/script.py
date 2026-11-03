# bbs_shots.py steps: caller file browsing on the CYD
SCRIPT = [
    ('01-menu', b'Rev\r\n', 5, 'Command:'),
    ('02-areas', b'f', 15, 'Area number'),
    ('03-axis-area', b'1\r', 10, 'iew a text file'),
    ('04-view-readme', b'vREADME.TXT\r', 10, 'more|end of file'),
    ('05-back-to-list', b'q', 10, 'iew a text file'),
    ('06-areas', b'q', 15, 'Area number'),
    ('07-bbs-software', b'16\r', 10, 'iew a text file'),
    ('08-doors', b'q13\r', 15, 'iew a text file'),
    ('09-music', b'q17\r', 15, 'iew a text file'),
    ('10-main-menu', b'q\r', 15, 'Command:'),
]
