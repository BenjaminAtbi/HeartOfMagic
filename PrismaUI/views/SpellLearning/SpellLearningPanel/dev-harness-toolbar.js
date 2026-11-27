/**
 * Dev Harness - Toolbar Wiring
 * Connects harness toolbar buttons to panel functionality.
 * Loaded AFTER all modules and script.js.
 */

(function() {
    console.log('[DevHarness] Toolbar initializing...');

    // ========================================================================
    // TOOLBAR BUTTONS
    // ========================================================================

    var btnTogglePanel = document.getElementById('btn-toggle-panel');
    var btnScanMock = document.getElementById('btn-scan-mock');
    var btnLoadTestData = document.getElementById('btn-load-test-data');
    var btnBuildTree = document.getElementById('btn-build-tree');
    var btnLearnAll = document.getElementById('btn-learn-all');
    var btnToggleLog = document.getElementById('btn-toggle-log');
    var btnClearLog = document.getElementById('btn-clear-log');
    var btnClearLogInner = document.getElementById('btn-clear-log-inner');
    var cppLogPanel = document.getElementById('cpp-log');

    // Toggle panel visibility
    if (btnTogglePanel) {
        btnTogglePanel.addEventListener('click', function() {
            var panel = document.getElementById('spellPanel');
            if (panel) {
                var visible = panel.style.display !== 'none';
                panel.style.display = visible ? 'none' : '';
                btnTogglePanel.classList.toggle('active', !visible);
            }
        });
    }

    // Mock scan - trigger ScanSpells via bridge
    if (btnScanMock) {
        btnScanMock.addEventListener('click', function() {
            var config = {
                scanMode: 'all',
                fields: {
                    editorId: true,
                    magickaCost: true,
                    minimumSkill: true,
                    castingType: true,
                    delivery: true,
                    chargeTime: false,
                    plugin: true,
                    effects: true,
                    effectNames: false,
                    keywords: false
                }
            };
            window.callCpp('ScanSpells', JSON.stringify(config));
        });
    }

    // Load test data directly (no scan simulation)
    if (btnLoadTestData) {
        btnLoadTestData.addEventListener('click', function() {
            btnLoadTestData.textContent = 'Loading...';
            btnLoadTestData.disabled = true;

            var schools = ['Alteration', 'Conjuration', 'Destruction', 'Illusion', 'Restoration'];
            var allSpells = [];
            var loaded = 0;

            schools.forEach(function(school) {
                fetch('test-data/' + school + '_spells.json')
                    .then(function(r) { return r.json(); })
                    .then(function(data) {
                        var spells = data.spells || (Array.isArray(data) ? data : []);
                        spells.forEach(function(s) {
                            s.school = s.school || school;
                            allSpells.push(s);
                        });
                    })
                    .catch(function(err) {
                        console.error('[Harness] Failed to load ' + school + ':', err);
                    })
                    .finally(function() {
                        loaded++;
                        if (loaded === schools.length) {
                            window._mockSpellData = allSpells;
                            logCppCall('in', 'updateSpellData', allSpells.length + ' spells loaded');

                            if (typeof window.updateSpellData === 'function') {
                                window.updateSpellData(JSON.stringify({ scanMode: 'all_spells', spellCount: allSpells.length, spells: allSpells }));
                            }

                            updateHarnessStatus();
                            btnLoadTestData.textContent = 'Load Test Data';
                            btnLoadTestData.disabled = false;
                        }
                    });
            });
        });
    }

    // Build tree: the panel's own Build button (Tree Growth's active mode, Classic by default),
    // from whatever scan the panel holds (the game's scan under the dev renderer, or the test
    // data). It sends ProceduralTreeGenerate, which the dev renderer answers with the plugin's
    // own builder (dev-harness-live.js), then lays the tree out and runs Pre Req Master as in
    // game. Before, it drew the bridge's stand-in chain tree (mockTreeFromSpells) straight into
    // the panel, skipping the builder, the layout and the locks.
    if (btnBuildTree) {
        btnBuildTree.addEventListener('click', function() {
            if (typeof state === 'undefined' || !state.lastSpellData || !(state.lastSpellData.spells || []).length) {
                alert('Scan or load test data first!');
                return;
            }
            var panelBuild = document.getElementById('tgBuildBtn');
            if (!panelBuild) {
                alert('The panel has no Build button (tgBuildBtn)');
                return;
            }
            panelBuild.click();
        });
    }

    // Learn All: the mock player knows every spell (the tree's and the scan's), as if learned
    // in game; the panel takes it through onPlayerKnownSpells like the game's reply, so every
    // spell shows unlocked. Click again to forget them all. Later GetPlayerKnownSpells replies
    // (dev-harness-bridge.js) keep the choice.
    if (btnLearnAll) {
        btnLearnAll.addEventListener('click', function() {
            if (window._harnessKnownSpells) {
                window._harnessKnownSpells = null;
            } else {
                var ids = {};
                var nodes = (typeof state !== 'undefined' && state.treeData && state.treeData.nodes) || [];
                var spells = (typeof state !== 'undefined' && state.lastSpellData && state.lastSpellData.spells) || [];
                nodes.forEach(function(n) { if (n.formId) ids[n.formId] = true; });
                spells.forEach(function(s) { if (s.formId) ids[s.formId] = true; });
                if (!Object.keys(ids).length) {
                    alert('Load a tree or scan first!');
                    return;
                }
                window._harnessKnownSpells = Object.keys(ids);
            }
            btnLearnAll.classList.toggle('active', !!window._harnessKnownSpells);
            mockGetKnownSpells();
        });
    }

    // Toggle C++ log panel
    if (btnToggleLog) {
        btnToggleLog.addEventListener('click', function() {
            cppLogPanel.classList.toggle('visible');
            btnToggleLog.classList.toggle('active', cppLogPanel.classList.contains('visible'));
        });
    }

    // Clear log
    function clearLog() {
        window._cppLog = [];
        var logBody = document.getElementById('cpp-log-body');
        if (logBody) logBody.innerHTML = '';
    }
    if (btnClearLog) btnClearLog.addEventListener('click', clearLog);
    if (btnClearLogInner) btnClearLogInner.addEventListener('click', clearLog);

    // ========================================================================
    // OVERRIDE CLOSE BEHAVIOR
    // ========================================================================

    // In dev harness, closing the panel should just hide it (not call C++)
    var originalCloseClick = window.onCloseClick;
    window.onCloseClick = function() {
        var panel = document.getElementById('spellPanel');
        if (panel) panel.style.display = 'none';
        if (btnTogglePanel) btnTogglePanel.classList.remove('active');
        console.log('[DevHarness] Panel hidden (use Panel button to show again)');
    };

    // ========================================================================
    // AUTO-INIT
    // ========================================================================

    // Ensure panel is visible on load
    var panel = document.getElementById('spellPanel');
    if (panel) {
        panel.style.display = '';
        // In dev harness, fill viewport below toolbar (36px) and above status bar (28px)
        panel.style.width = '100%';
        panel.style.height = 'calc(100vh - 64px)';
        panel.style.left = '0';
        panel.style.top = '36px';
        panel.style.transform = 'none';
        panel.style.borderRadius = '0';
    }

    console.log('[DevHarness] Toolbar ready. Load test data > Build tree to test.');
})();
