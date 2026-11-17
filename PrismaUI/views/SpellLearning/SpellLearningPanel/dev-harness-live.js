/**
 * Dev Harness - live game data and reload (the dev renderer, tools/dev-renderer/)
 *
 * Only when the harness is served over http by tools/dev-renderer/serve.js; opened as a file it
 * does nothing and the mock bridge (dev-harness-bridge.js) answers as before. Loaded right after
 * the bridge, before every module script.
 *
 * - ScanSpells, LoadUnifiedConfig and LoadSpellTree answer with the game's own files
 *   (/__live/scan, /__live/config, /__live/tree: spell_scan_output.json, config.json,
 *   spell_tree.json), handed to updateSpellData, onUnifiedConfigLoaded and updateTreeData as C++
 *   does. A file the server does not have falls back on the mock.
 * - Saves (the tree, the config) stay in memory: the game's files are never written from here,
 *   and the mock's download of spell_tree.json is skipped.
 * - The page reloads when the server says a panel file changed (/__live/events), and when it comes
 *   back after a restart (serve.js itself changed).
 */
(function() {
    if (!/^https?:$/.test(location.protocol) || typeof window.callCpp !== 'function') return;

    var mock = window.callCpp;
    var LIVE = { ScanSpells: ['scan', 'updateSpellData'], LoadUnifiedConfig: ['config', 'onUnifiedConfigLoaded'],
        LoadSpellTree: ['tree', 'updateTreeData'] };
    var IN_MEMORY = { SaveSpellTree: true, SaveBuiltSpellTree: true, SaveImportedSpellTree: true, SaveUnifiedConfig: true };
    window._liveSaves = {};

    window.callCpp = function(method, data) {
        var live = LIVE[method];
        if (live) {
            logCppCall('out', method, data);
            fetch('/__live/' + live[0], { cache: 'no-store' })
                .then(function(r) { if (!r.ok) throw new Error(r.status); return r.text(); })
                .then(function(text) {
                    logCppCall('in', live[1], 'game file, ' + text.length + ' bytes');
                    if (typeof window[live[1]] === 'function') window[live[1]](text);
                })
                .catch(function() { console.log('[DevLive] no game ' + live[0] + ': mock'); mock(method, data); });
            return;
        }
        if (IN_MEMORY[method]) {
            logCppCall('out', method, data);
            window._liveSaves[method] = data;
            console.log('[DevLive] ' + method + ' kept in memory (window._liveSaves), not written');
            return;
        }
        return mock(method, data);
    };

    if (typeof EventSource === 'function') {
        var events = new EventSource('/__live/events');
        var lost = false;
        events.onerror = function() { lost = true; };
        events.onmessage = function(e) { if (e.data === 'reload' || (e.data === 'hello' && lost)) location.reload(); };
    }
    console.log('[DevLive] live game data and reload on');
})();
