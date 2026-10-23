const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {once} = require('node:events');
const fixture = require('./open_stage_control_fixture.cjs');
const path = require('node:path');
const root = path.resolve(__dirname, '../integrations/open-stage-control');
async function until(condition) {
    const deadline = Date.now() + 5000;
    while (!condition()) { if (Date.now() > deadline) assert.fail('Timed out waiting for controller state'); await new Promise(r => setTimeout(r, 10)); }
}
test('session uses unique widgets, explicit one-shot triggers and dedicated pages', () => {
    const session = JSON.parse(fs.readFileSync(path.join(root, 'pinol.json')));
    assert.equal(session.type, 'session');
    const ids = new Set();
    function walk(widget) {
        assert(!ids.has(widget.id), 'duplicate widget id ' + widget.id); ids.add(widget.id);
        if (widget.type === 'button' && widget.address !== '/pinol/noop') assert.equal(widget.mode, 'momentary');
        for (const child of [...(widget.widgets || []), ...(widget.tabs || [])]) walk(child);
    }
    walk(session.content);
    for (const id of ['show_tab', 'snapshots_tab', 'presets_tab', 'timelines_tab', 'connect_tab']) assert(ids.has(id));
});
test('custom module follows actual WebSocket OSC feedback without command echoes', async t => {
    const peer = fixture(); await once(peer.server, 'listening');
    const incoming = [];
    const context = {nativeRequire: require, module: {exports: {}}, console, setTimeout, clearTimeout, setInterval, clearInterval,
        receive: (...args) => incoming.push(args)};
    vm.runInNewContext(fs.readFileSync(path.join(root, 'pinol.js'), 'utf8'), context, {filename: 'pinol.js'});
    const controller = context.module.exports;
    t.after(async () => { controller.unload(); await peer.close(); });
    const send = (address, ...values) => controller.oscOutFilter({address, args: values.map(value => ({value, type: typeof value === 'number' ? 'i' : 's'}))});
    const value = address => incoming.filter(m => m[0] === address).at(-1)?.[1];
    send('/pinol/connect', '127.0.0.1', peer.server.address().port);
    await until(() => value('/pinol/ui/online') === 1);
    assert.equal(value('/sequence/cue/current/text'), 'Start the film');
    assert.equal(peer.commands.length, 0, 'feedback never recalls a cue');
    send('/pinol/sequence/go'); send('/pinol/recall', 'snapshots', '102');
    send('/pinol/timeline/control', '401', 'pause'); send('/pinol/sequence/cue/go', '501', '603');
    await until(() => peer.commands.length === 4);
    assert.deepEqual(peer.commands.map(m => m.address), ['/sequence/by-id/501/go', '/snapshot/by-id/102/recall', '/timeline/by-id/401/pause', '/sequence/by-id/501/cue/go']);
    assert.equal(peer.commands[3].args[0].value, '603');
    send('/pinol/library/page', 'snapshots', 1);
    const page = incoming.filter(m => m[0] === '/EDIT' && m[1] === 'snapshots_items').at(-1);
    const rows = JSON.parse(page[2]).widgets;
    assert.equal(rows.length, 6); assert.equal(rows[0].preArgs[1], '124');
    assert.equal(value('/pinol/library/snapshots/page'), '25–30 / 30');
    send('/pinol/session/ready');
    assert.equal(peer.commands.length, 4, 'late client synchronization sends no show command');
    for (const socket of peer.ws.clients) socket.close();
    await until(() => value('/pinol/ui/online') === 0);
    send('/pinol/sequence/go');
    assert.equal(peer.commands.length, 4, 'offline commands are dropped');
    assert.equal(send('/unrelated', 1).address, '/unrelated');
});
