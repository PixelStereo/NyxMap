const {IgnorePlugin} = require('webpack');
module.exports = {
    // ws has pure JavaScript fallbacks. Keep the distributable independent of
    // optional native accelerators and the platform that packaged it.
    plugins: [new IgnorePlugin({resourceRegExp: /^(bufferutil|utf-8-validate)$/})]
};
