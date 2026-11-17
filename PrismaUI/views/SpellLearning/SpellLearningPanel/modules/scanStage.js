/**
 * ScanStage - the Scan tab's summary line and its one main button (2.0 review, item 7)
 *
 * One line says where the player is: "1812 spells found · 1735 go in the tree · not built
 * yet". Found counts the spells that could go on the tree (isTomeCandidate: a tome teaches
 * it, not a voice slot spell, not another plugin's copy), whatever the player's choices;
 * the scan's other spells are invalid (no whitelist brings them in). A second line says
 * "X spells invalid (no tome, powers, copies); X spells left off the tree." Left off is
 * found minus in the tree: off by a source switch (Where Spells Come From), the
 * blacklist or a plugin turned off; once built, the ones the builder did not place follow.
 *
 * The main button follows the stage: Scan Spells, Build Tree, Apply Tree (Build and Apply
 * stay two steps: the player looks at the preview before applying), then "Tree applied".
 * It is a relay, as the Easy page's buttons are (easyMode.js): it clicks the real button
 * (#scanBtn, the Easy or Complex Build, #tgApplyBtn), whose handlers, guards and the build
 * at game start (autoBuild.js) stay as they were. The other actions are small text
 * buttons: Scan again, Import, Clear draft tree (it clears the built draft, not anyone's
 * progress). "Scan details" is a section: the counts per school, beside previews of the
 * Blacklisted (settings.spellBlacklist) and Whitelisted (settings.gateReviewed, Gate Review's
 * Include) lists (on Where Spells Come From's Spells tab) and the override button (the blacklist window for now).
 *
 * "Applied" is this module's own flag: set when Apply (or the build at game start) saved
 * the draft (TreeGrowthClassic.applyTree calls markApplied), dropped when the draft changes
 * (TreeGrowth.setTreeBuilt, Pre Req Master's lock changes and an Import call onDraftChanged) or a
 * new scan comes in (updateSpellData calls onScanned). A draft keeps the real Build off until it is
 * cleared (TreeGrowth.updateBuildButton), so while one exists the stage is Apply, and Clear draft
 * tree - shown whenever a draft exists - is the way back to Build.
 *
 * Depends on: state (lastSpellData), settings (spellBlacklist, pluginWhitelist),
 * isTomeCandidate / isTaughtByTome / filterBlacklistedSpells / filterWhitelistedSpells /
 * spellPluginName (proceduralTreeBuilder.js), foldPluginName (constants.js), GateReview.key
 * and TomeSourceSetting.renderCounts (optional), TreeGrowth, setI18n / tOr (i18n.js),
 * HomIcons (the relayed buttons' labels are left alone).
 */
var ScanStage = {
    SEPARATOR: ' · ',
    GAP_SEPARATOR: '; ',

    _applied: false,
    _initialized: false,

    /** The page's document (a stand-in under node tests) */
    _doc: function() { return document; },
    _el: function(id) { return this._doc().getElementById(id); },

    // =========================================================================
    // WHAT THE PAGE SAYS (pure: tested without a page)
    // =========================================================================

    /**
     * total: every scanned spell (not shown); found: the ones that could go on the tree
     * (isTomeCandidate); invalid: total minus found; inTree: found through the build's filters
     * (getPrimedSpells: the source switches and Include, the blacklist, the plugin list);
     * leftOff: found minus inTree.
     * @param {Array} spells - the scan's spells
     * @returns {{total:number, found:number, invalid:number, inTree:number, leftOff:number}}
     */
    counts: function(spells) {
        var all = Array.isArray(spells) ? spells : [];
        var found = (typeof isTomeCandidate === 'function') ? all.filter(isTomeCandidate) : all.slice();
        var a = (typeof isTaughtByTome === 'function') ? found.filter(isTaughtByTome) : found;
        var b = (typeof filterBlacklistedSpells === 'function') ? filterBlacklistedSpells(a) : a;
        var c = (typeof filterWhitelistedSpells === 'function') ? filterWhitelistedSpells(b) : b;
        return {
            total: all.length,
            found: found.length,
            invalid: all.length - found.length,
            inTree: c.length,
            leftOff: found.length - c.length
        };
    },

    /**
     * @param {{scanning:boolean, hasSpells:boolean, building:boolean, built:boolean, applied:boolean}} s
     * @returns {string} 'scanning' | 'scan' | 'building' | 'build' | 'apply' | 'applied'
     */
    stageOf: function(s) {
        if (s.scanning) return 'scanning';
        if (!s.hasSpells) return 'scan';
        if (s.building) return 'building';
        if (!s.built) return 'build';
        return s.applied ? 'applied' : 'apply';
    },

    /**
     * The summary as language keys: { parts, gap } - each item [key, params, English].
     * @param {string} stage
     * @param {Object} c - counts()
     * @param {number} placed - spells the built draft placed (TreeGrowth._nodeCount)
     * @param {number} [treeCount] - spells in the saved tree (before a scan)
     * @param {boolean} [scanned] - a scan came in, even one with no spell
     */
    summary: function(stage, c, placed, treeCount, scanned) {
        // A scan that found no spell: say so (the bar's "0 spells scanned" is out of sight), not "No scan yet"
        if (stage === 'scan' && scanned) return { parts: [['scan.summaryFound', { count: c.found }, '{{count}} spells found']], gap: [] };
        if (stage === 'scan') {
            // A saved tree came in with the game, but no scan this session (the build at game start only
            // scans when there is no tree): say the tree is there, not that nothing is
            if (treeCount > 0) return { parts: [['scan.summaryTreeNoScan', { count: treeCount }, 'Your tree has {{count}} spells · scan to build a new one']], gap: [] };
            return { parts: [['scan.summaryNoScan', null, 'No scan yet: scan your spells to build a tree from them']], gap: [] };
        }
        if (stage === 'scanning') return { parts: [['status.scanningAllSpells', null, 'Scanning all spells...']], gap: [] };
        var built = stage === 'apply' || stage === 'applied';
        var inTree = built ? placed : c.inTree;
        var parts = [['scan.summaryFound', { count: c.found }, '{{count}} spells found']];
        if (stage === 'applied') parts.push(['scan.summaryInYourTree', { count: inTree }, '{{count}} in your tree']);
        else if (built) parts.push(['scan.summaryInNewTree', { count: inTree }, '{{count}} in the new tree']);
        else parts.push(['scan.summaryInTree', { count: inTree }, '{{count}} go in the tree']);
        var state = { build: ['scan.summaryNotBuilt', 'not built yet'], building: ['scan.summaryBuilding', 'being built'],
            apply: ['scan.summaryNotApplied', 'not applied yet'], applied: ['scan.summaryApplied', 'applied'] }[stage];
        if (state) parts.push([state[0], null, state[1]]);

        // "3070 spells invalid (no tome, powers, copies); 77 spells left off the tree." (refresh joins them)
        var gap = [];
        if (c.invalid > 0) gap.push(['scan.gapInvalid', { count: c.invalid }, '{{count}} spells invalid (no tome, powers, copies)']);
        if (c.leftOff > 0) gap.push(['scan.gapLeftOff', { count: c.leftOff }, '{{count}} spells left off the tree']);
        var notPlaced = built ? c.inTree - placed : 0;
        if (notPlaced > 0) gap.push(['scan.gapNotPlaced', { count: notPlaced }, '{{count}} not placed by the builder']);
        return { parts: parts, gap: gap };
    },

    /** The main button for a stage: [key, English, which button it clicks] */
    mainButton: function(stage) {
        switch (stage) {
            case 'scan': return ['buttons.scanSpells', 'Scan Spells', 'scan'];
            case 'scanning': return ['status.scanning', 'Scanning...', null];
            case 'build': case 'building': return ['buttons.buildTree', 'Build Tree', 'build'];
            case 'apply': return ['buttons.applyTree', 'Apply Tree', 'apply'];
            default: return ['scan.stageApplied', 'Tree applied', null];
        }
    },

    // =========================================================================
    // THE PAGE
    // =========================================================================

    init: function() {
        if (this._initialized) return;
        this._initialized = true;
        var self = this;
        var bind = function(id, fn) { var el = self._el(id); if (el) el.addEventListener('click', fn); };
        bind('scanStageBtn', function() { self.onMainClick(); });
        bind('scanAgainBtn', function() { self._click('scanBtn'); });
        bind('scanClearDraftBtn', function() { self._click('tgClearBtn'); });
        this.refresh();
    },

    _click: function(id) {
        var btn = this._el(id);
        if (btn && !btn.disabled) btn.click();
    },

    _isComplex: function() {
        var complex = this._el('scannerComplexContent');
        return !!complex && complex.style.display !== 'none';
    },

    /** Easy's Build applies the chosen preset first; Complex builds on the page's settings */
    _buildId: function() { return this._isComplex() ? 'tgBuildBtn' : 'easyBuildBtn'; },

    /** The live inputs of stageOf */
    _inputs: function() {
        var data = (typeof state !== 'undefined' && state) ? state.lastSpellData : null;
        var scanBtn = this._el('scanBtn');
        var growth = (typeof TreeGrowth !== 'undefined') ? TreeGrowth : null;
        return {
            // The Scan button's own scan, or one C++ says is running (edit mode's spawn list asks for one
            // without the button; its "Scanning all spells..." line in the bar is out of sight)
            scanning: !!(scanBtn && scanBtn.disabled) ||
                (typeof scanStatusKey === 'function' && scanStatusKey() === 'status.scanningAllSpells'),
            scanned: !!(data && Array.isArray(data.spells)),
            hasSpells: !!(data && data.spells && data.spells.length > 0),
            building: !!(growth && growth._building),
            built: !!(growth && growth._treeBuilt),
            applied: this._applied
        };
    },

    stage: function() { return this.stageOf(this._inputs()); },

    /** The main button: the real button of its stage, as the player would press it */
    onMainClick: function() {
        var target = this.mainButton(this.stage())[2];
        if (target === 'scan') this._click('scanBtn');
        else if (target === 'build') this._click(this._buildId());
        else if (target === 'apply') this._click('tgApplyBtn');
    },

    /** Apply (or the build at game start) saved the draft */
    markApplied: function() {
        this._applied = true;
        this.refresh();
    },

    /** A new draft, a cleared one, its locks changed, or an Import replaced the tree: what is shown is not applied */
    onDraftChanged: function() {
        this._applied = false;
        this.refresh();
    },

    /** A scan came in: nothing on the page is the applied tree any more (a draft from before it stays, to apply or clear) */
    onScanned: function() {
        this._applied = false;
        this.refresh();
        if (typeof SourceListTabs !== 'undefined') SourceListTabs.refresh();   // the plugin list, when it shows
    },

    /** Write a list of [key, params, English] as spans joined by sep */
    _writeParts: function(el, parts, sep) {
        if (!el) return;
        var doc = this._doc();
        el.innerHTML = '';
        for (var i = 0; i < parts.length; i++) {
            if (i > 0) el.appendChild(doc.createTextNode(sep));
            var span = doc.createElement('span');
            setI18n(span, parts[i][0], parts[i][1], parts[i][2]);
            el.appendChild(span);
        }
    },

    /** Everything on the page from the current state; cheap, called on every change */
    refresh: function() {
        var doc = this._doc();
        if (!doc || !this._el('scanStageBtn')) return;
        var inputs = this._inputs();
        var stage = this.stageOf(inputs);
        var data = (typeof state !== 'undefined' && state) ? state.lastSpellData : null;
        var spells = (data && data.spells) || [];
        var c = this.counts(spells);
        var placed = (typeof TreeGrowth !== 'undefined' && TreeGrowth._nodeCount) || 0;
        var tree = (typeof state !== 'undefined' && state && state.treeData && state.treeData.nodes) ? state.treeData.nodes.length : 0;
        var s = this.summary(stage, c, placed, tree, inputs.scanned);

        this._writeParts(this._el('scanSummary'), s.parts, this.SEPARATOR);
        var gapEl = this._el('scanSummaryGap');
        if (gapEl) {
            this._writeParts(gapEl, s.gap, this.GAP_SEPARATOR);
            if (s.gap.length > 0) gapEl.appendChild(doc.createTextNode('.'));
            gapEl.classList.toggle('hidden', s.gap.length === 0);
        }

        var main = this.mainButton(stage);
        var mainBtn = this._el('scanStageBtn');
        var mainLabel = this._el('scanStageLabel');
        setI18n(mainLabel || mainBtn, main[0], null, main[1]);
        // Its state from the Complex buttons, which Easy's mirror (_syncEasyButtonStates, a MutationObserver
        // that runs after this): Easy's copy may not have caught up yet
        var target = main[2] === 'scan' ? 'scanBtn' : main[2] === 'build' ? 'tgBuildBtn' : main[2] === 'apply' ? 'tgApplyBtn' : null;
        var targetBtn = target ? this._el(target) : null;
        mainBtn.disabled = !targetBtn || !!targetBtn.disabled || stage === 'building';
        mainBtn.setAttribute('data-stage', stage);

        this._show('scanAgainBtn', inputs.hasSpells && !inputs.scanning);
        var clearBtn = this._el('tgClearBtn');
        // While a draft exists (also once applied): clearing it is the way back to Build
        this._show('scanClearDraftBtn', inputs.built && !!clearBtn && !clearBtn.disabled);
        // Scan details: the spells found (the counts per school are updatePrimedCount's)
        var foundEl = this._el('statTotalSpells');
        if (foundEl) foundEl.textContent = inputs.scanned ? String(c.found) : '--';

        // The list previews
        var opts = (typeof settings !== 'undefined' && settings) ? settings : {};
        var lists = this.listPreview(spells, opts.spellBlacklist, opts.gateReviewed);
        this._writeList('scanBlacklistTitle', 'scanBlacklistPreview', ['scan.blacklisted', 'Blacklisted ({{count}})'], lists.blacklisted);
        this._writeList('scanWhitelistTitle', 'scanWhitelistPreview', ['scan.whitelisted', 'Whitelisted ({{count}})'], lists.whitelisted);
        if (typeof TomeSourceSetting !== 'undefined' && TomeSourceSetting.renderCounts) TomeSourceSetting.renderCounts(spells);
    },

    /** A preview list: its title with the count, one item per name (a muted "None" when empty) */
    _writeList: function(titleId, listId, title, names) {
        var titleEl = this._el(titleId);
        if (titleEl) setI18n(titleEl, title[0], { count: names.length }, title[1]);
        var list = this._el(listId);
        if (!list) return;
        var doc = this._doc();
        list.innerHTML = '';
        if (!names.length) {
            var empty = doc.createElement('li');
            empty.className = 'scan-list-empty';
            setI18n(empty, 'scan.listEmpty', null, 'None');
            list.appendChild(empty);
            return;
        }
        for (var i = 0; i < names.length; i++) {
            var li = doc.createElement('li');
            li.textContent = names[i];
            list.appendChild(li);
        }
    },

    /**
     * The names in the two lists, sorted: the blacklist's saved names, and the Include list's keys
     * named by the scan's spell of that key (GateReview.key), the key itself when the scan has none.
     * @returns {{blacklisted: string[], whitelisted: string[]}}
     */
    listPreview: function(spells, blacklist, kept) {
        var byKey = {};
        if (typeof GateReview !== 'undefined' && GateReview.key) {
            (spells || []).forEach(function(s) { if (s && s.plugin && s.formId) byKey[GateReview.key(s)] = s.name || s.formId; });
        }
        var sort = function(a, b) { return String(a).localeCompare(String(b)); };
        var entryName = function(e) { return e.name || e.formId || ((e.plugin || '') + ':' + (e.localFormId || '')); };
        return {
            blacklisted: (blacklist || []).map(entryName).sort(sort),
            whitelisted: (kept || []).map(function(k) { return byKey[k] || k; }).sort(sort)
        };
    },

    _show: function(id, on) {
        var el = this._el(id);
        if (el) el.classList.toggle('hidden', !on);
    }
};

window.ScanStage = ScanStage;
