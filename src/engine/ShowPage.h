#pragma once
// The show desk the remote serves at /show (RemoteServer.h), for a tablet or a browser: the same desk as the Show Control tab
// (ShowControlPanel.h), and a page for the layers' values. A single file, no library, no external request. Its style and its
// script are hashed into the Content-Security-Policy at start-up, so that nothing but this exact code can run in it (and no
// style attribute: sizes and colors go through the CSSOM). The script is in parts: a compiler's limit on one string literal.
#include <QByteArray>

namespace show_page {
inline constexpr char kStyle[] = R"CSS(
:root { color-scheme: dark; --bg:#202024; --panel:#26262b; --line:#3a3a42; --text:#ececf0; --dim:#a4a4ae; --go:#4cd964; --stop:#e84c3d; --pre:#e0b43a; --post:#569ce6; --wait:#d5bd82; --follow:#79cbb6; --auto:#b7a1d9; }
* { box-sizing:border-box; -webkit-tap-highlight-color:transparent; }
html, body { margin:0; background:var(--bg); color:var(--text); font:15px/1.35 Roboto, -apple-system, system-ui, "Segoe UI", sans-serif; font-variant-numeric:tabular-nums; }
header { display:flex; align-items:center; gap:14px; padding:12px 16px; }
#brand { flex:1; font-size:19px; font-weight:600; letter-spacing:2px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
#status { font-size:11px; letter-spacing:1px; color:var(--dim); text-align:right; }
#status.ok::before { content:"● "; color:var(--go); }
#status.bad::before { content:"● "; color:var(--stop); }
header a { font-size:12px; font-weight:600; letter-spacing:1px; color:var(--follow); text-decoration:none; white-space:nowrap; border:1px solid var(--follow); border-radius:5px; padding:7px 10px; }
nav { display:flex; overflow-x:auto; background:var(--panel); }
nav button { flex:none; padding:13px 18px; background:none; border:0; color:var(--dim); font:inherit; font-size:13px; letter-spacing:1px; }
nav button.on { color:var(--text); box-shadow:inset 0 -2px var(--go); }
section { padding:12px 16px 40px; }
.btn { min-height:52px; width:100%; padding:6px 10px; border:1px solid color-mix(in srgb, var(--c, #6d6d78) 55%, transparent); border-radius:5px; background:color-mix(in srgb, var(--c, #6d6d78) 12%, transparent); color:var(--text); font:inherit; font-size:13px; font-weight:600; touch-action:manipulation; user-select:none; -webkit-user-select:none; }
.btn:active:not(:disabled) { background:color-mix(in srgb, var(--c, #6d6d78) 45%, transparent); }
.btn:disabled { opacity:.35; }
.go { --c:var(--go); } .stop { --c:var(--stop); } .teal { --c:var(--follow); } .amber { --c:var(--pre); } .blue { --c:#56c4ff; } .grey { --c:#94949f; }
select, input[type=text], input[type=number], textarea { width:100%; min-height:48px; padding:8px 10px; background:var(--panel); color:var(--text); border:1px solid var(--line); border-radius:5px; font:inherit; }
input[type=color] { width:100%; height:48px; padding:3px; background:var(--panel); border:1px solid var(--line); border-radius:5px; }
input[type=range] { width:100%; margin:0; height:30px; accent-color:var(--follow); }
.toolbar { display:grid; grid-template-columns:3fr 2fr; gap:10px; align-items:center; margin:10px 0; }
#running { color:#b2b2bc; text-align:center; font-size:13px; }
#dash { display:grid; gap:12px; }
#cards, #console { display:grid; gap:10px; align-content:start; }
@media (min-width:1000px) { #dash { grid-template-columns:minmax(0,1.6fr) minmax(320px,1fr); } }
.cue { position:relative; overflow:hidden; padding:9px 12px; border-left:3px solid var(--edge); border-radius:5px; background:var(--tint); min-height:96px; }
.cue .fill { position:absolute; inset:0 auto 0 0; width:0; opacity:.18; pointer-events:none; }
.cue > div:not(.fill) { position:relative; }
.cue .head { font-size:11px; font-weight:700; letter-spacing:2px; color:var(--edge); }
.cue .cont { font-size:12px; font-weight:700; letter-spacing:.5px; margin:3px 0; }
.cue .label { font-size:19px; font-weight:600; }
.cue .desc { font-size:15px; color:#d2d2d9; min-height:1em; }
.cue .phase { font-size:12px; font-weight:600; letter-spacing:1px; text-transform:uppercase; margin-top:3px; }
.cue .times { font-size:12px; font-family:monospace; color:var(--dim); }
#card-previous { --edge:#676771; --tint:#242428; } #card-previous .label { color:var(--dim); } #card-previous .desc { color:#9797a0; }
#card-previous .phase, #card-previous .times { display:none; }
#card-current { --edge:var(--go); --tint:#262b28; } #card-current .label { font-size:28px; } #card-current .desc { font-size:20px; }
#card-next { --edge:var(--stop); --tint:#2c2728; }
#transport { display:grid; gap:8px; grid-template-columns:1fr 2fr; grid-template-areas:"back go" "stop stopall"; }
#back { grid-area:back; } #go { grid-area:go; font-size:28px; min-height:84px; } #stop { grid-area:stop; } #stopAll { grid-area:stopall; }
.box { padding:9px 12px; background:var(--panel); border-radius:5px; }
.box h3 { margin:0; font-size:11px; font-weight:400; letter-spacing:2px; color:var(--dim); }
.clock { font-size:46px; font-family:monospace; letter-spacing:1px; text-align:center; }
#remaining { color:var(--pre); }
.meter { height:20px; margin:8px 0; background:rgba(255,255,255,.06); border-radius:3px; overflow:hidden; }
.meter div { height:100%; width:0; opacity:.6; }
.legend { font-size:12px; line-height:1.6; white-space:pre-line; color:var(--dim); }
.note { font-size:11px; line-height:1.4; color:#9999a4; }
h2 { margin:14px 0 8px; font-size:11px; font-weight:600; letter-spacing:2px; color:var(--dim); }
#cues { display:grid; gap:6px; }
.row { display:grid; grid-template-columns:minmax(0,1fr) 150px; gap:6px; }
.row .btn { min-height:72px; text-align:left; padding-left:14px; line-height:1.5; font-weight:400; font-size:14px; }
.row .btn small { display:block; color:var(--dim); font-size:12px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.row.current .btn { --c:var(--go); } .row.next .btn { --c:var(--stop); }
.badge { display:flex; flex-direction:column; align-items:center; justify-content:center; text-align:center; padding:4px; border:1px solid var(--m); color:var(--m); border-radius:4px; font-size:12px; font-weight:600; line-height:1.7; }
.badge small { font-weight:400; }
.title { margin:4px 0; font-size:26px; font-weight:700; }
.help { color:var(--dim); margin:0 0 10px; }
.grid { display:grid; gap:10px; grid-template-columns:repeat(auto-fill,minmax(170px,1fr)); }
.grid .btn { min-height:76px; font-size:14px; }
.timeline { padding:9px; background:var(--panel); border-radius:5px; display:grid; gap:6px; }
.timeline b { font-size:17px; font-weight:600; }
.timeline code { font-size:12px; color:var(--dim); }
.timeline .meter { height:12px; margin:0; } .timeline .meter div { background:#56c4ff; }
.timeline .btns { display:grid; grid-template-columns:repeat(3,1fr); gap:6px; }
.timeline .btn { min-height:42px; font-size:12px; }
#timelineItems { display:grid; gap:10px; grid-template-columns:repeat(auto-fill,minmax(300px,1fr)); }
#layerControls { display:grid; gap:12px; grid-template-columns:repeat(auto-fill,minmax(280px,1fr)); align-items:start; margin-top:12px; }
.param { padding:9px 10px; background:#28282e; border-left:2px solid #638c80; border-radius:5px; display:grid; gap:5px; }
.param .name { font-size:15px; font-weight:600; overflow-wrap:anywhere; }
.param .name.reset::after { content:" ↺"; opacity:.45; }
.param .name.reset { cursor:pointer; user-select:none; -webkit-user-select:none; }
.param .hint { font-size:11px; color:var(--dim); }
.param .pair { display:grid; grid-auto-flow:column; grid-auto-columns:1fr; gap:6px; }
.param .pair label { font-size:11px; color:#b2b2bc; display:grid; gap:2px; text-align:center; }
.param .on { --c:var(--go); background:color-mix(in srgb, var(--go) 40%, transparent); }
#layerStatus { margin:8px 0; color:var(--dim); } #layerStatus.locked { color:var(--pre); }
#layerSelect { margin-bottom:8px; }
.empty { color:var(--dim); padding:18px 2px; }
#login { max-width:340px; margin:18vh auto 0; padding:0 20px; text-align:center; }
#login input { width:100%; padding:14px; font-size:22px; text-align:center; letter-spacing:.3em; background:var(--panel); color:var(--text); border:1px solid var(--line); border-radius:10px; }
#login p { color:var(--dim); }
#login button { width:100%; margin-top:12px; min-height:56px; border-radius:10px; border:0; background:var(--go); color:#14251a; font:inherit; font-size:18px; font-weight:600; }
[hidden] { display:none !important; }
)CSS";

inline constexpr char kBody[] = R"HTML(
<header><div id="brand">NYXMAP  /  SHOW CONTROL</div><div id="status">…</div><a id="toControl" href="/control">CONTROLS →</a></header>
<div id="login" hidden><h2>NyxMap show control</h2><p id="loginText">Enter the code set in NyxMap (Settings › OSC › Remote).</p><input id="pin" type="password" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" enterkeyhint="go"><button id="enter">Connect</button></div>
<div id="desk" hidden>
<nav id="tabs"><button data-tab="show" class="on">SHOW</button><button data-tab="snapshots">SNAPSHOTS</button><button data-tab="presets">PRESETS</button><button data-tab="timelines">TIMELINES</button><button data-tab="layers">LAYERS</button></nav>
<section id="show">
<button class="btn teal" id="toLayers">LAYER CONTROL →</button>
<div class="toolbar"><select id="sequence" aria-label="Sequence"></select><div id="running"></div></div>
<div id="dash"><div id="cards"></div><div id="console">
<div id="transport"><button class="btn grey" id="back">‹ GO BACK</button><button class="btn go" id="go">GO ▶</button><button class="btn stop" id="stop">STOP WAITS</button><button class="btn stop" id="stopAll">STOP ALL WAITS</button></div>
<div class="box"><h3>ELAPSED</h3><div class="clock" id="elapsed">0:00.0</div></div>
<div class="box"><h3>REMAINING</h3><div class="clock" id="remaining">0:00.0</div></div>
<div class="box"><h3>PHASE PROGRESS</h3><div class="meter"><div id="phaseMeter"></div></div><div class="legend">WAIT · GO: manual advance
AUTO: action end + post-wait</div></div>
<div class="note">STOP WAITS cancels pending cues. It does not stop media or timelines. Double-tap STOP ALL WAITS for every sequence.</div>
</div></div>
<h2>CUE STACK</h2><div id="cues"></div>
</section>
<section id="snapshots" hidden><div class="title">SNAPSHOTS</div><p class="help">Launch on demand. The list follows the open NyxMap project.</p><div class="grid" id="snapshotItems"></div></section>
<section id="presets" hidden><div class="title">PRESETS</div><p class="help">Launch on demand. The list follows the open NyxMap project.</p><div class="grid" id="presetItems"></div></section>
<section id="timelines" hidden><div class="title">TIMELINES</div><p class="help">Play, pause and stop. The list follows the open NyxMap project.</p><div id="timelineItems"></div></section>
<section id="layers" hidden><div class="title">LAYER REMOTE</div><p class="help">Choose a layer, group or output. Double-click / double-tap a name marked ↺ to reset to its default. Edits are live.</p>
<select id="layerSelect" aria-label="Layer"></select><select id="layerSection" aria-label="Section"></select><div id="layerStatus">Choose a layer to edit.</div><div id="layerControls"></div></section>
</div>
)HTML";

inline constexpr char kScript[] = R"JS(
(() => {
  const $ = s => document.querySelector(s);
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
  let pin = new URLSearchParams(location.search).get('pin') || sessionStorage.getItem('nyxmap-pin') || '', needPin = true;
  let ws = null, opened = false, failures = 0, catalog = null, state = null, selected = '0', tab = 'show', cueKey = '';
  const status = (text, cls) => { const s = $('#status'); s.textContent = text; s.className = cls || ''; };
  const send = obj => { if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj)); };
  const command = (c, more) => send(Object.assign({ t: 'do', c }, more || {}));
  const val = key => state && state.values ? state.values[key] : undefined;
  const clock = seconds => {
    if (seconds < 0 || !Number.isFinite(seconds)) return '∞';
    const tenths = Math.round(Math.max(0, seconds) * 10);
    return Math.floor(tenths / 600) + ':' + String(Math.floor(tenths / 10) % 60).padStart(2, '0') + '.' + (tenths % 10);
  };
  const phaseColor = p => p === 'pre-wait' ? '#e0b43a' : p === 'post-wait' ? '#569ce6' : '#4cd964';
  const modeColor = m => m === 'wait' ? '#d5bd82' : m === 'auto' ? '#b7a1d9' : '#a4a4ae';
  const continuation = (mode, postwait) => {
    const delay = clock(Number(postwait) || 0);
    if (mode === 'auto') return ['AUTO', 'End + ' + delay];
    if (mode === 'wait') return ['WAIT · GO', 'Manual advance'];
    return ['—', ''];
  };
  const doubleTap = (button, action) => {
    let last = 0;
    button.addEventListener('click', () => { const now = Date.now(); if (now - last < 450) { last = 0; action(); } else last = now; });
  };

  function login(message) {
    $('#desk').hidden = true; $('#login').hidden = false;
    if (message) $('#loginText').textContent = message;
    const input = $('#pin'); input.value = ''; input.focus();
    const go = () => { if (!input.value) return; pin = input.value.trim(); failures = 0; $('#login').hidden = true; connect(); };
    input.onkeydown = e => { if (e.key === 'Enter') go(); };
    $('#enter').onclick = go; // a phone's keypad may have no Enter key
  }
  function connect() {
    if (needPin && !pin) return login();
    status('CONNECTING');
    opened = false;
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws' + (needPin ? '?pin=' + encodeURIComponent(pin) : ''));
    ws.onopen = () => {
      opened = true; failures = 0; if (pin) sessionStorage.setItem('nyxmap-pin', pin);
      status('CONNECTED · ' + location.host, 'ok'); $('#desk').hidden = false;
      send({ t: 'desk' });
      if (layer.selected) layer.load();
    };
    ws.onmessage = e => {
      let m; try { m = JSON.parse(e.data); } catch (_) { return; }
      if (m.t === 'catalog') { catalog = m.catalog; renderCatalog(); }
      else if (m.t === 'state') { state = m.state; renderState(); }
      else if (m.t === 'layer') layer.tree(m);
      else if (m.t === 'params') layer.params(m.v || {});
      else if (m.t === 'changed') layer.load();
    };
    ws.onclose = () => {
      status('DISCONNECTED · reconnecting', 'bad'); enable();
      if (needPin && !opened && ++failures >= 2) { sessionStorage.removeItem('nyxmap-pin'); pin = ''; login('Wrong PIN, or NyxMap cannot be reached. Enter the PIN again.'); return; }
      setTimeout(connect, Math.min(4000, 400 + failures * 500));
    };
  }
  const online = () => !!(ws && ws.readyState === 1);

  // The pages
  function show(name) {
    tab = name;
    document.querySelectorAll('section').forEach(s => { s.hidden = s.id !== name; });
    document.querySelectorAll('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.tab === name));
    if (name !== 'layers') layer.unlisten(); else layer.relisten();
  }
  document.querySelectorAll('#tabs button').forEach(b => { b.onclick = () => show(b.dataset.tab); });
  $('#toLayers').onclick = () => show('layers');

  // The cue cards
  const cards = {};
  for (const role of ['previous', 'current', 'next']) {
    const c = el('div', 'cue'); c.id = 'card-' + role;
    const part = name => { const d = el('div', name); c.appendChild(d); return d; };
    cards[role] = { fill: part('fill'), head: part('head'), cont: part('cont'), label: part('label'), desc: part('desc'), phase: part('phase'), times: part('times') };
    cards[role].head.textContent = role.toUpperCase();
    $('#cards').appendChild(c);
  }
  function renderCard(role) {
    const c = cards[role], at = name => val('/sequence/cue/' + role + '/' + name);
    const mode = at('continue'), m = continuation(mode, at('postwait')), phase = at('phase') || '', action = Number(at('action'));
    c.fill.style.width = (Math.max(0, Math.min(1, Number(at('progress')) || 0)) * 100) + '%';
    c.fill.style.background = phaseColor(phase);
    c.cont.textContent = at('number') ? m[0] + (m[1] ? ' · ' + m[1] : '') : '';
    c.cont.style.color = modeColor(mode);
    c.label.textContent = at('label') || '—';
    c.desc.textContent = at('description') || '';
    c.phase.textContent = phase;
    c.phase.style.color = phase === 'pre-wait' ? '#e0b43a' : phase === 'post-wait' ? '#569ce6' : phase === 'action' ? '#4cd964' : '#9797a0';
    c.times.textContent = at('number') ? 'Pre ' + Number(at('prewait') || 0).toFixed(1) + ' s · Action ' + (action < 0 ? '∞' : (action || 0).toFixed(1) + ' s') + ' · Post ' + Number(at('postwait') || 0).toFixed(1) + ' s' : '';
  }
  function enable() {
    const on = online();
    $('#go').disabled = !on || !val('/sequence/go_enabled');
    $('#back').disabled = !on || !val('/sequence/back_enabled');
    $('#stop').disabled = !on; $('#stopAll').disabled = !on; $('#sequence').disabled = !on;
  }
  $('#go').onclick = () => command('go', { id: selected });
  $('#back').onclick = () => command('back', { id: selected });
  $('#stop').onclick = () => command('stop', { id: selected });
  doubleTap($('#stopAll'), () => command('stop_all'));
  $('#sequence').onchange = e => command('select', { id: e.target.value });

  function renderState() {
    if (!state || !state.values) return;
    const before = selected;
    selected = String(val('/sequence/id') || '0');
    if ($('#sequence').value !== selected) $('#sequence').value = selected;
    const background = (state.sequences || []).filter(s => s.active && s.id !== selected).length;
    $('#running').textContent = (val('/sequence/running') ? 'WAITS ACTIVE' : 'WAITING FOR GO') + ' · ' + background + ' background';
    ['previous', 'current', 'next'].forEach(renderCard);
    $('#elapsed').textContent = val('/sequence/cue/current/elapsed_text') || '0:00.0';
    $('#remaining').textContent = val('/sequence/cue/current/remaining_text') || '0:00.0';
    const meter = $('#phaseMeter');
    meter.style.width = (Math.max(0, Math.min(1, Number(val('/sequence/cue/current/phase_progress')) || 0)) * 100) + '%';
    meter.style.background = phaseColor(val('/sequence/cue/current/phase'));
    enable();
    if (before !== selected) renderCues();
    markCues();
    for (const t of state.timelines || []) {
      const card = timelineCards[t.id]; if (!card) continue;
      card.fill.style.width = (Math.max(0, Math.min(1, t.progress || 0)) * 100) + '%';
      card.clock.textContent = t.state.toUpperCase() + ' · ' + clock(t.position) + ' / ' + clock(t.duration) + ' · Pass ' + t.pass + ' · Left ' + clock(t.remaining);
    }
  }
)JS" R"JS(
  // What the project holds
  function renderCatalog() {
    if (!catalog) return;
    const seq = $('#sequence'); seq.textContent = '';
    for (const s of catalog.sequences) { const o = el('option', '', s.index + ' · ' + (s.name || 'Untitled')); o.value = s.id; seq.appendChild(o); }
    seq.value = selected;
    renderCues(); renderLibrary('snapshot', catalog.snapshots, '#snapshotItems', 'go'); renderLibrary('preset', catalog.presets, '#presetItems', 'amber'); renderTimelines();
    layer.setCatalog(catalog.layers || []);
    renderState();
  }
  function renderLibrary(kind, list, where, color) {
    const box = $(where); box.textContent = '';
    if (!list.length) box.appendChild(el('div', 'empty', 'No ' + kind + 's in this project.'));
    for (const entry of list) {
      const b = el('button', 'btn ' + color, entry.index + ' · ' + (entry.name || 'Untitled'));
      b.onclick = () => command(kind, { id: entry.id });
      box.appendChild(b);
    }
  }
  const timelineCards = {};
  function renderTimelines() {
    const box = $('#timelineItems'); box.textContent = '';
    Object.keys(timelineCards).forEach(k => delete timelineCards[k]);
    if (!catalog.timelines.length) box.appendChild(el('div', 'empty', 'No timelines in this project.'));
    for (const entry of catalog.timelines) {
      const card = el('div', 'timeline');
      card.appendChild(el('b', '', entry.index + ' · ' + (entry.name || 'Untitled')));
      const code = el('code', '', 'Stopped'); card.appendChild(code);
      const meter = el('div', 'meter'), fill = el('div'); meter.appendChild(fill); card.appendChild(meter);
      const row = el('div', 'btns');
      for (const action of ['play', 'pause', 'stop']) {
        const b = el('button', 'btn ' + (action === 'stop' ? 'stop' : 'blue'), action.toUpperCase());
        b.onclick = () => command('timeline', { id: entry.id, do: action });
        row.appendChild(b);
      }
      card.appendChild(row); box.appendChild(card);
      timelineCards[entry.id] = { fill, clock: code };
    }
  }
  function renderCues() {
    const box = $('#cues'); box.textContent = ''; cueKey = '';
    const sequence = catalog && catalog.sequences.find(s => s.id === selected);
    const cues = sequence ? sequence.cues : [];
    if (!cues.length) { box.appendChild(el('div', 'empty', 'Select a sequence containing cues.')); return; }
    for (const cue of cues) {
      const row = el('div', 'row'); row.dataset.number = cue.index;
      const b = el('button', 'btn', cue.index + ' · ' + (cue.description || cue.name || (cue.missing ? 'Missing target' : 'Empty cue')));
      b.appendChild(el('small', '', [cue.target_type, cue.name].filter(Boolean).join(' · ')));
      b.onclick = () => command('cue', { id: selected, cue: cue.id });
      const m = continuation(cue.continue, cue.postwait), badge = el('div', 'badge', m[0]);
      badge.style.setProperty('--m', modeColor(cue.continue));
      badge.appendChild(el('small', '', m[1]));
      row.appendChild(b); row.appendChild(badge); box.appendChild(row);
    }
  }
  function markCues() {
    const current = val('/sequence/cue/current/number'), next = val('/sequence/cue/next/number'), key = current + '/' + next;
    if (key === cueKey) return;
    cueKey = key;
    document.querySelectorAll('#cues .row').forEach(r => {
      r.classList.toggle('current', Number(r.dataset.number) === current);
      r.classList.toggle('next', Number(r.dataset.number) === next);
    });
  }

  // The layers: what OSCQuery says of a layer, as cards by section. Only the values shown are followed.
  const layer = (() => {
    const self = { selected: '' };
    let entries = [], nodes = [], section = '', locked = false, listening = new Set(), updaters = new Map();
    const current = () => entries.find(e => e.id === self.selected);
    const openWhenLocked = path => ['enable', 'solo', 'testcard', 'locked', 'play', 'restart', 'time'].includes(path);
    const tuple = node => {
      const c = node.CONTENTS || {};
      if (/^ffff?$/.test(node.TYPE) && c.r && c.g && c.b) return { type: 'rgb', keys: node.TYPE.length === 4 ? ['r', 'g', 'b', 'a'] : ['r', 'g', 'b'] };
      if (node.TYPE === 'ff' && c.x && c.y) return { type: 'xy', keys: ['x', 'y'] };
      return null;
    };
    const supported = node => node.TYPE && (['f', 'i', 's', 'T', 'F', 'N'].includes(node.TYPE) || tuple(node));
    const group = node => {
      const path = node.relative;
      if (path.includes('/anim/')) return ['animation', 'Parameter animation'];
      if (path.startsWith('fx/')) { const name = path.split('/')[1]; return name === 'enable' ? ['effects', 'Effects'] : ['fx/' + name, 'FX · ' + name.replaceAll('_', ' ')]; }
      if (/^(position|scale|pivot|corner|roi)(\/|$)/.test(path) || ['rotation', 'width', 'height'].includes(path)) return ['transform', 'Transform'];
      if (/^(color|temp|tint|add|remove|mask)(\/|$)/.test(path) || (node.DESCRIPTION || '').startsWith('Color ›')) return ['color', 'Color'];
      if (/^(param|source|text|clock|tc|tox|web)(\/|$)/.test(path) || ['file', 'source_layer', 'source_tap'].includes(path)) return ['source', 'Source'];
      if (/^(viewport|output)(\/|$)/.test(path)) return ['routing', 'Output & routing'];
      if (['play', 'restart', 'time', 'duration', 'speed', 'in', 'out', 'play_mode', 'volume', 'mute'].includes(path)) return ['playback', 'Playback'];
      return ['general', 'General'];
    };
    const setStatus = (text, warn) => { const s = $('#layerStatus'); s.textContent = text; s.classList.toggle('locked', !!warn); };
    const empty = text => { const box = $('#layerControls'); box.textContent = ''; box.appendChild(el('div', 'empty', text)); };
    self.unlisten = () => { for (const path of listening) send({ t: 'ignore', path }); listening = new Set(); };
    self.relisten = () => { if (self.selected) render(); };
    self.setCatalog = list => {
      entries = list;
      const byId = new Map(entries.map(e => [e.id, e])), box = $('#layerSelect'), keep = self.selected;
      box.textContent = '';
      const none = el('option', '', 'Choose a layer'); none.value = ''; box.appendChild(none);
      for (const entry of entries) {
        const names = [entry.name || 'Untitled'], seen = new Set([entry.id]);
        let parent = byId.get(entry.parent);
        while (parent && !seen.has(parent.id)) { seen.add(parent.id); names.unshift(parent.name || 'Untitled'); parent = byId.get(parent.parent); }
        const o = el('option', '', entry.index + ' · ' + names.join(' / ') + (entry.viewport ? ' · OUTPUT' : entry.group ? ' · GROUP' : ''));
        o.value = entry.id; box.appendChild(o);
      }
      if (keep && !current()) { self.selected = ''; nodes = []; self.unlisten(); $('#layerSection').textContent = ''; empty('This layer is gone: choose another.'); setStatus('Choose a layer to edit.'); } // never another one in its place
      box.value = self.selected;
    };
    self.load = () => { self.unlisten(); if (self.selected) send({ t: 'layer', id: self.selected }); };
    self.tree = m => {
      if (m.id !== self.selected || !m.tree || !m.tree.FULL_PATH) return;
      const root = m.tree.FULL_PATH;
      nodes = [];
      const visit = (node, skip) => {
        const path = node.FULL_PATH;
        if (typeof path !== 'string' || !path.startsWith(root + '/')) return;
        const relative = path.slice(root.length + 1);
        const entry = current();
        if (relative !== 'id' && (relative !== 'testcard' || (entry && entry.viewport)) && supported(node) && !skip.has(path)) nodes.push(Object.assign({}, node, { relative }));
        const pair = tuple(node), next = new Set(skip);
        if (pair) for (const key of pair.keys) next.add(path + '/' + key);
        for (const [key, child] of Object.entries(node.CONTENTS || {})) {
          if (relative === 'layer' || key === 'layer') continue; // groups are selected on their own
          if (child.FULL_PATH === node.NYXMAP_RESET) continue; // reset is on the name, not another card
          visit(child, next);
        }
      };
      for (const [key, node] of Object.entries(m.tree.CONTENTS || {})) if (key !== 'layer') visit(node, new Set());
      locked = !!(m.tree.CONTENTS && m.tree.CONTENTS.locked && m.tree.CONTENTS.locked.VALUE && m.tree.CONTENTS.locked.VALUE[0]);
      const sections = new Map();
      for (const node of nodes) { const g = group(node); sections.set(g[0], g[1]); }
      if (!sections.has(section)) section = sections.has('general') ? 'general' : (sections.keys().next().value || '');
      const box = $('#layerSection'); box.textContent = '';
      for (const [key, label] of sections) { const o = el('option', '', label); o.value = key; box.appendChild(o); }
      box.value = section;
      render();
    };
)JS" R"JS(
    const write = (node, args) => send({ t: 'write', path: node.FULL_PATH, args });
    const writable = node => !!(node.ACCESS & 2);
    const open = node => writable(node) && (!locked || openWhenLocked(node.relative));
    const bound = [];
    const follow = (node, fn) => {
      bound.push(() => { fn.disabled = !open(node); });
      if (!(node.ACCESS & 1)) return;
      updaters.set(node.FULL_PATH, fn);
      if (Array.isArray(node.VALUE)) fn(node.VALUE);
    };
    function editor(node) {
      const box = el('div');
      const range = node.RANGE && node.RANGE[0];
      if (!writable(node)) { const t = el('code'); box.appendChild(t); follow(node, v => { t.textContent = v.join(' '); }); return box; }
      if (node.TYPE === 'N') { const b = el('button', 'btn', 'TRIGGER'); b.onclick = () => write(node, []); bound.push(() => { b.disabled = !open(node); }); box.appendChild(b); return box; }
      if (range && Array.isArray(range.VALS)) {
        const s = el('select');
        for (const v of range.VALS) { const o = el('option', '', String(v)); o.value = String(v); s.appendChild(o); }
        s.onchange = () => write(node, [node.TYPE === 's' ? s.value : Number(s.value)]);
        bound.push(() => { s.disabled = !open(node); });
        follow(node, v => { if (document.activeElement !== s) s.value = String(v[0]); });
        box.appendChild(s); return box;
      }
      if (node.TYPE === 'T' || node.TYPE === 'F') {
        const b = el('button', 'btn', 'OFF');
        let on = false;
        b.onclick = () => write(node, [!on]);
        bound.push(() => { b.disabled = !open(node); });
        follow(node, v => { on = !!v[0]; b.textContent = on ? 'ON' : 'OFF'; b.classList.toggle('on', on); });
        box.appendChild(b); return box;
      }
      if (node.TYPE === 's') {
        const t = el('input'); t.type = 'text'; t.maxLength = 3900;
        t.onchange = () => write(node, [t.value]);
        bound.push(() => { t.disabled = !open(node); });
        follow(node, v => { if (document.activeElement !== t) t.value = v[0] === undefined ? '' : v[0]; });
        box.appendChild(t); return box;
      }
      const n = el('input'); n.type = 'number'; n.step = node.TYPE === 'i' ? '1' : 'any';
      n.onchange = () => { if (n.value !== '') write(node, [Number(n.value)]); };
      box.appendChild(n);
      const hasRange = range && Number.isFinite(range.MIN) && Number.isFinite(range.MAX) && range.MAX > range.MIN;
      let r = null, sent = 0;
      if (hasRange) {
        r = el('input'); r.type = 'range'; r.min = range.MIN; r.max = range.MAX; r.step = node.TYPE === 'i' ? 1 : (range.MAX - range.MIN) / 1000;
        r.oninput = () => { n.value = r.value; const now = performance.now(); if (now - sent > 33) { sent = now; write(node, [Number(r.value)]); } };
        r.onchange = () => write(node, [Number(r.value)]);
        box.appendChild(r);
      }
      const format = v => node.TYPE === 'i' ? String(Math.round(v)) : String(Math.round(v * 1000) / 1000);
      bound.push(() => { n.disabled = !open(node); if (r) r.disabled = !open(node); });
      follow(node, v => { if (document.activeElement !== n) n.value = format(v[0]); if (r && document.activeElement !== r) r.value = v[0]; });
      return box;
    }
    function hint(node, pair) {
      if (!writable(node)) return 'READ ONLY';
      if (node.relative.includes('/anim/')) return node.relative.split('/anim/')[0].replaceAll('/param/', ' › ').replaceAll('/', ' › ').replaceAll('_', ' ');
      if (pair) return pair.type === 'rgb' ? 'RGB' + (pair.keys.length === 4 ? 'A' : '') + ' · 0–1' : 'XY · native units';
      const r = node.RANGE && node.RANGE[0];
      if (r && r.VALS) return 'Choose a value';
      if (node.TYPE === 's') return 'Enter to apply';
      if (node.TYPE === 'N') return 'Execute action';
      if (node.TYPE === 'T' || node.TYPE === 'F') return 'On / Off';
      return r && Number.isFinite(r.MIN) && Number.isFinite(r.MAX) ? r.MIN + ' – ' + r.MAX : 'Enter to apply';
    }
    const name = node => {
      const d = el('div', 'name', node.DESCRIPTION || node.relative);
      if (writable(node) && node.TYPE !== 'N' && node.NYXMAP_RESET === node.FULL_PATH + '/reset') {
        d.classList.add('reset');
        const reset = () => { if (!locked) send({ t: 'write', path: node.NYXMAP_RESET, args: [] }); };
        d.ondblclick = reset;
        let last = 0;
        d.addEventListener('touchend', () => { const now = Date.now(); if (now - last < 450) { last = 0; reset(); } else last = now; });
      }
      return d;
    };
    function render() {
      if (tab !== 'layers' || !current()) return;
      updaters = new Map(); bound.length = 0;
      const box = $('#layerControls'), visible = nodes.filter(n => group(n)[0] === section);
      box.textContent = '';
      if (!visible.length) box.appendChild(el('div', 'empty', 'No controls in this section.'));
      for (const node of visible) {
        const card = el('div', 'param'), pair = tuple(node);
        card.appendChild(name(node));
        card.appendChild(el('div', 'hint', hint(node, pair)));
        if (pair && writable(node)) {
          if (pair.type === 'rgb') {
            const c = el('input'); c.type = 'color';
            const hex = v => '#' + v.slice(0, 3).map(x => Math.round(Math.max(0, Math.min(1, x)) * 255).toString(16).padStart(2, '0')).join('');
            let held = [];
            c.oninput = () => {
              const rgb = [1, 3, 5].map(i => parseInt(c.value.substr(i, 2), 16) / 255);
              write(node, pair.keys.length === 4 ? rgb.concat([held[3] === undefined ? 1 : held[3]]) : rgb);
            };
            bound.push(() => { c.disabled = !open(node); });
            follow(node, v => { held = v; if (document.activeElement !== c) c.value = hex(v); });
            card.appendChild(c);
          }
          const row = el('div', 'pair');
          for (const key of pair.keys) {
            const child = Object.assign({}, node.CONTENTS[key], { relative: node.relative + '/' + key });
            const l = el('label', '', key.toUpperCase());
            l.appendChild(editor(child));
            row.appendChild(l);
          }
          card.appendChild(row);
        } else card.appendChild(editor(node));
        box.appendChild(card);
      }
      refreshLock();
      const paths = new Set(updaters.keys()), lockedPath = '/show/layer/by-id/' + self.selected + '/locked';
      paths.add(lockedPath);
      for (const path of listening) if (!paths.has(path)) send({ t: 'ignore', path });
      for (const path of paths) if (!listening.has(path)) send({ t: 'listen', path });
      listening = paths;
    }
    const refreshLock = () => {
      bound.forEach(fn => fn());
      const entry = current();
      if (entry) setStatus((entry.name || 'Untitled') + (locked ? ' · LOCKED' : ' · LIVE EDIT'), locked);
    };
    self.params = values => {
      for (const [path, v] of Object.entries(values)) {
        if (path === '/show/layer/by-id/' + self.selected + '/locked') { locked = !!v[0]; refreshLock(); }
        const fn = updaters.get(path); if (fn) fn(v);
        for (const node of nodes) { // the section left and come back to starts from what is true now
          if (node.FULL_PATH === path) node.VALUE = v;
          for (const child of Object.values(node.CONTENTS || {})) if (child.FULL_PATH === path) child.VALUE = v;
        }
      }
    };
    $('#layerSelect').onchange = e => {
      self.selected = e.target.value; section = ''; nodes = [];
      $('#layerSection').textContent = '';
      if (!self.selected) { self.unlisten(); empty('Choose a layer above.'); setStatus('Choose a layer to edit.'); return; }
      setStatus('Loading layer controls…'); self.load();
    };
    $('#layerSection').onchange = e => { section = e.target.value; render(); };
    return self;
  })();

  // Whether it asks a code: the server says (it says nothing else before the code)
  fetch('/info').then(r => r.json()).then(i => { needPin = !!i.pin; }).catch(() => {}).then(connect);
})();
)JS";
} // namespace show_page
