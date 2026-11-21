/**
 * ClassicSettings Module - Settings panel for Classic Growth mode
 *
 * Builds the Classic Growth settings panel HTML (buttons, status, sliders)
 * and binds event handlers for user interaction. Tracks builder and
 * spell scan state to enable/disable the Build, Apply, and Clear buttons.
 *
 * Usage:
 *   var html = ClassicSettings.buildHTML(settings);
 *   ClassicSettings.bindEvents({ onBuild, onApply, onClear, onSettingChanged });
 *   ClassicSettings.updateScanStatus(hasSpells);
 *   ClassicSettings.setTreeBuilt(built);
 *   ClassicSettings.setStatusText(text, tone, key, params);   // tone: TreeGrowth.STATUS_COLORS key
 *
 * Depends on: treePreviewUtils.js (settingHTML, bindInput), treeBuilderSetting.js (optional)
 */

var ClassicSettings = {

    // Internal state tracking
    _hasSpells: false,
    _treeBuilt: false,
    _nodeCount: 0,

    // How far a tier zone handle's grab area reaches into its bar, as a share of the handle's height
    HANDLE_GRACE: 0.5,

    // Tier zone defaults: settings without tierZones start from a copy of these
    DEFAULT_TIER_ZONES: {
        Novice:     { min: 0,  max: 40 },
        Apprentice: { min: 10, max: 55 },
        Adept:      { min: 30, max: 75 },
        Expert:     { min: 50, max: 90 },
        Master:     { min: 65, max: 100 }
    },

    // The object whose tierZones the chart shows and a drag changes: the settings
    // buildHTML was given (TreeGrowthClassic.settings). One copy, so a preset written
    // into it (scannerPresets.js) is what the chart shows and what a drag starts from
    _zoneSource: {},

    // Tier display config
    _tierConfig: [
        { key: 'Novice',     label: 'Novice',     color: 'rgba(100,180,100,0.5)' },
        { key: 'Apprentice', label: 'Apprentice', color: 'rgba(80,150,200,0.5)' },
        { key: 'Adept',      label: 'Adept',      color: 'rgba(180,160,80,0.5)' },
        { key: 'Expert',     label: 'Expert',     color: 'rgba(200,120,60,0.5)' },
        { key: 'Master',     label: 'Master',     color: 'rgba(180,60,60,0.5)' }
    ],

    // =========================================================================
    // BUILD HTML
    // =========================================================================

    /**
     * Build the settings panel HTML string.
     *
     * @param {Object} settings - Current settings values
     * @param {number} settings.ghostOpacity - Ghost node opacity (0-100)
     * @param {number} settings.nodeRadius - Node radius in pixels
     * @returns {string} HTML string for the settings panel
     */
    buildHTML: function (settings) {
        var s = settings || {};
        this._zoneSource = s;
        var opacity = s.ghostOpacity !== undefined ? s.ghostOpacity : 35;
        var nodeSize = s.nodeRadius !== undefined ? s.nodeRadius : 5;
        var spread = s.spread !== undefined ? s.spread : 50;
        var radialBias = s.radialBias !== undefined ? s.radialBias : 50;
        var centerMask = s.centerMask !== undefined ? s.centerMask : 3;
        var spellMatching = s.spellMatching || 'layered';
        var dynamicExpansion = s.dynamicGridExpansion !== false;
        var H = TreePreviewUtils.settingHTML;

        // Toggle button styles
        var btnBase = 'display:inline-block; padding:3px 10px; font-size:0.8em; cursor:pointer; border:1px solid rgba(184,168,120,0.3); transition:background 0.15s;';
        var btnAct = 'background:rgba(184,168,120,0.25); color:rgba(184,168,120,0.9);';
        var btnOff = 'background:transparent; color:rgba(184,168,120,0.4);';

        return '' +
            '<div class="tree-preview-settings-title">' + t('preview.classic.title') + '</div>' +

            // --- Spell Matching toggle (3-way) ---
            '<div style="display:flex; align-items:center; gap:8px; margin-bottom:2px; padding:0 4px;">' +
                '<span style="font-size:0.8em; color:rgba(184,168,120,0.6); white-space:nowrap;">' + t('preview.classic.spellMatching') + '</span>' +
                '<div style="display:flex;">' +
                    '<div id="tgClassicMatchSimple" style="' + btnBase + ' border-radius:3px 0 0 3px; ' + (spellMatching === 'simple' ? btnAct : btnOff) + '">' + t('preview.classic.simple') + '</div>' +
                    '<div id="tgClassicMatchLayered" style="' + btnBase + ' border-left:none; ' + (spellMatching === 'layered' ? btnAct : btnOff) + '">' + t('preview.classic.layered') + '</div>' +
                    '<div id="tgClassicMatchSmart" style="' + btnBase + ' border-radius:0 3px 3px 0; border-left:none; ' + (spellMatching === 'smart' ? btnAct : btnOff) + '">' + t('preview.classic.smart') + '</div>' +
                '</div>' +
            '</div>' +
            '<div id="tgClassicMatchDesc" style="font-size:0.75em; color:rgba(184,168,120,0.35); padding:0 4px 4px; min-height:22px;">' +
                (spellMatching === 'simple' ? tOr('preview.classic.matchSimpleDesc', null, 'No theme awareness. Placement by distance, tier, and radial bias only. Fastest.') :
                 spellMatching === 'smart' ? tOr('preview.classic.matchSmartDesc', null, 'Re-discovers themes in JS, overrides NLP assignments. Strongest clustering but slower.') :
                 tOr('preview.classic.matchLayeredDesc', null, 'Uses NLP-assigned themes for spatial clustering. Same-theme spells group angularly. Default.')) +
            '</div>' +

            // --- Slider grid ---
            '<div class="tree-preview-settings-grid">' +
                H(t('preview.ghostOpacity'), 'tgClassicOpacity', 0, 100, 5, opacity, '%', t('preview.classic.ghostOpacityHint')) +
                H(t('preview.nodeSize'), 'tgClassicNodeSize', 1, 20, 1, nodeSize, '', t('preview.classic.nodeSizeHint')) +
                H(t('preview.classic.spread'), 'tgClassicSpread', 0, 100, 5, spread, '', t('preview.classic.spreadHint')) +
                H(t('preview.classic.radialBias'), 'tgClassicRadialBias', 0, 100, 5, radialBias, '', t('preview.classic.radialBiasHint')) +
                H(t('preview.classic.globeMask'), 'tgClassicCenterMask', 0, 10, 1, centerMask, ' ' + tOr('preview.classic.tiersUnit', null, 'tiers'),
                    t('preview.classic.globeMaskHint')) +
            '</div>' +

            // --- Builder options the player sets (treeBuilderSetting.js), applied at the next build ---
            (typeof TreeBuilderSetting !== 'undefined' ? TreeBuilderSetting.html() : '') +

            // --- Dynamic Grid Expansion toggle ---
            // (laid out like Tiers by Magicka cost above it: the option, then its description)
            '<div class="tree-preview-option">' +
                '<label>' +
                    '<input type="checkbox" id="tgClassicDynExpand"' + (dynamicExpansion ? ' checked' : '') + '>' +
                    t('preview.classic.dynamicExpansion') +
                '</label>' +
            '</div>' +
            '<div class="tree-preview-hint">' +
                t('preview.classic.dynamicExpansionHint') +
            '</div>' +

            // --- Tier zone bar chart ---
            '<div class="tree-preview-settings-title" style="margin-top:8px;">' + t('preview.classic.tierZones') +
                TreePreviewUtils.helpHTML(t('preview.classic.tierZonesHint')) + '</div>' +
            this._buildTierChartHTML(s) +
            '<div style="text-align:center; margin-top:2px; font-size:0.75em; color:rgba(184,168,120,0.3);">' + t('preview.dragHandles') + '</div>';
    },

    /** Build the tier zone bar chart HTML. @private */
    _buildTierChartHTML: function (settings) {
        var zones = this._zones();
        // Use inline styles to avoid CSS loading issues (matches treeSettings alloc bar pattern)
        var html = '<div id="tgClassicTierChart" style="display:flex; flex-direction:row; gap:6px; padding:6px 8px; height:260px; border:1px solid rgba(184,168,120,0.2); border-radius:4px; user-select:none; -webkit-user-select:none;">';

        for (var i = 0; i < this._tierConfig.length; i++) {
            var cfg = this._tierConfig[i];
            var zone = zones[cfg.key] || { min: 0, max: 100 };
            var fillH = zone.max - zone.min;

            // Column: vertical flex, equal width
            html += '<div class="classic-tier-col" data-tier="' + cfg.key + '" ' +
                'style="flex:1; display:flex; flex-direction:column; align-items:center; min-width:0;">' +

                // Track: the vertical bar area
                '<div class="classic-tier-track" style="flex:1; width:100%; position:relative; background:rgba(184,168,120,0.08); border-radius:3px; overflow:visible;">' +

                    // Fill: positioned from bottom, height = zone range
                    '<div class="classic-tier-fill" ' +
                        'style="position:absolute; left:2px; right:2px; bottom:' + zone.min + '%; height:' + fillH + '%; background:' + cfg.color + '; border-radius:2px; cursor:grab;">' +

                        // Top handle (max)
                        '<div class="classic-tier-handle" data-handle="max" ' +
                            'style="position:absolute; left:-1px; right:-1px; top:-2px; height:5px; cursor:ns-resize; background:rgba(255,255,255,0.7); border-radius:2px;"></div>' +

                        // Bottom handle (min)
                        '<div class="classic-tier-handle" data-handle="min" ' +
                            'style="position:absolute; left:-1px; right:-1px; bottom:-2px; height:5px; cursor:ns-resize; background:rgba(255,255,255,0.7); border-radius:2px;"></div>' +

                        // Percentage label centered in fill
                        '<span class="classic-tier-pct" style="position:absolute; width:100%; text-align:center; font-size:0.75em; color:rgba(255,255,255,0.8); pointer-events:none; top:50%; transform:translateY(-50%); white-space:nowrap;">' +
                            zone.min + '-' + zone.max +
                        '</span>' +

                    '</div>' +
                '</div>' +

                // Label below the bar
                '<div class="classic-tier-label" style="font-size:0.75em; color:rgba(184,168,120,0.7); margin-top:3px; white-space:nowrap;">' + t('preview.classic.tier' + cfg.key) + '</div>' +
            '</div>';
        }

        html += '</div>';
        return html;
    },

    // =========================================================================
    // BIND EVENTS
    // =========================================================================

    /**
     * Bind click handlers on buttons and change handlers on sliders.
     *
     * @param {Object} callbacks
     * @param {function} callbacks.onBuild - Called when Build Tree is clicked
     * @param {function} callbacks.onApply - Called when Apply Tree is clicked
     * @param {function} callbacks.onClear - Called when Clear Tree is clicked
     * @param {function} callbacks.onSettingChanged - Called with (key, value)
     */
    bindEvents: function (callbacks) {
        var cb = callbacks || {};

        // Slider bindings
        var onChanged = cb.onSettingChanged || function () {};

        TreePreviewUtils.bindInput('tgClassicOpacity', function (v) {
            onChanged('ghostOpacity', v);
        });

        TreePreviewUtils.bindInput('tgClassicNodeSize', function (v) {
            onChanged('nodeRadius', v);
        });

        TreePreviewUtils.bindInput('tgClassicSpread', function (v) {
            onChanged('spread', v);
        });

        TreePreviewUtils.bindInput('tgClassicRadialBias', function (v) {
            onChanged('radialBias', v);
        });

        TreePreviewUtils.bindInput('tgClassicCenterMask', function (v) {
            onChanged('centerMask', v);
        });

        // Spell Matching 3-way toggle
        var matchBtns = {
            simple: document.getElementById('tgClassicMatchSimple'),
            layered: document.getElementById('tgClassicMatchLayered'),
            smart: document.getElementById('tgClassicMatchSmart')
        };
        var actBg = 'background:rgba(184,168,120,0.25)';
        var actCol = 'color:rgba(184,168,120,0.9)';
        var offBg = 'background:transparent';
        var offCol = 'color:rgba(184,168,120,0.4)';
        var setMatchActive = function (mode) {
            for (var mk in matchBtns) {
                if (!matchBtns.hasOwnProperty(mk) || !matchBtns[mk]) continue;
                var isAct = (mk === mode);
                matchBtns[mk].style.cssText = matchBtns[mk].style.cssText
                    .replace(/background:[^;]+/, isAct ? actBg : offBg)
                    .replace(/color:[^;]+/, isAct ? actCol : offCol);
            }
        };
        var matchDescs = {
            simple: tOr('preview.classic.matchSimpleDesc', null, 'No theme awareness. Placement by distance, tier, and radial bias only. Fastest.'),
            layered: tOr('preview.classic.matchLayeredDesc', null, 'Uses NLP-assigned themes for spatial clustering. Same-theme spells group angularly. Default.'),
            smart: tOr('preview.classic.matchSmartDesc', null, 'Re-discovers themes in JS, overrides NLP assignments. Strongest clustering but slower.')
        };
        var descEl = document.getElementById('tgClassicMatchDesc');
        var modes = ['simple', 'layered', 'smart'];
        for (var mi = 0; mi < modes.length; mi++) {
            (function (mode) {
                if (matchBtns[mode]) {
                    matchBtns[mode].addEventListener('click', function () {
                        setMatchActive(mode);
                        if (descEl) descEl.textContent = matchDescs[mode];
                        onChanged('spellMatching', mode);
                    });
                    matchBtns[mode].addEventListener('mouseenter', function () {
                        if (descEl) descEl.textContent = matchDescs[mode];
                    });
                }
            })(modes[mi]);
        }

        // Dynamic Grid Expansion toggle
        var dynExpandCheck = document.getElementById('tgClassicDynExpand');
        if (dynExpandCheck) {
            dynExpandCheck.addEventListener('change', function () {
                onChanged('dynamicGridExpansion', this.checked);
            });
        }

        if (typeof TreeBuilderSetting !== 'undefined') TreeBuilderSetting.bind();

        // Tier zone drag
        this._bindTierZoneDrag(cb);

        // Re-apply internal state to fresh DOM
        this._refreshDOM();
    },

    /** The settings' tier zones; a missing tier is made from the defaults. @private */
    _zones: function () {
        var src = this._zoneSource || (this._zoneSource = {});
        if (!src.tierZones || typeof src.tierZones !== 'object') src.tierZones = {};
        for (var key in this.DEFAULT_TIER_ZONES) {
            if (this.DEFAULT_TIER_ZONES.hasOwnProperty(key) && !src.tierZones[key]) {
                src.tierZones[key] = { min: this.DEFAULT_TIER_ZONES[key].min, max: this.DEFAULT_TIER_ZONES[key].max };
            }
        }
        return src.tierZones;
    },

    /** Return a copy of the current tier zones. */
    getTierZones: function () {
        var zones = this._zones(), copy = {};
        for (var key in zones) {
            if (zones.hasOwnProperty(key)) copy[key] = { min: zones[key].min, max: zones[key].max };
        }
        return copy;
    },

    /** Write tier zones into the settings and refresh the bar chart. */
    setTierZones: function (zones) {
        if (!zones) return;
        var current = this._zones();
        for (var key in zones) {
            if (zones.hasOwnProperty(key) && current.hasOwnProperty(key)) {
                current[key] = { min: zones[key].min, max: zones[key].max };
            }
        }
        this.refreshTierChart();
    },

    /** Redraw the bar chart from the settings' tier zones (after a preset wrote them). */
    refreshTierChart: function () {
        var chart = document.getElementById('tgClassicTierChart');
        if (!chart) return;
        var cols = chart.querySelectorAll('.classic-tier-col');
        for (var ci = 0; ci < cols.length; ci++) {
            var tier = cols[ci].getAttribute('data-tier');
            var zone = this._zones()[tier];
            if (!zone) continue;
            var fill = cols[ci].querySelector('.classic-tier-fill');
            if (!fill) continue;
            fill.style.bottom = zone.min + '%';
            fill.style.height = (zone.max - zone.min) + '%';
            var pctEl = fill.querySelector('.classic-tier-pct');
            if (pctEl) pctEl.textContent = zone.min + '-' + zone.max;
        }
    },

    /**
     * Bind mousedown/mousemove/mouseup on the tier zone bars: a handle (the bar's top or bottom edge) moves that end,
     * the bar itself moves the whole range within 0-100. Each handle reaches into the bar by half the bar's length
     * (_handleNear), so the ends are easier to take than their thin rendered edges. @private
     */
    _bindTierZoneDrag: function (callbacks) {
        var chart = document.getElementById('tgClassicTierChart');
        if (!chart) return;

        var self = this;
        var dragging = null;

        // Y is inverted: top of track = 100%, bottom = 0%; unsnapped
        var pctAt = function (track, clientY) {
            var rect = track.getBoundingClientRect();
            return rect.height > 0 ? ((rect.bottom - clientY) / rect.height) * 100 : null;
        };

        // handleType: 'min' / 'max' (an end) or 'move' (the bar)
        var start = function (el, handleType, e) {
            var col = el.closest('.classic-tier-col');
            if (!col) return;
            var track = col.querySelector('.classic-tier-track');
            if (!track) return;
            var tier = col.getAttribute('data-tier');
            var zone = self._zones()[tier];
            if (!zone) return;
            var at = handleType === 'move' && typeof e.clientY === 'number' ? pctAt(track, e.clientY) : null;
            dragging = {
                tier: tier,
                handleType: handleType,
                track: track,
                fill: track.querySelector('.classic-tier-fill'),
                startMin: zone.min,
                startMax: zone.max,
                grab: at === null ? null : at - zone.min   // where in the bar it was taken, so it does not jump
            };
        };

        var handles = chart.querySelectorAll('.classic-tier-handle');
        for (var i = 0; i < handles.length; i++) {
            (function (handle) {
                handle.addEventListener('mousedown', function (e) {
                    e.preventDefault();
                    e.stopPropagation();
                    start(handle, handle.getAttribute('data-handle'), e);
                });
            })(handles[i]);
        }

        var fills = chart.querySelectorAll('.classic-tier-fill');
        for (var fi = 0; fi < fills.length; fi++) {
            (function (fill) {
                var col = fill.closest('.classic-tier-col');
                var own = [];
                for (var hi = 0; hi < handles.length; hi++) {
                    if (handles[hi].closest('.classic-tier-col') === col) own.push(handles[hi]);
                }
                fill.addEventListener('mousedown', function (e) {
                    e.preventDefault();
                    start(fill, self._handleNear(own, e.clientY) || 'move', e);
                });
                // The cursor says which: resize within a handle's grace, move elsewhere on the bar
                fill.addEventListener('mousemove', function (e) {
                    if (dragging || !fill.style) return;
                    fill.style.cursor = self._handleNear(own, e.clientY) ? 'ns-resize' : 'grab';
                });
            })(fills[fi]);
        }

        var onDocMove = function (e) {
            if (!dragging) return;
            var raw = pctAt(dragging.track, e.clientY);
            if (raw === null) return;
            // Snap to 5%
            var pct = Math.max(0, Math.min(100, Math.round(raw / 5) * 5));

            // Read on every move: a preset or the change callback may have replaced it
            var zone = self._zones()[dragging.tier];
            if (!zone) return;

            if (dragging.handleType === 'move') {
                // The whole bar, its width kept, inside 0-100
                var width = zone.max - zone.min;
                var min = Math.round((raw - (dragging.grab === null ? width / 2 : dragging.grab)) / 5) * 5;
                min = Math.max(0, Math.min(100 - width, min));
                zone.min = min;
                zone.max = min + width;
            } else if (dragging.handleType === 'max') {
                // Top handle: can't go below min + 5
                if (pct <= zone.min + 5) pct = zone.min + 5;
                if (pct > 100) pct = 100;
                zone.max = pct;
            } else {
                // Bottom handle: can't go above max - 5
                if (pct >= zone.max - 5) pct = zone.max - 5;
                if (pct < 0) pct = 0;
                zone.min = pct;
            }

            // Update visuals
            dragging.fill.style.bottom = zone.min + '%';
            dragging.fill.style.height = (zone.max - zone.min) + '%';
            var pctLabel = dragging.fill.querySelector('.classic-tier-pct');
            if (pctLabel) pctLabel.textContent = zone.min + '-' + zone.max;
        };
        // The change goes out on release, once: the preview lays the tree out again on each one, and doing that on
        // every move made the drag hitch
        var onDocUp = function () {
            if (!dragging) return;
            var done = dragging;
            dragging = null;
            var zone = self._zones()[done.tier];
            if (!zone || (zone.min === done.startMin && zone.max === done.startMax)) return;
            if (callbacks.onTierZoneChanged) callbacks.onTierZoneChanged(done.tier, zone.min, zone.max);
        };

        // One document pair at a time: every bind (a rebuilt panel, a preset) used to add one more
        if (this._tierZoneDocDrag) {
            document.removeEventListener('mousemove', this._tierZoneDocDrag.move);
            document.removeEventListener('mouseup', this._tierZoneDocDrag.up);
        }
        document.addEventListener('mousemove', onDocMove);
        document.addEventListener('mouseup', onDocUp);
        this._tierZoneDocDrag = { move: onDocMove, up: onDocUp };
    },

    /**
     * The handle ('max' / 'min') whose grace window holds clientY, or null (the bar moves). A handle's window is its
     * rendered height plus half its height again, reaching into the bar; where two windows meet, the nearer handle
     * wins. Null without a position. @private
     */
    _handleNear: function (handles, clientY) {
        if (typeof clientY !== 'number') return null;
        var best = null, bestDist = Infinity;
        for (var i = 0; i < handles.length; i++) {
            var h = handles[i];
            if (!h.getBoundingClientRect) continue;
            var r = h.getBoundingClientRect();
            if (!(r.height > 0)) continue;
            var type = h.getAttribute('data-handle');
            var grace = r.height * this.HANDLE_GRACE, end = r.top + r.height;
            var top = type === 'min' ? r.top - grace : r.top;               // the bottom handle reaches up,
            var bottom = type === 'max' ? end + grace : end;                // the top one down
            if (clientY < top || clientY > bottom) continue;
            var dist = Math.abs(clientY - (r.top + r.height / 2));
            if (dist < bestDist) { best = type; bestDist = dist; }
        }
        return best;
    },

    /** Re-apply tracked state to freshly built DOM elements. @private */
    _refreshDOM: function () {
        this.refreshTierChart();
    },

    // =========================================================================
    // SCAN STATUS
    // =========================================================================

    /**
     * Called when spell scan data changes.
     *
     * @param {boolean} hasSpells - True if spell data is available
     */
    updateScanStatus: function (hasSpells) {
        if (typeof TreeGrowth !== 'undefined') TreeGrowth.updateScanStatus(hasSpells);
    },

    // =========================================================================
    // TREE BUILT STATE
    // =========================================================================

    /**
     * Update button states after a tree is built or cleared.
     *
     * @param {boolean} built - True if a tree has been generated
     * @param {number} [nodeCount] - Number of nodes in the built tree
     */
    setTreeBuilt: function (built, nodeCount, totalPool) {
        if (typeof TreeGrowth !== 'undefined') TreeGrowth.setTreeBuilt(built, nodeCount, totalPool);
    },

    // =========================================================================
    // STATUS TEXT HELPER
    // =========================================================================

    /**
     * Set the status text element's content and color.
     *
     * @param {string} text - Status message
     * @param {string} tone - TreeGrowth.STATUS_COLORS key: working, done or error
     * @param {string} [key] - the text's language key, so a language switch writes it again
     * @param {Object} [params] - the key's {{variables}}
     */
    setStatusText: function (text, tone, key, params) {
        if (typeof TreeGrowth !== 'undefined' && TreeGrowth.setStatusText) TreeGrowth.setStatusText(text, tone, key, params);
    },

    // =========================================================================
    // INTERNAL HELPERS
    // =========================================================================

    /**
     * Enable or disable the Build Tree button based on current state.
     * Requires spells scanned.
     * @private
     */
    _updateBuildButton: function () {
        if (typeof TreeGrowth !== 'undefined') TreeGrowth.updateBuildButton();
    }
};

console.log('[ClassicSettings] Loaded');
