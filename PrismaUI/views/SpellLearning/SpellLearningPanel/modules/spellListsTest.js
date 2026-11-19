/**
 * SpellListsTest - node tests for the Spells tab's three columns (spellLists.js).
 *
 * Checks: the Spell list holds tome spells on neither list, by name, in its views (All, Flagged, one category); each row's
 * description (plugin, category, a source switched off; the gate's reasons in every column in the Flagged view, in their own span); blacklist entries named by
 * the scan's spell, Include keys the scan lacks named by the key; the titles' counts; the move buttons
 * per column in the columns' left-to-right order (X, B, W), B off for a key with no spell; an unchanged
 * state painting nothing; the filter (name or plugin, every column); the card's effects table and tags; the moves (onto and off each list, saved) and the click listener.
 *
 * Depends on: spellLists.js, run in a vm sandbox with stand-ins for everything it reads.
 */

var SpellListsTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** A fake element: attributes, children, text, one listener per type */
    _el: function(tag) {
        var self = this, attrs = {}, listeners = {};
        var e = {
            tagName: tag.toUpperCase(), children: [], textContent: '', disabled: false, parentNode: null,
            setAttribute: function(n, v) { attrs[n] = String(v); },
            getAttribute: function(n) { return attrs.hasOwnProperty(n) ? attrs[n] : null; },
            addEventListener: function(t, fn) { listeners[t] = fn; },
            fire: function(t, ev) { if (listeners[t]) listeners[t](ev); },
            appendChild: function(c) { c.parentNode = e; e.children.push(c); return c; },
            set innerHTML(v) { e.children = []; },
            cls: function() { return attrs['class'] || ''; },
            text: function() { return e.textContent + e.children.map(function(c) { return c.text(); }).join(''); },
            /** closest for 'button[data-move]' and 'li[data-index]' */
            closest: function(sel) {
                var m = /^(\w+)\[([\w-]+)\]$/.exec(sel);
                for (var n = e; n; n = n.parentNode) {
                    if (m && n.tagName === m[1].toUpperCase() && n.getAttribute(m[2]) !== null) return n;
                }
                return null;
            },
            find: function(test) {
                var out = [];
                (function walk(n) { n.children.forEach(function(c) { if (test(c)) out.push(c); walk(c); }); })(e);
                return out;
            }
        };
        return e;
    },

    _box: function() {
        var vm = require('vm'), fs = require('fs'), path = require('path'), self = this;
        var els = {};
        ['scanSpellListTitle', 'scanSpellListPreview', 'scanBlacklistTitle', 'scanBlacklistPreview',
            'scanWhitelistTitle', 'scanWhitelistPreview'].forEach(function(id) { els[id] = self._el(/Preview$/.test(id) ? 'ul' : 'div'); });
        var calls = { saved: 0, added: [], removed: [] };
        var box = {
            window: {}, console: console, calls: calls,
            document: { getElementById: function(id) { return els[id] || null; }, createElement: function(t) { return self._el(t); },
                documentElement: { lang: 'en' } },
            setI18n: function(el, key, params, fallback) {
                var t = fallback;
                for (var p in (params || {})) t = t.split('{{' + p + '}}').join(params[p]);
                el.textContent = t;
            },
            foldPluginName: function(p) { return String(p).toLowerCase(); },
            getLocalFormId: function(f) { return String(f).replace(/^0x/i, '').toLowerCase().slice(-6); },
            isTomeCandidate: function(s) { return s.taughtByTome !== false; },
            TomeSourceSetting: {
                NAMES: { standardSale: ['k.sale', 'Standard Vendors'], unreachable: ['k.un', 'Unreachable'] },
                categoryOf: function(s) { return s.source ? s.source.segment : null; },
                offTree: function(s) { return !!(s.source && s.source.segment === 'unreachable'); }
            },
            GateReview: {
                key: function(s) { return String(s.plugin).toLowerCase() + ':' + String(s.formId).replace(/^0x/i, '').toLowerCase().slice(-6); },
                flagged: function(s) { return !!s.gate; },
                _reasons: function(s) { return s.gate.reasons.slice(); }
            },
            settings: { spellBlacklist: [], gateReviewed: [] },
            addToBlacklist: function(s) { calls.added.push(s.name); box.settings.spellBlacklist.push({ formId: s.formId, name: s.name, plugin: s.plugin, localFormId: box.getLocalFormId(s.formId) }); },
            removeFromBlacklist: function(p, l, f) {
                calls.removed.push(p + ':' + l);
                box.settings.spellBlacklist = box.settings.spellBlacklist.filter(function(e) { return e.formId !== f; });
            },
            autoSaveSettings: function() { calls.saved++; }
        };
        vm.createContext(box);
        var file = path.join(__dirname, 'spellLists.js');
        vm.runInContext(fs.readFileSync(file, 'utf8'), box, { filename: file });
        box.els = els;
        return box;
    },

    _spells: function() {
        var sale = { segment: 'standardSale' };
        return [
            { formId: '0x00012FCC', name: 'Flames', plugin: 'Skyrim.esm', source: sale },
            { formId: '0x00012FCD', name: 'Frostbite', plugin: 'Skyrim.esm', source: sale },
            { formId: '0x01000801', name: 'Call Horse', plugin: 'Mod.esp', source: sale, gate: { reasons: ['summons a horse'] } },
            { formId: '0x01000802', name: 'Lost Art', plugin: 'Mod.esp', source: { segment: 'unreachable' } },
            { formId: '0x01000803', name: 'No Tome', plugin: 'Mod.esp', taughtByTome: false },
            { formId: '0x01000804', name: 'Kept One', plugin: 'Mod.esp', source: sale }
        ];
    },

    _model: function(box) {
        var L = box.SpellLists;
        var m = L.model(this._spells(), [{ formId: '0x00012FCD', plugin: 'Skyrim.esm', localFormId: '012fcd' }], ['mod.esp:000804', 'gone.esp:000001']);
        var names = function(rows) { return rows.map(function(r) { return r.name; }).join(','); };
        this.check(names(m.spells) === 'Call Horse,Flames,Lost Art',
            'Spell list: tome spells on neither list (no tome, blacklisted and kept left out), by name');
        this.check(m.spells[0].lines.join(' - ') === 'Mod.esp - Standard Vendors' && m.spells[0].reasons.join() === 'summons a horse' &&
            m.spells[2].lines.join(' - ') === 'Mod.esp - Unreachable - its source is switched off' &&
            m.spells[1].lines.join(' - ') === 'Skyrim.esm - Standard Vendors' && m.spells[1].reasons.length === 0,
            'Rows: the plugin, the category, a source switched off; the gate\'s reasons kept apart');
        L.setView('flagged');
        this.check(names(L.model(this._spells(), [], []).spells) === 'Call Horse', 'Flagged: only the spells to look over');
        L.setView('category', 'unreachable');
        this.check(names(L.model(this._spells(), [], []).spells) === 'Lost Art', 'View spells: only that category');
        L.setView('all');
        this.check(L.model(this._spells(), [], []).spells.length === 5, 'All: back to every tome spell');
        L.setQuery('  F ');
        var f = L.model(this._spells(), [{ formId: '0x00012FCD', plugin: 'Skyrim.esm', localFormId: '012fcd' }], ['mod.esp:000804']);
        this.check(names(f.spells) === 'Flames' && names(f.blacklisted) === 'Frostbite' && f.whitelisted.length === 0,
            'Filter: every column keeps the rows whose name holds the text, any case');
        L.setQuery('mod.esp');
        this.check(names(L.model(this._spells(), [], []).spells) === 'Call Horse,Kept One,Lost Art', 'Filter: a plugin name matches too');
        L.setQuery('');
        this.check(names(m.blacklisted) === 'Frostbite' && names(m.whitelisted) === 'gone.esp:000001,Kept One' &&
            m.whitelisted[0].plugin === 'gone.esp' && m.whitelisted[0].lines.join() === 'gone.esp',
            'Lists: a blacklist entry named by the scan\'s spell; an Include key the scan lacks named by the key, its plugin from it');
    },

    _render: function(box) {
        var L = box.SpellLists, els = box.els;
        box.settings.spellBlacklist = [{ formId: '0x00012FCD', plugin: 'Skyrim.esm', localFormId: '012fcd' }];
        box.settings.gateReviewed = ['mod.esp:000804', 'gone.esp:000001'];
        var spells = this._spells();
        spells[1].gate = { reasons: ['too costly'] };   // Frostbite: blacklisted, and flagged
        this.check(L.render(spells, box.settings) === true, 'Render: the first paint');
        this.check(els.scanSpellListTitle.textContent === 'Spell list: All (3)' && els.scanBlacklistTitle.textContent === 'Blacklisted (1)' &&
            els.scanWhitelistTitle.textContent === 'Whitelisted (2)', 'Render: each title with its count, the Spell list title with its view');
        var marks = function(list, i) {
            return list.children[i].find(function(c) { return c.tagName === 'BUTTON'; }).map(function(b) { return b.textContent; }).join('');
        };
        this.check(marks(els.scanSpellListPreview, 0) === 'BW' && marks(els.scanBlacklistPreview, 0) === 'XW' && marks(els.scanWhitelistPreview, 0) === 'XB',
            'Render: the move buttons in the columns\' left-to-right order (Spell list B W, Blacklisted X W, Whitelisted X B)');
        var gone = els.scanWhitelistPreview.children[0].find(function(c) { return c.getAttribute('data-move') === 'blacklisted'; })[0];
        var kept = els.scanWhitelistPreview.children[1].find(function(c) { return c.getAttribute('data-move') === 'blacklisted'; })[0];
        this.check(gone.disabled && !kept.disabled && /spell-move-blacklisted/.test(gone.cls()),
            'Render: B is off for an Include key the scan has no spell for (no form id to blacklist)');
        this.check(els.scanSpellListPreview.children[0].text() === 'Call Horse?Mod.esp - Standard VendorsBW',
            'Render: the name with its "?", then its description under it; no reasons in the All view');
        L.setView('flagged');
        var reason = els.scanSpellListPreview.children[0].find(function(c) { return /spell-list-reasons/.test(c.cls()); });
        this.check(els.scanSpellListPreview.children.length === 1 && reason.length === 1 && reason[0].textContent === ' - summons a horse',
            'Render Flagged: the reasons in their own span (their own colour)');
        var listed = els.scanBlacklistPreview.children[0].find(function(c) { return /spell-list-reasons/.test(c.cls()); });
        this.check(listed.length === 1 && listed[0].textContent === ' - too costly' &&
            els.scanSpellListTitle.textContent === 'Spell list: Flagged (1)',
            'Render Flagged: a listed spell has its reasons in its column too; the title names the view');
        L.setView('category', 'unreachable');
        this.check(els.scanSpellListTitle.textContent === 'Spell list: Unreachable (1)', 'Render: a category\'s view names it in the title');
        L.setView('all');
        this.check(L.render(spells, box.settings) === false, 'Render: an unchanged state paints nothing');
        this.check(L.render(spells, box.settings, true) === true, 'Render: force paints again');
    },

    _moves: function(box) {
        var L = box.SpellLists, c = box.calls;
        box.settings.spellBlacklist = [{ formId: '0x00012FCD', plugin: 'Skyrim.esm', localFormId: '012fcd' }];
        box.settings.gateReviewed = ['mod.esp:000804', 'gone.esp:000001'];
        var m = L.model(this._spells(), box.settings.spellBlacklist, box.settings.gateReviewed);
        var flames = m.spells.filter(function(r) { return r.name === 'Flames'; })[0];
        L.move(flames, 'blacklisted');
        this.check(c.added.join() === 'Flames' && c.saved === 1, 'Move: Spell list -> B blacklists it and saves');
        L.move(m.whitelisted[1], 'spells');
        this.check(box.settings.gateReviewed.join() === 'gone.esp:000001' && c.removed.length === 0,
            'Move: Whitelisted -> X takes the key off the Include list, no blacklist call');
        L.move(m.blacklisted[0], 'whitelisted');
        this.check(c.removed.join() === 'Skyrim.esm:012fcd' && box.settings.gateReviewed.indexOf('skyrim.esm:012fcd') !== -1,
            'Move: Blacklisted -> W takes it off the blacklist and onto the Include list');
        L.move(m.whitelisted[0], 'blacklisted');
        this.check(box.settings.gateReviewed.indexOf('gone.esp:000001') === -1 && c.added.length === 1,
            'Move: a key with no spell leaves the Include list but is not blacklisted (no form id)');

        // The click listener: the button's column, the row's index
        box.settings.spellBlacklist = [];
        box.settings.gateReviewed = [];
        L.render(this._spells(), box.settings, true);
        var list = box.els.scanSpellListPreview;
        var row = list.children[1];
        var w = row.find(function(b) { return b.getAttribute('data-move') === 'whitelisted'; })[0];
        list.fire('click', { target: w });
        this.check(box.settings.gateReviewed.join() === 'skyrim.esm:012fcc', 'Click: W on a Spell list row whitelists that row\'s spell');
    },

    /** The "?" card's text (spellPeek.js): the effects' descriptions filled in, hidden effects left out */
    _card: function() {
        var vm = require('vm'), fs = require('fs'), path = require('path');
        var box = { window: {} };
        vm.createContext(box);
        var file = path.join(__dirname, 'spellPeek.js');
        vm.runInContext(fs.readFileSync(file, 'utf8'), box, { filename: file });
        var P = box.SpellPeek;
        var spell = { effects: [
            { name: 'Flames', magnitude: 8, duration: 1, area: 0, description: 'A gout of fire that does <mag> points per second.' },
            { name: 'Burn', magnitude: 2.5, duration: 5, area: 10, description: 'Burns for <dur> seconds in <area> feet, <15> <Global=X>.' },
            { name: 'Helper', magnitude: 1, duration: 0, flags: { hideInUI: true }, description: 'Hidden <mag>.' },
            { name: 'Flames', magnitude: 8, duration: 1, area: 0, description: 'A gout of fire that does <mag> points per second.' }
        ] };
        this.check(P.description(spell) === 'A gout of fire that does 8 points per second. Burns for 5 seconds in 10 feet, .',
            'Card: the description is the shown effects\' texts, figures filled in, other tags dropped, each text once');
        var rows = P.effectRows(spell).map(function(r) { return r.join(','); }).join(' | ');
        this.check(rows === 'Flames,8,1s,— | Burn,3,5s,10 ft | Flames,8,1s,—',
            'Card: one table row per shown effect: power, duration, size, a dash where there is none');
        this.check(P.descriptionParts(spell).length === 2,
            'Card: one description block per distinct effect text (a line drawn between them)');
        this.check(P.chipIds({ traits: ['school.destruction', 'element.fire', 'form.spray'] }).join() === 'element.fire,form.spray' &&
            P.chipIds({ chips: ['cast.concentration'], traits: ['element.frost'] }).join() === 'cast.concentration',
            'Card: the spell\'s tags (chips before traits), its school left to the badge');
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var parts = [['Model', this._model], ['Render', this._render], ['Moves', this._moves], ['Card', this._card]];
        for (var i = 0; i < parts.length; i++) {
            try { parts[i][1].call(this, this._box()); } catch (e) { this.check(false, parts[i][0] + ' threw: ' + (e && e.stack ? e.stack : e)); }
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.SpellListsTest = SpellListsTest;
if (typeof global !== 'undefined') global.SpellListsTest = SpellListsTest;
