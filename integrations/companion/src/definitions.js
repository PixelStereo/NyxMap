const COLORS = {text: 0xececf0, background: 0x202024, green: 0x4cd964, red: 0xe84c3d, yellow: 0xe0b43a, blue: 0x569ce6};
const ROLES = ['previous', 'current', 'next'];
const FIELDS = ['number', 'name', 'label', 'text', 'description', 'phase', 'elapsed', 'remaining', 'elapsed_text', 'remaining_text', 'progress', 'phase_progress'];
const TIMELINE_FIELDS = ['name', 'state', 'position', 'position_text', 'duration', 'duration_text', 'remaining', 'remaining_text', 'progress', 'pass'];
const dropdown = (id, label, choices, fallback = '') => ({type: 'dropdown', id, label, choices, default: choices[0]?.id ?? fallback});
const clock = seconds => {
    if (!Number.isFinite(seconds) || seconds < 0) return '∞';
    const tenths = Math.round(seconds * 10);
    return `${Math.floor(tenths / 600)}:${String(Math.floor(tenths / 10) % 60).padStart(2, '0')}.${tenths % 10}`;
};
const phaseColor = phase => phase === 'pre-wait' ? COLORS.yellow : phase === 'post-wait' ? COLORS.blue : COLORS.green;

function variableDefinitions(catalog) {
    const definitions = ['connected', 'connection_status', 'sequence_id', 'sequence_name', 'sequence_index', 'sequence_running', 'go_enabled', 'back_enabled', 'background_sequences', 'composition_pause', 'composition_blackout']
        .map(variableId => ({variableId, name: variableId.replaceAll('_', ' ')}));
    for (const role of ROLES) for (const field of FIELDS)
        definitions.push({variableId: role + '_' + field, name: role + ' cue ' + field.replaceAll('_', ' ')});
    for (const timeline of catalog.timelines) for (const field of TIMELINE_FIELDS)
        definitions.push({variableId: `timeline_${timeline.id}_${field}`, name: `Timeline ${timeline.name || timeline.id} · ${field.replaceAll('_', ' ')}`});
    return definitions;
}
function variableValues(state, catalog, online, status) {
    const v = state?.values || {}, result = {
        connected: !!online, connection_status: status || 'Disconnected',
        sequence_id: online ? String(v['/sequence/id'] || '0') : '0',
        sequence_name: online ? v['/sequence/name'] || '' : 'OFFLINE',
        sequence_index: online ? Number(v['/sequence/index'] || 0) : 0,
        sequence_running: online && !!v['/sequence/running'], go_enabled: online && !!v['/sequence/go_enabled'], back_enabled: online && !!v['/sequence/back_enabled'],
        background_sequences: online ? (state?.sequences || []).filter(s => s.active && s.id !== String(v['/sequence/id'])).length : 0,
        composition_pause: online && !!v['/composition/pause'], composition_blackout: online && !!v['/composition/blackout']
    };
    for (const role of ROLES) for (const field of FIELDS) {
        const numeric = ['number', 'elapsed', 'remaining', 'progress', 'phase_progress'].includes(field);
        result[role + '_' + field] = online ? (v[`/sequence/cue/${role}/${field}`] ?? (numeric ? 0 : '')) : (numeric ? 0 : field.endsWith('_text') ? '--' : '');
    }
    const playback = new Map((state?.timelines || []).map(t => [t.id, t]));
    for (const timeline of catalog.timelines) {
        const t = online ? playback.get(timeline.id) : null, prefix = `timeline_${timeline.id}_`;
        const data = {name: timeline.name, state: t?.state || (online ? 'stopped' : 'offline'),
            position: t?.position || 0, position_text: t ? clock(t.position) : '--', duration: t?.duration ?? timeline.duration ?? 0,
            duration_text: clock(t?.duration ?? timeline.duration ?? 0), remaining: t?.remaining ?? 0,
            remaining_text: t ? clock(t.remaining) : '--', progress: t?.progress || 0, pass: t?.pass || 0};
        for (const field of TIMELINE_FIELDS) result[prefix + field] = data[field];
    }
    return result;
}
function actionDefinitions(instance) {
    const c = instance.catalog;
    const choices = kind => c[kind].map(item => ({id: item.id, label: `${item.index} · ${item.name || 'Untitled'}`}));
    const sequence = () => dropdown('sequence', 'Sequence', [{id: 'selected', label: 'Selected sequence in NyxMap'}, ...choices('sequences')]);
    const actions = {};
    for (const action of ['go', 'back', 'stop', 'select']) actions['sequence_' + action] = {
        name: 'Sequence: ' + ({go: 'GO', back: 'GO BACK', stop: 'Stop waits / follows', select: 'Select'}[action]), options: [sequence()],
        callback: event => instance.sequenceCommand(action, event.options.sequence)
    };
    actions.stop_all_waits = {name: 'Sequence: Stop all waits / follows', options: [{type: 'checkbox', id: 'confirm', label: 'Confirm stopping waits in every sequence', default: false}],
        callback: event => { if (!event.options.confirm) throw Error('Confirmation is required'); instance.client.send('/sequence/stop_all'); }};
    actions.cue_go = {name: 'Cue: GO directly to cue', options: [dropdown('cue', 'Cue', c.sequences.flatMap(s => s.cues.map(cue => ({id: s.id + ':' + cue.id, label: `${s.name} · ${cue.index} · ${cue.text || cue.name || 'Empty cue'}`}))))],
        callback: event => instance.cueCommand(event.options.cue)};
    for (const kind of ['snapshots', 'presets']) actions[kind.slice(0, -1) + '_recall'] = {
        name: (kind === 'snapshots' ? 'Snapshot' : 'Preset') + ': Recall', options: [dropdown('id', 'Target', choices(kind))],
        callback: event => { instance.requireTarget(kind, event.options.id); instance.client.send(`/${kind.slice(0, -1)}/by-id/${event.options.id}/recall`); }
    };
    for (const action of ['play', 'pause', 'stop', 'rewind']) actions['timeline_' + action] = {
        name: 'Timeline: ' + action, options: [dropdown('id', 'Timeline', choices('timelines'))],
        callback: event => { instance.requireTarget('timelines', event.options.id); instance.client.send(`/timeline/by-id/${event.options.id}/${action}`); }
    };
    for (const property of ['pause', 'blackout']) actions['composition_' + property] = {
        name: 'Composition: ' + property, options: [dropdown('value', 'State', [{id: 'toggle', label: 'Toggle'}, {id: 'on', label: 'On'}, {id: 'off', label: 'Off'}])],
        callback: event => {
            if (!['toggle', 'on', 'off'].includes(event.options.value)) throw Error('Invalid state');
            const value = event.options.value === 'toggle' ? !instance.client.state?.values['/composition/' + property] : event.options.value === 'on';
            instance.client.send('/composition/' + property, [{type: 'i', value: Number(value)}]);
        }
    };
    actions.refresh = {name: 'Connection: Refresh catalog', options: [], callback: () => instance.client.fetchCatalog()};
    return actions;
}
function progressImage(image, progress, color) {
    const width = Math.max(1, Math.min(512, Math.round(Number(image?.width) || 72)));
    const fullHeight = Math.max(1, Math.min(512, Math.round(Number(image?.height) || 72)));
    const height = Math.min(fullHeight, Math.max(4, Math.round(fullHeight * .12)));
    const filled = Math.round(width * Math.max(0, Math.min(1, Number(progress) || 0)));
    const buffer = Buffer.alloc(width * height * 4);
    for (let y = 0; y < height; ++y) for (let x = 0; x < width; ++x) {
        const offset = (y * width + x) * 4, rgb = x < filled ? color : 0x70707c;
        buffer[offset] = (rgb >> 16) & 255; buffer[offset + 1] = (rgb >> 8) & 255; buffer[offset + 2] = rgb & 255;
        buffer[offset + 3] = x < filled ? 200 : 55;
    }
    return {imageBuffer: buffer.toString('base64'), imageBufferEncoding: {pixelFormat: 'RGBA'},
        imageBufferPosition: {x: 0, y: fullHeight - height, width, height}};
}
function feedbackDefinitions(instance) {
    const boolean = (name, callback, color = COLORS.green, options = []) => ({type: 'boolean', name, options, defaultStyle: {color, bgcolor: COLORS.background}, callback});
    const role = () => dropdown('role', 'Cue', ['current', 'next', 'previous'].map(id => ({id, label: id})));
    return {
        offline: boolean('Connection: Offline', () => !instance.client.online, COLORS.red),
        running: boolean('Sequence: Waits / follows active', () => instance.client.online && !!instance.client.state?.values['/sequence/running']),
        sequence_selected: boolean('Sequence: Selected', event => instance.client.online && String(instance.client.state?.values['/sequence/id']) === event.options.id,
            COLORS.green, [dropdown('id', 'Sequence', instance.catalog.sequences.map(s => ({id: s.id, label: s.name})))]),
        cue_role: boolean('Cue: Current / next / previous', event => {
            if (!instance.client.online) return false;
            const sequence = instance.catalog.sequences.find(s => s.cues.some(cue => s.id + ':' + cue.id === event.options.cue));
            const cue = sequence?.cues.find(cue => sequence.id + ':' + cue.id === event.options.cue);
            return !!cue && String(instance.client.state?.values['/sequence/id']) === sequence.id &&
                Number(instance.client.state?.values[`/sequence/cue/${event.options.role}/number`]) === cue.index;
        }, COLORS.green, [role(), dropdown('cue', 'Cue', instance.catalog.sequences.flatMap(s => s.cues.map(c => ({id: s.id + ':' + c.id, label: `${s.name} · ${c.index} · ${c.text || c.name}`}))))]),
        cue_phase: boolean('Cue: Phase', event => instance.client.online && instance.client.state?.values[`/sequence/cue/${event.options.role}/phase`] === event.options.phase, COLORS.yellow,
            [role(), dropdown('phase', 'Phase', ['pre-wait', 'action', 'post-wait', 'done'].map(id => ({id, label: id})))]),
        cue_progress: {type: 'advanced', name: 'Cue: Progress bar', options: [role(), dropdown('basis', 'Progress', [{id: 'phase', label: 'Current phase'}, {id: 'cue', label: 'Whole cue'}])],
            callback: event => {
                const base = '/sequence/cue/' + event.options.role, v = instance.client.online ? instance.client.state?.values || {} : {};
                return progressImage(event.image, v[base + (event.options.basis === 'cue' ? '/progress' : '/phase_progress')], phaseColor(v[base + '/phase']));
            }},
        timeline_playing: boolean('Timeline: Playing', event => instance.client.online && (instance.client.state?.timelines || []).some(t => t.id === event.options.id && t.state === 'playing'),
            COLORS.green, [dropdown('id', 'Timeline', instance.catalog.timelines.map(t => ({id: t.id, label: t.name})))]),
        timeline_progress: {type: 'advanced', name: 'Timeline: Progress bar', options: [dropdown('id', 'Timeline', instance.catalog.timelines.map(t => ({id: t.id, label: t.name})))],
            callback: event => progressImage(event.image, instance.client.online ? instance.client.state?.timelines.find(t => t.id === event.options.id)?.progress : 0, COLORS.blue)},
        paused: boolean('Composition: Paused', () => instance.client.online && !!instance.client.state?.values['/composition/pause'], COLORS.yellow),
        blackout: boolean('Composition: Blackout', () => instance.client.online && !!instance.client.state?.values['/composition/blackout'], COLORS.red)
    };
}
function presetDefinitions(label, catalog, limit = 96) {
    const presets = {}, variable = name => `$(${label}:${name})`;
    const offline = {feedbackId: 'offline', options: {}, style: {color: COLORS.red, bgcolor: 0x302428}};
    const add = (id, category, name, caption, actionId, options = {}, color = COLORS.text, feedbacks = []) => {
        presets[id] = {type: 'button', category, name, style: {text: caption, size: 'auto', color, bgcolor: COLORS.background, show_topbar: false},
            steps: [{down: actionId ? [{actionId, options}] : [], up: []}], feedbacks: [...feedbacks, offline]};
    };
    add('go', 'Transport', 'GO selected sequence', 'GO\n' + variable('next_number') + ' · ' + variable('next_name'), 'sequence_go', {sequence: 'selected'}, COLORS.green);
    add('back', 'Transport', 'GO BACK selected sequence', 'BACK\n' + variable('previous_number'), 'sequence_back', {sequence: 'selected'});
    add('stop', 'Transport', 'Stop selected sequence waits', 'STOP\nWAITS', 'sequence_stop', {sequence: 'selected'}, COLORS.yellow);
    // The all-sequence stop requires a deliberate hold; releasing early does nothing.
    add('stop_all', 'Transport', 'Hold 2s: Stop all waits', 'HOLD 2s\nSTOP ALL\nWAITS', null, {}, COLORS.red);
    presets.stop_all.steps[0][2000] = {options: {runWhileHeld: true}, actions: [{actionId: 'stop_all_waits', options: {confirm: true}}]};
    add('pause', 'Transport', 'Toggle composition pause', 'PAUSE', 'composition_pause', {value: 'toggle'}, COLORS.text, [{feedbackId: 'paused', options: {}, style: {color: COLORS.yellow}}]);
    add('blackout', 'Transport', 'Toggle blackout', 'BLACKOUT', 'composition_blackout', {value: 'toggle'}, COLORS.text, [{feedbackId: 'blackout', options: {}, style: {color: COLORS.red}}]);
    add('sequence', 'Monitor', 'Selected sequence', 'SEQUENCE\n' + variable('sequence_name'), null);
    for (const role of ROLES) {
        const color = role === 'current' ? COLORS.green : role === 'next' ? COLORS.red : 0x90909a;
        const phases = ['pre-wait', 'action', 'post-wait'].map(phase => ({feedbackId: 'cue_phase', options: {role, phase}, style: {color: phaseColor(phase)}}));
        add(role, 'Monitor', role + ' cue', role.toUpperCase() + '\n' + variable(role + '_number') + ' · ' + variable(role + '_name'), null, {}, color,
            [...phases, {feedbackId: 'cue_progress', options: {role, basis: 'phase'}}]);
    }
    for (const field of ['elapsed', 'remaining']) add(field, 'Monitor', 'Current cue ' + field, field.toUpperCase() + '\n' + variable('current_' + field + '_text'), null, {}, COLORS.text,
        [{feedbackId: 'cue_progress', options: {role: 'current', basis: 'phase'}}]);
    add('phase', 'Monitor', 'Current phase', variable('current_phase') + '\n' + variable('current_remaining_text'), null, {}, COLORS.text,
        [{feedbackId: 'cue_progress', options: {role: 'current', basis: 'phase'}}]);
    add('connection', 'Monitor', 'Connection status', variable('connection_status'), null);
    add('refresh', 'Transport', 'Refresh project catalog', 'REFRESH', 'refresh');
    let remaining = Math.floor(Math.max(0, Math.min(512, Number(limit) || 0)));
    const lists = ['sequences', 'snapshots', 'presets', 'timelines'].map(kind => ({kind, items: catalog[kind]}));
    lists.push({kind: 'cues', items: catalog.sequences.flatMap(sequence => sequence.cues.map(cue => ({...cue, sequence: sequence.id, id: sequence.id + ':' + cue.id, name: cue.text || cue.name || 'Empty cue'})))});
    // Round-robin keeps every library represented within the configured gallery limit.
    for (let index = 0; remaining && lists.some(list => index < list.items.length); ++index) for (const {kind, items} of lists) {
        if (!remaining || !items[index]) continue;
        const item = items[index]; --remaining;
        const caption = `${item.index}\n${item.name || 'Untitled'}`;
        if (kind === 'sequences') add('sequence_' + item.id, 'Project / Sequences', item.name, caption, 'sequence_select', {sequence: item.id}, COLORS.text,
            [{feedbackId: 'sequence_selected', options: {id: item.id}, style: {color: COLORS.green}}]);
        else if (kind === 'cues') add('cue_' + item.id.replace(':', '_'), 'Project / Cues', item.name, caption, 'cue_go', {cue: item.id}, COLORS.text,
            [{feedbackId: 'cue_role', options: {cue: item.id, role: 'next'}, style: {color: COLORS.red}},
                {feedbackId: 'cue_role', options: {cue: item.id, role: 'current'}, style: {color: COLORS.green}}]);
        else if (kind === 'timelines') add('timeline_' + item.id, 'Project / Timelines', item.name, caption + '\n' + variable(`timeline_${item.id}_position_text`), 'timeline_play', {id: item.id}, COLORS.blue,
            [{feedbackId: 'timeline_playing', options: {id: item.id}, style: {color: COLORS.green}}, {feedbackId: 'timeline_progress', options: {id: item.id}}]);
        else add(kind + '_' + item.id, 'Project / ' + (kind === 'snapshots' ? 'Snapshots' : 'Presets'), item.name, caption,
            kind.slice(0, -1) + '_recall', {id: item.id}, kind === 'snapshots' ? COLORS.green : COLORS.yellow);
    }
    return presets;
}
module.exports = {COLORS, ROLES, FIELDS, clock, variableDefinitions, variableValues, actionDefinitions, feedbackDefinitions, presetDefinitions, progressImage};
