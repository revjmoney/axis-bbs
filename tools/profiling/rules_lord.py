def town(st, tail):
    seq = st.setdefault('town', list('FVDLPQ'))
    return seq.pop(0) if seq else 'Q'
def cmd(st, tail):
    st['n']['cmd'] = st['n'].get('cmd', 0) + 1
    if 'ook for something' in st.get('screen', '') or st.get('inforest'):
        st['looks'] = st.get('looks', 0) + 1
        if st['looks'] <= 10: return 'L'
        st['inforest'] = False; return 'R'
    k = town(st, tail)
    if k == 'F': st['inforest'] = True
    return k
RULES = [
 (r'Name or handle', '\r'),
 (r'join this place', 'Y'),
 (r'alias\?.*Name:', 'Revman\r'),
 (r'choice, warrior', lambda st, t: 'Q' if st.get('played') else (st.update(played=1) or 'E')),
 (r'Pick one\. \( K', 'K'),
 (r'\( A \)ttack|\(A\)ttack', 'A'),
 (r'Your command', cmd),
 (r'quit|leave|sure', 'Y'),
 (r'.', '\r'),
]
