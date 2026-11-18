#pragma once
// The page the remote serves at /control (RemoteServer.h): a single file, no library, no external request. Its style and its script
// are hashed into the Content-Security-Policy at start-up, so that nothing but this exact code can run in it.
//
// The controls are laid out on a free 12-column grid, a page for each top-level Group; in Edit mode a tile is dragged, resized and
// set (its widget, range, curve, accent). Every widget is drawn in SVG from a few tokens (--a, the accent). It runs on old iPads
// too (iOS 12: no Pointer Events, no ResizeObserver): `press` falls back on touch and mouse events.
//
// A script literal stays under the 16 KB a compiler takes: the script is several pieces joined (script()).
#include <QByteArray>

namespace remote_page {
inline constexpr char kStyleA[] = R"CSS(
:root { color-scheme: dark; --bg:#121215; --panel:#1c1c21; --panel2:#25252b; --line:#31313a; --text:#ececf0; --dim:#8a8a94; --acc:#29c7e0; }
* { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
[hidden] { display:none !important; }
html, body { margin:0; background:var(--bg); color:var(--text); font:15px/1.3 -apple-system, system-ui, "Segoe UI", sans-serif; -webkit-user-select:none; user-select:none; }
header { position:-webkit-sticky; position:sticky; top:0; z-index:5; display:flex; align-items:center; padding:10px 14px; background:var(--bg); border-bottom:1px solid var(--line); }
header > * { margin-right:10px; }
header h1 { flex:1; margin:0 10px 0 0; font-size:16px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
#state { font-size:12px; color:var(--dim); }
#state.ok::before { content:"● "; color:#46c46a; }
#state.bad::before { content:"● "; color:#e5564a; }
.hb { font:inherit; font-size:13px; color:var(--text); text-decoration:none; white-space:nowrap; border:1px solid var(--line); background:var(--panel); border-radius:8px; padding:7px 12px; }
.hb.on { background:var(--acc); border-color:var(--acc); color:#06222a; font-weight:600; }
#tabs { display:flex; overflow-x:auto; padding:8px 14px 0; border-bottom:1px solid var(--line); -webkit-overflow-scrolling:touch; }
#tabs button { font:inherit; font-size:14px; color:var(--dim); background:none; border:0; border-bottom:3px solid transparent; padding:8px 14px; white-space:nowrap; }
#tabs button.on { color:var(--text); border-bottom-color:var(--acc); }
#grid { position:relative; margin:10px; }
.tile { position:absolute; transition:left .12s, top .12s, width .12s, height .12s; }
.tile.moving { transition:none; z-index:4; }
.face { position:absolute; left:4px; top:4px; right:4px; bottom:4px; background:var(--panel); border:1px solid var(--line); border-radius:14px; padding:9px 12px 10px; display:flex; flex-direction:column; overflow:hidden; --a:var(--acc); }
.tile.sec .face { background:transparent; border:0; border-bottom:1px solid var(--line); border-radius:0; padding:12px 2px 0; }
.sec .hd { font-size:13px; }
.hd { display:flex; justify-content:space-between; align-items:baseline; font-size:11px; letter-spacing:.06em; text-transform:uppercase; color:var(--dim); white-space:nowrap; }
.hd span { overflow:hidden; text-overflow:ellipsis; }
.hd b { color:var(--text); font-weight:600; font-variant-numeric:tabular-nums; letter-spacing:0; margin-left:8px; }
.bd { flex:1; min-height:0; position:relative; display:flex; align-items:center; justify-content:center; }
.fader { width:100%; padding:10px 6px; touch-action:none; }
.fader .trk { position:relative; height:8px; background:var(--panel2); border-radius:6px; }
.fader .fill { position:absolute; left:0; top:0; bottom:0; border-radius:6px; background:var(--a); box-shadow:0 0 10px var(--a); }
.fader .cap { position:absolute; top:50%; width:22px; height:22px; margin:-11px 0 0 -11px; border-radius:50%; background:#f2f2f5; border:3px solid var(--a); box-shadow:0 0 8px var(--a); }
.fader.v { width:auto; height:100%; padding:6px 10px; }
.fader.v .trk { width:8px; height:100%; }
.fader.v .fill { left:0; right:0; top:auto; bottom:0; }
.fader.v .cap { left:50%; top:auto; margin:0 0 -11px -11px; }
.knob { height:100%; max-height:100%; width:100%; display:flex; justify-content:center; touch-action:none; }
.knob svg { height:100%; max-width:100%; }
.kbg { fill:none; stroke:var(--panel2); stroke-width:8; stroke-linecap:round; }
.kval { fill:none; stroke:var(--a); stroke-width:8; stroke-linecap:round; filter:drop-shadow(0 0 3px var(--a)); }
.kdot { stroke:#f2f2f5; stroke-width:3.5; stroke-linecap:round; }
.ktx { fill:var(--text); font:600 15px -apple-system, system-ui, sans-serif; text-anchor:middle; font-variant-numeric:tabular-nums; }
.adsr { position:relative; width:100%; height:100%; align-self:stretch; touch-action:none; }
.adsr svg { display:block; }
.aarea { fill:var(--a); opacity:.14; }
.aline { fill:none; stroke:var(--a); stroke-width:3; stroke-linejoin:round; filter:drop-shadow(0 0 4px var(--a)); }
.ahdl { fill:#f2f2f5; stroke:var(--a); stroke-width:3; }
.alab { position:absolute; left:0; right:0; bottom:0; text-align:center; font-size:12px; color:var(--dim); font-variant-numeric:tabular-nums; white-space:pre; }
.num { display:flex; align-items:center; width:100%; }
.num button { width:42px; height:42px; border-radius:50%; border:1px solid var(--line); background:var(--panel2); color:var(--text); font-size:22px; line-height:1; }
.num output { flex:1; text-align:center; font-size:26px; font-weight:600; font-variant-numeric:tabular-nums; }
.sw, .tapb, .holdb { width:100%; height:100%; min-height:44px; max-height:84px; border-radius:12px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font:inherit; font-size:15px; touch-action:none; position:relative; }
.sw::after { content:""; position:absolute; left:14px; top:50%; width:12px; height:12px; margin-top:-6px; border-radius:50%; background:#3a3a43; }
.sw.on { border-color:var(--a); } .sw.on::after { background:var(--a); box-shadow:0 0 10px var(--a); }
.holdb.on, .tapb.on { background:var(--a); color:#06222a; border-color:var(--a); box-shadow:0 0 14px var(--a); }
.choice { display:flex; width:100%; height:100%; max-height:84px; }
.choice button { flex:1; min-width:0; margin-right:6px; border-radius:10px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font:inherit; font-size:14px; padding:0 4px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.choice button:last-child { margin-right:0; }
.choice button.on { border-color:var(--a); color:var(--a); box-shadow:0 0 8px var(--a) inset; }
select.menu { width:100%; height:44px; border-radius:10px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font:inherit; font-size:16px; padding:0 10px; }
input[type=color] { width:100%; height:100%; min-height:40px; max-height:80px; border:1px solid var(--line); border-radius:12px; background:var(--panel2); padding:4px; }
.pad { position:relative; width:100%; height:100%; border-radius:10px; background:var(--panel2); touch-action:none; overflow:hidden; }
.pad::before, .pad::after { content:""; position:absolute; background:#34343d; }
.pad::before { left:50%; top:0; bottom:0; width:1px; } .pad::after { top:50%; left:0; right:0; height:1px; }
.pad i { position:absolute; width:26px; height:26px; margin:-13px 0 0 -13px; border-radius:50%; background:#f2f2f5; border:4px solid var(--a); box-shadow:0 0 12px var(--a); pointer-events:none; }
)CSS";

inline constexpr char kStyleB[] = R"CSS(
.edit .tile .face { border-style:dashed; }
.edit .hd { padding-right:34px; }
.shield { position:absolute; left:0; top:0; right:0; bottom:0; z-index:3; cursor:grab; border-radius:14px; touch-action:none; }
.tile.sel .face { border-color:var(--acc); border-style:solid; box-shadow:0 0 0 2px rgba(41,199,224,.35); }
.rz { position:absolute; right:4px; bottom:4px; width:34px; height:34px; z-index:4; touch-action:none; cursor:nwse-resize; }
.rz::after { content:""; position:absolute; right:8px; bottom:8px; width:12px; height:12px; border-right:3px solid var(--acc); border-bottom:3px solid var(--acc); border-radius:0 0 4px 0; }
.gear { position:absolute; right:10px; top:8px; z-index:4; width:30px; height:30px; border-radius:15px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font-size:15px; line-height:1; }
#grid.compact .tile { position:relative; transition:none; }
#empty { color:var(--dim); text-align:center; padding:50px 16px; }
#sheet { position:fixed; right:0; top:0; bottom:0; width:min(360px,100%); z-index:20; background:var(--panel); border-left:1px solid var(--line); padding:14px 16px 24px; overflow-y:auto; transform:translateX(105%); transition:transform .18s; -webkit-user-select:auto; user-select:auto; }
#sheet.open { transform:none; box-shadow:-12px 0 30px rgba(0,0,0,.5); }
#sheet h2 { margin:0 0 12px; font-size:16px; display:flex; justify-content:space-between; align-items:center; }
#sheet label { display:block; margin:12px 0 4px; font-size:11px; letter-spacing:.06em; text-transform:uppercase; color:var(--dim); }
#sheet input[type=text], #sheet input[type=number], #sheet select { width:100%; height:42px; border-radius:9px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font:inherit; font-size:16px; padding:0 10px; }
#sheet .row { display:flex; } #sheet .row > div { flex:1; margin-right:8px; } #sheet .row > div:last-child { margin-right:0; }
#sheet input[type=range] { width:100%; height:34px; }
#sheet .sw8 { display:flex; flex-wrap:wrap; }
#sheet .sw8 button { width:34px; height:34px; margin:0 8px 8px 0; border-radius:17px; border:2px solid transparent; }
#sheet .sw8 button.on { border-color:#fff; }
#sheet .del { margin-top:22px; width:100%; height:44px; border-radius:10px; border:1px solid #6b2b27; background:#2a1716; color:#f08a82; font:inherit; font-size:15px; }
#sheet .hint { color:var(--dim); font-size:12px; margin-top:4px; }
#login { max-width:340px; margin:18vh auto 0; padding:0 20px; text-align:center; }
#login input { width:100%; padding:14px; font-size:22px; text-align:center; letter-spacing:.3em; background:var(--panel); color:var(--text); border:1px solid var(--line); border-radius:10px; }
#login p { color:var(--dim); }
#login button { width:100%; margin-top:12px; min-height:56px; border-radius:10px; border:0; background:var(--acc); color:#06222a; font:inherit; font-size:18px; font-weight:600; }
)CSS";

inline constexpr char kBody[] = R"HTML(
<header><h1 id="title">NyxMap</h1><span id="state">…</span><button class="hb" id="edit" hidden>Edit</button><a class="hb" id="toShow" href="/show">Show control →</a></header>
<nav id="tabs" hidden></nav>
<div id="login" hidden><h2>NyxMap remote</h2><p id="loginText">Enter the code set in NyxMap (Settings › OSC › Remote).</p><input id="pin" type="password" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" inputmode="numeric" placeholder="PIN"><button id="go">Connect</button></div>
<main id="grid" hidden></main>
<aside id="sheet"></aside>
)HTML";

// --- Script, in pieces -------------------------------------------------------------------------------------------------------
// A: state, the socket, helpers, `press` (pointer, touch or mouse), the value <-> position maps
inline constexpr char kScriptA[] = R"JS(
(() => {
  'use strict';
  const $ = s => document.querySelector(s);
  const NS = 'http://www.w3.org/2000/svg';
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
  const sv = (tag, attrs, parent) => { const e = document.createElementNS(NS, tag); for (const k in attrs || {}) e.setAttribute(k, attrs[k]); if (parent) parent.appendChild(e); return e; };
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  let pin = new URLSearchParams(location.search).get('pin') || sessionStorage.getItem('nyxmap-pin') || '', needPin = true;
  let ws = null, opened = false, failures = 0, controls = [], values = {}, byId = {}, pageId = -1, editing = false, busy = 0, again = false, sheetId = 0;
  const active = new Set(), sendAt = {}, tiles = {};
  const state = (text, cls) => { const s = $('#state'); s.textContent = text; s.className = cls || ''; };
  const send = obj => { if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj)); };
  const throttled = (key, fn) => { const now = performance.now(); if (now - (sendAt[key] || 0) > 33) { sendAt[key] = now; fn(); return true; } return false; };
  const fmt = (c, v) => { const st = c.step || 0.01, d = st >= 1 ? 0 : Math.min(4, Math.ceil(-Math.log10(st))); return Number(v).toFixed(d) + (c.unit || ''); };
  const hex = rgb => '#' + rgb.map(x => ('0' + Math.round(clamp(x, 0, 1) * 255).toString(16)).slice(-2)).join('');
  const unhex = h => [1, 3, 5].map(i => parseInt(h.substr(i, 2), 16) / 255);
  // The position of a value on a slider (0..1) and back, through the curve of its gesture
  const toPos = (c, v) => { const span = c.max - c.min; return span ? Math.pow(clamp((v - c.min) / span, 0, 1), 1 / (c.curve || 1)) : 0; };
  const fromPos = (c, p) => { let v = c.min + (c.max - c.min) * Math.pow(clamp(p, 0, 1), c.curve || 1); const st = c.step || 0.01; v = Math.round((v - c.min) / st) * st + c.min; return clamp(Number(v.toFixed(6)), Math.min(c.min, c.max), Math.max(c.min, c.max)); };
  // A gesture on an element, whatever the browser has: pointer events, else touch and mouse. h.down(p) may refuse (false)
  function press(elm, h) {
    let on = false, id = null;
    if (window.PointerEvent) {
      elm.addEventListener('pointerdown', e => { if (e.button > 0 || on) return; if (h.down({ x: e.clientX, y: e.clientY, e }) === false) return; on = true; id = e.pointerId; try { elm.setPointerCapture(id); } catch (_) {} e.preventDefault(); });
      elm.addEventListener('pointermove', e => { if (on && e.pointerId === id) h.move({ x: e.clientX, y: e.clientY, e }); });
      const end = e => { if (on && e.pointerId === id) { on = false; h.up({ x: e.clientX, y: e.clientY, e }); } };
      elm.addEventListener('pointerup', end); elm.addEventListener('pointercancel', end);
    } else {
      const pt = e => { const t = e.changedTouches ? e.changedTouches[0] : e; return { x: t.clientX, y: t.clientY, e }; };
      elm.addEventListener('touchstart', e => { if (on) return; if (h.down(pt(e)) === false) return; on = true; e.preventDefault(); }, { passive: false });
      elm.addEventListener('touchmove', e => { if (on) { h.move(pt(e)); e.preventDefault(); } }, { passive: false });
      const tend = e => { if (on) { on = false; h.up(pt(e)); } };
      elm.addEventListener('touchend', tend); elm.addEventListener('touchcancel', tend);
      elm.addEventListener('mousedown', e => {
        if (on || h.down(pt(e)) === false) return; on = true; e.preventDefault();
        const mv = ev => h.move(pt(ev)), up = ev => { on = false; window.removeEventListener('mousemove', mv); window.removeEventListener('mouseup', up); h.up(pt(ev)); };
        window.addEventListener('mousemove', mv); window.addEventListener('mouseup', up);
      });
    }
  }
  function login(message) {
    $('#grid').hidden = true; $('#login').hidden = false;
    if (message) $('#loginText').textContent = message;
    const input = $('#pin'); input.value = ''; input.focus();
    const go = () => { if (!input.value) return; pin = input.value.trim(); failures = 0; $('#login').hidden = true; connect(); };
    input.onkeydown = e => { if (e.key === 'Enter') go(); };
    $('#go').onclick = go;
  }
  function connect() {
    if (needPin && !pin) return login();
    state('connecting…'); opened = false;
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws' + (needPin ? '?pin=' + encodeURIComponent(pin) : ''));
    ws.onopen = () => { opened = true; failures = 0; sessionStorage.setItem('nyxmap-pin', pin); state('connected', 'ok'); };
    ws.onmessage = e => {
      let m; try { m = JSON.parse(e.data); } catch (_) { return; }
      if (m.t === 'controls') {
        controls = m.list || []; byId = {}; $('#title').textContent = m.title || 'NyxMap';
        controls.forEach(c => { byId[c.id] = c; values[c.id] = c.kind === 'color' ? c.rgb : c.kind === 'pad' ? [c.x, c.y] : c.kind === 'adsr' ? c.adsr : c.value; });
        if (busy) again = true; else render();
      } else if (m.t === 'values') { Object.assign(values, m.v || {}); update(); }
    };
    ws.onclose = () => {
      state('offline', 'bad');
      if (needPin && !opened && ++failures >= 2) { sessionStorage.removeItem('nyxmap-pin'); pin = ''; login('Wrong PIN, or NyxMap cannot be reached. Enter the PIN again.'); return; }
      setTimeout(connect, Math.min(4000, 400 + failures * 500));
    };
  }
)JS";

// B: the widgets, in SVG and CSS. Each takes its control and `io` (begin, input(v), end) and gives { el, set(v) }
inline constexpr char kScriptB[] = R"JS(
  const wDefault = { slider: 'fader', toggle: 'switch', choice: 'buttons', button: 'hold', color: 'picker', pad: 'pad', adsr: 'adsr' };
  const wChoices = { slider: ['fader', 'vfader', 'knob', 'number'], toggle: ['switch'], choice: ['buttons', 'menu'], button: ['hold', 'tap'], color: ['picker'], pad: ['pad'], adsr: ['adsr'] };
  const wName = { fader: 'Fader', vfader: 'Vertical fader', knob: 'Encoder', number: 'Number', switch: 'Switch', buttons: 'Buttons', menu: 'Menu', hold: 'Hold button', tap: 'Tap button', picker: 'Color', pad: 'XY pad', adsr: 'ADSR' };
  const widgetOf = c => (wChoices[c.kind] || []).indexOf(c.widget) >= 0 ? c.widget : wDefault[c.kind];
  function fader(c, io, vertical) {
    const root = el('div', 'fader' + (vertical ? ' v' : '')), trk = el('div', 'trk'), fill = el('div', 'fill'), cap = el('div', 'cap');
    trk.appendChild(fill); trk.appendChild(cap); root.appendChild(trk);
    let pos = 0, down = false;
    const draw = () => { const p = (pos * 100) + '%'; if (vertical) { fill.style.height = p; cap.style.bottom = p; } else { fill.style.width = p; cap.style.left = p; } };
    const at = p => { const r = trk.getBoundingClientRect(); return clamp(vertical ? 1 - (p.y - r.top) / r.height : (p.x - r.left) / r.width, 0, 1); };
    const move = p => { pos = at(p); draw(); io.input(fromPos(c, pos)); };
    press(root, { down: p => { down = true; io.begin(); move(p); }, move, up: p => { move(p); down = false; io.end(); } });
    return { el: root, set: v => { if (!down) { pos = toPos(c, v); draw(); } } };
  }
  function knob(c, io) {
    const root = el('div', 'knob'), s = sv('svg', { viewBox: '0 0 100 100' }, root), R = 38;
    const pt = a => [50 + R * Math.cos(a * Math.PI / 180), 50 + R * Math.sin(a * Math.PI / 180)];
    const arc = (a0, a1) => { const p0 = pt(a0), p1 = pt(a1); return 'M' + p0[0] + ' ' + p0[1] + 'A' + R + ' ' + R + ' 0 ' + (a1 - a0 > 180 ? 1 : 0) + ' 1 ' + p1[0] + ' ' + p1[1]; };
    sv('path', { d: arc(135, 405), 'class': 'kbg' }, s);
    const val = sv('path', { 'class': 'kval' }, s), dot = sv('line', { 'class': 'kdot' }, s), tx = sv('text', { 'class': 'ktx', x: 50, y: 56 }, s);
    let pos = 0, down = false, y0 = 0, p0 = 0;
    const draw = v => { const a = 135 + 270 * pos, e = pt(a), i = [50 + 22 * Math.cos(a * Math.PI / 180), 50 + 22 * Math.sin(a * Math.PI / 180)];
      val.setAttribute('d', pos > 0.004 ? arc(135, a) : ''); dot.setAttribute('x1', i[0]); dot.setAttribute('y1', i[1]); dot.setAttribute('x2', e[0]); dot.setAttribute('y2', e[1]); tx.textContent = fmt(c, v); };
    press(root, { down: p => { down = true; y0 = p.y; p0 = pos; io.begin(); }, move: p => { pos = clamp(p0 + (y0 - p.y) / (p.e && p.e.shiftKey ? 600 : 170), 0, 1); const v = fromPos(c, pos); draw(v); io.input(v); }, up: () => { down = false; io.end(); } });
    return { el: root, set: v => { if (!down) { pos = toPos(c, v); draw(v); } } };
  }
  function number(c, io) {
    const root = el('div', 'num'), m = el('button', '', '−'), p = el('button', '', '+'), out = el('output');
    let v = c.min;
    const step = d => { v = clamp(Number((v + d * c.step).toFixed(6)), Math.min(c.min, c.max), Math.max(c.min, c.max)); out.textContent = fmt(c, v); io.begin(); io.input(v); io.end(); };
    m.onclick = () => step(-1); p.onclick = () => step(1);
    root.appendChild(m); root.appendChild(out); root.appendChild(p);
    return { el: root, set: x => { v = x; out.textContent = fmt(c, x); } };
  }
  function widget(c, io) {
    const w = widgetOf(c);
    if (c.kind === 'slider') return w === 'knob' ? knob(c, io) : w === 'number' ? number(c, io) : fader(c, io, w === 'vfader');
    if (c.kind === 'toggle') { const b = el('button', 'sw', c.name); b.onclick = () => { io.begin(); io.input(values[c.id] >= 0.5 ? 0 : 1); io.end(); }; return { el: b, set: v => b.classList.toggle('on', v >= 0.5) }; }
    if (c.kind === 'button') {
      const b = el('button', w === 'tap' ? 'tapb' : 'holdb', w === 'tap' ? 'Tap' : 'Hold');
      press(b, { down: () => { io.begin(); io.input(1); }, move: () => {}, up: () => { if (w === 'tap') setTimeout(() => { io.input(0); io.end(); }, 90); else { io.input(0); io.end(); } } });
      b.addEventListener('contextmenu', e => e.preventDefault());
      return { el: b, set: v => b.classList.toggle('on', v >= 0.5) };
    }
    if (c.kind === 'choice') {
      if (w === 'menu') { const s = el('select', 'menu'); c.choices.forEach((n, i) => { const o = el('option', '', n); o.value = i; s.appendChild(o); }); s.onchange = () => { io.begin(); io.input(Number(s.value)); io.end(); }; return { el: s, set: v => { s.value = Math.round(v); } }; }
      const row = el('div', 'choice'), bs = c.choices.map((n, i) => { const b = el('button', '', n); b.onclick = () => { io.begin(); io.input(i); io.end(); }; row.appendChild(b); return b; });
      return { el: row, set: v => bs.forEach((b, i) => b.classList.toggle('on', i === Math.round(v))) };
    }
    if (c.kind === 'color') {
      const p = el('input'); p.type = 'color';
      p.addEventListener('input', () => { io.begin(); throttled(c.id, () => send({ t: 'color', id: c.id, rgb: unhex(p.value) })); });
      p.addEventListener('change', () => { send({ t: 'color', id: c.id, rgb: unhex(p.value) }); io.end(); });
      return { el: p, set: v => { if (!active.has(c.id)) p.value = hex(v); } };
    }
    if (c.kind === 'pad') {
      const pad = el('div', 'pad'), dot = el('i'); pad.appendChild(dot);
      const span = c.max - c.min, draw = v => { dot.style.left = ((v[0] - c.min) / span * 100) + '%'; dot.style.top = ((1 - (v[1] - c.min) / span) * 100) + '%'; };
      const at = p => { const r = pad.getBoundingClientRect(); return [c.min + clamp((p.x - r.left) / r.width, 0, 1) * span, c.min + clamp(1 - (p.y - r.top) / r.height, 0, 1) * span]; };
      const move = p => { const v = at(p); values[c.id] = v; draw(v); throttled(c.id, () => send({ t: 'point', id: c.id, xy: v })); };
      let down = false;
      press(pad, { down: p => { down = true; active.add(c.id); move(p); }, move, up: p => { down = false; active.delete(c.id); send({ t: 'point', id: c.id, xy: at(p) }); } });
      return { el: pad, set: v => { if (!down) draw(v); } };
    }
    if (c.kind === 'adsr') return adsr(c, io);
    return { el: el('div'), set: () => {} };
  }
  // An envelope in the hand: attack, decay, sustain and release, each by its own handle (the times along four slots, the sustain up and down)
  function adsr(c, io) {
    const root = el('div', 'adsr'), s = sv('svg', {}, root), area = sv('path', { 'class': 'aarea' }, s), line = sv('path', { 'class': 'aline' }, s);
    const hs = [0, 1, 2, 3].map(() => sv('circle', { 'class': 'ahdl', r: 9 }, s)), lbl = el('div', 'alab');
    root.appendChild(lbl);
    let v = c.adsr.slice(), W = 1, H = 1, grab = -1;
    const slot = 0.28, hold = 0.72, pad = 12;
    const pts = () => { const w = W - 2 * pad, h = H - 2 * pad, m = c.max || 2, X = f => pad + w * f, Y = l => pad + h * (1 - l);
      return [[X(slot * Math.min(1, v[0] / m)), Y(1)], [X(slot + slot * Math.min(1, v[1] / m)), Y(v[2])], [X(hold), Y(v[2])], [X(hold + slot * Math.min(1, v[3] / m)), Y(0)]]; };
    const draw = () => {
      W = root.clientWidth || 1; H = (root.clientHeight || 1) - 18; s.setAttribute('viewBox', '0 0 ' + W + ' ' + Math.max(H, 1)); s.setAttribute('width', W); s.setAttribute('height', Math.max(H, 1));
      const p = pts(), base = H - pad, d = 'M' + pad + ' ' + base + p.map(q => 'L' + q[0] + ' ' + q[1]).join('');
      line.setAttribute('d', d); area.setAttribute('d', d + 'L' + p[3][0] + ' ' + base + 'Z');
      p.forEach((q, i) => { hs[i].setAttribute('cx', q[0]); hs[i].setAttribute('cy', q[1]); });
      lbl.textContent = 'A ' + v[0].toFixed(2) + ' s   D ' + v[1].toFixed(2) + ' s   S ' + v[2].toFixed(2) + '   R ' + v[3].toFixed(2) + ' s';
    };
    const apply = p => { const r = s.getBoundingClientRect(), x = p.x - r.left - pad, y = p.y - r.top - pad, w = W - 2 * pad, h = H - 2 * pad, m = c.max || 2, f = a => clamp(a, 0, 1) * m;
      if (grab === 0) v[0] = f(x / (w * slot)); else if (grab === 1) v[1] = f((x - w * slot) / (w * slot)); else if (grab === 2) v[2] = clamp(1 - y / h, 0, 1); else v[3] = f((x - w * hold) / (w * slot));
      v = v.map((a, i) => Number(a.toFixed(3))); draw(); io.input(v); };
    press(root, { down: p => { const r = s.getBoundingClientRect(), q = pts(); let best = 1e9; grab = -1;
        q.forEach((h, i) => { const dd = Math.hypot(p.x - r.left - h[0], p.y - r.top - h[1]); if (dd < best) { best = dd; grab = i; } });
        if (best > 40) { grab = -1; return false; } io.begin(); apply(p); }, move: p => { if (grab >= 0) apply(p); }, up: () => { grab = -1; io.end(); } });
    return { el: root, resize: draw, set: x => { if (grab < 0) { v = x.slice(); draw(); } } };
  }
)JS";

// C: pages, the grid, the tiles, Edit mode (drag, resize)
inline constexpr char kScriptC[] = R"JS(
  const sizeOf = c => { const w = widgetOf(c); return c.kind === 'group' ? [12, 1] : c.kind === 'pad' ? [5, 5] : c.kind === 'adsr' ? [6, 3] : c.kind === 'choice' ? [6, 2] : c.kind === 'slider' ? (w === 'knob' ? [3, 3] : w === 'vfader' ? [2, 5] : w === 'number' ? [3, 2] : [5, 2]) : [3, 2]; };
  const minOf = c => c.kind === 'pad' ? [2, 2] : c.kind === 'slider' && widgetOf(c) === 'knob' ? [2, 2] : c.kind === 'slider' && widgetOf(c) === 'vfader' ? [1, 3] : c.kind === 'group' ? [2, 1] : [2, 1];
  const hit = (a, b) => a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
  const topOf = c => { let p = c.group; while (p && byId[p] && byId[p].group) p = byId[p].group; return p || 0; };
  function pages() {
    const tops = controls.filter(c => c.kind === 'group' && !c.group), out = [];
    if (controls.some(c => !c.group && c.kind !== 'group')) out.push({ id: 0, name: tops.length ? 'Main' : 'Controls' });
    tops.forEach(g => out.push({ id: g.id, name: g.name }));
    return out;
  }
  const pageItems = id => controls.filter(c => c.id !== id && (id === 0 ? !c.group && c.kind !== 'group' : topOf(c) === id));
  function layoutOf(list) { // [{c, x, y, w, h}]: where each is saved, the rest flowed into the first free place
    const placed = [], rest = [];
    list.forEach(c => (c.layout ? placed.push({ c, x: c.layout.x, y: c.layout.y, w: c.layout.w, h: c.layout.h }) : rest.push(c)));
    rest.forEach(c => { const s = sizeOf(c); let found = false; for (let y = 0; !found; y++) for (let x = 0; x <= 12 - s[0] && !found; x++) if (!placed.some(p => hit(p, { x, y, w: s[0], h: s[1] }))) { placed.push({ c, x, y, w: s[0], h: s[1] }); found = true; } });
    return placed;
  }
  function resolve(items, fixed) { // the one moved stays; those it lands on are pushed down
    const rest = items.filter(i => i !== fixed).sort((a, b) => a.y - b.y || a.x - b.x), placed = [fixed];
    rest.forEach(it => { let y = it.y, at; while ((at = placed.find(p => hit(p, { x: it.x, y, w: it.w, h: it.h })))) y = at.y + at.h; it.y = y; placed.push(it); });
  }
  let items = [], cell = 60, rowH = 60, compact = false;
  function geometry() { const w = $('#grid').clientWidth; compact = w < 560; cell = w / 12; rowH = clamp(cell, 46, 84); }
  function place(it, t) {
    if (compact) { t.el.style.height = (it.h * 54) + 'px'; t.el.style.width = ''; t.el.style.left = t.el.style.top = ''; if (t.api && t.api.resize) t.api.resize(); return; }
    t.el.style.left = (it.x * cell) + 'px'; t.el.style.top = (it.y * rowH) + 'px'; t.el.style.width = (it.w * cell) + 'px'; t.el.style.height = (it.h * rowH) + 'px';
    if (t.api && t.api.resize) t.api.resize();
  }
  function placeAll() {
    geometry(); $('#grid').classList.toggle('compact', compact);
    const order = compact ? items.slice().sort((a, b) => a.y - b.y || a.x - b.x) : items;
    order.forEach(it => { if (compact) $('#grid').appendChild(tiles[it.c.id].el); place(it, tiles[it.c.id]); });
    if (!compact) $('#grid').style.height = (Math.max.apply(null, items.map(i => i.y + i.h).concat([1])) * rowH + (editing ? rowH * 2 : 0)) + 'px'; else $('#grid').style.height = '';
    $('#edit').hidden = false; $('#edit').style.display = compact ? 'none' : '';
  }
  function tile(it) {
    const c = it.c, t = { it, el: el('div', 'tile' + (c.kind === 'group' ? ' sec' : '')), api: null };
    const face = el('div', 'face'), hd = el('div', 'hd'), name = el('span', '', c.name), val = el('b');
    face.style.setProperty('--a', c.accent || '');
    if (!c.accent) face.style.removeProperty('--a');
    hd.appendChild(name); if (c.kind === 'slider') hd.appendChild(val); face.appendChild(hd);
    if (c.kind !== 'group' && c.kind !== 'toggle') {
      const bd = el('div', 'bd');
      const kindMsg = (v) => c.kind === 'adsr' ? { t: 'adsr', id: c.id, v } : { t: 'set', id: c.id, v };
      const io = { begin: () => active.add(c.id), input: v => { values[c.id] = v; if (val) val.textContent = fmt(c, v); throttled(c.id, () => send(kindMsg(v))); }, end: () => { active.delete(c.id); send(kindMsg(values[c.id])); } };
      t.api = widget(c, io); t.val = val; bd.appendChild(t.api.el); face.appendChild(bd);
    } else if (c.kind === 'toggle') {
      const bd = el('div', 'bd'), io = { begin() {}, input: v => send({ t: 'set', id: c.id, v }), end() {} };
      t.api = widget(c, io); bd.appendChild(t.api.el); face.appendChild(bd);
    }
    t.el.appendChild(face); t.face = face;
    if (editing && !compact) {
      const shield = el('div', 'shield'), rz = el('div', 'rz'), gear = el('button', 'gear', '⚙');
      t.el.appendChild(shield); t.el.appendChild(rz); t.el.appendChild(gear);
      gear.addEventListener('click', e => { e.stopPropagation(); openSheet(c.id); });
      gear.addEventListener('touchend', e => { e.stopPropagation(); }, { passive: true });
      shield.addEventListener('dblclick', () => openSheet(c.id));
      const gesture = (grab, apply) => { let x0 = 0, y0 = 0, o, base; return { down: p => { x0 = p.x; y0 = p.y; o = { x: it.x, y: it.y, w: it.w, h: it.h }; base = items.map(i => ({ i, x: i.x, y: i.y })); busy++; t.el.classList.add('moving'); selectTile(c.id); }, move: p => { base.forEach(b => { if (b.i !== it) { b.i.x = b.x; b.i.y = b.y; } }); apply(o, Math.round((p.x - x0) / cell), Math.round((p.y - y0) / rowH)); resolve(items, it); items.forEach(i => place(i, tiles[i.c.id])); $('#grid').style.height = (Math.max.apply(null, items.map(i => i.y + i.h)) * rowH + rowH * 2) + 'px'; }, up: () => { t.el.classList.remove('moving'); busy--; saveLayout(); } }; };
      press(shield, gesture(0, (o, dx, dy) => { it.x = clamp(o.x + dx, 0, 12 - it.w); it.y = Math.max(0, o.y + dy); }));
      const mn = minOf(c);
      press(rz, gesture(1, (o, dx, dy) => { it.w = clamp(o.w + dx, mn[0], 12 - o.x); it.h = clamp(o.h + dy, mn[1], 40); }));
    }
    tiles[c.id] = t;
    return t;
  }
  function selectTile(id) { Object.keys(tiles).forEach(k => tiles[k].el.classList.toggle('sel', Number(k) === id || k === String(id))); }
  function saveLayout() {
    const o = {}; items.forEach(i => { o[i.c.id] = { x: i.x, y: i.y, w: i.w, h: i.h }; i.c.layout = { x: i.x, y: i.y, w: i.w, h: i.h }; });
    send({ t: 'layout', items: o });
    if (again) { again = false; render(); }
  }
  function render() {
    const list = pages(), grid = $('#grid');
    $('#login').hidden = true; grid.hidden = false;
    if (!list.some(p => p.id === pageId)) pageId = list.length ? list[0].id : -1;
    const tabs = $('#tabs'); tabs.textContent = ''; tabs.hidden = list.length < 2;
    list.forEach(p => { const b = el('button', p.id === pageId ? 'on' : '', p.name); b.onclick = () => { pageId = p.id; render(); }; tabs.appendChild(b); });
    grid.textContent = ''; Object.keys(tiles).forEach(k => delete tiles[k]);
    grid.classList.toggle('edit', editing);
    if (pageId < 0) { grid.appendChild(el('div', '', 'No control yet. Right-click a parameter in NyxMap › Add to Controls Page, or make some in NyxMap › Controls.')).id = 'empty'; $('#edit').hidden = true; return; }
    items = layoutOf(pageItems(pageId));
    items.forEach(it => grid.appendChild(tile(it).el));
    placeAll(); update();
  }
  function update() {
    for (const id in tiles) {
      const t = tiles[id], c = t.it.c;
      if (!t.api || active.has(c.id)) continue;
      const v = values[c.id]; t.api.set(v);
      if (t.val) t.val.textContent = fmt(c, v);
    }
  }
  window.addEventListener('resize', () => { if (items.length) placeAll(); });
  window.addEventListener('orientationchange', () => setTimeout(() => { if (items.length) placeAll(); }, 250));
  $('#edit').onclick = () => { editing = !editing; $('#edit').classList.toggle('on', editing); if (!editing) closeSheet(); render(); };
)JS";

// D: the settings of a tile, then the start
inline constexpr char kScriptD[] = R"JS(
  const accents = ['', '#29c7e0', '#46c46a', '#e5c14a', '#f0883e', '#e5564a', '#d36bd9', '#7b8cff'];
  let sheetTimer = 0, sheetProps = {};
  const push = props => { sheetProps = Object.assign(sheetProps, props); clearTimeout(sheetTimer); sheetTimer = setTimeout(() => { send({ t: 'edit', id: sheetId, props: sheetProps }); sheetProps = {}; }, 220); };
  function closeSheet() { sheetId = 0; $('#sheet').classList.remove('open'); selectTile(0); }
  function openSheet(id) {
    const c = byId[id]; if (!c) return;
    sheetId = id; selectTile(id);
    const sh = $('#sheet'); sh.textContent = '';
    const h2 = el('h2', '', c.kind === 'group' ? 'Section' : 'Tile'), x = el('button', 'hb', 'Done'); x.onclick = closeSheet; h2.appendChild(x); sh.appendChild(h2);
    const field = (label, node) => { sh.appendChild(el('label', '', label)); sh.appendChild(node); return node; };
    const text = (v, on) => { const i = el('input'); i.type = 'text'; i.value = v; i.oninput = () => on(i.value); return i; };
    const numeric = (v, on, step) => { const i = el('input'); i.type = 'number'; i.value = v; i.step = step || 'any'; i.inputMode = 'decimal'; i.onchange = () => { if (i.value !== '' && isFinite(Number(i.value))) on(Number(i.value)); }; return i; };
    field('Name', text(c.name, v => push({ name: v })));
    if (c.kind !== 'group') {
      const ws = wChoices[c.kind] || [];
      if (ws.length > 1) {
        const s = el('select'); ws.forEach(w => { const o = el('option', '', wName[w]); o.value = w; s.appendChild(o); }); s.value = widgetOf(c);
        s.onchange = () => { // the tile takes the size of its new widget, where it is
          const it = items.find(i => i.c.id === c.id), size = sizeOf(Object.assign({}, c, { widget: s.value }));
          send({ t: 'edit', id: c.id, props: { widget: s.value } });
          if (it) { it.w = size[0]; it.h = size[1]; it.x = Math.min(it.x, 12 - it.w); resolve(items, it); saveLayout(); }
        };
        field('Widget', s);
      }
      const sw = el('div', 'sw8'); accents.forEach(a => { const b = el('button', (c.accent || '') === a ? 'on' : '', ''); b.style.background = a || '#34343b'; b.title = a || 'default'; b.onclick = () => { push({ accent: a }); sw.querySelectorAll('button').forEach(o => o.classList.remove('on')); b.classList.add('on'); }; sw.appendChild(b); });
      field('Accent', sw);
    }
    if (c.kind === 'adsr') field('Longest time (s)', numeric(c.max, v => push({ max: v })));
    if (c.kind === 'slider') {
      const row = el('div', 'row'), cell2 = (l, n) => { const d = el('div'); d.appendChild(el('label', '', l)); d.appendChild(n); return d; };
      row.appendChild(cell2('Min', numeric(c.min, v => push({ min: v })))); row.appendChild(cell2('Max', numeric(c.max, v => push({ max: v })))); sh.appendChild(row);
      const row2 = el('div', 'row'); row2.appendChild(cell2('Step', numeric(c.step, v => push({ step: v })))); row2.appendChild(cell2('Unit', text(c.unit || '', v => push({ unit: v })))); sh.appendChild(row2);
      sh.appendChild(el('label', '', 'Curve'));
      const r = el('input'), hint = el('div', 'hint'); r.type = 'range'; r.min = -100; r.max = 100; r.value = Math.round(Math.log(c.curve || 1) / Math.log(4) * 100);
      const say = k => { hint.textContent = Math.abs(k - 1) < 0.03 ? 'Linear' : k > 1 ? 'Fine at the low end (×' + k.toFixed(2) + ')' : 'Fine at the high end (×' + k.toFixed(2) + ')'; };
      r.oninput = () => { const k = Math.pow(4, Number(r.value) / 100); say(k); push({ curve: Number(k.toFixed(3)) }); };
      say(c.curve || 1); sh.appendChild(r); sh.appendChild(hint);
    }
    const del = el('button', 'del', 'Delete this control'); del.onclick = () => { if (confirm('Delete "' + c.name + '"? Its OSC address goes with it.')) { send({ t: 'remove', id: c.id }); closeSheet(); } };
    sh.appendChild(del);
    sh.classList.add('open');
  }
  fetch('/info').then(r => r.json()).then(i => { needPin = !!i.pin; }).catch(() => {}).then(connect);
})();
)JS";

inline QByteArray style() { return QByteArray(kStyleA) + kStyleB; }
inline QByteArray script() { return QByteArray(kScriptA) + kScriptB + kScriptC + kScriptD; }
} // namespace remote_page
