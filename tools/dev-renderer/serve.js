/**
 * serve.js - the dev renderer's server: the panel's dev harness over http, with the game's own data
 * and a reload on every saved change.
 *
 *   node tools/dev-renderer/serve.js [--port 5557] [--data <SKSE/Plugins/SpellLearning folder>]
 *
 * - Serves the panel folder (PrismaUI/views/SpellLearning/SpellLearningPanel) from the working tree,
 *   so an edit shows without a build or a deploy. Nothing is cached. dev-harness.html is built on
 *   every request (tools/make-dev-harness.js), so an index.html edit shows without regenerating it.
 * - With --data (or HOM_DEV_DATA), GET /__live/scan, /__live/config and /__live/tree answer with that
 *   folder's spell_scan_output.json (debug.json makes the game write it), config.json and
 *   spell_tree.json; 404 when a file is not there. dev-harness-live.js hands them to the panel the
 *   way C++ does. The server never writes the game's files.
 * - The design add-ons (addons/designs/<Name>/, _shared/<part>/) are laid under the panel: a panel path
 *   the working tree lacks is looked up in each add-on's PrismaUI/views/SpellLearning/SpellLearningPanel,
 *   and GET /__live/designs lists every add-on's SKSE/Plugins/SpellLearning/presets/design/*.json as
 *   [{ key, data }], the answer C++ gives to LoadPresets { type: 'design' }.
 * - POST /__live/build and /__live/declutter build and declutter trees with the plugin's C++ (native.js),
 *   so a tree built in the harness has the game's layout.
 * - GET /__live/events is a server-sent event stream: "reload" after any file under the panel folder
 *   or the add-ons changes (debounced), and the page reloads itself.
 *
 * Plain Node, no packages; binds to 127.0.0.1 only. launch.ps1 starts it with a full-screen browser.
 */
'use strict';

var http = require('http');
var fs = require('fs');
var path = require('path');
var harness = require('../make-dev-harness.js');
var native = require('./native.js');

var PANEL = path.resolve(__dirname, '..', '..', 'PrismaUI', 'views', 'SpellLearning', 'SpellLearningPanel');
var ADDONS = path.resolve(__dirname, '..', '..', 'addons', 'designs');
var ADDON_PANEL = path.join('PrismaUI', 'views', 'SpellLearning', 'SpellLearningPanel');
var ADDON_PRESETS = path.join('SKSE', 'Plugins', 'SpellLearning', 'presets', 'design');
var DEFAULT_PORT = 5557;
var RELOAD_DEBOUNCE_MS = 200;
var LIVE_FILES = { scan: 'spell_scan_output.json', config: 'config.json', tree: 'spell_tree.json' };
var TYPES = {
    '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
    '.json': 'application/json; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg',
    '.webp': 'image/webp', '.ttf': 'font/ttf', '.otf': 'font/otf', '.woff': 'font/woff', '.woff2': 'font/woff2'
};

function args(argv) {
    var out = { port: DEFAULT_PORT, data: process.env.HOM_DEV_DATA || '' };
    for (var i = 2; i < argv.length; i++) {
        if (argv[i] === '--port') out.port = parseInt(argv[++i], 10);
        else if (argv[i] === '--data') out.data = argv[++i];
        else throw new Error('unknown argument: ' + argv[i]);
    }
    return out;
}

function send(res, status, type, body) {
    res.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store' });
    res.end(body);
}

/** A file under root, or null when the path leaves it */
function inside(root, urlPath) {
    var rel = decodeURIComponent(urlPath).replace(/^\/+/, '');
    var file = path.resolve(root, rel || 'dev-harness.html');
    return (file === root || file.indexOf(root + path.sep) === 0) ? file : null;
}

/** Every add-on root: addons/designs/<Name> and addons/designs/_shared/<part> (not _package) */
function addonRoots() {
    var roots = [];
    function dirs(dir) {
        try { return fs.readdirSync(dir, { withFileTypes: true }).filter(function(d) { return d.isDirectory(); }); }
        catch (e) { return []; }
    }
    dirs(ADDONS).forEach(function(d) {
        if (d.name === '_package') return;
        if (d.name === '_shared') {
            dirs(path.join(ADDONS, d.name)).forEach(function(s) { roots.push(path.join(ADDONS, d.name, s.name)); });
        } else {
            roots.push(path.join(ADDONS, d.name));
        }
    });
    return roots;
}

/** A panel path the working tree lacks, from the first add-on that has it */
function addonFile(urlPath) {
    var roots = addonRoots();
    for (var i = 0; i < roots.length; i++) {
        var file = inside(path.join(roots[i], ADDON_PANEL), urlPath);
        if (file && fs.existsSync(file)) return file;
    }
    return null;
}

/** Every add-on's design presets, as OnLoadPresets hands them over */
function addonDesigns() {
    var out = [];
    addonRoots().forEach(function(root) {
        var dir = path.join(root, ADDON_PRESETS);
        var names = [];
        try { names = fs.readdirSync(dir).filter(function(n) { return /\.json$/i.test(n); }); } catch (e) { return; }
        names.forEach(function(n) {
            try { out.push({ key: n.replace(/\.json$/i, ''), data: JSON.parse(fs.readFileSync(path.join(dir, n), 'utf8')) }); }
            catch (e) { console.log('design ' + n + ': ' + e.message); }
        });
    });
    return out;
}

function serveFile(res, file) {
    fs.readFile(file, function(err, body) {
        if (err) return send(res, 404, 'text/plain', 'not found');
        send(res, 200, TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream', body);
    });
}

function main() {
    var opt = args(process.argv);
    var clients = [];
    var timer = null;
    // Windows reports a read too (the last-access time), so the first fetch of a theme's files would
    // reload the page: only a new size or mtime, or a file appearing or going, counts as a change.
    var stamps = {};
    function stamp(file) {
        try { var st = fs.statSync(file); return st.isDirectory() ? 'dir' : st.size + ':' + st.mtimeMs; }
        catch (e) { return 'gone'; }
    }
    function seed(root) {
        try {
            fs.readdirSync(root, { recursive: true }).forEach(function(rel) {
                var file = path.join(root, rel);
                stamps[file] = stamp(file);
            });
        } catch (e) { /* nothing to seed */ }
    }
    function watch(root) {
        seed(root);
        fs.watch(root, { recursive: true }, function(event, name) {
            if (!name) return;
            var file = path.join(root, name);
            var now = stamp(file);
            if (stamps[file] === now) return;
            stamps[file] = now;
            changed(event, name);
        });
    }
    function changed(event, name) {
        if (/(^|[\\/])(test-data|_tmp)([\\/]|$)|Test\.js$/.test(name)) return;
        clearTimeout(timer);
        timer = setTimeout(function() {
            console.log('changed: ' + name + ' - reloading ' + clients.length + ' page(s)');
            clients.forEach(function(c) { c.write('data: reload\n\n'); });
        }, RELOAD_DEBOUNCE_MS);
    }
    watch(PANEL);
    if (fs.existsSync(ADDONS)) watch(ADDONS);

    var server = http.createServer(function(req, res) {
        var url = req.url.split('?')[0];
        if (url === '/__live/events') {
            res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-store', Connection: 'keep-alive' });
            res.write('data: hello\n\n');
            clients.push(res);
            req.on('close', function() { clients = clients.filter(function(c) { return c !== res; }); });
            return;
        }
        if (url === '/__live/designs') return send(res, 200, TYPES['.json'], JSON.stringify(addonDesigns()));
        var nativeKind = /^\/__live\/(\w+)$/.exec(url);
        if (req.method === 'POST' && nativeKind && native.kinds[nativeKind[1]]) {
            return native.handle(nativeKind[1], req, res, send);
        }
        var live = /^\/__live\/(\w+)$/.exec(url);
        if (live) {
            if (!opt.data || !LIVE_FILES[live[1]]) return send(res, 404, 'text/plain', 'no live data');
            return serveFile(res, path.join(opt.data, LIVE_FILES[live[1]]));
        }
        var file = inside(PANEL, url);
        if (!file) return send(res, 403, 'text/plain', 'outside the panel');
        if (file === path.join(PANEL, 'dev-harness.html')) {
            try { return send(res, 200, TYPES['.html'], harness.make()); }
            catch (e) { return send(res, 500, 'text/plain', 'harness: ' + e.message); }
        }
        if (!fs.existsSync(file)) file = addonFile(url) || file;
        serveFile(res, file);
    });
    server.listen(opt.port, '127.0.0.1', function() {
        console.log('dev renderer: http://127.0.0.1:' + opt.port + '/dev-harness.html');
        console.log('panel: ' + PANEL);
        console.log('game data: ' + (opt.data || '(none: the harness\'s test data)'));
    });
}

main();
