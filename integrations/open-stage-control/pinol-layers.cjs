// Metadata-driven layer desk. Only the displayed controls subscribe to feedback.
module.exports = function createLayerDesk({emit, edit, button, text, target, query, listen, sendCommand, isOnline}) {
    const PAGE = 12;
    let entries = [], selected = '', section = '', page = 0, serial = 0, epoch = 0;
    let nodes = new Map(), bindings = new Map(), subscribed = new Set(), ready = false, locked = false;
    let listKey = '', layoutKey = '';
    const current = () => entries.find(entry => entry.id === selected);
    const allowedWhenLocked = path => ['enable', 'solo', 'testcard', 'locked', 'play', 'restart', 'time'].includes(path);
    const interaction = path => `JS{return !!OSC{/pinol/ui/online,0,false} && !!OSC{/pinol/layer/ready,0,false}${allowedWhenLocked(path) ? '' : ' && !OSC{/pinol/layer/locked,0,false}'}}`;
    function subscriptions(paths) {
        const next = new Set(paths);
        for (const path of subscribed) if (!next.has(path)) listen('IGNORE', path);
        for (const path of next) if (!subscribed.has(path)) listen('LISTEN', path);
        subscribed = next;
    }
    function invalidate(message = 'Updating layer controls…') {
        ++serial; ready = false; bindings.clear(); subscriptions([]);
        emit('/pinol/layer/ready', 0);
        emit('/pinol/layer/status', message);
    }
    function reset() {
        invalidate('Select a layer after connecting.');
        entries = []; selected = ''; nodes.clear(); listKey = layoutKey = '';
        edit('layer_select', {values: {labels: [], values: []}});
        edit('layer_sections', {values: {labels: [], values: []}});
        edit('layer_items', {widgets: [text('layer_empty', 'Connect to Pinol to edit layers.')]});
    }
    function setCatalog(layers, force = false) {
        if (!Array.isArray(layers)) {
            reset(); emit('/pinol/layer/status', 'Update Pinol to use the layer remote.'); return;
        }
        entries = layers.filter(entry => /^[1-9][0-9]*$/.test(entry.id) && entry.path === '/show/layer/by-id/' + entry.id);
        const key = JSON.stringify(entries);
        if (force || key !== listKey) {
            listKey = key;
            const byId = new Map(entries.map(entry => [entry.id, entry]));
            const label = entry => {
                const names = [entry.name || 'Untitled'], visited = new Set([entry.id]);
                let parent = byId.get(entry.parent);
                while (parent && !visited.has(parent.id)) {
                    visited.add(parent.id); names.unshift(parent.name || 'Untitled'); parent = byId.get(parent.parent);
                }
                return `${entry.index} · ${names.join(' / ')}${entry.viewport ? ' · OUTPUT' : entry.group ? ' · GROUP' : ''}`;
            };
            edit('layer_select', {values: {labels: entries.map(label), values: entries.map(entry => entry.id)}});
        }
        if (!current()) {
            // Never silently replace a removed selection with a different layer.
            selected = ''; invalidate(entries.length ? 'Choose a layer to edit.' : 'No layers in this project.');
            edit('layer_items', {widgets: [text('layer_empty', entries.length ? 'Choose a layer above.' : 'No layers in this project.')]});
            edit('layer_sections', {values: {labels: [], values: []}});
        } else if (force || !ready || key !== layoutKey) load();
        emit('/pinol/layer/select', selected, true);
    }
    function group(path, node) {
        if (path.includes('/anim/')) return ['animation', 'Parameter animation'];
        if (path.startsWith('fx/')) {
            const name = path.split('/')[1];
            return name === 'enable' ? ['effects', 'Effects'] : ['fx/' + name, 'FX · ' + name.replaceAll('_', ' ')];
        }
        if (/^(position|scale|pivot|corner|roi)(\/|$)/.test(path) || ['rotation', 'width', 'height'].includes(path)) return ['transform', 'Transform'];
        if (/^(color|temp|tint|add|remove|mask)(\/|$)/.test(path) || (node.DESCRIPTION || '').startsWith('Color ›')) return ['color', 'Color'];
        if (/^(param|source|text|clock|tc|tox|web)(\/|$)/.test(path) || ['file', 'source_layer', 'source_tap'].includes(path)) return ['source', 'Source'];
        if (/^(viewport|output)(\/|$)/.test(path)) return ['routing', 'Output & routing'];
        if (['play', 'restart', 'time', 'duration', 'speed', 'in', 'out', 'play_mode', 'volume', 'mute'].includes(path)) return ['playback', 'Playback'];
        return ['general', 'General'];
    }
    function tuple(node) {
        const children = node.CONTENTS || {};
        if (/^ffff?$/.test(node.TYPE) && ['r', 'g', 'b'].every(key => children[key]))
            return {type: 'rgb', keys: node.TYPE.length === 4 ? ['r', 'g', 'b', 'a'] : ['r', 'g', 'b']};
        if (node.TYPE === 'ff' && children.x && children.y) return {type: 'xy', keys: ['x', 'y']};
        return null;
    }
    function supported(node) {
        return node.TYPE && (['f', 'i', 's', 'T', 'F', 'N'].includes(node.TYPE) || tuple(node));
    }
    function load() {
        const entry = current(); if (!entry) return;
        invalidate('Loading layer controls…');
        const token = serial;
        query(entry.path).then(tree => {
            if (token !== serial) return;
            if (tree.FULL_PATH !== entry.path || tree.CONTENTS?.id?.VALUE?.[0] !== selected) throw Error('Layer identity changed; refresh the list.');
            nodes = new Map();
            function visit(node, components = new Set()) {
                const path = node.FULL_PATH;
                if (typeof path !== 'string' || !path.startsWith(entry.path + '/')) return;
                const relative = path.slice(entry.path.length + 1);
                if (relative !== 'id' && (relative !== 'testcard' || entry.viewport) && supported(node) && !components.has(path)) nodes.set(path, {...node, relative});
                const paired = tuple(node), skip = new Set(components);
                if (paired) for (const key of paired.keys) skip.add(path + '/' + key);
                for (const [key, child] of Object.entries(node.CONTENTS || {})) {
                    if (relative === 'layer') continue; // groups are selected independently
                    if (key !== 'layer') visit(child, skip);
                }
            }
            for (const [key, node] of Object.entries(tree.CONTENTS || {})) if (key !== 'layer') visit(node);
            locked = !!tree.CONTENTS.locked?.VALUE?.[0];
            ready = true; layoutKey = listKey;
            emit('/pinol/layer/locked', locked ? 1 : 0, true);
            emit('/pinol/layer/ready', 1, true);
            const sections = new Map();
            for (const node of nodes.values()) { const [key, label] = group(node.relative, node); sections.set(key, label); }
            if (!sections.has(section)) { section = sections.has('general') ? 'general' : sections.keys().next().value || ''; page = 0; }
            edit('layer_sections', {values: {labels: [...sections.values()], values: [...sections.keys()]}});
            emit('/pinol/layer/section', section, true);
            render();
        }).catch(error => {
            if (token !== serial) return;
            invalidate(error.message);
            edit('layer_items', {widgets: [text('layer_error', 'Layer controls unavailable. Refresh or choose another layer.')]});
        });
    }
    function bind(node, paths) {
        const key = 'v' + epoch + '_' + bindings.size;
        bindings.set(key, node);
        if (node.ACCESS & 1) paths.push(node.FULL_PATH);
        return {address: '/pinol/layer/value/' + key, preArgs: [selected, key], interaction: interaction(node.relative),
            ...target, decimals: node.TYPE === 'i' ? 0 : 3};
    }
    function editor(node, key, paths) {
        const properties = bind(node, paths), writable = !!(node.ACCESS & 2);
        const common = {...properties, id: key, label: false, width: '100%', height: 48};
        if (!writable) return {...common, type: 'text', bypass: true, wrap: true};
        const values = node.RANGE?.[0]?.VALS;
        if (Array.isArray(values)) return {...common, type: 'dropdown', label: '%key', values: {labels: values.map(String), values}};
        if (['T', 'F'].includes(node.TYPE)) return {...common, type: 'switch', layout: 'horizontal', values: {OFF: 0, ON: 1}, colorWidget: '#4cd964'};
        if (node.TYPE === 'N') return button(key, 'TRIGGER', '/pinol/layer/write', [selected, properties.preArgs[1]], {interaction: properties.interaction});
        if (node.TYPE === 's') return {...common, type: 'textarea', default: ''};
        return {...common, type: 'input', numeric: node.TYPE === 'i' ? 1 : .001, asYouType: false, noCanvas: true};
    }
    function hint(node, pair) {
        if (!(node.ACCESS & 2)) return 'READ ONLY';
        if (node.relative.includes('/anim/')) return node.relative.split('/anim/')[0].replaceAll('/param/', ' › ').replaceAll('/', ' › ').replaceAll('_', ' ');
        if (pair) return pair.type === 'rgb' ? 'RGB' + (pair.keys.length === 4 ? 'A' : '') + ' · 0–1' : 'XY · native units';
        if (node.RANGE?.[0]?.VALS) return 'Choose a value';
        if (node.TYPE === 's') return 'Enter to apply · Shift+Enter for a new line';
        if (node.TYPE === 'N') return 'Execute action';
        if (['T', 'F'].includes(node.TYPE)) return 'On / Off';
        const range = node.RANGE?.[0];
        return Number.isFinite(range?.MIN) && Number.isFinite(range?.MAX) ? `${range.MIN} – ${range.MAX}` : 'Enter to apply';
    }
    function render() {
        if (!ready || !current()) return;
        ++epoch; bindings.clear();
        const list = [...nodes.values()].filter(node => group(node.relative, node)[0] === section);
        page = Math.max(0, Math.min(page, Math.max(0, Math.ceil(list.length / PAGE) - 1)));
        const paths = [current().path + '/locked'];
        const widgets = list.slice(page * PAGE, (page + 1) * PAGE).map((node, index) => {
            const key = 'layer_control_' + epoch + '_' + index, pair = tuple(node);
            const title = node.DESCRIPTION || node.relative;
            const controls = [text(key + '_title', title, {align: 'left', height: 36, css: 'font-weight:600;font-size:15px;'}),
                text(key + '_hint', hint(node, pair), {align: 'left', height: 24, colorText: '#a4a4ae', css: 'font-size:11px;'})];
            if (pair && node.ACCESS & 2) {
                const properties = bind(node, paths), range = node.RANGE || [];
                controls.push({...properties, type: pair.type, id: key + '_pad', label: false, width: '100%', height: 190,
                    colorWidget: '#85b8aa', snap: true, range: {min: 0, max: 1}, rangeAlpha: {min: 0, max: 1},
                    alpha: pair.keys.length === 4, rangeX: limits(range[0]), rangeY: limits(range[1]), pips: true});
                controls.push({type: 'panel', id: key + '_components', label: false, layout: 'horizontal', height: 66, width: '100%', padding: 2,
                    widgets: pair.keys.map(component => {
                        const child = node.CONTENTS[component];
                        return {...editor({...child, relative: node.relative + '/' + component}, key + '_' + component, paths), label: component.toUpperCase(), width: 'auto', expand: true, height: 60};
                    })});
            } else {
                controls.push(editor(node, key + '_input', paths));
                const r = node.RANGE?.[0];
                if ((node.ACCESS & 2) && ['f', 'i'].includes(node.TYPE) && Number.isFinite(r?.MIN) && Number.isFinite(r?.MAX) && r.MAX > r.MIN)
                    controls.push({...bind(node, paths), type: 'fader', id: key + '_slider', label: false, horizontal: true, design: 'compact',
                        width: '100%', height: 28, input: false, range: limits(r), colorWidget: '#85b8aa'});
            }
            return {type: 'panel', id: key, label: false, layout: 'vertical', width: '100%', height: pair ? 344 : controls.length > 3 ? 164 : 136,
                padding: 8, colorBg: '#28282e', alphaStroke: .2, borderRadius: 5,
                css: 'class: pinol-layer-card;\n> inner {border-left:2rem solid #638c80;}', widgets: controls};
        });
        edit('layer_items', {widgets: widgets.length ? widgets : [text('layer_empty', 'No controls in this section.')]});
        subscriptions(paths);
        emit('/pinol/layer/page', list.length ? `${page * PAGE + 1}–${Math.min((page + 1) * PAGE, list.length)} / ${list.length}` : 'EMPTY', true);
        emit('/pinol/layer/status', `${current().name || 'Untitled'}${locked ? ' · LOCKED' : ' · LIVE EDIT'}`, true);
        for (const [key, node] of bindings) publish(key, node);
        emit('/pinol/layer/ready', ready ? 1 : 0, true);
    }
    function limits(range) {
        return {min: Number.isFinite(range?.MIN) ? range.MIN : 0, max: Number.isFinite(range?.MAX) ? range.MAX : 1};
    }
    function publish(key, node) {
        if (!(node.ACCESS & 1) || !Array.isArray(node.VALUE)) return;
        // Open Stage Control matches incoming preArgs before applying the value.
        emit('/pinol/layer/value/' + key, [selected, key, ...node.VALUE], true);
    }
    function feedback(address, args) {
        if (address === current()?.path + '/locked') {
            locked = !!args[0]; emit('/pinol/layer/locked', locked ? 1 : 0);
            emit('/pinol/layer/status', `${current().name || 'Untitled'}${locked ? ' · LOCKED' : ' · LIVE EDIT'}`);
            for (const [key, node] of bindings) publish(key, node);
        }
        // Keep the schema's cached values current for section changes and late clients.
        for (const node of nodes.values()) {
            if (node.FULL_PATH === address) node.VALUE = args;
            for (const child of Object.values(node.CONTENTS || {})) if (child.FULL_PATH === address) child.VALUE = args;
        }
        for (const [key, node] of bindings) {
            if (node.FULL_PATH !== address) continue;
            node.VALUE = args; publish(key, node);
        }
    }
    function write(layer, key, values) {
        const node = bindings.get(key);
        if (!ready || !isOnline() || layer !== selected || !node || !(node.ACCESS & 2) || (locked && !allowedWhenLocked(node.relative))) return;
        const types = node.TYPE.split('');
        if (node.TYPE === 'N') {
            if (values.length !== 1 || ![1, true].includes(values[0])) return;
            sendCommand(node.FULL_PATH); return;
        }
        if (values.length !== types.length) return;
        const args = [];
        for (let k = 0; k < types.length; ++k) {
            const type = types[k], value = values[k], range = node.RANGE?.[k];
            if (range?.VALS && !range.VALS.includes(value)) return;
            if (type === 's') {
                if (typeof value !== 'string' || value.length > 4096) return;
                args.push({type: 's', value});
            } else if (type === 'T' || type === 'F') {
                if (![0, 1, true, false].includes(value)) return;
                args.push({type: value ? 'T' : 'F', value: !!value});
            } else {
                if (typeof value !== 'number' || !Number.isFinite(value)) return;
                args.push({type, value: type === 'i' ? Math.round(value) : value});
            }
        }
        sendCommand(node.FULL_PATH, args);
    }
    function command(address, args) {
        if (address === '/pinol/layer/select') {
            if (!entries.some(entry => entry.id === args[0])) return;
            selected = args[0]; section = ''; page = 0; emit(address, selected, true); load();
        } else if (address === '/pinol/layer/section') {
            if (![...nodes.values()].some(node => group(node.relative, node)[0] === args[0])) return;
            section = args[0]; page = 0; emit(address, section, true); render();
        } else if (address === '/pinol/layer/page/change' && [-1, 1].includes(args[0])) {
            page += args[0]; render();
        } else if (address === '/pinol/layer/write' || address.startsWith('/pinol/layer/value/')) {
            write(args[0], args[1], args.slice(2));
        }
    }
    return {reset, invalidate, setCatalog, feedback, command, replay: () => { if (ready) render(); }};
};
