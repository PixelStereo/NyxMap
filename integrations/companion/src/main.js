const {InstanceBase, InstanceStatus, runEntrypoint} = require('@companion-module/base');
const {NyxMapClient} = require('./client');
const {variableDefinitions, variableValues, actionDefinitions, feedbackDefinitions, presetDefinitions} = require('./definitions');
const emptyCatalog = () => ({version: 1, sequences: [], snapshots: [], presets: [], timelines: []});

class NyxMapInstance extends InstanceBase {
    async init(config) {
        this.catalog = emptyCatalog(); this.lastValues = {}; this.statusText = 'Disconnected';
        this.client = new NyxMapClient();
        this.client.on('catalog', catalog => { this.catalog = catalog; this.defineControls(); this.publish(); });
        this.client.on('state', () => {
            if (!this.publishTimer) this.publishTimer = setTimeout(() => { this.publishTimer = null; this.publish(); }, 100);
        });
        this.client.on('status', (online, message) => {
            this.statusText = message;
            this.updateStatus(online ? InstanceStatus.Ok : message === 'Connecting' ? InstanceStatus.Connecting : InstanceStatus.Disconnected, message);
            this.publish();
        });
        await this.configUpdated(config);
    }
    getConfigFields() {
        return [
            {type: 'textinput', id: 'host', label: 'NyxMap host / IP', width: 8, default: '127.0.0.1'},
            {type: 'number', id: 'port', label: 'OSCQuery TCP port', width: 4, min: 1, max: 65535, default: 5678},
            {type: 'number', id: 'preset_limit', label: 'Project buttons in the preset gallery (0 disables)', width: 12, min: 0, max: 512, default: 96},
            {type: 'static-text', id: 'help', label: 'Connection', width: 12,
                value: 'Enable OSC / OSCQuery in NyxMap Settings. This module uses TCP 5678 for commands and live feedback; no UDP target or Open Stage Control is required.'}
        ];
    }
    async configUpdated(config) {
        this.config = {...config}; this.catalog = emptyCatalog();
        this.defineControls();
        try { this.client.connect(String(config.host || '').trim(), Number(config.port)); }
        catch (error) { this.updateStatus(InstanceStatus.BadConfig, error.message); this.statusText = error.message; this.publish(); }
    }
    defineControls() {
        this.setVariableDefinitions(variableDefinitions(this.catalog));
        this.setActionDefinitions(actionDefinitions(this));
        this.setFeedbackDefinitions(feedbackDefinitions(this));
        this.setPresetDefinitions(presetDefinitions(this.label, this.catalog, this.config?.preset_limit ?? 96));
        this.lastValues = {};
    }
    publish() {
        if (!this.client) return;
        const values = variableValues(this.client.state, this.catalog, this.client.online, this.statusText), changed = {};
        for (const [key, value] of Object.entries(values)) if (this.lastValues[key] !== value) changed[key] = value;
        this.lastValues = values;
        if (Object.keys(changed).length) this.setVariableValues(changed);
        this.checkFeedbacks('offline', 'running', 'sequence_selected', 'cue_role', 'cue_phase', 'cue_progress', 'timeline_playing', 'timeline_progress', 'paused', 'blackout');
    }
    requireTarget(kind, id) {
        if (typeof id !== 'string' || !/^[1-9][0-9]*$/.test(id)) throw Error('Choose a valid target from the project catalog.');
        const target = this.client.catalog?.[kind]?.find(item => item.id === id);
        if (!target) throw Error('Target is missing or the catalog is unavailable; refresh before launching.');
        return target;
    }
    sequenceCommand(action, selection) {
        const id = selection === 'selected' ? String(this.client.state?.values['/sequence/id'] || '0') : selection;
        this.requireTarget('sequences', id);
        this.client.send(`/sequence/by-id/${id}/${action}`);
    }
    cueCommand(selection) {
        const match = typeof selection === 'string' && /^([1-9][0-9]*):([1-9][0-9]*)$/.exec(selection);
        if (!match) throw Error('Choose a cue from the catalog.');
        const sequence = this.requireTarget('sequences', match[1]);
        if (!sequence.cues.some(cue => cue.id === match[2])) throw Error('Cue no longer exists; refresh the catalog.');
        this.client.send(`/sequence/by-id/${match[1]}/cue/go`, [{type: 's', value: match[2]}]);
    }
    async destroy() {
        clearTimeout(this.publishTimer); this.publishTimer = null;
        this.client?.stop(); this.client?.removeAllListeners();
    }
}
runEntrypoint(NyxMapInstance, []);
