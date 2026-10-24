const {EventEmitter} = require('node:events');
const http = require('node:http');
const WebSocket = require('ws');
// Only the packet codec is needed; do not bundle OSC's optional serial/UDP transports.
const osc = require('osc/src/osc');

// One connection for commands and feedback. Offline presses are never queued.
class PinolClient extends EventEmitter {
    constructor() {
        super(); this.generation = 0; this.online = false; this.catalog = null; this.state = null;
    }
    connect(host, port) {
        this.stop();
        if (typeof host !== 'string' || !/^[a-zA-Z0-9_.:\-]+$/.test(host) || !Number.isInteger(port) || port < 1 || port > 65535)
            throw Error('Enter a hostname/IP and an OSCQuery port from 1 to 65535.');
        this.host = host; this.port = port; this.open();
        this.timer = setInterval(() => {
            if (!this.socket || this.socket.readyState === WebSocket.CLOSED) {
                if (Date.now() >= this.retryAt) this.open();
            } else if (this.socket.readyState === WebSocket.OPEN) {
                if (Date.now() - this.lastPong > 6000) { this.setOnline(false, 'Connection lost'); this.socket.terminate(); return; }
                this.socket.ping();
                if (Date.now() - this.lastCatalog > 15000) this.fetchCatalog();
            }
        }, 2000);
    }
    setOnline(value, message = value ? 'Connected' : 'Disconnected') {
        if (this.online === value && this.statusText === message) return;
        this.online = value; this.statusText = message; this.emit('status', value, message);
    }
    updateOnline() {
        const ready = !!(this.socket?.readyState === WebSocket.OPEN && this.catalog && this.state && Date.now() - this.lastPong < 6000);
        if (ready) this.setOnline(true);
        else if (this.online) this.setOnline(false, 'Waiting for Pinol catalog / state');
    }
    open() {
        const token = ++this.generation;
        if (this.request) this.request.destroy(); this.request = null;
        this.catalog = this.state = null; this.catalogKey = ''; this.lastCatalog = 0;
        this.setOnline(false, 'Connecting');
        const host = this.host.includes(':') ? `[${this.host}]` : this.host;
        const socket = this.socket = new WebSocket(`ws://${host}:${this.port}/`, {handshakeTimeout: 3000, maxPayload: 8 * 1024 * 1024});
        socket.on('open', () => {
            if (token !== this.generation) return;
            this.lastPong = Date.now();
            socket.send(JSON.stringify({COMMAND: 'LISTEN', DATA: '/show/state'}));
            this.fetchCatalog();
        });
        socket.on('pong', () => { if (token === this.generation) { this.lastPong = Date.now(); this.updateOnline(); } });
        socket.on('message', (data, binary) => {
            if (token !== this.generation) return;
            try {
                if (!binary) {
                    if (JSON.parse(data.toString()).COMMAND === 'PATH_CHANGED') this.fetchCatalog();
                    return;
                }
                const visit = packet => {
                    if (packet.packets) { packet.packets.forEach(visit); return; }
                    if (packet.address !== '/show/state') return;
                    const state = JSON.parse(packet.args[0].value);
                    if (state.version !== 1 || !state.values || !Array.isArray(state.timelines)) throw Error('Unsupported Pinol state');
                    this.state = state; this.emit('state', state); this.updateOnline();
                };
                visit(osc.readPacket(data, {metadata: true}));
            } catch (error) { this.state = null; this.setOnline(false, 'Invalid Pinol feedback: ' + error.message); }
        });
        socket.on('error', error => { if (token === this.generation) this.setOnline(false, error.message); });
        socket.on('close', () => {
            if (token !== this.generation) return;
            this.catalog = this.state = null; this.retryAt = Date.now() + 2000;
            this.setOnline(false, 'Disconnected; reconnecting');
        });
    }
    fetchCatalog() {
        if (this.request) { this.refreshPending = true; return; }
        if (this.socket?.readyState !== WebSocket.OPEN) return;
        const token = this.generation;
        const request = this.request = http.get({hostname: this.host, port: this.port, path: '/show/catalog?VALUE', timeout: 3000}, response => {
            let body = '', size = 0;
            response.setEncoding('utf8');
            response.on('data', chunk => {
                size += Buffer.byteLength(chunk);
                if (size > 4 * 1024 * 1024) { response.destroy(Error('Pinol catalog is too large')); return; }
                body += chunk;
            });
            const failed = error => { if (token === this.generation) { this.catalog = null; this.setOnline(false, error.message); } };
            response.on('error', failed);
            response.on('end', () => {
                if (token !== this.generation) return;
                try {
                    if (response.statusCode !== 200) throw Error('Update Pinol: show-control API unavailable');
                    const catalog = JSON.parse(JSON.parse(body).VALUE[0]);
                    if (catalog.version !== 1 || !['sequences', 'snapshots', 'presets', 'timelines'].every(key => Array.isArray(catalog[key]))) throw Error('Unsupported Pinol catalog');
                    this.catalog = catalog; this.lastCatalog = Date.now();
                    const key = JSON.stringify(catalog);
                    if (key !== this.catalogKey) { this.catalogKey = key; this.emit('catalog', catalog); }
                    this.updateOnline();
                } catch (error) { failed(error); }
            });
        });
        request.on('timeout', () => request.destroy(Error('OSCQuery request timed out')));
        request.on('error', error => { if (token === this.generation) { this.catalog = null; this.setOnline(false, error.message); } });
        request.on('close', () => {
            if (token !== this.generation) return;
            this.request = null;
            if (this.refreshPending) { this.refreshPending = false; this.fetchCatalog(); }
        });
    }
    send(address, args = []) {
        if (!this.online || this.socket?.readyState !== WebSocket.OPEN || Date.now() - this.lastPong >= 6000)
            throw Error('Pinol is offline; command was not sent.');
        this.socket.send(osc.writePacket({address, args}, {metadata: true}));
    }
    stop() {
        ++this.generation;
        clearInterval(this.timer); this.timer = null;
        if (this.request) this.request.destroy(); this.request = null;
        if (this.socket) this.socket.terminate(); this.socket = null;
        this.catalog = this.state = null; this.refreshPending = false;
        this.setOnline(false);
    }
}
module.exports = {PinolClient};
