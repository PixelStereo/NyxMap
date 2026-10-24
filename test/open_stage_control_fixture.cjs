const http = require('node:http');
const {WebSocketServer} = require('ws');
const osc = require('osc');
function fixture(port = 0) {
    const catalog = {version: 1,
        layers: [{id: '801', index: 1, name: 'Main image', parent: '0', group: false, viewport: false, path: '/show/layer/by-id/801'},
            {id: '802', index: 2, name: 'Background', parent: '0', group: false, viewport: false, path: '/show/layer/by-id/802'}],
        snapshots: Array.from({length: 30}, (_, i) => ({id: String(100 + i), index: i + 1, name: 'Look ' + (i + 1)})),
        presets: [{id: '301', index: 1, name: 'Warm look'}], timelines: [{id: '401', index: 1, name: 'Opening fade', duration: 12}],
        sequences: [{id: '501', index: 1, name: 'Evening show', cues: [
            {id: '601', index: 1, name: 'Opening look', text: 'House out', description: 'Stand by lighting', target_type: 'snapshot', continue: 'wait'},
            {id: '602', index: 2, name: 'Opening fade', text: 'Start the film', description: 'On the musical accent', target_type: 'timeline', continue: 'auto-follow'},
            {id: '603', index: 3, name: 'Warm look', text: 'Scene two', description: 'Hold for performer', target_type: 'preset', continue: 'wait'},
            {id: '604', index: 4, name: 'Opening look', text: 'Overlapping entrance', target_type: 'snapshot', continue: 'follow', postwait: 2.5}]}]};
    const values = {'/sequence/id': '501', '/sequence/name': 'Evening show', '/sequence/index': 1, '/sequence/running': 1,
        '/sequence/go_enabled': 1, '/sequence/back_enabled': 1, '/composition/pause': false, '/composition/blackout': false};
    for (const [role, index] of [['previous', 0], ['current', 1], ['next', 2]]) {
        const cue = catalog.sequences[0].cues[index], base = '/sequence/cue/' + role;
        for (const [key, value] of Object.entries({number: cue.index, name: cue.name, label: `${cue.index} · ${cue.name}`,
            text: cue.text, description: cue.description, phase: role === 'current' ? 'action' : role === 'previous' ? 'done' : '',
            elapsed_text: role === 'current' ? '0:04.2' : '0:00.0', remaining_text: role === 'current' ? '0:07.8' : '',
            prewait: 0, action: role === 'current' ? 12 : 0, postwait: 0,
            progress: role === 'current' ? .35 : role === 'previous' ? 1 : 0,
            phase_progress: role === 'current' ? .35 : role === 'previous' ? 1 : 0, continue: cue.continue})) values[base + '/' + key] = value;
    }
    const state = {version: 1, values, timelines: [{id: '401', state: 'playing', position: 4.2, duration: 12, elapsed: 4.2, remaining: 7.8, progress: .35, pass: 1, speed: 1}], sequences: [{id: '501', active: true}]};
    const nodes = new Map(), subscriptions = new Map();
    const trees = new Map();
    for (const entry of catalog.layers) {
        const root = {FULL_PATH: entry.path, CONTENTS: {}}; trees.set(entry.path, root);
        const leaf = (path, type, value, range, access = 3, description = path) => {
            let parent = root, full = entry.path;
            const parts = path.split('/');
            for (const part of parts) {
                full += '/' + part; parent.CONTENTS ||= {};
                parent.CONTENTS[part] ||= {FULL_PATH: full, CONTENTS: {}};
                parent = parent.CONTENTS[part];
            }
            Object.assign(parent, {TYPE: type, ACCESS: access, DESCRIPTION: description});
            if (value !== undefined) parent.VALUE = Array.isArray(value) ? value : [value];
            if (range) parent.RANGE = range;
            nodes.set(full, parent); return parent;
        };
        leaf('id', 's', entry.id, null, 1);
        leaf('name', 's', entry.name, null, 3, 'Name');
        leaf('type', 's', 'image', null, 1, 'Type');
        leaf('locked', 'T', false, null, 3, 'Locked');
        leaf('enable', 'T', true, null, 3, 'Enable');
        leaf('opacity', 'f', .65, [{MIN: 0, MAX: 1}], 3, 'Opacity');
        leaf('blend_mode', 's', 'normal', [{VALS: ['normal', 'add', 'multiply']}], 3, 'Blend Mode');
        leaf('position', 'ff', [960, 540], [{MIN: 0, MAX: 1920}, {MIN: 0, MAX: 1080}], 3, 'Position');
        leaf('position/x', 'f', 960, [{MIN: 0, MAX: 1920}], 3, 'Position X');
        leaf('position/y', 'f', 540, [{MIN: 0, MAX: 1080}], 3, 'Position Y');
        leaf('add', 'fff', [.1, .2, .3], Array(3).fill({MIN: 0, MAX: 1}), 3, 'Add Color');
        for (const [k, value] of [['r', .1], ['g', .2], ['b', .3]]) leaf('add/' + k, 'f', value, [{MIN: 0, MAX: 1}]);
        leaf('file', 's', '/media/image.png', null, 3, 'File');
        leaf('duration', 'f', 12, null, 1, 'Duration');
        leaf('restart', 'N', undefined, null, 2, 'Restart');
        leaf('fx/Blur/param/radius', 'f', 5, [{MIN: 0, MAX: 40}], 3, 'Blur radius');
    }
    const commands = [];
    const server = http.createServer((req, res) => {
        if (req.url === '/test/commands') { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(commands)); return; }
        if (trees.has(req.url)) { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(trees.get(req.url))); return; }
        if (req.url !== '/show/catalog?VALUE') { res.writeHead(404).end(); return; }
        res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify({VALUE: [JSON.stringify(catalog)]}));
    });
    const ws = new WebSocketServer({server});
    function publish(socket) { socket.send(osc.writePacket({address: '/show/state', args: [{type: 's', value: JSON.stringify(state)}]}, {metadata: true})); }
    function publishNode(address) {
        const node = nodes.get(address); if (!node || !node.VALUE) return;
        for (const socket of ws.clients) if (subscriptions.get(socket)?.has(address))
            socket.send(osc.writePacket({address, args: node.VALUE.map((value, k) => ({type: 'TF'.includes(node.TYPE[k]) ? (value ? 'T' : 'F') : node.TYPE[k], value}))}, {metadata: true}));
    }
    ws.on('connection', socket => {
        subscriptions.set(socket, new Set());
        socket.on('close', () => subscriptions.delete(socket));
        socket.on('message', (data, binary) => {
            if (binary) {
                const command = osc.readPacket(data, {metadata: true}); commands.push(command);
                const node = nodes.get(command.address);
                if (node && node.TYPE !== 'N') {
                    node.VALUE = command.args.map(arg => arg.value); publishNode(command.address);
                    for (const [part, k] of [['x', 0], ['y', 1], ['r', 0], ['g', 1], ['b', 2]])
                        if (node.CONTENTS?.[part] && k < node.VALUE.length) {
                            node.CONTENTS[part].VALUE = [node.VALUE[k]]; publishNode(command.address + '/' + part);
                        }
                }
            } else {
                const {COMMAND, DATA} = JSON.parse(data);
                if (COMMAND === 'IGNORE') subscriptions.get(socket).delete(DATA);
                else if (COMMAND === 'LISTEN') {
                    subscriptions.get(socket).add(DATA);
                    if (DATA === '/show/state') publish(socket); else publishNode(DATA);
                }
            }
        });
    });
    server.listen(port, '127.0.0.1');
    return {server, ws, catalog, state, commands, publish, nodes, trees, subscriptions, publishNode,
        close: async () => { for (const c of ws.clients) c.terminate(); await new Promise(r => server.close(r)); ws.close(); }};
}
module.exports = fixture;
if (require.main === module) fixture(5678);
