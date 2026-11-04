// AXIS sysop panel: everything is built from what the boards report, and redrawn when it changes.
(() => {
  const $ = (s) => document.querySelector(s);
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  let ws, boards = [], nodes = {}, events = [], authed = false, waiting = {};

  function connect() {
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
    ws.onopen = () => { $('#link').textContent = 'linked'; $('#link').className = 'ok'; };
    ws.onclose = () => { // the app restarted or the network dropped: log in again once it's back
      authed = false; $('#link').textContent = 'offline'; $('#link').className = 'dim';
      $('#app').hidden = true; $('#login').hidden = false;
      setTimeout(connect, 2000);
    };
    ws.onmessage = (e) => handle(JSON.parse(e.data));
  }

  function send(m) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(m)); }
  function cmd(c, board) {  // resolves with the reply lines
    return new Promise((ok) => { (waiting[c] = waiting[c] || []).push(ok); send({ cmd: c, board: board || null }); });
  }

  function handle(m) {
    if (m.need === 'login') { $('#login').hidden = false; $('#app').hidden = true; }
    if ('login' in m) {
      authed = m.login;
      $('#loginmsg').textContent = authed ? '' : 'Wrong handle or password (or no board answered).';
      $('#login').hidden = authed; $('#app').hidden = !authed;
      if (authed) { $('#who').textContent = $('#handle').value; refreshAll(); }
    }
    if (m.boards) { boards = m.boards; drawBoards(); }
    if (m.evt) { events.push(m.evt.board + '  ' + m.evt.line); events = events.slice(-300); $('#events').textContent = events.join('\n'); }
    if (m.reply) { const q = waiting[m.reply.cmd]; if (q && q.length) q.shift()(m.reply.lines); }
  }

  // ---- nodes: each board's @NODES, cards per board
  async function loadNodes() {
    for (const b of boards.filter((b) => b.alive)) {
      const lines = await cmd('@NODES', b.ip);
      nodes[b.ip] = lines.map((l) => {
        const m = l.match(/node (\d+) ip (\S+) handle '(.*)' activity '(.*)'(.*)$/);
        return m && { num: +m[1], ip: m[2], handle: m[3], activity: m[4] };
      }).filter(Boolean);
    }
    drawBoards();
  }

  function drawBoards() {
    const all = Object.values(nodes).flat();
    const seen = new Set();
    let html = '';
    for (const b of boards) {
      const s = b.stat || {};
      const mine = all.filter((n) => n.ip === b.ip && !seen.has(n.num) && seen.add(n.num));
      html += `<div class="win"><h2>[ ${esc(b.ip)} ${s.role === 'main' ? '<span class="tag">MAIN</span>' : ''} ${b.alive ? '' : 'OFFLINE'} ]</h2>`;
      html += `<div class="dim">heap ${esc(s.heap || '?')} &middot; up ${fmtUp(s.up)} &middot; wifi ${esc(s.rssi || '?')} dBm &middot; SD ${s.sd_mb && s.sd_mb !== '0' ? esc(s.sd_mb) + ' MB free' : 'none'}</div>`;
      for (const n of mine) {
        const busy = n.handle !== '';
        html += `<div class="node ${busy ? 'busy' : ''}"><span class="tag">node ${n.num}</span> <span class="who">${busy ? esc(n.handle) : 'waiting for a caller'}</span> <span class="dim">${esc(n.activity)}</span>`;
        if (busy) html += `<div class="acts">
          <button data-act="note" data-n="${n.num}" data-b="${esc(b.ip)}">Note</button>
          <button data-act="time" data-n="${n.num}" data-b="${esc(b.ip)}">+15 min</button>
          <button data-act="tc" data-n="${n.num}" data-b="${esc(b.ip)}">To teleconf</button>
          <button data-act="kick" data-n="${n.num}" data-b="${esc(b.ip)}" class="danger">Kick</button></div>`;
        html += '</div>';
      }
      html += '</div>';
    }
    $('#boards').innerHTML = html || '<p class="dim">Looking for boards on the network...</p>';
    drawSystem();
  }

  function fmtUp(s) { s = +s || 0; const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60); return h ? `${h}h${m}m` : `${m}m`; }

  $('#boards').addEventListener('click', async (e) => {
    const t = e.target.closest('button[data-act]'); if (!t) return;
    const n = t.dataset.n, b = t.dataset.b;
    if (t.dataset.act === 'kick' && !confirmIn(t, 'Kick?')) return;
    if (t.dataset.act === 'note') { const text = prompt(`Note to node ${n}`); if (!text) return; await cmd(`@NOTE ${n} ${text}`, b); }
    if (t.dataset.act === 'time') await cmd(`@ADDTIME ${n} 15`, b);
    if (t.dataset.act === 'tc') await cmd(`@FORCETC ${n}`, b);
    if (t.dataset.act === 'kick') await cmd(`@KICK ${n}`, b);
    setTimeout(loadNodes, 800);
  });
  function confirmIn(btn, label) {  // a second tap confirms (no dialogs on some phones)
    if (btn.dataset.armed) return true;
    btn.dataset.armed = 1; const old = btn.textContent; btn.textContent = label;
    setTimeout(() => { delete btn.dataset.armed; btn.textContent = old; }, 3000);
    return false;
  }
  $('#bcast').addEventListener('submit', async (e) => {
    e.preventDefault(); const t = $('#bcasttext').value.trim(); if (!t) return;
    for (const b of boards.filter((b) => b.alive)) await cmd('@BCAST ' + t, b.ip);
    $('#bcasttext').value = '';
  });

  // ---- users
  async function loadUsers() {
    const lines = await cmd('@USERS');
    const rows = lines.filter((l) => l.startsWith('user ')).map((l) => l.slice(5).split('|'));
    $('#users').innerHTML = '<tr><th>Handle</th><th>Status</th><th>Sec</th><th>Calls</th><th>F M D $ T</th></tr>' +
      rows.map((r) => `<tr class="pick" data-h="${esc(r[1])}"><td>${esc(r[1])}</td><td class="${r[2] === 'pending' ? 'warn' : ''}">${esc(r[2])}</td><td>${esc(r[3])}</td><td>${esc(r[4])}</td><td>${esc(r[5].split('').join(' '))}</td></tr>`).join('');
  }
  $('#users').addEventListener('click', async (e) => {
    const tr = e.target.closest('tr[data-h]'); if (tr) showUser(tr.dataset.h);
  });
  async function showUser(h) {
    const lines = await cmd('@USER ' + h);
    const d = $('#userdetail'); d.hidden = false;
    d.innerHTML = `<h2>[ ${esc(h)} ]</h2><pre>${esc(lines.join('\n'))}</pre>
      <div class="acts">
        <button data-u="@VALIDATE">Validate</button><button data-u="@DENY" class="danger">Deny</button>
        <button data-u="sec">Security...</button><button data-u="pass">Password...</button>
        <button data-u="@PERM files">Files on/off</button><button data-u="@PERM msgs">Msgs on/off</button>
        <button data-u="@PERM doors">Doors on/off</button><button data-u="@PERM freedl">Free DL on/off</button>
        <button data-u="@TWIT">Twit on/off</button></div>`;
    d.onclick = async (e) => {
      const b = e.target.closest('button[data-u]'); if (!b) return;
      const u = b.dataset.u;
      if (u === 'sec') { const v = prompt('Security level (1 to 255)'); if (v) await cmd(`@SEC ${h} ${v}`); }
      else if (u === 'pass') { const v = prompt(`New password for ${h} (4+ characters)`); if (v) await cmd(`@USERPASS ${h} ${v}`); }
      else if (u.startsWith('@PERM') || u === '@TWIT') {
        const on = confirm(`${u.replace('@', '')}: OK = on, Cancel = off`);
        const parts = u.split(' ');
        await cmd(parts.length > 1 ? `@PERM ${h} ${parts[1]} ${on ? 'on' : 'off'}` : `@TWIT ${h} ${on ? 'on' : 'off'}`);
      } else await cmd(`${u} ${h}`);
      loadUsers(); showUser(h);
    };
  }

  // ---- settings
  async function loadSettings() {
    const lines = await cmd('@SETTINGS');
    $('#settings').innerHTML = lines.filter((l) => l.includes('=')).map((l) => {
      const i = l.indexOf('='), k = l.slice(0, i), v = l.slice(i + 1);
      return `<label>${esc(k)} <input data-k="${esc(k)}" value="${esc(v)}" data-v="${esc(v)}"></label>`;
    }).join('') + '<button type="submit">Save changes</button> <span id="savemsg" class="ok"></span>';
  }
  $('#settings').addEventListener('submit', async (e) => {
    e.preventDefault();
    let n = 0;
    for (const i of $('#settings').querySelectorAll('input[data-k]'))
      if (i.value !== i.dataset.v) { await cmd(`@SET ${i.dataset.k}=${i.value}`); i.dataset.v = i.value; n++; }
    $('#savemsg').textContent = n ? `${n} saved` : 'nothing changed';
  });
  $('#pw').addEventListener('click', async (e) => {
    const b = e.target.closest('button[data-pw]'); if (!b) return; e.preventDefault();
    if (b.dataset.pw === 'sysop' && $('#sysoppw').value.length >= 4) { await cmd('@SYSOPPASS ' + $('#sysoppw').value); $('#sysoppw').value = ''; }
    if (b.dataset.pw === 'nup' && $('#nup').value) { await cmd('@NUP ' + $('#nup').value); $('#nup').value = ''; }
    if (b.dataset.pw === 'nupoff') await cmd('@NUP');
    b.textContent = 'Done'; setTimeout(() => (b.textContent = b.dataset.pw === 'nupoff' ? 'Clear NUP' : 'Set'), 1500);
  });

  // ---- lists
  async function loadLists() {
    for (const [id, c] of [['rumors', '@RUMORS'], ['liners', '@LINERS'], ['bbslist', '@BBSLIST'], ['votes', '@VOTES']])
      $('#list-' + id).textContent = (await cmd(c)).join('\n') || '(empty)';
  }
  $('#rumor').addEventListener('submit', async (e) => {
    e.preventDefault(); const t = $('#rumortext').value.trim(); if (!t) return;
    await cmd('@RUMOR ' + t); $('#rumortext').value = ''; loadLists();
  });

  // ---- system: per board status and web extras
  async function drawSystem() {
    $('#system').innerHTML = boards.map((b) => `<div class="win"><h2>[ ${esc(b.ip)} ]</h2><pre>${esc(Object.entries(b.stat || {}).map(([k, v]) => k + ' ' + v).join('\n'))}</pre>
      <div class="acts"><button data-s="@WEB" data-b="${esc(b.ip)}">Web / No-IP</button><button data-s="@CARDS" data-b="${esc(b.ip)}">Cards</button><button data-s="@LOG" data-b="${esc(b.ip)}">Event log</button><button data-s="@NOIPNOW" data-b="${esc(b.ip)}">Update No-IP</button></div><pre class="out"></pre></div>`).join('');
  }
  $('#system').addEventListener('click', async (e) => {
    const b = e.target.closest('button[data-s]'); if (!b) return;
    b.closest('.win').querySelector('.out').textContent = (await cmd(b.dataset.s, b.dataset.b)).join('\n');
  });

  // ---- tabs, login, refresh
  $('#tabs').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-tab]'); if (!b) return;
    document.querySelectorAll('#tabs button').forEach((x) => x.classList.toggle('on', x === b));
    document.querySelectorAll('.tab').forEach((t) => (t.hidden = t.id !== 'tab-' + b.dataset.tab));
    ({ nodes: loadNodes, users: loadUsers, settings: loadSettings, lists: loadLists, system: drawSystem })[b.dataset.tab]();
  });
  $('#loginform').addEventListener('submit', (e) => {
    e.preventDefault();
    send({ login: { handle: $('#handle').value, password: $('#password').value } });
    $('#password').value = '';
  });
  function refreshAll() { loadNodes(); loadUsers(); }
  setInterval(() => { if (authed && !document.hidden) loadNodes(); }, 5000);
  setInterval(() => { $('#clock').textContent = new Date().toLocaleTimeString(); }, 1000);
  connect();
})();
