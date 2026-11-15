/**
 * TomeSourceSetting - Settings > Spell Tomes, "Where tomes come from": which
 * distribution segments put their spells on the tree.
 *
 * The scan gives every spell with a tome a source object (TomeReach's distribution
 * model, SpellScannerScan.cpp): { segment, class, subtype, detail, shop? }. The
 * segment is one of unreachable, generalSale, specialtyModVendor, wideDrops,
 * specificLoot, questReward, craftedOnly, uncommonVendor, review. Each segment but
 * unreachable and review has a switch here; a spell of a segment switched off is
 * off the tree (offTree) until the player includes it in Gate Review. Unreachable
 * spells (tomeUnreachable) are always off; review ones always on (Gate Review lists
 * them). A scan without source objects (the reach pass answered: the model could
 * not run) falls back on its questOnly flag.
 *
 * Saved as config.tomeSources = { generalSale: true, ... } (the keys of SegmentKey,
 * TomeReachSwitches.h, with the same defaults). The DLL reads it for the scan's
 * questOnly flag and Tree Graft (GraftCandidates.h); the panel reads it live, so a
 * switch counts at the next build without a new scan. Not in settings presets.
 *
 * Depends on: settings, autoSaveSettings, updatePrimedCount (optional), tOr (i18n.js, optional)
 */

var TomeSourceSetting = {

    /** The switched segments, in the order the settings page lists them */
    SEGMENTS: ['generalSale', 'wideDrops', 'uncommonVendor', 'specialtyModVendor', 'specificLoot', 'questReward', 'craftedOnly'],

    DEFAULTS: {
        generalSale: true,
        wideDrops: true,
        uncommonVendor: true,
        specialtyModVendor: false,
        specificLoot: false,
        questReward: false,
        craftedOnly: false
    },

    /** Every segment's name ([i18n key, English]); also the Gate Review reason of a spell off the tree */
    NAMES: {
        unreachable: ['settings.tomeSources.unreachable', 'nothing hands out its tomes'],
        generalSale: ['settings.tomeSources.generalSale', 'General sale'],
        wideDrops: ['settings.tomeSources.wideDrops', 'Wide drops'],
        uncommonVendor: ['settings.tomeSources.uncommonVendor', 'Uncommon vendors'],
        specialtyModVendor: ['settings.tomeSources.specialtyModVendor', 'Specialty mod vendors'],
        specificLoot: ['settings.tomeSources.specificLoot', 'Specific loot'],
        questReward: ['settings.tomeSources.questReward', 'Quest rewards'],
        craftedOnly: ['settings.tomeSources.craftedOnly', 'Crafted only'],
        review: ['settings.tomeSources.review', 'source unclear'],
        questOnly: ['settings.tomeSources.questOnly', 'only a quest hands out its tomes']
    },

    /** The toggle of a segment on the settings page */
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

    /** Whether a segment's spells go on the tree (TomeReachSwitches.h SegmentSwitches::OnTree) */
    onTree: function(segment) {
        if (segment === 'review') return true;
        if (!this.DEFAULTS.hasOwnProperty(segment)) return false;
        return this._switches()[segment] === true;
    },

    /** The spell's segment key, or null for a scan without source objects */
    segmentOf: function(spell) {
        var src = spell && spell.source;
        return (src && typeof src === 'object' && typeof src.segment === 'string') ? src.segment : null;
    },

    /**
     * Off the tree by where its tomes come from: nothing hands them out (tomeUnreachable), or
     * its segment is switched off; without a source object, the scan's questOnly flag. The
     * player's Include (GateReview.isKept) is the caller's to apply. GraftCandidates.h
     * OffTreeBySource is the DLL's copy.
     */
    offTree: function(spell) {
        if (!spell) return false;
        if (spell.tomeUnreachable === true) return true;
        var segment = this.segmentOf(spell);
        if (segment !== null && (this.DEFAULTS.hasOwnProperty(segment) || segment === 'review' || segment === 'unreachable')) {
            return !this.onTree(segment);
        }
        return spell.questOnly === true;
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
        var name = this.NAMES[segment] ? this._t(this.NAMES[segment]) : segment;
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
        });
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
