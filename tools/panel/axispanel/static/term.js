// A small 80x25 ANSI terminal for the panel's screen wall and console: CP437 text, colours,
// cursor moves and clears, drawn as rows of coloured spans.
const CP437 = ' ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼ !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~⌂' +
  'ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ';
const PAL = ['#000', '#a00', '#0a0', '#a50', '#00a', '#a0a', '#0aa', '#aaa', '#555', '#f55', '#5f5', '#ff5', '#55f', '#f5f', '#5ff', '#fff'];

class Term {
  constructor(el, cols = 80, rows = 25) {
    this.el = el; this.cols = cols; this.rows = rows;
    this.reset();
  }
  reset() {
    this.grid = Array.from({ length: this.rows }, () => Array.from({ length: this.cols }, () => [' ', 7, 0]));
    this.x = 0; this.y = 0; this.fg = 7; this.bg = 0; this.bold = false;
    this.state = 0; this.params = ''; this.saved = [0, 0]; this.iac = 0;
    this.dirty = true;
  }
  feed(bytes) {
    for (const b of bytes) this.byte(b);
    this.dirty = true;
  }
  byte(b) {
    if (this.iac) { this.iac--; return; }                 // telnet option bytes
    if (b === 0xff) { this.iac = 2; return; }
    if (this.state === 1) { this.state = b === 0x5b ? 2 : 0; this.params = ''; return; }  // ESC [
    if (this.state === 2) {
      if ((b >= 0x30 && b <= 0x3f)) { this.params += String.fromCharCode(b); return; }
      this.state = 0; this.csi(String.fromCharCode(b)); return;
    }
    if (b === 0x1b) { this.state = 1; return; }
    if (b === 13) { this.x = 0; return; }
    if (b === 10) { this.newline(); return; }
    if (b === 8) { if (this.x > 0) this.x--; return; }
    if (b === 12) { this.clear(); return; }
    if (b === 7 || b === 0) return;
    if (this.x >= this.cols) { this.x = 0; this.newline(); }
    this.grid[this.y][this.x++] = [CP437[b] || ' ', this.fg + (this.bold ? 8 : 0), this.bg];
  }
  newline() {
    if (++this.y >= this.rows) { this.grid.shift(); this.grid.push(Array.from({ length: this.cols }, () => [' ', 7, 0])); this.y = this.rows - 1; }
  }
  clear() { this.grid.forEach((r) => r.forEach((c, i) => (r[i] = [' ', 7, 0]))); this.x = this.y = 0; }
  csi(c) {
    const p = this.params.replace('?', '').split(';').map((v) => parseInt(v, 10));
    const n = isNaN(p[0]) ? 1 : p[0];
    const clamp = () => { this.x = Math.max(0, Math.min(this.cols - 1, this.x)); this.y = Math.max(0, Math.min(this.rows - 1, this.y)); };
    if (c === 'm') {
      for (const v of (this.params === '' ? [0] : p)) {
        if (isNaN(v) || v === 0) { this.fg = 7; this.bg = 0; this.bold = false; }
        else if (v === 1) this.bold = true;
        else if (v === 22) this.bold = false;
        else if (v >= 30 && v <= 37) this.fg = v - 30;
        else if (v >= 40 && v <= 47) this.bg = v - 40;
      }
    } else if (c === 'H' || c === 'f') { this.y = (isNaN(p[0]) ? 1 : p[0]) - 1; this.x = (isNaN(p[1]) ? 1 : p[1]) - 1; clamp(); }
    else if (c === 'A') { this.y -= n; clamp(); } else if (c === 'B') { this.y += n; clamp(); }
    else if (c === 'C') { this.x += n; clamp(); } else if (c === 'D') { this.x -= n; clamp(); }
    else if (c === 'J') { if (p[0] === 2 || p[0] === 3) this.clear(); else for (let y = this.y; y < this.rows; y++) this.grid[y].fill(null).forEach((_, i, r) => (r[i] = [' ', 7, 0])); }
    else if (c === 'K') { for (let x = this.x; x < this.cols; x++) this.grid[this.y][x] = [' ', 7, this.bg]; }
    else if (c === 's') this.saved = [this.x, this.y];
    else if (c === 'u') [this.x, this.y] = this.saved;
  }
  draw() {
    if (!this.dirty) return;
    this.dirty = false;
    let html = '';
    for (const row of this.grid) {
      let cur = null, run = '';
      for (const [ch, fg, bg] of row) {
        const key = fg + ',' + bg;
        if (key !== cur) { if (cur !== null) html += '</span>'; const [f, b] = key.split(','); html += `<span style="color:${PAL[f]};background:${PAL[b]}">`; cur = key; }
        html += ch === '<' ? '&lt;' : ch === '&' ? '&amp;' : ch;
      }
      html += '</span>\n';
    }
    this.el.innerHTML = html;
  }
}
