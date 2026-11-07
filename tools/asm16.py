# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""A small 8086 assembler for AXIS door programs.

Usage: python -I tools/asm16.py doors/axisfour/four.asm doors/axisfour/FOUR.EXE

One segment. DOS enters at `start` with DS = ES = PSP. SS:SP is 0000:FFFE in
the load segment (writable memory above the image).
"""
import sys

REGS16 = {"ax": 0, "cx": 1, "dx": 2, "bx": 3, "sp": 4, "bp": 5, "si": 6, "di": 7}
REGS8 = {"al": 0, "cl": 1, "dl": 2, "bl": 3, "ah": 4, "ch": 5, "dh": 6, "bh": 7}
JCC = {
    "o": 0, "no": 1, "b": 2, "c": 2, "nae": 2, "ae": 3, "nb": 3, "nc": 3,
    "e": 4, "z": 4, "ne": 5, "nz": 5, "be": 6, "na": 6, "a": 7, "nbe": 7,
    "s": 8, "ns": 9, "l": 12, "nge": 12, "ge": 13, "nl": 13, "le": 14, "ng": 14,
    "g": 15, "nle": 15,
}
ARITH = {"add": 0, "or": 1, "adc": 2, "sbb": 3, "and": 4, "sub": 5, "xor": 6, "cmp": 7}


def jcc_code(mnem):
    if mnem.startswith("j") and mnem[1:] in JCC:
        return JCC[mnem[1:]]
    return None
AXIMM = {"add": 0x05, "or": 0x0D, "adc": 0x15, "sbb": 0x1D, "and": 0x25, "sub": 0x2D, "xor": 0x35, "cmp": 0x3D}
ALIMM = {"add": 0x04, "or": 0x0C, "adc": 0x14, "sbb": 0x1C, "and": 0x24, "sub": 0x2C, "xor": 0x34, "cmp": 0x3C}


class AsmError(Exception):
    pass


def number(tok, consts):
    if tok in consts:
        return consts[tok]
    if len(tok) >= 3 and tok[0] == "'" and tok[-1] == "'":
        if len(tok) != 3:
            raise AsmError("bad char " + tok)
        return ord(tok[1])
    neg = tok.startswith("-") and tok[1:2].isdigit()
    body = tok[1:] if neg else tok
    try:
        v = int(body[:-1], 16) if body.lower().endswith("h") else int(body, 10)
    except ValueError:
        raise AsmError("bad number " + tok)
    return (-v if neg else v) & 0xFFFFFFFF


def expr(text, consts):
    # A character literal may be a space. Drop spaces only outside quotes.
    kept, quote = [], False
    for ch in text:
        if ch == "'":
            quote = not quote
        if ch == " " and not quote:
            continue
        kept.append(ch)
    text = "".join(kept)
    if not text:
        raise AsmError("empty expression")
    total, sign, i = 0, 1, 0
    if text[0] == "-":
        sign = -1
        i = 1
    while i < len(text):
        if text[i] == "+":
            sign = 1
            i += 1
            continue
        if text[i] == "-":
            sign = -1
            i += 1
            continue
        j = i + 1
        while j < len(text) and text[j] not in "+-":
            j += 1
        total += sign * number(text[i:j], consts)
        i = j
    return total & 0xFFFF


def strip_comment(raw):
    quote = False
    for i, ch in enumerate(raw):
        if ch == '"':
            quote = not quote
        elif ch == ";" and not quote:
            return raw[:i]
    return raw


def split_ops(text):
    out, cur, br, quote = [], "", 0, False
    for ch in text:
        if ch == '"':
            quote = not quote
        if ch == "," and br == 0 and not quote:
            out.append(cur.strip())
            cur = ""
            continue
        if not quote and ch == "[":
            br += 1
        elif not quote and ch == "]":
            br -= 1
        cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def db_bytes(text, consts):
    out, i = [], 0
    while i < len(text):
        if text[i] in " \t,":
            i += 1
            continue
        if text[i] == '"':
            j = text.find('"', i + 1)
            if j < 0:
                raise AsmError("unterminated string")
            out.extend(text[i + 1:j].encode("latin1"))
            i = j + 1
            continue
        j = i
        while j < len(text) and text[j] not in " \t,":
            j += 1
        out.append(expr(text[i:j], consts) & 0xFF)
        i = j
    return bytes(out)


class Mem:
    def __init__(self, raw, consts):
        self.seg = None
        self.size = None
        text = raw.strip()
        for name, pref in (("es", 0x26), ("cs", 0x2E), ("ss", 0x36), ("ds", 0x3E)):
            if text.lower().startswith(name + ":"):
                self.seg = pref
                text = text[len(name) + 1:].strip()
                break
        if text.lower().startswith("byte "):
            self.size = 8
            text = text[5:].strip()
        elif text.lower().startswith("word "):
            self.size = 16
            text = text[5:].strip()
        if not (text.startswith("[") and text.endswith("]")):
            raise AsmError("bad memory " + raw)
        inside = text[1:-1].strip()
        for name, pref in (("es", 0x26), ("cs", 0x2E), ("ss", 0x36), ("ds", 0x3E)):
            if inside.lower().startswith(name + ":"):
                self.seg = pref
                inside = inside[len(name) + 1:]
                break
        regs, disp = [], []
        for bit in inside.split("+"):
            b = bit.strip()
            if b.lower() in ("bx", "bp", "si", "di"):
                regs.append(b.lower())
            elif b:
                disp.append(b)
        self.base = "".join(regs) if regs else None
        if self.base not in (None, "bx", "bp", "si", "di", "bxsi", "bxdi", "bpsi", "bpdi"):
            raise AsmError("bad address " + text)
        self.disp = expr("+".join(disp) if disp else "0", consts)

    def encode(self, regfield):
        rm_of = {None: 6, "bxsi": 0, "bxdi": 1, "bpsi": 2, "bpdi": 3, "si": 4, "di": 5, "bp": 6, "bx": 7}
        rm = rm_of[self.base]
        disp = self.disp & 0xFFFF
        signed = disp - 0x10000 if disp & 0x8000 else disp
        if self.base is not None and disp == 0 and self.base != "bp":
            mod, extra = 0, b""
        elif self.base is not None and -128 <= signed <= 127:
            mod, extra = 1, bytes([disp & 0xFF])
        else:
            mod = 0 if self.base is None else 2
            extra = bytes([disp & 0xFF, (disp >> 8) & 0xFF])
        pref = bytes([self.seg]) if self.seg else b""
        return pref, bytes([(mod << 6) | (regfield << 3) | rm]) + extra


SREG = {"es": 0, "cs": 1, "ss": 2, "ds": 3}


def operand(text, consts):
    t = text.strip()
    low = t.lower()
    if low in REGS16:
        return ("r16", REGS16[low])
    if low in REGS8:
        return ("r8", REGS8[low])
    if low in SREG:
        return ("sreg", SREG[low])
    if "[" in t or low.startswith("byte ") or low.startswith("word ") or low.startswith(("es:", "cs:", "ss:", "ds:")):
        return ("m", Mem(t, consts))
    return ("i", expr(t, consts))


class Asm:
    def __init__(self, lines):
        self.lines = lines
        self.longj = set()

    def assemble(self):
        for _ in range(16):
            consts = self.measure()
            code, grew = self.emit(consts)
            if not grew:
                return code, consts
        raise AsmError("branches did not settle")

    def walk(self, consts, emit):
        code = bytearray()
        grew = False
        pos = 0
        pending = None
        for n, raw in self.lines:
            text = strip_comment(raw).strip()
            if not text:
                continue
            if text.endswith(":") and " " not in text:
                consts[text[:-1]] = pos
                continue
            parts = text.split(None, 1)
            if len(parts) == 2 and parts[1].lower().split()[0] == "equ":
                consts[parts[0]] = expr(parts[1].split(None, 1)[1], consts)
                continue
            mnem = parts[0].lower()
            rest = parts[1] if len(parts) > 1 else ""
            if mnem == "db":
                blob = db_bytes(rest, consts) if emit else bytes(len(db_bytes(rest, consts)))
            elif mnem == "rb":
                blob = bytes(expr(rest, consts))
            elif mnem == "dw":
                blob = b""
                for p in split_ops(rest):
                    v = expr(p, consts) if emit else 0
                    blob += bytes([v & 0xFF, (v >> 8) & 0xFF])
            else:
                try:
                    blob, g = self.encode(n, mnem, rest, pos, consts, emit)
                except AsmError as e:
                    raise AsmError("line %d: %s" % (n, e))
                grew = grew or g
            if emit:
                code += blob
            pos += len(blob)
            pending = n
        return (bytes(code) if emit else pos), grew

    def measure(self):
        consts = {}
        self.walk(consts, emit=False)
        return consts

    def emit(self, consts):
        # Labels were measured into `consts`. Emit must not move them, so a
        # branch that grows is reported and the whole program is measured again.
        code, grew = self.walk(dict(consts), emit=True)
        return code, grew

    def encode(self, n, mnem, rest, at, consts, emit):
        if mnem == "rep":
            if rest.strip().lower() != "stosb":
                raise AsmError("only rep stosb")
            return b"\xF3\xAA", False
        if not emit:
            return bytes(self.size_of(n, mnem, rest)), False
        ops = [operand(p, consts) for p in split_ops(rest)] if rest else []
        if mnem == "nop":
            return b"\x90", False
        if mnem in ("ret",):
            return b"\xC3", False
        if mnem == "cld":
            return b"\xFC", False
        if mnem == "clc":
            return b"\xF8", False
        if mnem == "stc":
            return b"\xF9", False
        if mnem == "lodsb":
            return b"\xAC", False
        if mnem == "int":
            return bytes([0xCD, ops[0][1] & 0xFF]), False
        if mnem == "push":
            if ops[0][0] == "sreg":
                return bytes([{0: 0x06, 1: 0x0E, 2: 0x16, 3: 0x1E}[ops[0][1]]]), False
            self.need(ops[0], "r16")
            return bytes([0x50 + ops[0][1]]), False
        if mnem == "pop":
            if ops[0][0] == "sreg":
                if ops[0][1] == 1:
                    raise AsmError("pop cs")
                return bytes([{0: 0x07, 2: 0x17, 3: 0x1F}[ops[0][1]]]), False
            self.need(ops[0], "r16")
            return bytes([0x58 + ops[0][1]]), False
        if mnem in ("inc", "dec"):
            return self.incdec(mnem, ops[0]), False
        if mnem == "neg":
            self.need(ops[0], "r8")
            return self.regop(0xF6, 3, ops[0]), False
        if mnem in ("mul", "div"):
            sub = 4 if mnem == "mul" else 6
            if ops[0][0] == "r16":
                return bytes([0xF7, 0xC0 | (sub << 3) | ops[0][1]]), False
            self.need(ops[0], "r8")
            return self.regop(0xF6, sub, ops[0]), False
        if mnem == "xchg":
            a, b = ops
            if a[0] != b[0] or a[0] not in ("r8", "r16"):
                raise AsmError("xchg")
            return bytes([0x86 if a[0] == "r8" else 0x87, 0xC0 | (a[1] << 3) | b[1]]), False
        if mnem == "loop":
            return self.rel8(0xE2, ops[0][1], at), False
        if mnem == "jcxz":
            return self.rel8(0xE3, ops[0][1], at), False
        if mnem in ("jmp", "call"):
            return self.rel16(0xE9 if mnem == "jmp" else 0xE8, ops[0][1], at), False
        code = jcc_code(mnem)
        if code is not None:
            return self.jcc(n, code, ops[0][1], at)
        if mnem == "mov":
            return self.mov(ops), False
        if mnem == "lea":
            if ops[0][0] != "r16" or ops[1][0] != "m":
                raise AsmError("lea")
            pref, restb = ops[1][1].encode(ops[0][1])
            return pref + bytes([0x8D]) + restb, False
        if mnem in ARITH:
            return self.arith(mnem, ops), False
        raise AsmError("unknown " + mnem)

    def size_of(self, n, mnem, rest):
        if mnem == "rep":
            return 2
        if mnem in ("nop", "ret", "cld", "clc", "stc", "lodsb"):
            return 1
        if mnem in ("push", "pop", "inc", "dec", "neg", "mul", "div"):
            # inc/dec word reg is 1, byte or memory is 2 plus disp. Measure for real.
            return None
        if mnem in ("loop", "jcxz"):
            return 2
        if mnem in ("jmp", "call"):
            return 3
        if jcc_code(mnem) is not None:
            return 5 if n in self.longj else 2
        return None

    def walk(self, consts, emit):
        code = bytearray()
        grew = False
        pos = 0
        for n, raw in self.lines:
            text = strip_comment(raw).strip()
            if not text:
                continue
            if text.endswith(":") and " " not in text:
                consts[text[:-1]] = pos
                continue
            parts = text.split(None, 1)
            if len(parts) == 2 and parts[1].lower().split()[0] == "equ":
                consts[parts[0]] = expr(parts[1].split(None, 1)[1], consts)
                continue
            words = text.split()
            if len(words) >= 2 and words[1].lower() in ("db", "dw", "rb"):
                consts[words[0]] = pos
                mnem = words[1].lower()
                rest = " ".join(words[2:])
            else:
                mnem = parts[0].lower()
                rest = parts[1] if len(parts) > 1 else ""
            if mnem == "db":
                blob = db_bytes(rest, consts)
            elif mnem == "rb":
                blob = bytes(expr(rest, consts))
            elif mnem == "dw":
                blob = b""
                for p in split_ops(rest):
                    v = expr(p, consts)
                    blob += bytes([v & 0xFF, (v >> 8) & 0xFF])
            else:
                known = self.size_of(n, mnem, rest)
                if not emit and known is not None:
                    blob = bytes(known)
                else:
                    # Sizing a form whose length depends on the operands still
                    # needs constants (for displacements). Labels of later code
                    # are the only forward refs, and those are jumps, which
                    # size_of already handled.
                    try:
                        blob, g = self.encode(n, mnem, rest, pos, consts, True)
                    except AsmError as e:
                        raise AsmError("line %d: %s" % (n, e))
                    grew = grew or g
                    if not emit:
                        blob = bytes(len(blob))
            if emit:
                code += blob
            pos += len(blob)
        return (bytes(code) if emit else pos), grew

    @staticmethod
    def need(op, kind):
        if op[0] != kind:
            raise AsmError("expected " + kind)

    def regop(self, opcode, sub, op):
        if op[0] == "r8":
            return bytes([opcode, 0xC0 | (sub << 3) | op[1]])
        pref, rest = op[1].encode(sub)
        return pref + bytes([opcode]) + rest

    def incdec(self, mnem, op):
        plus = 0 if mnem == "inc" else 1
        if op[0] == "r16":
            return bytes([(0x40 if mnem == "inc" else 0x48) + op[1]])
        if op[0] == "r8":
            return bytes([0xFE, 0xC0 | (plus << 3) | op[1]])
        if op[0] != "m" or op[1].size not in (8, 16):
            raise AsmError("inc/dec needs a size")
        pref, rest = op[1].encode(plus)
        return pref + bytes([0xFE if op[1].size == 8 else 0xFF]) + rest

    def rel8(self, opcode, target, at):
        disp = (target - (at + 2)) & 0xFFFF
        signed = disp - 0x10000 if disp & 0x8000 else disp
        if not -128 <= signed <= 127:
            raise AsmError("short branch out of range")
        return bytes([opcode, disp & 0xFF])

    def rel16(self, opcode, target, at):
        disp = (target - (at + 3)) & 0xFFFF
        return bytes([opcode, disp & 0xFF, (disp >> 8) & 0xFF])

    def jcc(self, n, code, target, at):
        disp = (target - (at + 2)) & 0xFFFF
        signed = disp - 0x10000 if disp & 0x8000 else disp
        if n not in self.longj and -128 <= signed <= 127:
            return bytes([0x70 + code, disp & 0xFF]), False
        if n not in self.longj:
            self.longj.add(n)
            return b"\x00" * 5, True
        inv = code ^ 1
        near = self.rel16(0xE9, target, at + 2)
        return bytes([0x70 + inv, len(near)]) + near, False

    def mov(self, ops):
        dst, src = ops
        if dst[0] == "sreg" and src[0] == "r16":
            return bytes([0x8E, 0xC0 | (dst[1] << 3) | src[1]])
        if dst[0] == "r16" and src[0] == "sreg":
            return bytes([0x8C, 0xC0 | (src[1] << 3) | dst[1]])
        if dst[0] == "sreg" and src[0] == "m":
            pref, rest = src[1].encode(dst[1])
            return pref + bytes([0x8E]) + rest
        if dst[0] in ("r16", "r8") and src[0] == "i":
            if dst[0] == "r16":
                return bytes([0xB8 + dst[1], src[1] & 0xFF, (src[1] >> 8) & 0xFF])
            return bytes([0xB0 + dst[1], src[1] & 0xFF])
        if dst[0] in ("r16", "r8") and src[0] in ("r16", "r8", "m"):
            if src[0] != "m" and src[0] != dst[0]:
                raise AsmError("mov size")
            op = 0x8B if dst[0] == "r16" else 0x8A
            if src[0] == "m":
                pref, rest = src[1].encode(dst[1])
                return pref + bytes([op]) + rest
            return bytes([op, 0xC0 | (dst[1] << 3) | src[1]])
        if dst[0] in ("m", "r16", "r8") and src[0] in ("r16", "r8"):
            if dst[0] != "m" and dst[0] != src[0]:
                raise AsmError("mov size")
            op = 0x89 if src[0] == "r16" else 0x88
            if dst[0] == "m":
                pref, rest = dst[1].encode(src[1])
                return pref + bytes([op]) + rest
            return bytes([op, 0xC0 | (src[1] << 3) | dst[1]])
        if dst[0] == "m" and src[0] == "i":
            if dst[1].size not in (8, 16):
                raise AsmError("mov to memory needs byte or word")
            pref, rest = dst[1].encode(0)
            if dst[1].size == 8:
                return pref + bytes([0xC6]) + rest + bytes([src[1] & 0xFF])
            return pref + bytes([0xC7]) + rest + bytes([src[1] & 0xFF, (src[1] >> 8) & 0xFF])
        raise AsmError("mov form")

    def arith(self, mnem, ops):
        g = ARITH[mnem]
        dst, src = ops
        if dst[0] in ("r16", "r8") and src[0] == "i":
            if dst == ("r16", 0):
                return bytes([AXIMM[mnem], src[1] & 0xFF, (src[1] >> 8) & 0xFF])
            if dst == ("r8", 0):
                return bytes([ALIMM[mnem], src[1] & 0xFF])
            op = 0x80 if dst[0] == "r8" else 0x81
            tail = bytes([src[1] & 0xFF]) if dst[0] == "r8" else bytes([src[1] & 0xFF, (src[1] >> 8) & 0xFF])
            return bytes([op, 0xC0 | (g << 3) | dst[1]]) + tail
        if dst[0] in ("r16", "r8") and src[0] in ("r16", "r8", "m"):
            if src[0] != "m" and src[0] != dst[0]:
                raise AsmError("size")
            base = 0x02 + g * 8 + (1 if dst[0] == "r16" else 0)
            if src[0] == "m":
                pref, rest = src[1].encode(dst[1])
                return pref + bytes([base]) + rest
            return bytes([base, 0xC0 | (dst[1] << 3) | src[1]])
        if src[0] in ("r16", "r8") and dst[0] in ("m", "r16", "r8"):
            base = g * 8 + (1 if src[0] == "r16" else 0)
            if dst[0] == "m":
                pref, rest = dst[1].encode(src[1])
                return pref + bytes([base]) + rest
            return bytes([base, 0xC0 | (src[1] << 3) | dst[1]])
        if dst[0] == "m" and src[0] == "i":
            if dst[1].size not in (8, 16):
                raise AsmError("arith memory needs a size")
            pref, rest = dst[1].encode(g)
            op = 0x80 if dst[1].size == 8 else 0x81
            tail = bytes([src[1] & 0xFF]) if dst[1].size == 8 else bytes([src[1] & 0xFF, (src[1] >> 8) & 0xFF])
            return pref + bytes([op]) + rest + tail
        raise AsmError(mnem + " form")


def mz(image, entry):
    total = 32 + len(image)
    f = bytearray(32)

    def w(at, v):
        f[at] = v & 0xFF
        f[at + 1] = (v >> 8) & 0xFF

    f[0], f[1] = ord("M"), ord("Z")
    w(2, total % 512)
    w(4, (total + 511) // 512)
    w(8, 2)
    w(10, 0x10)
    w(12, 0xFFFF)
    w(14, 0)
    w(16, 0xFFFE)
    w(20, entry)
    w(22, 0)
    w(24, 28)
    return bytes(f) + image


def main(src, dst):
    lines = [(n, line.rstrip("\n")) for n, line in enumerate(open(src, encoding="latin1"), 1)]
    code, consts = Asm(lines).assemble()
    if "start" not in consts:
        raise SystemExit("no start label")
    data = mz(code, consts["start"])
    open(dst, "wb").write(data)
    print("assembled %s: %d code bytes, entry %d, file %d" % (dst, len(code), consts["start"], len(data)))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: asm16.py in.asm out.exe")
    try:
        main(sys.argv[1], sys.argv[2])
    except AsmError as e:
        raise SystemExit("asm: " + str(e))
