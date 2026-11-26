/**
 * native.js - the dev renderer's tree builds and declutter passes, made by the plugin's own C++.
 *
 * The game answers ProceduralTreeGenerate with TreeBuilder::Build and DeclutterTree with
 * LayoutDeclutter::Run. The harness alone has neither: it built a stand-in tree
 * (dev-harness-bridge.js mockTreeFromSpells) and declined the declutter, so the panel laid the tree
 * out with its JavaScript fallback - a different layout from the game's. Here the same C++ runs
 * through the build's command-line tools:
 *
 *   POST /__live/build      body: the ProceduralTreeGenerate request ({ spells, config })
 *                           reply: what onProceduralTreeComplete gets ({ success, treeData, elapsed }
 *                           or { success: false, error })
 *   POST /__live/declutter  body: the DeclutterTree request; reply: what onDeclutterResult gets
 *
 * Tools: build/tools/Release/treebuilder-test.exe and declutter-test.exe (BuildRelease.ps1 makes
 * them). Without them the server answers 503 and the harness falls back on its mock.
 */
'use strict';

var childProcess = require('child_process');
var fs = require('fs');
var os = require('os');
var path = require('path');

var TOOLS = path.resolve(__dirname, '..', '..', 'build', 'tools', 'Release');
var EXE = { build: 'treebuilder-test.exe', declutter: 'declutter-test.exe' };
var MAX_BODY = 256 * 1024 * 1024;   // a full scan's spells are 9-20 MB

function readBody(req, done) {
    var chunks = [];
    var size = 0;
    req.on('data', function(chunk) {
        size += chunk.length;
        if (size > MAX_BODY) { req.destroy(); return; }
        chunks.push(chunk);
    });
    req.on('end', function() { done(Buffer.concat(chunks).toString('utf8')); });
}

/** The tool's arguments, with its input files written into dir */
function toolArgs(kind, body, dir, output) {
    var input = path.join(dir, 'in.json');
    if (kind === 'declutter') {
        fs.writeFileSync(input, body);
        return ['-i', input, '-o', output];
    }
    // As UIManager::OnProceduralTreeGenerate: the classic builder, the request's config as it is.
    // A config without a seed gets 0 from the tool, which the builder takes as time-based, as in game.
    var request = JSON.parse(body);
    var config = path.join(dir, 'config.json');
    fs.writeFileSync(input, JSON.stringify({ spells: request.spells || [] }));
    fs.writeFileSync(config, JSON.stringify(request.config || {}));
    return ['-i', input, '-o', output, '-t', 'classic', '-c', config];
}

/** The reply the panel gets from the tool's output, or its failure */
function reply(kind, body, out, failure, seconds) {
    if (kind === 'build') {
        if (out === null) return JSON.stringify({ success: false, error: 'native build failed: ' + failure });
        return '{"success":true,"elapsed":' + seconds + ',"treeData":' + out + '}';
    }
    if (out !== null) return out;
    var id = null;
    try { id = JSON.parse(body).id; } catch (e) { /* no id to give back */ }
    // As the plugin's ErrorReply: the panel then runs its own pass at once
    return JSON.stringify({ id: id === undefined ? null : id, error: 'native declutter failed: ' + failure });
}

function handle(kind, req, res, send) {
    var exe = path.join(TOOLS, EXE[kind]);
    if (!fs.existsSync(exe)) {
        req.resume();
        return send(res, 503, 'text/plain', exe + ' is not built (BuildRelease.ps1)');
    }
    readBody(req, function(body) {
        var dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hom-dev-' + kind + '-'));
        var output = path.join(dir, 'out.json');
        var started = Date.now();
        var args;
        try {
            args = toolArgs(kind, body, dir, output);
        } catch (e) {
            fs.rmSync(dir, { recursive: true, force: true });
            return send(res, 200, 'application/json; charset=utf-8', reply(kind, body, null, e.message, 0));
        }
        childProcess.execFile(exe, args, { windowsHide: true, maxBuffer: 16 * 1024 * 1024 }, function(err, stdout, stderr) {
            // treebuilder-test exits 2 after writing the tree when its own structure check fails;
            // the game runs no such check, so a written tree is the answer
            var out = null;
            try { out = fs.readFileSync(output, 'utf8'); } catch (e) { /* the tool wrote nothing */ }
            fs.rmSync(dir, { recursive: true, force: true });
            var seconds = (Date.now() - started) / 1000;
            var failure = String(stderr || stdout || (err && err.message) || '').trim();
            console.log('native ' + kind + ': ' + (out !== null ? 'done' : 'FAILED (' + failure + ')') + ' in ' + seconds + ' s');
            send(res, 200, 'application/json; charset=utf-8', reply(kind, body, out, failure, seconds));
        });
    });
}

module.exports = { handle: handle, kinds: EXE };
