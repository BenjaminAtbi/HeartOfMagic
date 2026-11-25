/**
 * TomeSourceSetting - the Complex tree builder's "Where spells come from": which
 * distribution segments put their spells on the tree.
 *
 * The scan gives every spell with a tome a source object (TomeReach's distribution
 * model, SpellScannerScan.cpp): { segment, class, subtype, detail, shop? }. The
 * segment is one of unreachable, spellpackVendor, ritualQuest, standardSale,
 * standardLoot, regionalLoot, uniqueVendor, uniqueReward, crafted, review (the
 * order the model tries them, first match winning). Each segment but review has a
 * switch here; a spell of a segment switched off is off the tree (offTree) until the
 * player includes it in Gate Review. Unreachable is the last row, off by default: its
 * spells (tomeUnreachable, nothing hands their tomes out) may be unfinished or bugged.
 * Review is no category of its own: every spell is meant to fall in one of the others,
 * and one that does not (review, or a segment key this panel does not know) goes with
 * Unreachable, counted and switched with it. A scan without source objects (the reach
 * pass answered: the model could not run) falls back on its questOnly flag.
 *
 * Each row shows how many found spells (isTomeCandidate) its category holds, switched on
 * or not, and how many of them are on the Whitelisted (Gate Review's Include) and
 * Blacklisted lists (renderCounts, after a scan and on every list change). A hidden
 * button per row waits for the scoped override menu.
 *
 * Saved as config.tomeSources = { standardSale: true, ... } (the keys of SegmentKey,
 * TomeReachSwitches.h, with the same defaults). The DLL reads it for the scan's
 * questOnly flag and Tree Graft (GraftCandidates.h); the panel reads it live, so a
 * switch counts at the next build without a new scan. Not in settings presets.
 *
 * Depends on: settings, autoSaveSettings, updatePrimedCount (optional), tOr (i18n.js, optional),
 *   isTomeCandidate / filterBlacklistedSpells (proceduralTreeBuilder.js), GateReview.isKept (optional)
 */

var TomeSourceSetting = {

    /** The switched segments, in the page's order: the model's, with Unreachable (its first rule) last */
    SEGMENTS: ['spellpackVendor', 'ritualQuest', 'standardSale', 'standardLoot', 'regionalLoot', 'uniqueVendor',
        'uniqueReward', 'crafted', 'unreachable'],

    /** The segment review and unknown segment keys go with */
    CATCH_ALL: 'unreachable',

    DEFAULTS: {
        unreachable: false,
        spellpackVendor: false,
        ritualQuest: true,
        standardSale: true,
        standardLoot: true,
        regionalLoot: false,
        uniqueVendor: false,
        uniqueReward: false,
        crafted: false
    },

    /** Every segment's name ([i18n key, English]); also the Gate Review reason of a spell off the tree */
    NAMES: {
        unreachable: ['settings.tomeSources.unreachable', 'Unreachable'],
        spellpackVendor: ['settings.tomeSources.spellpackVendor', 'Spellpack mod vendors'],
        ritualQuest: ['settings.tomeSources.ritualQuest', 'Ritual Quest'],
        standardSale: ['settings.tomeSources.standardSale', 'Standard Vendors'],
        standardLoot: ['settings.tomeSources.standardLoot', 'Standard Loot'],
        regionalLoot: ['settings.tomeSources.regionalLoot', 'Regional Loot'],
        uniqueVendor: ['settings.tomeSources.uniqueVendor', 'Unique vendors'],
        uniqueReward: ['settings.tomeSources.uniqueReward', 'Unique rewards'],
        crafted: ['settings.tomeSources.crafted', 'Crafted'],
        review: ['settings.tomeSources.review', 'source unclear'],
        questOnly: ['settings.tomeSources.questOnly', 'only a quest hands out its tomes']
    },

    /** The toggle of a segment on the page */
    toggleId: function(segment) {
        return 'tomeSource' + segment.charAt(0).toUpperCase() + segment.slice(1) + 'Toggle';
    },

    /** settings.tomeSources, made with the defaults when missing */
    _switches: function() {
        if (typeof settings === 'undefined' || !settings) return this.DEFAULTS;
        if (!settings.tomeSources || typeof settings.tomeSources !== 'object') settings.tomeSources = {};
        var s = settings.tomeSources;
        for (var k in this.DEFAULTS) {
            if (this.DEFAULTS.hasOwnProperty(k) && typeof s[k] !== 'boolean') s[k] = this.DEFAULTS[k];
        }
        return s;
    },

    /** Whether a segment's spells go on the tree (TomeReachSwitches.h SegmentSwitches::OnTree): review and
     *  unknown keys follow Unreachable's switch */
    onTree: function(segment) {
        var key = this.DEFAULTS.hasOwnProperty(segment) ? segment : this.CATCH_ALL;
        return this._switches()[key] === true;
    },

    /** The spell's segment key, or null for a scan without source objects */
    segmentOf: function(spell) {
        var src = spell && spell.source;
        return (src && typeof src === 'object' && typeof src.segment === 'string') ? src.segment : null;
    },

    /** The row a spell counts in: its segment, Unreachable for tomeUnreachable, review and unknown keys;
     *  null for a scan without source objects */
    categoryOf: function(spell) {
        if (!spell) return null;
        if (spell.tomeUnreachable === true) return this.CATCH_ALL;
        var segment = this.segmentOf(spell);
        if (segment === null) return null;
        return this.DEFAULTS.hasOwnProperty(segment) ? segment : this.CATCH_ALL;
    },

    /**
     * Off the tree by where its tomes come from: its category is switched off (Unreachable, off by
     * default, for tomeUnreachable, review and unknown keys); without a source object, the scan's
     * questOnly flag. The player's Include (GateReview.isKept) is the caller's to apply.
     * GraftCandidates.h OffTreeBySource is the DLL's copy.
     */
    offTree: function(spell) {
        if (!spell) return false;
        var category = this.categoryOf(spell);
        if (category !== null) return !this.onTree(category);
        return spell.questOnly === true;
    },

    /**
     * Per category, the found spells (isTomeCandidate) it holds, and how many of them are on the
     * Include list (whitelisted) and the blacklist. Spells of a scan without source objects count nowhere.
     * @returns {Object} { <segment>: { spells, whitelisted, blacklisted } }
     */
    counts: function(spells) {
        var out = {}, self = this;
        this.SEGMENTS.forEach(function(k) { out[k] = { spells: 0, whitelisted: 0, blacklisted: 0 }; });
        var found = (spells || []).filter(function(s) { return typeof isTomeCandidate !== 'function' || isTomeCandidate(s); });
        // filterBlacklistedSpells keeps the order, so one walk tells which it dropped
        var kept = typeof filterBlacklistedSpells === 'function' ? filterBlacklistedSpells(found) : found;
        var k = 0;
        found.forEach(function(spell) {
            var dropped = kept[k] !== spell;
            if (!dropped) k++;
            var row = out[self.categoryOf(spell)];
            if (!row) return;
            row.spells++;
            if (dropped) row.blacklisted++;
            if (typeof GateReview !== 'undefined' && GateReview.isKept(spell)) row.whitelisted++;
        });
        return out;
    },

    /** "345 spells (2 Whitelisted / 5 Blacklisted)", the lists' part only when either has any */
    countText: function(c) {
        var t = function(key, params, english) {
            if (typeof tOr === 'function') return tOr(key, params, english);
            for (var p in params) english = english.split('{{' + p + '}}').join(params[p]);
            return english;
        };
        var text = t('settings.tomeSources.count', { count: c.spells }, '{{count}} spells');
        // Each list only when it has any: "(2 Whitelisted)", "(1 Blacklisted)" or both
        var lists = [];
        if (c.whitelisted > 0) lists.push(t('settings.tomeSources.countWhitelisted', { count: c.whitelisted }, '{{count}} Whitelisted'));
        if (c.blacklisted > 0) lists.push(t('settings.tomeSources.countBlacklisted', { count: c.blacklisted }, '{{count}} Blacklisted'));
        if (lists.length) text += ' ' + t('settings.tomeSources.countLists', { lists: lists.join(' / ') }, '({{lists}})');
        return text;
    },

    /** Write each row's count (empty before a scan); ScanStage.refresh calls it on every change */
    renderCounts: function(spells) {
        var counts = (spells && spells.length) ? this.counts(spells) : null;
        for (var i = 0; i < this.SEGMENTS.length; i++) {
            var cell = this._countCell(this.SEGMENTS[i]);
            if (cell) cell.firstChild.textContent = counts ? this.countText(counts[this.SEGMENTS[i]]) : '';
        }
    },

    /**
     * A row's count column, made once after its text (the switch leads the row): the count, then the button of
     * the scoped override menu (hidden until that menu exists); with it, the View spells column at the row's end
     */
    _countCell: function(segment) {
        var toggle = document.getElementById(this.toggleId(segment));
        var row = (toggle && toggle.closest) ? toggle.closest('.setting-row') : null;
        if (!row) return null;
        var cell = row.querySelector('.tome-source-count-cell');
        if (cell) return cell;
        cell = document.createElement('div');
        cell.className = 'tome-source-count-cell';
        var count = document.createElement('span');
        count.className = 'tome-source-count';
        cell.appendChild(count);
        var scoped = document.createElement('button');
        scoped.type = 'button';
        scoped.className = 'btn scan-text-btn tome-source-scoped hidden';
        scoped.setAttribute('data-segment', segment);
        scoped.textContent = this._t(['settings.tomeSources.scopedOverride', 'Override']);
        cell.appendChild(scoped);
        // After the row's text: the switch leads the row (index.html), the count follows the fixed-width text
        var info = row.querySelector('.setting-info');
        row.insertBefore(cell, info ? info.nextSibling : null);
        row.appendChild(this._viewCell(segment));
        return cell;
    },

    /** The row's thin last column: View spells, the category in the Spell list (spellLists.js) */
    _viewCell: function(segment) {
        var cell = document.createElement('div');
        cell.className = 'tome-source-view-cell';
        var btn = document.createElement('button');
        btn.type = 'button';
        btn.className = 'btn scan-text-btn tome-source-view';
        btn.setAttribute('data-segment', segment);
        btn.textContent = this._t(['settings.tomeSources.viewSpells', 'View spells']);
        btn.addEventListener('click', function() {
            if (typeof SpellLists !== 'undefined') SpellLists.showCategory(segment);
        });
        cell.appendChild(btn);
        return cell;
    },

    _t: function(entry) {
        return typeof tOr === 'function' ? tOr(entry[0], null, entry[1]) : entry[1];
    },

    /** Why a spell is off the tree by its source, in the panel's language (Gate Review's rows) */
    reasonText: function(spell) {
        if (!spell) return '';
        if (spell.tomeUnreachable === true) return this._t(this.NAMES.unreachable);
        var segment = this.segmentOf(spell);
        if (segment === null) return spell.questOnly === true ? this._t(this.NAMES.questOnly) : '';
        var name = this._t(this.NAMES[segment] || this.NAMES[this.CATCH_ALL]);
        var detail = spell.source.shop || spell.source.detail || '';
        return detail ? name + ' (' + detail + ')' : name;
    },

    init: function() {
        var self = this;
        this.SEGMENTS.forEach(function(segment) {
            var toggle = document.getElementById(self.toggleId(segment));
            if (!toggle) return;
            toggle.addEventListener('change', function() {
                self._switches()[segment] = this.checked;
                self._save();
            });
            // The whole row is the switch: a click on its name, description or count flips it (the switch's
            // own label already does, and the scoped override and View spells buttons are their own, so clicks
            // there are left alone)
            var row = toggle.closest ? toggle.closest('.setting-row') : null;
            if (row) {
                row.addEventListener('click', function(e) {
                    if (e.target && e.target.closest && e.target.closest('.toggle-switch, .tome-source-scoped, .tome-source-view')) return;
                    toggle.checked = !toggle.checked;
                    toggle.dispatchEvent(new Event('change'));
                });
            }
        });
        // The section folds by its header bar like every Scan page section (scanSections.js)
        this.sync();
    },

    /** Put the saved switches on the toggles */
    sync: function() {
        var s = this._switches();
        for (var i = 0; i < this.SEGMENTS.length; i++) {
            var toggle = document.getElementById(this.toggleId(this.SEGMENTS[i]));
            if (toggle) toggle.checked = s[this.SEGMENTS[i]] === true;
        }
    },

    /** Read the switches from a config, the defaults for what is missing or not a boolean */
    loadFrom: function(data) {
        var src = (data && data.tomeSources && typeof data.tomeSources === 'object') ? data.tomeSources : {};
        var out = {};
        for (var k in this.DEFAULTS) {
            if (this.DEFAULTS.hasOwnProperty(k)) out[k] = typeof src[k] === 'boolean' ? src[k] : this.DEFAULTS[k];
        }
        settings.tomeSources = out;
        this.sync();
    },

    /** Write the switches into a config (config.tomeSources, every switch) */
    saveTo: function(config) {
        var s = this._switches(), out = {};
        for (var k in this.DEFAULTS) {
            if (this.DEFAULTS.hasOwnProperty(k)) out[k] = s[k] === true;
        }
        config.tomeSources = out;
        return config;
    },

    reset: function() {
        settings.tomeSources = JSON.parse(JSON.stringify(this.DEFAULTS));
        this.sync();
    },

    /** A switch changed: save, and count the primed spells again (the tree's reach changed) */
    _save: function() {
        if (typeof autoSaveSettings === 'function') autoSaveSettings();
        if (typeof updatePrimedCount === 'function') updatePrimedCount();
    }
};

window.TomeSourceSetting = TomeSourceSetting;
