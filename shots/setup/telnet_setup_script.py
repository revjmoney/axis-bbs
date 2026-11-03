# Steps for bbs_shots.py: first-boot setup over telnet (PIN and test password are throwaway)
SCRIPT = [
    ('03-pin-prompt', b''),
    ('04-wizard-start', b'916339\r\n'),
    ('05-handle-password', b'Rev\r\ntest1234\r\ntest1234\r\n'),
    ('06-names-location', b'\r\n\r\nTest Shop, NC\r\n'),
    ('07-address-port-timezones', b'\r\n\r\n\r\n'),
    ('08-defaults', b'1\r\n\r\n\r\n\r\n\r\n'),
    ('09-saved', b'y'),
]
