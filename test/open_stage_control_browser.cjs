const {chromium} = require('playwright');
const assert = require('node:assert/strict');
const path = require('node:path');
async function waitForServer() {
    for (let k = 0; k < 100; ++k) {
        try { if ((await fetch('http://127.0.0.1:8080')).ok) return; } catch (_) {}
        await new Promise(r => setTimeout(r, 300));
    }
    throw Error('Open Stage Control server did not start');
}
async function commandSeen(address) {
    for (let k = 0; k < 100; ++k) {
        if ((await (await fetch('http://127.0.0.1:5678/test/commands')).json()).some(m => m.address === address)) return;
        await new Promise(r => setTimeout(r, 50));
    }
    assert.fail('Missing OSC command from actual browser: ' + address);
}
(async () => {
    await waitForServer();
    const browser = await chromium.launch({headless: true});
    try {
        const page = await browser.newPage({viewport: {width: 1440, height: 1080}});
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.goto('http://127.0.0.1:8080');
        await page.getByText('Evening show', {exact: false}).first().waitFor({timeout: 40000});
        await page.getByText('Start the film', {exact: true}).waitFor();
        await page.getByText('0:04.2', {exact: true}).waitFor();
        const capture = name => page.screenshot({path: path.join(process.env.RUNNER_TEMP, 'pinol-controller-' + name + '.png')});
        await capture('show');
        await page.getByText('GO ▶', {exact: true}).click();
        await commandSeen('/sequence/by-id/501/go');
        await page.getByText('SNAPSHOTS', {exact: true}).first().click();
        await page.getByText('1 · Look 1', {exact: true}).waitFor();
        await capture('snapshots');
        await page.getByText('1 · Look 1', {exact: true}).click();
        await commandSeen('/snapshot/by-id/100/recall');
        await page.getByText('PRESETS', {exact: true}).first().click();
        await page.getByText('1 · Warm look', {exact: true}).waitFor();
        await capture('presets');
        await page.getByText('TIMELINES', {exact: true}).first().click();
        await page.getByText('1 · Opening fade', {exact: true}).waitFor();
        await capture('timelines');
        await page.getByText('PAUSE', {exact: true}).click();
        await commandSeen('/timeline/by-id/401/pause');
        await page.getByText('SHOW', {exact: true}).first().click();
        await page.setViewportSize({width: 768, height: 1024});
        await capture('tablet');
        assert.deepEqual(errors, []);
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
