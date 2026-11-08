// AXIS sysop panel: everything is built from what the boards report, and redrawn when it changes.
(() => {
  const $ = (s) => document.querySelector(s);
  const esc = (s) => String(s).replace(/[&<>\"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '\"': '&quot;' }[c]));
  let ws, boards = [], nodes = {}, events = [], authed = false, waiting = {};
  const terms = {};
  let con = null, conTerm = null;
  const b64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
  const enc = (str) => btoa(unescape(encodeURIComponent(str)).replace(/[\u0080-\uffff]/g, '?'));

  function connect() {
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
    ws.onopen = () => { $('#link').textContent = 'linked'; $('#link').className = 'ok'; };
    ws.onclose = () => {
      authed = false; $('#link').textContent = 'offline'; $('#link').className = 'dim';
      $('#app').hidden = true; $('#login').hidden = false;
      setTimeout(connect, 2000);
    };
    ws.onmessage = (e) => handle(JSON.parse(e.data));
  }
  function send(m) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(m)); }
  function cmd(c, board) { return new Promise((ok) => { (waiting[c] = waiting[c] || []).push(ok); send({ cmd: c, board: board || null }); }); }
  function handle(m) {
    if (m.need === 'login') { $('#login').hidden = false; $('#app').hidden = true; }
    if ('login' in m) {
      authed = m.login;
      $('#loginmsg').textContent = authed ? '' : 'Wrong handle or password (or no board answered).';
      $('#login').hidden = authed; $('#app').hidden = !authed;
      if (m.token) document.cookie = 'axis_panel=' + m.token + '; max-age=315360000; path=/; samesite=strict' + (location.protocol === 'https:' ? '; secure' : '');
      if (authed) { $('#who').textContent = $('#handle').value || 'sysop'; refreshAll(); }
    }
    if (m.boards) { boards = m.boards; drawBoards(); }
    if (m.evt) { events.push(m.evt.board + '  ' + m.evt.line); events = events.slice(-300); $('#events').textContent = events.join('\n'); }
    if (m.reply) { const q = waiting[m.reply.cmd]; if (q && q.length) q.shift()(m.reply.lines); }
    if (m.scr) {
      const k = m.scr.board + '#' + m.scr.node, bytes = b64(m.scr.data);
      (terms[k] || tile(m.scr.board, m.scr.node)).feed(bytes);
      if (con && conTerm && con.board === m.scr.board && con.node === m.scr.node) conTerm.feed(bytes);
    }
    if (m.con) {
      const a = m.con.answer || '';
      const n = parseInt(a.split(' ')[1], 10);
      if (a.startsWith('CON NO')) { $('#conmsg').textContent = a.slice(7); $('#conmsg').className = 'warn'; }
      else if (n > 0) { con = { board: m.con.board, node: n }; $('#conmsg').textContent = `on node ${n}`; $('#conmsg').className = 'ok'; conTerm.reset(); const t = terms[m.con.board + '#' + n]; if (t) conTerm.copyFrom(t); }
      else { con = null; $('#conmsg').textContent = 'left'; $('#conmsg').className = 'dim'; }
    }
  }
  async function loadNodes() {
    for (const b of boards.filter((b) => b.alive)) {
      const lines = await cmd('@NODES', b.ip);
      nodes[b.ip] = lines.map((l) => { const m = l.match(/node (\d+) ip (\S+) handle '(.*)' activity '(.*)'(.*)$/); return m && { num: +m[1], ip: m[2], handle: m[3], activity: m[4] }; }).filter(Boolean);
    }
    drawBoards();
  }
  function drawBoards() {
    const all = Object.values(nodes).flat(), seen = new Set(); let html = '';
    for (const b of boards) {
      const s = b.stat || {}, mine = all.filter((n) => n.ip === b.ip && !seen.has(n.num) && seen.add(n.num));
      html += `<div class="win"><h2>[ ${esc(b.ip)} ${s.role === 'main' ? '<span class="tag">MAIN</span>' : ''} ${b.alive ? '' : 'OFFLINE'} ]</h2>`;
      html += `<div class="dim">heap ${esc(s.heap || '?')} &middot; up ${fmtUp(s.up)} &middot; wifi ${esc(s.rssi || '?')} dBm &middot; SD ${s.sd_mb && s.sd_mb !== '0' ? esc(s.sd_mb) + ' MB free' : 'none'}</div>`;
      for (const n of mine) {
        const busy = n.handle !== '';
        html += `<div class="node ${busy ? 'busy' : ''}"><span class="tag">node ${n.num}</span> <span class="who">${busy ? esc(n.handle) : 'waiting for a caller'}</span> <span class="dim">${esc(n.activity)}</span></div>`;
      }
      html += '</div>';
    }
    $('#boards').innerHTML = html || '<p class="dim">Looking for boards on the network...</p>';
    drawSystem(); drawWallTiles(); fillBoards();
  }
  function fmtUp(s) { s = +s || 0; const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60); return h ? `${h}h${m}m` : `${m}m`; }
  function tile(board, node) {
    const k = board + '#' + node, d = document.createElement('div');
    d.className = 'tile'; d.dataset.k = k;
    d.innerHTML = `<div class="cap"><span>node ${node}</span><span class="dim">${esc(board)}</span></div><pre class="term"></pre>`;
    d.onclick = () => { $('#conboard').value = board; fillNodes(); $('#connode').value = node; showTab('console'); };
    $('#wall').appendChild(d); terms[k] = new Term(d.querySelector('pre')); return terms[k];
  }
  function drawWallTiles() {
    for (const [ip, list] of Object.entries(nodes)) for (const n of list) if (n.ip === ip && !terms[ip + '#' + n.num]) tile(ip, n.num);
    for (const el of document.querySelectorAll('.tile')) {
      const [ip, num] = el.dataset.k.split('#'), n = (nodes[ip] || []).find((x) => x.num === +num);
      el.classList.toggle('on', !!(n && n.handle)); el.querySelector('.dim').textContent = n && n.handle ? n.handle : ip;
    }
  }
  setInterval(() => { for (const t of Object.values(terms)) t.draw(); if (conTerm) conTerm.draw(); }, 150);
  conTerm = new Term($('#conscreen'));
  function fillBoards() { const cur = $('#conboard').value; $('#conboard').innerHTML = boards.filter((b) => b.alive).map((b) => `<option value="${esc(b.ip)}">${esc(b.ip)}${b.stat && b.stat.role === 'main' ? ' (main)' : ''}</option>`).join(''); if (cur) $('#conboard').value = cur; fillNodes(); }
  function fillNodes() { const ip = $('#conboard').value, cur = $('#connode').value; $('#connode').innerHTML = (nodes[ip] || []).filter((n) => n.ip === ip && n.handle).map((n) => `<option value="${n.num}">node ${n.num} ${esc(n.handle)}</option>`).join('') || '<option value="">no callers</option>'; if (cur) $('#connode').value = cur; }
  $('#conboard').onchange = fillNodes;
  $('#connew').onclick = () => send({ con: 'new', board: $('#conboard').value });
  $('#conjoin').onclick = () => { if ($('#connode').value) send({ con: 'join ' + $('#connode').value, board: $('#conboard').value }); };
  $('#conoff').onclick = () => { if (con) send({ con: 'off', board: con.board }); };
  const keyFor = (e) => { const map = { Enter: '\r', Backspace: '\b', Tab: '\t', Escape: '\x1b', ArrowUp: '\x1b[A', ArrowDown: '\x1b[B', ArrowRight: '\x1b[C', ArrowLeft: '\x1b[D' }; if (map[e.key]) return map[e.key]; if (e.ctrlKey && e.key.length === 1) return String.fromCharCode(e.key.toUpperCase().charCodeAt(0) - 64); return e.key.length === 1 ? e.key : ''; };
  $('#conscreen').addEventListener('keydown', (e) => { if (!con) return; const k = keyFor(e); if (!k) return; e.preventDefault(); send({ key: enc(k), board: con.board }); });
  $('#conline').addEventListener('submit', (e) => { e.preventDefault(); if (!con) return; send({ key: enc($('#context').value + '\r'), board: con.board }); $('#context').value = ''; });
  async function loadUsers() { const lines = await cmd('@USERS'), rows = lines.filter((l) => l.startsWith('user ')).map((l) => l.slice(5).split('|')); $('#users').innerHTML = '<tr><th>Handle</th><th>Status</th><th>Sec</th><th>Calls</th></tr>' + rows.map((r) => `<tr><td>${esc(r[1])}</td><td>${esc(r[2])}</td><td>${esc(r[3])}</td><td>${esc(r[4])}</td></tr>`).join(''); }
  async function loadSettings() { const lines = await cmd('@SETTINGS'); $('#settings').innerHTML = lines.filter((l) => l.includes('=')).map((l) => { const i = l.indexOf('='); return `<label>${esc(l.slice(0, i))} <input data-k="${esc(l.slice(0, i))}" value="${esc(l.slice(i + 1))}"></label>`; }).join(''); }
  async function loadLists() { for (const [id, c] of [['rumors', '@RUMORS'], ['liners', '@LINERS'], ['bbslist', '@BBSLIST'], ['votes', '@VOTES']]) $('#list-' + id).textContent = (await cmd(c)).join('\n') || '(empty)'; }
  async function drawSystem() { $('#system').innerHTML = boards.map((b) => `<div class="win"><h2>[ ${esc(b.ip)} ]</h2><pre>${esc(Object.entries(b.stat || {}).map(([k, v]) => k + ' ' + v).join('\n'))}</pre></div>`).join(''); }
  function showTab(name) { document.querySelectorAll('#tabs button').forEach((x) => x.classList.toggle('on', x.dataset.tab === name)); document.querySelectorAll('.tab').forEach((t) => (t.hidden = t.id !== 'tab-' + name)); send({ wall: name === 'wall' || name === 'console' }); ({ nodes: loadNodes, wall: loadNodes, console: loadNodes, users: loadUsers, settings: loadSettings, lists: loadLists, system: drawSystem })[name](); }
  $('#tabs').addEventListener('click', (e) => { const b = e.target.closest('button[data-tab]'); if (b) showTab(b.dataset.tab); });
  $('#loginform').addEventListener('submit', (e) => { e.preventDefault(); send({ login: { handle: $('#handle').value, password: $('#password').value, remember: $('#remember').checked } }); $('#password').value = ''; });
  $('#logout').onclick = () => { const t = (document.cookie.match(/(?:^|; )axis_panel=([^;]*)/) || [])[1] || ''; document.cookie = 'axis_panel=; max-age=0; path=/'; authed = false; send({ logout: t }); };
  const logo = new Term($('#logo'), 80, 8);
  fetch('/static/logo.ans').then((r) => r.arrayBuffer()).then((b) => { logo.feed(new Uint8Array(b)); logo.draw(); });
  function refreshAll() { loadNodes(); loadUsers(); }
  setInterval(() => { if (authed && !document.hidden) loadNodes(); }, 5000);
  setInterval(() => { $('#clock').textContent = new Date().toLocaleTimeString(); }, 1000);
  connect();
})();
