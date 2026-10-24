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
        await page.getByText('Loading session…', {exact: true}).waitFor({state: 'hidden'});
        await page.getByText('Start the film', {exact: true}).waitFor();
        await page.waitForFunction(() => {
            const canvas = document.querySelectorAll('.pinol-cue-fill canvas')[1];
            return canvas && canvas.height > 50 && canvas.width > 50 &&
                canvas.getContext('2d').getImageData(Math.floor(canvas.width * .2), Math.floor(canvas.height * .5), 1, 1).data[3] > 0;
        }, null, {timeout: 10000});
        await page.getByText('0:04.2', {exact: true}).waitFor();
        await page.waitForFunction(() => {
            const c = document.querySelector('.pinol-phase-meter canvas');
            if (!c || c.width < 10 || c.height < 1) return false;
            const alpha = x => c.getContext('2d').getImageData(Math.floor(c.width * x), Math.floor(c.height * .5), 1, 1).data[3];
            return alpha(.2) > alpha(.8);
        }, null, {timeout: 10000});
        const capture = name => page.screenshot({path: path.join(process.env.RUNNER_TEMP, 'pinol-controller-' + name + '.png')});
        const cards = await page.locator('.pinol-cue').all();
        assert.equal(cards.length, 3);
        const [previous, current, next] = await Promise.all(cards.map(card => card.boundingBox()));
        assert.ok(previous.y < current.y && current.y < next.y, 'Cues stack previous/current/next vertically');
        assert.ok(current.height > previous.height, 'The current cue has visual priority');
        const cueText = await page.getByText('Start the film', {exact: true}).boundingBox();
        assert.ok(cueText.y >= current.y && cueText.y + cueText.height <= current.y + current.height, 'Cue text remains inside its card above the progress fill');
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
        await page.getByText('LAYERS', {exact: true}).first().click();
        const layerMenus = page.locator('.dropdown-container:visible select');
        await layerMenus.nth(0).locator('option').filter({hasText: 'Main image'}).waitFor({state: 'attached'});
        await layerMenus.nth(0).selectOption({index: 0});
        await page.getByText('Main image · LIVE EDIT', {exact: true}).waitFor();
        const layerCard = title => page.locator('.pinol-layer-card').filter({has: page.getByText(title, {exact: true})});
        const opacity = layerCard('Opacity').locator('input');
        await opacity.fill('0.42'); await opacity.press('Enter');
        await commandSeen('/show/layer/by-id/801/opacity');
        const readback = await opacity.inputValue();
        assert.ok(Math.abs(Number(readback) - .42) < .001, 'Layer opacity receives native feedback');
        await capture('layers');
        await layerMenus.nth(1).selectOption({label: 'Color'});
        const rgb = page.locator('.rgb-container canvas').first();
        await rgb.waitFor();
        const rgbBox = await rgb.boundingBox();
        await rgb.click({position: {x: rgbBox.width * .6, y: rgbBox.height * .4}});
        await commandSeen('/show/layer/by-id/801/add');
        await capture('layer-color');
        await layerMenus.nth(1).selectOption({label: 'Transform'});
        const xy = layerCard('Position').locator('.xy-container canvas').first();
        await xy.waitFor(); const xyBox = await xy.boundingBox();
        await xy.click({position: {x: xyBox.width * .4, y: xyBox.height * .6}});
        await commandSeen('/show/layer/by-id/801/position');
        await page.setViewportSize({width: 768, height: 1024});
        await capture('layer-tablet');
        await layerMenus.nth(1).selectOption({label: 'FX · Blur'});
        const radius = layerCard('Blur radius').locator('input');
        await radius.fill('18'); await radius.press('Enter');
        await commandSeen('/show/layer/by-id/801/fx/Blur/param/radius');
        const commands = await (await fetch('http://127.0.0.1:5678/test/commands')).json();
        assert.equal(commands.find(m => m.address === '/show/layer/by-id/801/add').args.length, 3);
        assert.equal(commands.find(m => m.address === '/show/layer/by-id/801/position').args.length, 2);
        await page.getByText('SHOW', {exact: true}).first().click();
        await page.setViewportSize({width: 768, height: 1024});
        await capture('tablet');
        assert.deepEqual(errors, []);
    } catch (error) {
        if (page) console.error(await page.locator('.pinol-cue-fill canvas').evaluateAll(nodes => nodes.map(c => ({width:c.width, height:c.height, box:c.getBoundingClientRect().toJSON(), parent:c.parentNode.getBoundingClientRect().toJSON()}))).catch(() => []));
        if (page) await page.screenshot({path: path.join(process.env.RUNNER_TEMP, 'pinol-controller-failure.png')}).catch(() => {});
        throw error;
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
