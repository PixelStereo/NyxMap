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
        for (const label of ['AUTO-FOLLOW · End + 0:00.0', 'WAIT · GO · Manual advance'])
            await page.getByText(label, {exact: true}).first().waitFor();
        await capture('show');
        const cueRows = page.locator('.pinol-cue-row');
        assert.equal(await cueRows.count(), 4);
        for (const [i, label] of [[0, 'WAIT · GO'], [1, 'AUTO-FOLLOW'], [3, 'FOLLOW']]) {
            const badge = cueRows.nth(i).locator('.pinol-continuation');
            await badge.scrollIntoViewIfNeeded();
            assert.ok((await badge.innerText()).includes(label));
            const box = await badge.boundingBox();
            assert.ok(box.width >= 150 && box.height >= 70, 'Continuation stays in a dedicated readable column');
        }
        await capture('cue-behavior');
        await page.setViewportSize({width: 768, height: 1024});
        const badge = cueRows.nth(3).locator('.pinol-continuation');
        await badge.scrollIntoViewIfNeeded();
        const rowBox = await cueRows.nth(3).boundingBox(), badgeBox = await badge.boundingBox();
        assert.ok(badgeBox.x >= rowBox.x && badgeBox.x + badgeBox.width <= rowBox.x + rowBox.width + 1, 'Tablet retains the continuation column');
        await capture('cue-behavior-tablet');
        await page.setViewportSize({width: 1440, height: 1080});
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
        const beforeNavigation = await (await fetch('http://127.0.0.1:5678/test/commands')).json();
        await button('LAYER CONTROL →').click();
        await page.getByText('LAYER REMOTE', {exact: true}).waitFor();
        assert.equal((await (await fetch('http://127.0.0.1:5678/test/commands')).json()).length, beforeNavigation.length,
            'Opening layer control never sends a Pinol show command');
        const layerMenus = page.locator('.dropdown-container:visible select');
        await layerMenus.nth(0).locator('option').filter({hasText: 'Main image'}).waitFor({state: 'attached'});
        await layerMenus.nth(0).selectOption({index: 0});
        await page.getByText('Main image · LIVE EDIT', {exact: true}).waitFor();
        const layerCard = title => page.locator('.pinol-layer-card').filter({has: page.getByText(title, {exact: true})});
        await layerCard('Locked').locator('.switch-container value.on').filter({hasText: 'OFF'}).waitFor();
        await layerCard('Enable').locator('.switch-container value.on').filter({hasText: 'ON'}).waitFor();
        const opacity = layerCard('Opacity').locator('input');
        assert.ok((await layerCard('Opacity').boundingBox()).height >= 176, 'Grid rows keep editor cards at a usable height');
        await layerCard('Opacity').locator('.input-container').click();
        await opacity.fill('0.42'); await opacity.press('Enter');
        await commandSeen('/show/layer/by-id/801/opacity');
        await layerCard('Opacity').locator('.input-container').click();
        const readback = await opacity.inputValue();
        assert.ok(Math.abs(Number(readback) - .42) < .001, 'Layer opacity receives native feedback');
        await opacity.press('Escape');
        await capture('layers');
        const opacityTitle = layerCard('Opacity').getByText('Opacity', {exact: true});
        await opacityTitle.click();
        await page.waitForTimeout(450); // single click must not become a destructive reset
        assert.equal((await (await fetch('http://127.0.0.1:5678/test/commands')).json()).filter(m => m.address.endsWith('/reset')).length, 0);
        await opacityTitle.dblclick();
        await commandSeen('/show/layer/by-id/801/opacity/reset');
        await layerCard('Opacity').locator('.input-container').click();
        assert.ok(Math.abs(Number(await opacity.inputValue()) - 1) < .001, 'Double-clicking the title restores the native default');
        await opacity.press('Escape');
        await layerMenus.nth(1).selectOption({label: 'Color'});
        const rgb = page.locator('.rgb-container canvas').first();
        await rgb.waitFor();
        const rgbBox = await rgb.boundingBox();
        await rgb.click({position: {x: rgbBox.width * .6, y: rgbBox.height * .4}});
        await commandSeen('/show/layer/by-id/801/add');
        for (const channel of ['R', 'G', 'B']) await layerCard('Add Color').getByText(channel, {exact: true}).waitFor();
        const colorCommands = await (await fetch('http://127.0.0.1:5678/test/commands')).json();
        const channels = colorCommands.filter(m => m.address === '/show/layer/by-id/801/add').at(-1).args.map(arg => arg.value);
        await layerCard('Add Color').locator('.input-container').first().click();
        const red = layerCard('Add Color').locator('input');
        assert.ok(Math.abs(Number(await red.inputValue()) - channels[0]) < .002, 'RGB channel fields follow the color picker feedback');
        await red.press('Escape');
        await layerCard('Add Color').getByText('R', {exact: true}).dblclick();
        await commandSeen('/show/layer/by-id/801/add/r/reset');
        await layerCard('Add Color').getByText('Add Color', {exact: true}).dblclick();
        await commandSeen('/show/layer/by-id/801/add/reset');
        await capture('layer-color');
        await layerMenus.nth(1).selectOption({label: 'Transform'});
        const xy = layerCard('Position').locator('.xy-container canvas').first();
        await xy.waitFor(); const xyBox = await xy.boundingBox();
        await xy.click({position: {x: xyBox.width * .4, y: xyBox.height * .6}});
        await commandSeen('/show/layer/by-id/801/position');
        await layerCard('Position').getByText('Position', {exact: true}).dblclick();
        await commandSeen('/show/layer/by-id/801/position/reset');
        await page.setViewportSize({width: 768, height: 1024});
        await capture('layer-tablet');
        await layerMenus.nth(1).selectOption({label: 'FX · Blur'});
        const radius = layerCard('Blur radius').locator('input');
        await layerCard('Blur radius').locator('.input-container').click();
        await radius.fill('18'); await radius.press('Enter');
        await commandSeen('/show/layer/by-id/801/fx/Blur/param/radius');
        await radius.press('Escape');
        await layerCard('Blur radius').getByText('Blur radius', {exact: true}).dblclick();
        await commandSeen('/show/layer/by-id/801/fx/Blur/param/radius/reset');
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
