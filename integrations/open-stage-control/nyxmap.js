// Open Stage Control 1.x custom module. Runtime dependencies ship in this folder.
// NyxMap state remains authoritative; feedback never replays a show command.
const {WebSocket, osc} = require('./nyxmap-dependencies.cjs');
const http = nativeRequire('http');
const {Buffer} = nativeRequire('buffer');
const createLayerDesk = require('./nyxmap-layers.cjs');
const PAGE_SIZE = 24;
let host = '127.0.0.1', port = 5678, socket, timer, request, generation = 0;
let catalog, playback, selected = '0', lastPong = 0, online = false, catalogKey = '';
let pages = {snapshots: 0, presets: 0, timelines: 0, cues: 0};
let retryAt = 0, lastCatalog = 0, catalogError = '';
let catalogRefreshPending = false;
const layerRequests = new Set();
const id = value => typeof value === 'string' && /^[1-9][0-9]*$/.test(value);
const sentValues = new Map();
function emit(address, value, force = false) {
    const previous = sentValues.get(address);
    if (!force && sentValues.has(address) && (previous === value ||
        (Array.isArray(previous) && Array.isArray(value) && previous.length === value.length && previous.every((v, i) => v === value[i])))) return;
    if (sentValues.size > 512) sentValues.clear();
    sentValues.set(address, value); receive(address, ...(Array.isArray(value) ? value : [value]));
}
const edit = (widget, properties) => receive('/EDIT', widget, JSON.stringify(properties));
const arg = (data, index) => data.args[index] && data.args[index].value;
const target = {target: '127.0.0.1:1234', ignoreDefaults: true};
const active = 'JS{return !!OSC{/nyxmap/ui/online,0,false}}';

function status(text) {
    const ready = !!(socket && socket.readyState === WebSocket.OPEN && catalog && playback && Date.now() - lastPong < 6000);
    if (online !== ready) { online = ready; emit('/nyxmap/ui/online', ready ? 1 : 0); }
    emit('/nyxmap/ui/status', text || catalogError || (ready ? `CONNECTED · ${host}:${port}` : `CONNECTING · ${host}:${port}`));
}
function button(widget, label, address, preArgs, extra) {
    return Object.assign({type: 'button', id: widget, label, mode: 'momentary', height: 48,
        width: '100%', address, preArgs: preArgs || [], interaction: active, wrap: true}, target, extra || {});
}
function text(widget, value, extra) {
    return Object.assign({type: 'text', id: widget, value, height: 32, width: '100%', bypass: true, wrap: true}, extra || {});
}
function range(kind, list) {
    const last = Math.max(0, Math.ceil(list.length / PAGE_SIZE) - 1);
    pages[kind] = Math.max(0, Math.min(last, pages[kind] || 0));
    const start = pages[kind] * PAGE_SIZE;
    emit(`/nyxmap/library/${kind}/page`, list.length ? `${start + 1}–${Math.min(start + PAGE_SIZE, list.length)} / ${list.length}` : 'EMPTY');
    return list.slice(start, start + PAGE_SIZE);
}
function renderLibrary(kind) {
    if (!catalog) return;
    const entries = range(kind, catalog[kind] || []);
    const widgets = entries.map(entry => {
        const key = `${kind}_${entry.id}`, label = `${entry.index} · ${entry.name || 'Untitled'}`;
        if (kind !== 'timelines')
            return button(key, label, '/nyxmap/recall', [kind, entry.id], {height: 76, colorWidget: kind === 'presets' ? '#e0b43a' : '#4cd964'});
        const base = `/nyxmap/timeline/${entry.id}`;
        return {type: 'panel', id: key, label: false, layout: 'vertical', height: 166, width: '100%',
            colorBg: '#26262b', contain: false, css: 'min-height:166rem!important;', padding: 8, widgets: [
                text(key + '_name', label, {align: 'left', css: 'font-weight:600;font-size:17px;'}),
                text(key + '_clock', 'Stopped', {address: base + '/clock', css: 'font-family:monospace;'}),
                {type: 'fader', id: key + '_progress', address: base + '/progress', horizontal: true, design: 'compact', knobSize: 0, label: false,
                    height: 16, padding: 0, lineWidth: 0, alphaFillOff: 0.08, alphaFillOn: 0.6, width: '100%', interaction: false, bypass: true, input: false, range: {min: 0, max: 1}, colorWidget: '#56c4ff'},
                {type: 'panel', id: key + '_buttons', label: false, layout: 'horizontal', height: 44, width: '100%', padding: 3,
                    widgets: ['play', 'pause', 'stop'].map(action => button(key + '_' + action, action.toUpperCase(),
                        '/nyxmap/timeline/control', [entry.id, action], {height: 40, width: 'auto', expand: true,
                            colorWidget: action === 'stop' ? '#e84c3d' : '#56c4ff'}))}
            ]};
    });
    edit(kind + '_items', {widgets: widgets.length ? widgets : [text(kind + '_empty', 'No ' + kind + ' in this project.')]});
    emit('/nyxmap/ui/online', online ? 1 : 0, true);
    if (playback) publishTimelineValues(true);
}
function renderCues() {
    const sequence = catalog && catalog.sequences.find(entry => entry.id === selected);
    const entries = range('cues', sequence ? sequence.cues : []);
    const widgets = entries.map(cue => {
        const number = Number(cue.index);
        const name = cue.text || cue.name || (cue.missing ? 'Missing target' : 'Empty cue');
        const details = [cue.target_type, cue.name, cue.description].filter(Boolean).join(' · ');
        const mode = continuation(cue.continue, cue.postwait);
        return {type: 'panel', id: 'cue_row_' + cue.id, label: false, layout: 'horizontal',
            height: 88, width: '100%', padding: 4, alphaStroke: 0, contain: false,
            css: 'class: nyxmap-cue-row;', widgets: [button('cue_' + cue.id, `${cue.index} · ${name}\n${details}`, '/nyxmap/sequence/cue/go', [selected, cue.id], {
            height: 80, width: 'auto', expand: true, align: 'left',
            colorWidget: `JS{return OSC{/sequence/cue/current/number,0,false} === ${number} ? '#4cd964' : OSC{/sequence/cue/next/number,0,false} === ${number} ? '#e84c3d' : '#6d6d78'}`,
            alphaFillOff: 0.08, alphaStroke: 0.45, borderRadius: 4,
            css: ':host {font-size:14px;min-width:0;} label {justify-content:flex-start;text-align:left;padding-left:14rem;line-height:1.5;}'}),
            text('cue_mode_' + cue.id, mode.label + '\n' + mode.detail, {height: 80, width: 160,
                colorText: mode.color, align: 'center', wrap: 'soft',
                css: `class: nyxmap-continuation;\n:host {border:1rem solid ${mode.color}66;border-radius:4rem;background:${mode.color}12;font-size:12px;font-weight:600;line-height:1.7;}`})]};
    });
    edit('cue_items', {widgets: widgets.length ? widgets : [text('cues_empty', 'Select a sequence containing cues.')]});
    emit('/nyxmap/ui/online', online ? 1 : 0, true);
    if (playback) for (const role of ['current', 'next']) {
        const address = `/sequence/cue/${role}/number`; emit(address, playback.values[address], true);
    }
}
function continuation(mode, postwait) {
    const delay = clock(Number(postwait) || 0);
    if (mode === 'follow') return {label: 'FOLLOW', detail: `Start + ${delay}`, color: '#79cbb6'};
    if (mode === 'auto-follow') return {label: 'AUTO-FOLLOW', detail: `End + ${delay}`, color: '#b7a1d9'};
    if (mode === 'wait') return {label: 'WAIT · GO', detail: 'Manual advance', color: '#d5bd82'};
    return {label: '—', detail: '', color: '#a4a4ae'};
}
function publishCatalog(force) {
    if (!catalog) return;
    layers.setCatalog(catalog.layers, force);
    const key = JSON.stringify(catalog);
    if (!force && key === catalogKey) return;
    catalogKey = key;
    edit('sequence_select', {values: {labels: catalog.sequences.map(s => `${s.index} · ${s.name || 'Untitled'}`),
        values: catalog.sequences.map(s => s.id)}});
    emit('/nyxmap/sequence/select', selected, true);
    for (const kind of ['snapshots', 'presets', 'timelines']) renderLibrary(kind);
    renderCues();
}
function clock(seconds) {
    if (seconds < 0 || !Number.isFinite(seconds)) return '∞';
    const tenths = Math.round(Math.max(0, seconds) * 10);
    return `${Math.floor(tenths / 600)}:${String(Math.floor(tenths / 10) % 60).padStart(2, '0')}.${tenths % 10}`;
}
function publishTimelineValues(force = false) {
    if (!catalog) return;
    const visible = new Set(catalog.timelines.slice(pages.timelines * PAGE_SIZE, (pages.timelines + 1) * PAGE_SIZE).map(t => t.id));
    for (const timeline of playback.timelines || []) {
        if (!visible.has(timeline.id)) continue;
        const base = `/nyxmap/timeline/${timeline.id}`;
        emit(base + '/progress', timeline.progress, force);
        emit(base + '/clock', `${timeline.state.toUpperCase()} · ${clock(timeline.position)} / ${clock(timeline.duration)} · Pass ${timeline.pass} · Left ${clock(timeline.remaining)}`, force);
    }
}
function publishState(state) {
    if (!state || state.version !== 1 || !state.values || !Array.isArray(state.timelines)) throw Error('Unsupported NyxMap state');
    const oldSelected = selected;
    const oldCue = playback && playback.values['/sequence/cue/current/number'];
    selected = String(state.values['/sequence/id'] || '0');
    playback = state;
    for (const address of Object.keys(state.values)) emit(address, state.values[address]);
    for (const role of ['previous', 'current', 'next']) {
        const base = '/sequence/cue/' + role;
        const mode = continuation(state.values[base + '/continue'], state.values[base + '/postwait']);
        emit('/nyxmap/cue/' + role + '/continuation', mode.label + ' · ' + mode.detail);
    }
    for (const property of ['pause', 'blackout']) emit('/nyxmap/composition/' + property, state.values['/composition/' + property]);
    emit('/nyxmap/sequence/select', selected);
    const cue = Math.max(0, Number(state.values['/sequence/cue/current/number']) - 1) || 0;
    if (selected !== oldSelected || (oldCue !== state.values['/sequence/cue/current/number'] && Math.floor(cue / PAGE_SIZE) !== pages.cues)) {
        pages.cues = Math.floor(cue / PAGE_SIZE); renderCues();
    }
    emit('/nyxmap/ui/background', (state.sequences || []).filter(s => s.active && s.id !== selected).length);
    publishTimelineValues();
    status();
}
function fetchCatalog(force) {
    if (request) { catalogRefreshPending = true; return; }
    if (!socket || socket.readyState !== WebSocket.OPEN) return;
    const token = generation;
    request = http.get({hostname: host, port, path: '/show/catalog?VALUE', timeout: 2500}, response => {
        let body = '', length = 0;
        response.setEncoding('utf8');
        response.on('data', chunk => {
            length += Buffer.byteLength(chunk);
            if (length > 4 * 1024 * 1024) { response.destroy(Error('NyxMap catalog is too large')); return; }
            body += chunk;
        });
        response.on('error', () => {});
        response.on('end', () => {
            if (token !== generation) return;
            try {
                if (response.statusCode !== 200) throw Error('Update NyxMap: show-control API unavailable');
                const parsed = JSON.parse(JSON.parse(body).VALUE[0]);
                if (parsed.version !== 1 || !['sequences', 'snapshots', 'presets', 'timelines'].every(k => Array.isArray(parsed[k])))
                    throw Error('Unsupported NyxMap catalog');
                catalog = parsed; lastCatalog = Date.now(); catalogError = '';
                publishCatalog(force); status();
            } catch (error) { catalog = null; catalogError = error.message; status(); }
        });
    });
    request.on('timeout', () => request && request.destroy(Error('OSCQuery request timed out')));
    request.on('error', error => { if (token === generation) { catalog = null; catalogError = error.message; status(); } });
    request.on('close', () => {
        if (token !== generation) return;
        request = null;
        if (catalogRefreshPending) { catalogRefreshPending = false; fetchCatalog(true); }
    });
}
function queryLayer(path) {
    for (const pending of layerRequests) pending.destroy();
    layerRequests.clear();
    return new Promise((resolve, reject) => {
        const token = generation;
        const req = http.get({hostname: host, port, path: encodeURI(path), timeout: 4000}, response => {
            let body = '', size = 0;
            response.setEncoding('utf8');
            response.on('data', chunk => {
                size += Buffer.byteLength(chunk);
                if (size > 4 * 1024 * 1024) { response.destroy(Error('Layer description is too large')); return; }
                body += chunk;
            });
            response.on('error', reject);
            response.on('end', () => {
                if (token !== generation) { reject(Error('Connection changed')); return; }
                try {
                    if (response.statusCode !== 200) throw Error('Layer no longer exists; refresh the list.');
                    resolve(JSON.parse(body));
                } catch (error) { reject(error); }
            });
        });
        layerRequests.add(req);
        req.on('timeout', () => req.destroy(Error('Layer request timed out')));
        req.on('error', reject);
        req.on('close', () => layerRequests.delete(req));
    });
}
function listenLayer(command, path) {
    if (socket?.readyState === WebSocket.OPEN) socket.send(JSON.stringify({COMMAND: command, DATA: path}));
}
const layers = createLayerDesk({emit, edit, button, text, target, query: queryLayer,
    listen: listenLayer, sendCommand, isOnline: () => online});
function connect(nextHost, nextPort) {
    if (typeof nextHost !== 'string' || !/^[a-zA-Z0-9_.:\-]+$/.test(nextHost) || !Number.isInteger(nextPort) || nextPort < 1 || nextPort > 65535) {
        status('Enter a hostname/IP and an OSCQuery port from 1 to 65535.'); return;
    }
    ++generation;
    const token = generation;
    layers.reset();
    for (const req of layerRequests) req.destroy();
    layerRequests.clear(); catalogRefreshPending = false;
    if (request) { request.destroy(); request = null; }
    if (socket) socket.terminate();
    host = nextHost; port = nextPort;
    sentValues.clear();
    catalog = playback = null; selected = '0'; catalogKey = ''; lastCatalog = 0; catalogError = '';
    emit('/nyxmap/ui/host', host); emit('/nyxmap/ui/port', port);
    emit('/sequence/name', 'Disconnected'); emit('/sequence/go_enabled', 0); emit('/sequence/back_enabled', 0);
    status();
    const authority = host.includes(':') ? `[${host}]` : host;
    socket = new WebSocket(`ws://${authority}:${port}/`, {handshakeTimeout: 3000, maxPayload: 8 * 1024 * 1024});
    socket.on('open', () => {
        if (token !== generation) return;
        lastPong = Date.now();
        socket.send(JSON.stringify({COMMAND: 'LISTEN', DATA: '/show/state'}));
        fetchCatalog(true);
    });
    socket.on('pong', () => { if (token === generation) { lastPong = Date.now(); status(); } });
    socket.on('message', (data, binary) => {
        if (token !== generation) return;
        try {
            if (binary) {
                const packet = osc.readPacket(data, {metadata: true});
                const visit = message => {
                    if (message.packets) { message.packets.forEach(visit); return; }
                    if (message.address === '/show/state') publishState(JSON.parse(message.args[0].value));
                    else layers.feedback(message.address, (message.args || []).map(arg => arg.value));
                };
                visit(packet);
            } else if (JSON.parse(data.toString()).COMMAND === 'PATH_CHANGED') { layers.invalidate(); fetchCatalog(false); }
        } catch (error) { playback = null; status('Invalid NyxMap feedback: ' + error.message); }
    });
    socket.on('error', error => { if (token === generation) status('Connection error: ' + error.message); });
    socket.on('close', () => {
        if (token !== generation) return;
        catalog = playback = null; retryAt = Date.now() + 2000;
        layers.reset();
        status('DISCONNECTED · reconnecting');
    });
}
function sendCommand(address, args) {
    status();
    if (!online) { status('OFFLINE · command not sent'); return; }
    socket.send(osc.writePacket({address, args: args || []}, {metadata: true}));
}
function command(data) {
    const address = data.address;
    if (address === '/nyxmap/session/ready') {
        emit('/nyxmap/ui/host', host); emit('/nyxmap/ui/port', port); status();
        publishCatalog(true); if (playback) publishState(playback);
        layers.replay();
        for (const [address, value] of sentValues) receive(address, ...(Array.isArray(value) ? value : [value]));
    } else if (address === '/nyxmap/connect') {
        connect(String(arg(data, 0) || '').trim(), Number(arg(data, 1)));
    } else if (address === '/nyxmap/refresh') {
        layers.invalidate();
        fetchCatalog(true);
    } else if (address.startsWith('/nyxmap/layer/')) {
        layers.command(address, data.args.map(arg => arg.value));
    } else if (address === '/nyxmap/library/page') {
        const kind = arg(data, 0), delta = Number(arg(data, 1));
        if (!Object.prototype.hasOwnProperty.call(pages, kind) || ![1, -1].includes(delta)) return;
        pages[kind] += delta;
        if (kind === 'cues') renderCues(); else renderLibrary(kind);
    } else if (address === '/nyxmap/sequence/select' && id(arg(data, 0))) {
        sendCommand(`/sequence/by-id/${arg(data, 0)}/select`);
    } else if (/^\/nyxmap\/sequence\/(go|back|stop)$/.test(address) && id(selected)) {
        sendCommand(`/sequence/by-id/${selected}/${address.split('/').pop()}`);
    } else if (address === '/nyxmap/sequence/stop_all') {
        sendCommand('/sequence/stop_all');
    } else if (address === '/nyxmap/sequence/cue/go' && id(arg(data, 0)) && id(arg(data, 1))) {
        sendCommand(`/sequence/by-id/${arg(data, 0)}/cue/go`, [{type: 's', value: arg(data, 1)}]);
    } else if (address === '/nyxmap/recall' && ['snapshots', 'presets'].includes(arg(data, 0)) && id(arg(data, 1))) {
        sendCommand(`/${arg(data, 0).slice(0, -1)}/by-id/${arg(data, 1)}/recall`);
    } else if (address === '/nyxmap/timeline/control' && id(arg(data, 0)) && ['play', 'pause', 'stop', 'rewind'].includes(arg(data, 1))) {
        sendCommand(`/timeline/by-id/${arg(data, 0)}/${arg(data, 1)}`);
    } else if (/^\/nyxmap\/composition\/(pause|blackout)$/.test(address) && [0, 1, true, false].includes(arg(data, 0))) {
        sendCommand(address.replace('/nyxmap', ''), [{type: 'i', value: Number(arg(data, 0))}]);
    }
}
function start() {
    connect(host, port);
    timer = setInterval(() => {
        if (!socket || socket.readyState === WebSocket.CLOSED) { if (Date.now() >= retryAt) connect(host, port); return; }
        if (socket.readyState === WebSocket.OPEN) {
            if (Date.now() - lastPong > 6000) { socket.terminate(); status('Connection lost'); return; }
            socket.ping(); status();
            if (Date.now() - lastCatalog > 15000) fetchCatalog(false);
        }
    }, 2000);
}
function stop() {
    ++generation;
    layers.reset();
    for (const req of layerRequests) req.destroy();
    layerRequests.clear();
    if (timer) clearInterval(timer);
    if (request) request.destroy();
    if (socket) socket.terminate();
    request = socket = timer = null; catalog = playback = null; status('DISCONNECTED');
}
module.exports = {
    init: start, reload: start, stop, unload: stop,
    oscOutFilter(data) { if (data.address.startsWith('/nyxmap/')) { command(data); return; } return data; }
};
