// Exercise the files a user obtains through git or a release, outside the
// checkout and with no NODE_PATH, npm dependencies or Open Stage Control install.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const {isBuiltin} = require('node:module');
const {execFileSync} = require('node:child_process');
const test = require('node:test');
if (process.argv[2] === '--isolated') {
    const root = process.argv[3];
    assert(!fs.existsSync(path.join(root, 'node_modules')));
    const nativeRequire = name => { assert(isBuiltin(name), 'Runtime tried to load an external npm dependency: ' + name); return require(name); };
    const sandbox = {nativeRequire, module: {exports: {}}, Buffer, process, console,
        setTimeout, clearTimeout, setInterval, clearInterval, setImmediate, clearImmediate};
    sandbox.require = file => {
        assert(['./pinol-dependencies.cjs', './pinol-layers.cjs'].includes(file));
        const child = {...sandbox, module: {exports: {}}};
        vm.runInNewContext(fs.readFileSync(path.join(root, file), 'utf8'), child, {filename: file});
        return child.module.exports;
    };
    const {WebSocket, osc} = sandbox.require('./pinol-dependencies.cjs');
    assert.equal(typeof WebSocket, 'function');
    assert.equal(typeof WebSocket.Server, 'function');
    const packet = osc.readPacket(osc.writePacket({address: '/roundtrip', args: [{type: 's', value: 'Ready'}, {type: 'f', value: .5}]}, {metadata: true}), {metadata: true});
    assert.equal(packet.address, '/roundtrip');
    assert.equal(packet.args[1].value, .5);
    vm.runInNewContext(fs.readFileSync(path.join(root, 'pinol.js'), 'utf8'), {...sandbox, receive: () => {}}, {filename: 'pinol.js'});
    assert.equal(typeof sandbox.module.exports.oscOutFilter, 'function');
    sandbox.module.exports.unload();
} else {
    test('a fresh checkout loads without npm or machine-specific paths', () => {
        const root = path.resolve(__dirname, '../integrations/open-stage-control');
        const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'pinol-remote-'));
        try {
            for (const name of ['pinol.json', 'pinol.js', 'pinol-layers.cjs', 'pinol-dependencies.cjs', 'pinol-dependencies.LICENSE.txt'])
                fs.copyFileSync(path.join(root, name), path.join(temporary, name));
            execFileSync(process.execPath, [__filename, '--isolated', temporary], {
                cwd: temporary, env: {...process.env, NODE_PATH: ''}, stdio: 'pipe', timeout: 10000
            });
        } finally { fs.rmSync(temporary, {recursive: true, force: true}); }
    });
}
