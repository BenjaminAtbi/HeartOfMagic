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
 * - GET /__live/events is a server-sent event stream: "reload" after any file under the panel folder
 *   changes (debounced), and the page reloads itself.
 *
 * Plain Node, no packages; binds to 127.0.0.1 only. launch.ps1 starts it with a full-screen browser.
 */
'use strict';

var http = require('http');
var fs = require('fs');
var path = require('path');
var harness = require('../make-dev-harness.js');

var PANEL = path.resolve(__dirname, '..', '..', 'PrismaUI', 'views', 'SpellLearning', 'SpellLearningPanel');
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
    fs.watch(PANEL, { recursive: true }, function(event, name) {
        if (!name || /(^|[\\/])(test-data|_tmp)([\\/]|$)|Test\.js$/.test(name)) return;
        clearTimeout(timer);
        timer = setTimeout(function() {
            console.log('changed: ' + name + ' - reloading ' + clients.length + ' page(s)');
            clients.forEach(function(c) { c.write('data: reload\n\n'); });
        }, RELOAD_DEBOUNCE_MS);
    });

    var server = http.createServer(function(req, res) {
        var url = req.url.split('?')[0];
        if (url === '/__live/events') {
            res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-store', Connection: 'keep-alive' });
            res.write('data: hello\n\n');
            clients.push(res);
            req.on('close', function() { clients = clients.filter(function(c) { return c !== res; }); });
            return;
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
        serveFile(res, file);
    });
    server.listen(opt.port, '127.0.0.1', function() {
        console.log('dev renderer: http://127.0.0.1:' + opt.port + '/dev-harness.html');
        console.log('panel: ' + PANEL);
        console.log('game data: ' + (opt.data || '(none: the harness\'s test data)'));
    });
}

main();
