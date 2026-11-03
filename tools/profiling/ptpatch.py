import re
f='src/cpu.c'; s=open(f).read()
hdr='''
/* AXIS page-touch profiler */
#include <stdio.h>
#include <stdlib.h>
uint8_t pt_r[256], pt_w[256];
static uint8_t *pt_snap;
#define PT_R(a) (pt_r[((a) & 0xFFFFF) >> 12] = 1)
#define PT_W(a) (pt_w[((a) & 0xFFFFF) >> 12] = 1)
static void pt_dump(void)
{
    const char *fn = getenv("PT_OUT"); if(!fn || !pt_snap) return;
    FILE *o = fopen(fn, "w"); if(!o) return;
    for(int p = 0; p < 256; p++) {
        int d = memcmp(pt_snap + p * 4096, memory + p * 4096, 4096) != 0;
        fprintf(o, "%05X %d %d %d\\n", p * 4096, pt_r[p], pt_w[p], d);
    }
    fclose(o);
}
'''
s=s.replace('static uint8_t GetMemAbsB(uint32_t addr)', hdr+'\nstatic uint8_t GetMemAbsB(uint32_t addr)',1)
s=s.replace('''static uint8_t GetMemAbsB(uint32_t addr)
{
    return''','''static uint8_t GetMemAbsB(uint32_t addr)
{
    PT_R(addr);
    return''',1)
s=s.replace('''static uint16_t GetMemAbsW(uint32_t addr)
{
    return''','''static uint16_t GetMemAbsW(uint32_t addr)
{
    PT_R(addr); PT_R(addr + 1);
    return''',1)
s=s.replace('''static void SetMemAbsB(uint32_t addr, uint8_t val)
{
''','''static void SetMemAbsB(uint32_t addr, uint8_t val)
{
    PT_W(addr);
''',1)
s=s.replace('''static void SetMemAbsW(uint32_t addr, uint16_t x)
{
''','''static void SetMemAbsW(uint32_t addr, uint16_t x)
{
    PT_W(addr); PT_W(addr + 1);
''',1)
s=s.replace('''static uint8_t GetMemB(int seg, uint16_t off)
{
    return memory[0xFFFFF & (sregs[seg] * 16 + off)];''','''static uint8_t GetMemB(int seg, uint16_t off)
{
    PT_R(sregs[seg] * 16 + off);
    return memory[0xFFFFF & (sregs[seg] * 16 + off)];''',1)
s=s.replace('''void execute(void)
{
''','''void execute(void)
{
    if(!pt_snap) { pt_snap = malloc(0x100000); memcpy(pt_snap, memory, 0x100000); atexit(pt_dump); }
''',1)
if '#include <string.h>' not in s: s='#include <string.h>\n'+s
open(f,'w').write(s)
print(s.count('PT_R('), s.count('PT_W('))
