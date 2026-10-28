// Maintainer tool, run by CI. No npm or separate Node installation is needed to use the remote.
const fs = require('node:fs');
const path = require('node:path');
const {isBuiltin} = require('node:module');
const webpack = require('webpack');
const root = __dirname;
const output = path.join(root, '.dependency-build');
const filename = 'nyxmap-dependencies.cjs';
const licenses = 'nyxmap-dependencies.LICENSE.txt';
const check = process.argv.includes('--check');
const compiler = webpack({
    mode: 'production', target: 'node18', context: root, devtool: false,
    entry: './dependencies.cjs',
    output: {path: output, filename, library: {type: 'commonjs2'}, clean: true},
    optimization: {minimize: false, moduleIds: 'deterministic'},
    externalsPresets: {node: false},
    // Open Stage Control has a custom file loader. Only Node built-ins remain
    // external, using its nativeRequire; ordinary Node can load the same bundle.
    externals: [({request}, done) => isBuiltin(request || '')
        ? done(null, `(typeof nativeRequire === 'function' ? nativeRequire : require)(${JSON.stringify(request)})`, 'var')
        : done()],
    plugins: [new webpack.IgnorePlugin({resourceRegExp: /^(bufferutil|utf-8-validate)$/}),
        new webpack.BannerPlugin({raw: true, banner:
            "const {Buffer} = (typeof nativeRequire === 'function' ? nativeRequire : require)('node:buffer');\n" +
            "const {setImmediate, clearImmediate} = (typeof nativeRequire === 'function' ? nativeRequire : require)('node:timers');"})]
});
compiler.run((error, stats) => {
    compiler.close(closeError => {
        try {
            if (error || closeError) throw error || closeError;
            if (stats.hasErrors() || stats.hasWarnings()) throw Error(stats.toString({colors: false}));
            const packages = new Map();
            for (const module of stats.compilation.modules) {
                if (!module.resource || !module.resource.includes(`${path.sep}node_modules${path.sep}`)) continue;
                let directory = path.dirname(module.resource);
                while (!fs.existsSync(path.join(directory, 'package.json'))) {
                    const parent = path.dirname(directory);
                    if (parent === directory) throw Error('Package metadata missing for ' + module.resource);
                    directory = parent;
                }
                const metadata = JSON.parse(fs.readFileSync(path.join(directory, 'package.json'), 'utf8'));
                const key = `${metadata.name}@${metadata.version}`;
                if (packages.has(key)) continue;
                // osc.js is dual-licensed; distribute it under its MIT option.
                const files = fs.readdirSync(directory).filter(name => /^(MIT-LICENSE\.txt|LICENSE(?:\.[^.]+)?|NOTICE(?:\.[^.]+)?)$/i.test(name)).sort();
                if (!files.length) throw Error('License missing for ' + key);
                packages.set(key, `${key}\n${metadata.homepage || metadata.repository?.url || ''}\n\n` +
                    files.map(name => fs.readFileSync(path.join(directory, name), 'utf8').trim()).join('\n\n'));
            }
            if (!packages.size) throw Error('No bundled dependencies found');
            const notice = 'Bundled runtime dependencies for the NyxMap Open Stage Control remote.\n' +
                'The original licenses below apply to their respective components.\n\n' +
                [...packages].sort(([a], [b]) => a.localeCompare(b, 'en')).map(([, text]) => text).join('\n\n' + '='.repeat(72) + '\n\n') + '\n';
            fs.writeFileSync(path.join(output, licenses), notice);
            for (const file of [filename, licenses]) {
                const built = fs.readFileSync(path.join(output, file));
                const committed = path.join(root, file);
                if (check) {
                    if (!fs.existsSync(committed) || !built.equals(fs.readFileSync(committed)))
                        throw Error(`${file} is stale. Run npm ci --ignore-scripts --omit=optional and npm run build:dependencies, then commit both generated files.`);
                } else fs.writeFileSync(committed, built);
            }
            console.log(`${check ? 'Verified' : 'Built'} portable runtime: ${[...packages.keys()].sort().join(', ')}`);
        } catch (failure) { console.error(failure); process.exitCode = 1; }
    });
});
