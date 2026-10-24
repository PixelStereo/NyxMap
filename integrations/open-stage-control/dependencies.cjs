// Build input only. Users load pinol.js; its dependency bundle is checked in.
module.exports = {WebSocket: require('ws'), osc: require('osc/src/osc')};
