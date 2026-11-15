/**
 * TomeSourceSettingTest - node tests for Settings > Spell Tomes > Where Tomes Come From
 * (tomeSourceSetting.js) and the tree filter that reads it (proceduralTreeBuilder.js
 * isTreeCandidate, run-tests.js).
 *
 * Checks: the defaults (general sale, wide drops and uncommon vendors on, the rest
 * off; review always on, unreachable always off), offTree by the source object, by
 * tomeUnreachable, and by questOnly for a scan without source objects; a toggle
 * that saves and changes the tree's reach at once; load with missing or wrong
 * values, the save -> load round trip into config.tomeSources, Reset; the reason
 * Gate Review shows; the player's Include bringing any one spell back; the spell
 * gate's "out" band and freeSpell no longer leaving a spell out.
 *
 * Depends on: tomeSourceSetting.js, proceduralTreeBuilder.js, gateReview.js - under
 * node this file runs them into the global scope the way the page's script tags do.
 * Swaps the globals `settings`, `document`, `autoSaveSettings` and `updatePrimedCount`
 * for its own while it runs.
 */

var TomeSourceSettingTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    _load: function(g) {
        if (typeof require !== 'function') return;
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var files = [['tomeSourceSetting.js', 'TomeSourceSetting'], ['proceduralTreeBuilder.js', 'isTreeCandidate'],
            ['gateReview.js', 'GateReview']];
        for (var i = 0; i < files.length; i++) {
            if (typeof g[files[i][1]] !== 'undefined') continue;
            var file = path.join(__dirname, files[i][0]);
            vm.runInThisContext(fs.readFileSync(file, 'utf8'), { filename: file });
        }
    },

    /** A stand-in toggle: checked, and its change listener */
    _el: function() {
        return {
            checked: false, _listeners: {},
            addEventListener: function(type, fn) { this._listeners[type] = fn; },
            fire: function(type) { if (this._listeners[type]) this._listeners[type].call(this); }
        };
    },

    run: function() {
        var g = (typeof global !== 'undefined') ? global : window;
        this.passed = 0;
        this.failed = 0;
        try { this._load(g); } catch (e) { console.log('  load failed: ' + e.message); }
        if (typeof g.TomeSourceSetting === 'undefined' || typeof g.isTreeCandidate !== 'function') {
            this.check(false, 'tomeSourceSetting.js and proceduralTreeBuilder.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var S = g.TomeSourceSetting;
        var old = { settings: g.settings, document: g.document, save: g.autoSaveSettings, count: g.updatePrimedCount,
            local: g.getLocalFormId };
        var els = {}, self = this, saves = 0, counts = 0;
        g.document = { getElementById: function(id) { return els[id] || (els[id] = self._el()); } };
        g.autoSaveSettings = function() { saves++; };
        g.updatePrimedCount = function() { counts++; };
        g.getLocalFormId = function(id) { return String(id).slice(-6).toLowerCase(); };
        var spell = function(id, segment, extra) {
            var s = { formId: '0x010008' + id, plugin: 'Mod.esp', taughtByTome: true };
            if (segment) s.source = { segment: segment, 'class': 'vendor', subtype: '', detail: '' };
            for (var k in (extra || {})) s[k] = extra[k];
            return s;
        };
        try {
            g.settings = {};
            S.loadFrom({});
            var on = S.SEGMENTS.filter(function(k) { return S.onTree(k); }).join(',');
            this.check(on === 'ritualQuest,standardSale,standardLoot' && S.onTree('review') && !S.onTree('unreachable') &&
                !S.onTree('somethingNew'), 'defaults: ritual quest, standard sale, standard loot on; review on; unreachable off');
            this.check(!S.offTree(spell('01', 'standardSale')) && S.offTree(spell('02', 'regionalLoot')) &&
                !S.offTree(spell('03', 'review')) && S.offTree(spell('04', 'unreachable')),
                'offTree goes by the source segment');
            this.check(S.offTree(spell('05', 'standardSale', { tomeUnreachable: true })) &&
                !S.offTree(spell('06', 'standardLoot', { questOnly: true })),
                'tomeUnreachable is always off; with a source, questOnly does not count');
            this.check(S.offTree(spell('07', null, { questOnly: true })) && !S.offTree(spell('08')) &&
                S.offTree(spell('09', 'somethingNew', { questOnly: true })),
                'a scan without source objects (or an unknown segment) falls back on questOnly');

            S.init();
            var toggle = els[S.toggleId('regionalLoot')];
            this.check(!!toggle && toggle.checked === false && els[S.toggleId('standardSale')].checked === true,
                'the toggles show the switches');
            toggle.checked = true;
            toggle.fire('change');
            this.check(g.settings.tomeSources.regionalLoot === true && !S.offTree(spell('02', 'regionalLoot')) &&
                saves === 1 && counts === 1, 'a toggle saves, and the tree takes the segment at once');

            var config = S.saveTo({ other: 1 });
            this.check(config.other === 1 && config.tomeSources.regionalLoot === true && config.tomeSources.standardSale === true &&
                Object.keys(config.tomeSources).length === S.SEGMENTS.length, 'save writes config.tomeSources, every switch');
            g.settings = {};
            S.loadFrom({ tomeSources: { regionalLoot: true, standardSale: 'no', crafted: 1 } });
            this.check(g.settings.tomeSources.regionalLoot === true && g.settings.tomeSources.standardSale === true &&
                g.settings.tomeSources.crafted === false && els[S.toggleId('regionalLoot')].checked === true,
                'load: booleans only, the defaults for the rest');
            S.reset();
            this.check(g.settings.tomeSources.regionalLoot === false && els[S.toggleId('regionalLoot')].checked === false,
                'Reset puts the defaults back');

            var shop = spell('10', 'spellpackVendor');
            shop.source.shop = 'apocalypse - magic of skyrim.esp';
            this.check(S.reasonText(shop) === 'Spellpack mod vendors (apocalypse - magic of skyrim.esp)' &&
                S.reasonText(spell('11', 'standardSale', { tomeUnreachable: true })) === 'nothing hands out its tomes' &&
                S.reasonText(spell('12', null, { questOnly: true })) === 'only a quest hands out its tomes',
                'the reason Gate Review shows');

            // The tree filter: the source decides, Include brings any one back, the gate only flags
            g.settings = { gateReviewed: [] };
            var loot = spell('13', 'regionalLoot'), lost = spell('14', 'unreachable', { tomeUnreachable: true });
            this.check(!g.isTreeCandidate(loot) && !g.isTreeCandidate(lost) && g.isTreeCandidate(spell('15', 'review')),
                'the tree leaves out switched-off and unreachable spells, keeps review ones');
            g.settings.gateReviewed = ['mod.esp:000813', 'mod.esp:000814'];
            this.check(g.isTreeCandidate(loot) && g.isTreeCandidate(lost), 'Include brings either back');
            g.settings.gateReviewed = [];
            this.check(g.isTaughtByTome(spell('16', 'standardSale', { gate: { band: 'out', reasons: ['free'] } })) &&
                g.isTaughtByTome(spell('17', null, { freeSpell: true })), 'the gate\'s "out" band and freeSpell stay in');
        } finally {
            g.settings = old.settings;
            g.document = old.document;
            g.autoSaveSettings = old.save;
            g.updatePrimedCount = old.count;
            g.getLocalFormId = old.local;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.TomeSourceSettingTest = TomeSourceSettingTest;
if (typeof global !== 'undefined') global.TomeSourceSettingTest = TomeSourceSettingTest;
