// Exercise the real Companion SDK and, after packaging, the distributable entrypoint.
const test = require('node:test');
const assert = require('node:assert/strict');
const {fork} = require('node:child_process');
const {once} = require('node:events');
const path = require('node:path');
const ejson = require('ejson');
const fixture = require('./open_stage_control_fixture.cjs');
async function until(condition) {
    const deadline = Date.now() + 10000;
    while (!condition()) { if (Date.now() > deadline) assert.fail('Timed out waiting for Companion SDK state'); await new Promise(r => setTimeout(r, 20)); }
}
test('real Companion IPC runtime exposes presets, sends stable actions and updates live timers', async t => {
    const peer = fixture(); await once(peer.server, 'listening');
    const root = path.resolve(__dirname, '../integrations/companion');
    const packed = process.env.PINOL_COMPANION_PACKED === '1';
    const entry = path.join(root, packed ? 'pkg/main.js' : 'src/main.js');
    const manifest = path.join(root, packed ? 'pkg/companion/manifest.json' : 'companion/manifest.json');
    const child = fork(entry, [], {stdio: ['ignore', 'pipe', 'pipe', 'ipc'], env: {...process.env,
        MODULE_MANIFEST: manifest, CONNECTION_ID: 'pinol-test', VERIFICATION_TOKEN: 'pinol-ci', NODE_PATH: packed ? '' : process.env.NODE_PATH}});
    let output = '', next = 1000, registered = false;
    const pending = new Map(), messages = [], variables = {};
    child.stdout.on('data', b => { output += b; }); child.stderr.on('data', b => { output += b; });
    child.on('message', msg => {
        if (msg.direction === 'response') { pending.get(msg.callbackId)?.(msg); return; }
        const payload = msg.payload ? ejson.parse(msg.payload) : undefined;
        messages.push({name: msg.name, payload});
        if (msg.name === 'setVariableValues' || msg.name === 'setVariableDefinitions')
            for (const v of payload.newValues || []) variables[v.id] = v.value;
        if (msg.callbackId) child.send({direction: 'response', callbackId: msg.callbackId, success: true, payload: ejson.stringify({})});
        if (msg.name === 'register') registered = true;
    });
    const call = (name, payload) => new Promise((resolve, reject) => {
        const id = ++next, timeout = setTimeout(() => { pending.delete(id); reject(Error('IPC timeout: ' + name + '\n' + output)); }, 5000);
        pending.set(id, msg => { clearTimeout(timeout); pending.delete(id); if (!msg.success) reject(Error(msg.payload)); else resolve(msg.payload ? ejson.parse(msg.payload) : undefined); });
        child.send({direction: 'call', name, callbackId: id, payload: ejson.stringify(payload)});
    });
    t.after(async () => {
        if (child.connected) await call('destroy', {}).catch(() => {});
        child.kill(); await peer.close();
    });
    try {
        await until(() => registered);
        await call('init', {label: 'Pinol', isFirstInit: false, lastUpgradeIndex: -1, secrets: {}, actions: {}, feedbacks: {},
            config: {host: '127.0.0.1', port: peer.server.address().port, preset_limit: 16}});
        await until(() => variables.connected === true && variables.current_elapsed_text === '0:04.2');
        const presets = messages.filter(m => m.name === 'setPresetDefinitions').at(-1).payload.presets;
        assert(presets.some(p => p.id === 'go')); assert(presets.some(p => p.id === 'remaining'));
        const execute = (actionId, options) => call('executeAction', {surfaceId: 'streamdeck', action: {id: 'ci-action', actionId, options, controlId: 'bank:1', disabled: false, upgradeIndex: -1}});
        assert.equal(peer.commands.length, 0);
        assert.equal((await execute('sequence_go', {sequence: 'selected'})).success, true);
        await until(() => peer.commands.length === 1);
        assert.equal(peer.commands[0].address, '/sequence/by-id/501/go');
        assert.equal((await execute('cue_go', {cue: '501:603'})).success, true);
        await until(() => peer.commands.length === 2);
        assert.equal(peer.commands[1].args[0].value, '603');
        const denied = await execute('stop_all_waits', {confirm: false}); assert.equal(denied.success, false);
        await call('updateFeedbacks', {feedbacks: {meter: {id: 'meter', controlId: 'bank:2', feedbackId: 'cue_progress', disabled: false,
            upgradeIndex: -1, isInverted: false, image: {width: 72, height: 72}, options: {role: 'current', basis: 'phase'}}}});
        await until(() => messages.some(m => m.name === 'updateFeedbackValues' && m.payload.values.some(v => v.id === 'meter' && v.value?.imageBuffer)));
        peer.state.values['/sequence/cue/current/elapsed_text'] = '0:05.5';
        for (const socket of peer.ws.clients) peer.publish(socket);
        await until(() => variables.current_elapsed_text === '0:05.5');
        assert.equal(peer.commands.length, 2, 'timing feedback does not execute a cue');
        peer.catalog.snapshots = [];
        for (const socket of peer.ws.clients) socket.send(JSON.stringify({COMMAND: 'PATH_CHANGED', DATA: '/'}));
        await until(() => messages.filter(m => m.name === 'setActionDefinitions').at(-1)?.payload.actions.find(a => a.id === 'snapshot_recall').options[0].choices.length === 0);
        assert.equal((await execute('snapshot_recall', {id: '100'})).success, false);
        for (const socket of peer.ws.clients) socket.close();
        await until(() => variables.connected === false);
        assert.equal(variables.current_elapsed_text, '--');
        assert.equal((await execute('sequence_go', {sequence: 'selected'})).success, false);
        await until(() => variables.connected === true);
        assert.equal(peer.commands.length, 2, 'offline actions are never replayed');
    } catch (error) { console.error(output); throw error; }
});
