# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Turn FabGL's i8086 core (flat 1 MB array) into one that reads and writes guest memory
through axis::door::MemoryBus. Run on the pristine upstream files; see third_party/i8086/AXIS_CHANGES.md.

usage: python -I tools/vendor/refactor_i8086.py third_party/i8086
"""
import re
import sys


def matching(s, i):
    """index of the ')' matching the '(' at s[i]"""
    depth = 0
    for j in range(i, len(s)):
        if s[j] == '(':
            depth += 1
        elif s[j] == ')':
            depth -= 1
            if depth == 0:
                return j
    raise ValueError('unbalanced')


def statement_end(s, i):
    """index of the ';' ending the expression starting at s[i] (depth 0)"""
    depth = 0
    for j in range(i, len(s)):
        c = s[j]
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == ';' and depth == 0:
            return j
    raise ValueError('no ;')


def convert_assignments(s):
    """MEM8(a) = v;  ->  WR8(a, v);   (same for MEM16); reads stay MEM8()/MEM16()"""
    out, i, n = [], 0, 0
    pat = re.compile(r'(?<![A-Za-z0-9_])MEM(8|16)\(')
    while True:
        m = pat.search(s, i)
        if not m:
            out.append(s[i:])
            break
        open_paren = m.end() - 1
        close = matching(s, open_paren)
        k = close + 1
        while k < len(s) and s[k] == ' ':
            k += 1
        if k < len(s) and s[k] == '=' and s[k + 1] != '=':
            end = statement_end(s, k + 1)
            arg = s[open_paren + 1:close]
            rhs = s[k + 1:end].strip()
            out.append(s[i:m.start()] + f'WR{m.group(1)}({arg}, {rhs})')
            i = end
            n += 1
        else:
            out.append(s[i:close + 1])
            i = close + 1
    return ''.join(out), n


def must(s, a, b, count=1):
    if s.count(a) < count:
        raise SystemExit(f'pattern not found ({s.count(a)} of {count}): {a[:70]!r}')
    return s.replace(a, b)


def main(d):
    h = open(f'{d}/i8086.h', encoding='utf-8').read()
    h = must(h, '#include "fabgl.h"', '#include <stdint.h>\n\nnamespace axis { namespace door { struct MemoryBus; } }')
    h = must(h, '#define I8086_SHOW_OPCODE_STATS 0', '#define I8086_SHOW_OPCODE_STATS 0\n#ifndef I80186MODE\n  #define I80186MODE 1\n#endif')
    h = re.sub(r'  typedef void \(\*WriteVideoMemory8\).*?\n.*?\n.*?\n.*?\n', '', h, count=1, flags=0)
    h = re.sub(r'static void setCallbacks\(.*?\n  \}', '''static void setCallbacks(void * context, ReadPort readPort, WritePort writePort, Interrupt interrupt) {
    s_context            = context;
    s_readPort           = readPort;
    s_writePort          = writePort;
    s_interrupt          = interrupt;
  }''', h, count=1, flags=re.S)
    h = must(h, 'static void setMemory(uint8_t * memory) { s_memory = memory; }',
             'static void setBus(axis::door::MemoryBus * bus) { s_bus = bus; }')
    h = re.sub(r'  static WriteVideoMemory8 .*?\n  static ReadVideoMemory16 +s_readVideoMemory16;\n', '', h, flags=re.S)
    h = must(h, '  static uint8_t *', '  static axis::door::MemoryBus *')
    h = re.sub(r'static axis::door::MemoryBus \*\s+s_memory;', 'static axis::door::MemoryBus * s_bus;', h)
    h = must(h, '  static void step();', '''  static void step();

  // AXIS: generic register access (index: 0 AX 1 CX 2 DX 3 BX 4 SP 5 BP 6 SI 7 DI 8 ES 9 CS 10 SS 11 DS)
  static uint16_t reg(int index);
  static void     setReg(int index, uint16_t value);
  static uint16_t flagsWord();
  static void     setFlagsWord(uint16_t value);''')
    open(f'{d}/i8086.h', 'w', encoding='utf-8').write(h)

    c = open(f'{d}/i8086.cpp', encoding='utf-8').read()
    c = must(c, '#include "i8086.h"', '#include "i8086.h"\n#include "door/membus.h"\n\n#define PSRAM_WORKAROUND2')
    c = re.sub(r'#if FABGL_ESP_IDF_VERSION.*?#endif', '#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"', c, count=1, flags=re.S)
    c = must(c, 'static uint8_t    regs[48];', 'alignas(4) static uint8_t regs[48];')
    c = re.sub(r'i8086::WriteVideoMemory8 .*?i8086::ReadVideoMemory16 +i8086::s_readVideoMemory16;\n', '', c, flags=re.S)
    c = must(c, 'uint8_t *                 i8086::s_memory;', 'axis::door::MemoryBus *   i8086::s_bus;')

    # memory access: registers live at a fake address above the 1 MB space (8086tiny's trick),
    # everything else goes through the bus with 8086 wraparound at 1 MB
    access = '''// AXIS: all guest memory goes through the MemoryBus; registers sit at kRegBase.
static constexpr uint32_t kRegBase = 0x110000;
static axis::door::MemoryBus * g_bus;
static inline uint8_t RD8(uint32_t a) {
  return a >= kRegBase ? regs[a - kRegBase] : g_bus->read8(a & 0xFFFFF);
}
static inline uint16_t RD16(uint32_t a) {
  return a >= kRegBase ? (uint16_t)(regs[a - kRegBase] | regs[a - kRegBase + 1] << 8) : g_bus->read16(a & 0xFFFFF);
}
static inline uint8_t WR8(uint32_t a, uint8_t v) {
  if (a >= kRegBase) regs[a - kRegBase] = v; else g_bus->write8(a & 0xFFFFF, v);
  return v;
}
static inline uint16_t WR16(uint32_t a, uint16_t v) {
  if (a >= kRegBase) { regs[a - kRegBase] = (uint8_t)v; regs[a - kRegBase + 1] = v >> 8; } else g_bus->write16(a & 0xFFFFF, v);
  return v;
}
// word on the stack, i words above SS:SP (offset wraps inside the segment)
#define STK(i) (16 * (uint32_t)regs16[REG_SS] + (uint16_t)(regs16[REG_SP] + 2 * (i)))
#define MEM8(addr)  RD8((uint32_t)(addr))
#define MEM16(addr) RD16((uint32_t)(addr))'''
    c = must(c, '#define MEM8(addr)  s_memory[addr]\n#define MEM16(addr) (*(uint16_t*)(s_memory + (addr)))', access)
    c = re.sub(r'uint8_t i8086::RMEM8\(int addr\)\n\{.*?\n\}', 'uint8_t i8086::RMEM8(int addr)\n{\n  return RD8(addr);\n}', c, count=1, flags=re.S)
    c = re.sub(r'uint16_t i8086::RMEM16\(int addr\)\n\{.*?\n\}', 'uint16_t i8086::RMEM16(int addr)\n{\n  return RD16(addr);\n}', c, count=1, flags=re.S)
    c = re.sub(r'(uint8_t i8086::WMEM8\(int addr, uint8_t value\)\n)\{.*?\n\}', r'\1{\n  return WR8(addr, value);\n}', c, count=1, flags=re.S)
    c = re.sub(r'(uint16_t i8086::WMEM16\(int addr, uint16_t value\)\n)\{.*?\n\}', r'\1{\n  return WR16(addr, value);\n}', c, count=1, flags=re.S)

    c = must(c, '''  regs_offset = (int32_t)(regs - s_memory);

  regs8  = (uint8_t *)(s_memory + regs_offset);
  regs16 = (uint16_t *)(s_memory + regs_offset);''', '''  g_bus       = s_bus;
  regs_offset = (int32_t)kRegBase;

  regs8  = regs;
  regs16 = (uint16_t *)regs;''')

    # stack frames that used raw pointers into memory
    c = must(c, '''    uint16_t * stack = &MEM16(16 * regs16[REG_SS] + regs16[REG_SP]);
    stack[2] = make_flags();
    stack[1] = regs16[REG_CS];
    stack[0] = reg_ip;''', '''    WR16(STK(2), make_flags());
    WR16(STK(1), regs16[REG_CS]);
    WR16(STK(0), reg_ip);''')
    c = must(c, '''        uint16_t * stack = &MEM16(16 * regs16[REG_SS] + regs16[REG_SP]);
        reg_ip         = stack[0];
        regs16[REG_CS] = stack[1];
        set_flags(stack[2]);''', '''        reg_ip         = RD16(STK(0));
        regs16[REG_CS] = RD16(STK(1));
        set_flags(RD16(STK(2)));''')
    c = must(c, '''        uint16_t * stack = &MEM16(16 * regs16[REG_SS] + regs16[REG_SP]);
        reg_ip         = stack[0];
        regs16[REG_CS] = stack[1];
        regs16[REG_SP] += 4;''', '''        uint16_t newIP = RD16(STK(0));
        regs16[REG_CS] = RD16(STK(1));
        reg_ip         = newIP;
        regs16[REG_SP] += 4;''')
    c = must(c, '''      uint16_t * stack = &MEM16(16 * regs16[REG_SS] + regs16[REG_SP]);
      stack[1] = regs16[REG_CS];
      stack[0] = reg_ip + 5;''', '''      WR16(STK(1), regs16[REG_CS]);
      WR16(STK(0), reg_ip + 5);''')

    # instruction fetch: copy the next bytes out of guest memory (offset wraps in the segment)
    c = must(c, '    uint8_t const * opcode_stream = s_memory + 16 * regs16[REG_CS] + reg_ip;',
             '''    uint8_t fetch[8];
    for (int k = 0; k < 8; k++)
      fetch[k] = g_bus->read8((16 * (uint32_t)regs16[REG_CS] + (uint16_t)(reg_ip + k)) & 0xFFFFF);
    uint8_t const * opcode_stream = fetch;''')
    # unaligned word reads of the opcode bytes (would fault on the ESP32)
    c = must(c, '*(uint16_t*)(opcode_stream + 1)', '(uint16_t)(opcode_stream[1] | opcode_stream[2] << 8)', 2)

    c, n = convert_assignments(c)
    if 's_memory' in c:
        raise SystemExit('s_memory still referenced')
    if '&MEM16' in c or '&MEM8' in c:
        raise SystemExit('a pointer into guest memory is left')
    c = c.replace('printf(', 'I8086_LOG(')
    c = must(c, '#define PSRAM_WORKAROUND2', '#define PSRAM_WORKAROUND2\n#ifndef I8086_LOG\n  #define I8086_LOG(...) ((void)0)  // unsupported-opcode messages; the session counts them instead\n#endif')
    c = must(c, '}   // namespace fabgl', '''uint16_t i8086::reg(int index)              { return regs16[index]; }
void     i8086::setReg(int index, uint16_t v) { regs16[index] = v; }
uint16_t i8086::flagsWord()                   { return make_flags(); }
void     i8086::setFlagsWord(uint16_t v)      { set_flags(v); }


}   // namespace fabgl''')
    open(f'{d}/i8086.cpp', 'w', encoding='utf-8').write(c)
    print(f'ok: {n} memory writes converted')


if __name__ == '__main__':
    main(sys.argv[1])
