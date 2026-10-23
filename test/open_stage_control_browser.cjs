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
    let page;
    try {
        page = await browser.newPage({viewport: {width: 1440, height: 1080}});
        const button = label => page.locator('.button-container:visible').filter({has: page.getByText(label, {exact: true})});
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.goto('http://127.0.0.1:8080');
        await page.getByText(/^CONNECTED ·/).waitFor({timeout: 40000});
        await page.getByText('Start the film', {exact: true}).waitFor();
        await page.getByText('0:04.2', {exact: true}).waitFor();
        const capture = name => page.screenshot({path: path.join(process.env.RUNNER_TEMP, 'pinol-controller-' + name + '.png')});
        const cards = await page.locator('.pinol-cue').all();
        assert.equal(cards.length, 3);
        const [previous, current, next] = await Promise.all(cards.map(card => card.boundingBox()));
        assert.ok(previous.y < current.y && current.y < next.y, 'Cues stack previous/current/next vertically');
        assert.ok(current.height > previous.height, 'The current cue has visual priority');
        await capture('show');
        await button('GO ▶').click();
        await commandSeen('/sequence/by-id/501/go');
        await page.getByText('SNAPSHOTS', {exact: true}).first().click();
        await page.getByText('1 · Look 1', {exact: true}).waitFor();
        assert.ok((await button('1 · Look 1').boundingBox()).height >= 60, 'Snapshot buttons remain touch-sized');
        await capture('snapshots');
        await button('NEXT ›').click();
        await button('25 · Look 25').click();
        await commandSeen('/snapshot/by-id/124/recall');
        await button('‹ PREVIOUS').click();
        await button('1 · Look 1').click();
        await commandSeen('/snapshot/by-id/100/recall');
        await page.getByText('PRESETS', {exact: true}).first().click();
        await page.getByText('1 · Warm look', {exact: true}).waitFor();
        await capture('presets');
        await page.getByText('TIMELINES', {exact: true}).first().click();
        await page.getByText('1 · Opening fade', {exact: true}).waitFor();
        await capture('timelines');
        await button('PAUSE').click();
        await commandSeen('/timeline/by-id/401/pause');
        await page.getByText('SHOW', {exact: true}).first().click();
        await page.setViewportSize({width: 768, height: 1024});
        await capture('tablet');
        assert.deepEqual(errors, []);
    } catch (error) {
        if (page) await page.screenshot({path: path.join(process.env.RUNNER_TEMP, 'pinol-controller-failure.png')}).catch(() => {});
        throw error;
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
