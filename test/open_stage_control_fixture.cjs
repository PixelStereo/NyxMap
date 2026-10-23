const http = require('node:http');
const {WebSocketServer} = require('ws');
const osc = require('osc');
function fixture(port = 0) {
    const catalog = {version: 1,
        snapshots: Array.from({length: 30}, (_, i) => ({id: String(100 + i), index: i + 1, name: 'Look ' + (i + 1)})),
        presets: [{id: '301', index: 1, name: 'Warm look'}], timelines: [{id: '401', index: 1, name: 'Opening fade', duration: 12}],
        sequences: [{id: '501', index: 1, name: 'Evening show', cues: [
            {id: '601', index: 1, name: 'Opening look', text: 'House out', description: 'Stand by lighting', target_type: 'snapshot', continue: 'wait'},
            {id: '602', index: 2, name: 'Opening fade', text: 'Start the film', description: 'On the musical accent', target_type: 'timeline', continue: 'auto-follow'},
            {id: '603', index: 3, name: 'Warm look', text: 'Scene two', description: 'Hold for performer', target_type: 'preset', continue: 'wait'}]}]};
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
    const commands = [];
    const server = http.createServer((req, res) => {
        if (req.url === '/test/commands') { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(commands)); return; }
        if (req.url !== '/show/catalog?VALUE') { res.writeHead(404).end(); return; }
        res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify({VALUE: [JSON.stringify(catalog)]}));
    });
    const ws = new WebSocketServer({server});
    function publish(socket) { socket.send(osc.writePacket({address: '/show/state', args: [{type: 's', value: JSON.stringify(state)}]}, {metadata: true})); }
    ws.on('connection', socket => socket.on('message', (data, binary) => {
        if (binary) commands.push(osc.readPacket(data, {metadata: true}));
        else if (JSON.parse(data).COMMAND === 'LISTEN') publish(socket);
    }));
    server.listen(port, '127.0.0.1');
    return {server, ws, catalog, state, commands, publish,
        close: async () => { for (const c of ws.clients) c.terminate(); await new Promise(r => server.close(r)); ws.close(); }};
}
module.exports = fixture;
if (require.main === module) fixture(5678);
