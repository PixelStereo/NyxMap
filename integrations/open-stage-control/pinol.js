// Open Stage Control 1.x custom module. Uses ws/osc from the controller pack.
// Pinol state remains authoritative; feedback never replays a show command.
const WebSocket = nativeRequire('ws');
const osc = nativeRequire('osc');
const http = nativeRequire('http');
const {Buffer} = nativeRequire('buffer');
const PAGE_SIZE = 24;
let host = '127.0.0.1', port = 5678, socket, timer, request, generation = 0;
let catalog, playback, selected = '0', lastPong = 0, online = false, catalogKey = '';
let pages = {snapshots: 0, presets: 0, timelines: 0, cues: 0};
let retryAt = 0, lastCatalog = 0, catalogError = '';
const id = value => typeof value === 'string' && /^[1-9][0-9]*$/.test(value);
const sentValues = new Map();
function emit(address, value, force = false) {
    if (!force && sentValues.has(address) && sentValues.get(address) === value) return;
    if (sentValues.size > 512) sentValues.clear();
    sentValues.set(address, value); receive(address, value);
}
const edit = (widget, properties) => receive('/EDIT', widget, JSON.stringify(properties));
const arg = (data, index) => data.args[index] && data.args[index].value;
const target = {target: '127.0.0.1:1234', ignoreDefaults: true};
const active = 'JS{return !!OSC{/pinol/ui/online,0,false}}';

function status(text) {
    const ready = !!(socket && socket.readyState === WebSocket.OPEN && catalog && playback && Date.now() - lastPong < 6000);
    if (online !== ready) { online = ready; emit('/pinol/ui/online', ready ? 1 : 0); }
    emit('/pinol/ui/status', text || catalogError || (ready ? `CONNECTED · ${host}:${port}` : `CONNECTING · ${host}:${port}`));
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
    emit(`/pinol/library/${kind}/page`, list.length ? `${start + 1}–${Math.min(start + PAGE_SIZE, list.length)} / ${list.length}` : 'EMPTY');
    return list.slice(start, start + PAGE_SIZE);
}
function renderLibrary(kind) {
    if (!catalog) return;
    const entries = range(kind, catalog[kind] || []);
    const widgets = entries.map(entry => {
        const key = `${kind}_${entry.id}`, label = `${entry.index} · ${entry.name || 'Untitled'}`;
        if (kind !== 'timelines')
            return button(key, label, '/pinol/recall', [kind, entry.id], {height: 76, colorWidget: kind === 'presets' ? '#e5ba71' : '#73cbd9'});
        const base = `/pinol/timeline/${entry.id}`;
        return {type: 'panel', id: key, label: false, layout: 'vertical', height: 166, width: '100%',
            colorBg: '#1a2330', contain: false, css: 'min-height:166rem!important;', padding: 8, widgets: [
                text(key + '_name', label, {align: 'left', css: 'font-weight:600;font-size:17px;'}),
                text(key + '_clock', 'Stopped', {address: base + '/clock', css: 'font-family:monospace;'}),
                {type: 'fader', id: key + '_progress', address: base + '/progress', horizontal: true, compact: true,
                    height: 12, width: '100%', interaction: false, bypass: true, input: false, range: {min: 0, max: 1}, colorWidget: '#73cbd9'},
                {type: 'panel', id: key + '_buttons', label: false, layout: 'horizontal', height: 44, width: '100%', padding: 3,
                    widgets: ['play', 'pause', 'stop'].map(action => button(key + '_' + action, action.toUpperCase(),
                        '/pinol/timeline/control', [entry.id, action], {height: 40, width: 'auto', expand: true,
                            colorWidget: action === 'stop' ? '#e9818b' : '#73cbd9'}))}
            ]};
    });
    edit(kind + '_items', {widgets: widgets.length ? widgets : [text(kind + '_empty', 'No ' + kind + ' in this project.')]});
    emit('/pinol/ui/online', online ? 1 : 0, true);
    if (playback) publishTimelineValues(true);
}
function renderCues() {
    const sequence = catalog && catalog.sequences.find(entry => entry.id === selected);
    const entries = range('cues', sequence ? sequence.cues : []);
    const widgets = entries.map(cue => {
        const number = Number(cue.index);
        const name = cue.text || cue.name || (cue.missing ? 'Missing target' : 'Empty cue');
        const details = [cue.target_type, cue.name, cue.continue, cue.description].filter(Boolean).join(' · ');
        return button('cue_' + cue.id, `${cue.index} · ${name}\n${details}`, '/pinol/sequence/cue/go', [selected, cue.id], {
            height: 76, align: 'left',
            colorWidget: `JS{return OSC{/sequence/cue/current/number,0,false} === ${number} ? '#78dfb5' : '#516078'}`,
            css: 'font-size:15px;text-align:left;'});
    });
    edit('cue_items', {widgets: widgets.length ? widgets : [text('cues_empty', 'Select a sequence containing cues.')]});
    emit('/pinol/ui/online', online ? 1 : 0, true);
    if (playback) emit('/sequence/cue/current/number', playback.values['/sequence/cue/current/number'], true);
}
function publishCatalog(force) {
    if (!catalog) return;
    const key = JSON.stringify(catalog);
    if (!force && key === catalogKey) return;
    catalogKey = key;
    edit('sequence_select', {values: {labels: catalog.sequences.map(s => `${s.index} · ${s.name || 'Untitled'}`),
        values: catalog.sequences.map(s => s.id)}});
    emit('/pinol/sequence/select', selected, true);
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
        const base = `/pinol/timeline/${timeline.id}`;
        emit(base + '/progress', timeline.progress, force);
        emit(base + '/clock', `${timeline.state.toUpperCase()} · ${clock(timeline.position)} / ${clock(timeline.duration)} · Pass ${timeline.pass} · Left ${clock(timeline.remaining)}`, force);
    }
}
function publishState(state) {
    if (!state || state.version !== 1 || !state.values || !Array.isArray(state.timelines)) throw Error('Unsupported Pinol state');
    const oldSelected = selected;
    const oldCue = playback && playback.values['/sequence/cue/current/number'];
    selected = String(state.values['/sequence/id'] || '0');
    playback = state;
    for (const address of Object.keys(state.values)) emit(address, state.values[address]);
    for (const property of ['pause', 'blackout']) emit('/pinol/composition/' + property, state.values['/composition/' + property]);
    emit('/pinol/sequence/select', selected);
    const cue = Math.max(0, Number(state.values['/sequence/cue/current/number']) - 1) || 0;
    if (selected !== oldSelected || (oldCue !== state.values['/sequence/cue/current/number'] && Math.floor(cue / PAGE_SIZE) !== pages.cues)) {
        pages.cues = Math.floor(cue / PAGE_SIZE); renderCues();
    }
    emit('/pinol/ui/background', (state.sequences || []).filter(s => s.active && s.id !== selected).length);
    publishTimelineValues();
    status();
}
function fetchCatalog(force) {
    if (request || !socket || socket.readyState !== WebSocket.OPEN) return;
    const token = generation;
    request = http.get({hostname: host, port, path: '/show/catalog?VALUE', timeout: 2500}, response => {
        let body = '', length = 0;
        response.setEncoding('utf8');
        response.on('data', chunk => {
            length += Buffer.byteLength(chunk);
            if (length > 4 * 1024 * 1024) { response.destroy(Error('Pinol catalog is too large')); return; }
            body += chunk;
        });
        response.on('error', () => {});
        response.on('end', () => {
            if (token !== generation) return;
            try {
                if (response.statusCode !== 200) throw Error('Update Pinol: show-control API unavailable');
                const parsed = JSON.parse(JSON.parse(body).VALUE[0]);
                if (parsed.version !== 1 || !['sequences', 'snapshots', 'presets', 'timelines'].every(k => Array.isArray(parsed[k])))
                    throw Error('Unsupported Pinol catalog');
                catalog = parsed; lastCatalog = Date.now(); catalogError = '';
                publishCatalog(force); status();
            } catch (error) { catalog = null; catalogError = error.message; status(); }
        });
    });
    request.on('timeout', () => request && request.destroy(Error('OSCQuery request timed out')));
    request.on('error', error => { if (token === generation) { catalog = null; catalogError = error.message; status(); } });
    request.on('close', () => { if (token === generation) request = null; });
}
function connect(nextHost, nextPort) {
    if (typeof nextHost !== 'string' || !/^[a-zA-Z0-9_.:\-]+$/.test(nextHost) || !Number.isInteger(nextPort) || nextPort < 1 || nextPort > 65535) {
        status('Enter a hostname/IP and an OSCQuery port from 1 to 65535.'); return;
    }
    ++generation;
    const token = generation;
    if (request) { request.destroy(); request = null; }
    if (socket) socket.terminate();
    host = nextHost; port = nextPort;
    sentValues.clear();
    catalog = playback = null; selected = '0'; catalogKey = ''; lastCatalog = 0; catalogError = '';
    emit('/pinol/ui/host', host); emit('/pinol/ui/port', port);
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
                };
                visit(packet);
            } else if (JSON.parse(data.toString()).COMMAND === 'PATH_CHANGED') fetchCatalog(false);
        } catch (error) { playback = null; status('Invalid Pinol feedback: ' + error.message); }
    });
    socket.on('error', error => { if (token === generation) status('Connection error: ' + error.message); });
    socket.on('close', () => {
        if (token !== generation) return;
        catalog = playback = null; retryAt = Date.now() + 2000;
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
    if (address === '/pinol/session/ready') {
        emit('/pinol/ui/host', host); emit('/pinol/ui/port', port); status();
        publishCatalog(true); if (playback) publishState(playback);
        for (const [address, value] of sentValues) receive(address, value);
    } else if (address === '/pinol/connect') {
        connect(String(arg(data, 0) || '').trim(), Number(arg(data, 1)));
    } else if (address === '/pinol/refresh') {
        fetchCatalog(true);
    } else if (address === '/pinol/library/page') {
        const kind = arg(data, 0), delta = Number(arg(data, 1));
        if (!Object.prototype.hasOwnProperty.call(pages, kind) || ![1, -1].includes(delta)) return;
        pages[kind] += delta;
        if (kind === 'cues') renderCues(); else renderLibrary(kind);
    } else if (address === '/pinol/sequence/select' && id(arg(data, 0))) {
        sendCommand(`/sequence/by-id/${arg(data, 0)}/select`);
    } else if (/^\/pinol\/sequence\/(go|back|stop)$/.test(address) && id(selected)) {
        sendCommand(`/sequence/by-id/${selected}/${address.split('/').pop()}`);
    } else if (address === '/pinol/sequence/stop_all') {
        sendCommand('/sequence/stop_all');
    } else if (address === '/pinol/sequence/cue/go' && id(arg(data, 0)) && id(arg(data, 1))) {
        sendCommand(`/sequence/by-id/${arg(data, 0)}/cue/go`, [{type: 's', value: arg(data, 1)}]);
    } else if (address === '/pinol/recall' && ['snapshots', 'presets'].includes(arg(data, 0)) && id(arg(data, 1))) {
        sendCommand(`/${arg(data, 0).slice(0, -1)}/by-id/${arg(data, 1)}/recall`);
    } else if (address === '/pinol/timeline/control' && id(arg(data, 0)) && ['play', 'pause', 'stop', 'rewind'].includes(arg(data, 1))) {
        sendCommand(`/timeline/by-id/${arg(data, 0)}/${arg(data, 1)}`);
    } else if (/^\/pinol\/composition\/(pause|blackout)$/.test(address) && [0, 1, true, false].includes(arg(data, 0))) {
        sendCommand(address.replace('/pinol', ''), [{type: 'i', value: Number(arg(data, 0))}]);
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
    if (timer) clearInterval(timer);
    if (request) request.destroy();
    if (socket) socket.terminate();
    request = socket = timer = null; catalog = playback = null; status('DISCONNECTED');
}
module.exports = {
    init: start, reload: start, stop, unload: stop,
    oscOutFilter(data) { if (data.address.startsWith('/pinol/')) { command(data); return; } return data; }
};
