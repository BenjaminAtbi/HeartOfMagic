/**
 * GateReview - the spell gate's list and the specialty sources list in the blacklist window
 * (docs/librarian/ToDo.md M9, SpellScannerGate.cpp; where tomes come from: TomeReach).
 *
 * "Spells to look over": tree spells the scan flagged - gate.band "review" (a horse
 * summon, a named unique summon, a summon that stays over an hour, a cheap
 * script-only spell, a rule from gate/rules.json), gate.band "out" (free to cast, or
 * one that only dismisses a summon: a free spell can be a real one, Mysticism's
 * Symmetry), or source segment "review" (the distribution model found no clear
 * source). They stay in the tree; Leave out puts one on the spell blacklist, Keep
 * takes it off the list. Since 2026-11-14 the gate only flags: where the tomes
 * come from decides the tree.
 *
 * "Specialty sources": tome spells off the tree by where their tomes come from
 * (TomeSourceSetting.offTree: a segment switched off in Settings > Spell Tomes,
 * or nothing hands their tomes out; questOnly in a scan without source objects).
 * Include puts one in the tree.
 *
 * Keep and Include write the same list, settings.gateReviewed (config.json,
 * "plugin.esp:localid" keys, plugin A-Z folded (foldPluginName), local id as getLocalFormId
 * writes it). The DLL reads it too (ReadGateKept), so a saved tree's load and
 * Spell Research take an included spell back.
 *
 * Depends on: state.lastSpellData, settings (spellBlacklist, gateReviewed),
 *   isTreeCandidate, isTomeCandidate, foldPluginName (constants.js), getPrimedSpells, getLocalFormId
 *   (uiHelpers.js), addToBlacklist, autoSaveSettings (settingsPanel.js), tOr (i18n.js)
 */

var GateReview = {

    REASON_KEYS: {
        free: ['modals.gateReview.reason.free', 'costs nothing to cast'],
        banishOnly: ['modals.gateReview.reason.banishOnly', 'only dismisses a summon'],
        longSummon: ['modals.gateReview.reason.longSummon', 'a summon that stays over an hour'],
        horseSummon: ['modals.gateReview.reason.horseSummon', 'summons a horse'],
        uniqueSummon: ['modals.gateReview.reason.uniqueSummon', 'summons one named character'],
        nonHostileSummon: ['modals.gateReview.reason.nonHostileSummon', 'summons a creature of a non-hostile race (a mount or pet?)'],
        cheapScript: ['modals.gateReview.reason.cheapScript', 'a cheap spell that only runs a script'],
        rule: ['modals.gateReview.reason.rule', 'matches a review rule']
    },

    _local: function(spell) {
        return typeof getLocalFormId === 'function' ? getLocalFormId(spell.formId) : spell.formId;
    },

    /**
     * "plugin.esp:localid", the plugin folded A-Z only (foldPluginName, constants.js):
     * the DLL makes the same bytes (SpellScannerGate.cpp PluginKey), so a Korean, Russian or
     * accented plugin name gives one key on both sides. Blacklist entries match by it too
     */
    key: function(spell) {
        return foldPluginName(spell.plugin || '') + ':' + this._local(spell);
    },

    /** The key Keep wrote before 2026-10-17 (toLowerCase): migrateKeys replaces it */
    _legacyKey: function(spell) {
        return String(spell.plugin || '').toLowerCase() + ':' + this._local(spell);
    },

    /** The player kept or included this spell (settings.gateReviewed) */
    isKept: function(spell) {
        var list = (typeof settings !== 'undefined' && settings.gateReviewed) || [];
        return list.indexOf(this.key(spell)) !== -1;
    },

    _blacklisted: function(spell, list) {
        var key = this.key(spell);
        for (var i = 0; i < list.length; i++) {
            var e = list[i];
            if (e.plugin && e.localFormId && (foldPluginName(e.plugin) + ':' + e.localFormId) === key) return true;
            if (e.formId === spell.formId) return true;
        }
        return false;
    },

    /** Whether config.json was read whole (not C++'s defaults): only then may migrateKeys save */
    _configReady: false,

    /**
     * onUnifiedConfigLoaded: the config was applied; readable is false when C++ could not
     * read config.json (configNotRead). Migrates the kept keys if a scan is here already.
     */
    configLoaded: function(readable) {
        this._configReady = readable === true;
        if (this._configReady && typeof state !== 'undefined' && state && state.lastSpellData) {
            this.migrateKeys(state.lastSpellData.spells);
        }
    },

    /**
     * Keys Keep wrote before 2026-10-17 with toLowerCase (a Cyrillic or accented capital
     * folded too) never match in the DLL: for each scanned spell whose old key is kept and
     * whose new key is not, the entry becomes the new key, and the settings are saved once.
     * An entry no scanned spell has stays as it is. Called on a new scan, a config load and
     * the gate lists' render; does nothing before the config is read.
     * @returns {number} entries replaced
     */
    migrateKeys: function(spells) {
        var list = (typeof settings !== 'undefined' && settings && settings.gateReviewed) || null;
        if (!this._configReady || !list || list.length === 0 || !spells) return 0;
        var at = {};
        for (var i = 0; i < list.length; i++) at[list[i]] = i;
        var replaced = 0;
        for (var j = 0; j < spells.length; j++) {
            var s = spells[j];
            if (!s || !s.plugin) continue;
            var oldKey = this._legacyKey(s);
            if (!at.hasOwnProperty(oldKey)) continue;
            var newKey = this.key(s);
            if (newKey === oldKey || at.hasOwnProperty(newKey)) continue;
            list[at[oldKey]] = newKey;
            at[newKey] = at[oldKey];
            delete at[oldKey];
            replaced++;
        }
        if (replaced > 0 && typeof autoSaveSettings === 'function') autoSaveSettings();
        return replaced;
    },

    /** The scan flagged the spell for a look: either gate band, or no clear source */
    flagged: function(spell) {
        var band = spell.gate && spell.gate.band;
        return band === 'review' || band === 'out' || !!(spell.source && spell.source.segment === 'review');
    },

    /**
     * The spells to look over: flagged, not blacklisted, not kept already.
     * @param {Array} spells - tree candidates (getPrimedSpells(isTreeCandidate))
     */
    pending: function(spells, blacklist, reviewed) {
        var self = this;
        return this._listed(spells, blacklist, reviewed, function(s) { return self.flagged(s); });
    },

    /**
     * The spells off the tree by their source: TomeSourceSetting.offTree, not blacklisted,
     * not included already.
     * @param {Array} spells - tome candidates before the source (getPrimedSpells(isTomeCandidate))
     */
    offSource: function(spells, blacklist, reviewed) {
        var off = (typeof TomeSourceSetting !== 'undefined')
            ? function(s) { return TomeSourceSetting.offTree(s); }
            : function(s) { return s.tomeUnreachable === true || s.questOnly === true; };
        return this._listed(spells, blacklist, reviewed, off);
    },

    /** The spells that pass `test`, not blacklisted and not kept or included already (settings.gateReviewed) */
    _listed: function(spells, blacklist, reviewed, test) {
        var kept = {};
        for (var r = 0; r < (reviewed || []).length; r++) kept[reviewed[r]] = true;
        var out = [];
        for (var i = 0; i < (spells || []).length; i++) {
            var s = spells[i];
            if (!s || !test(s)) continue;
            if (kept[this.key(s)] || this._blacklisted(s, blacklist || [])) continue;
            out.push(s);
        }
        return out;
    },

    _t: function(key, fallback) {
        return typeof tOr === 'function' ? tOr(key, null, fallback) : fallback;
    },

    /** One reason in the panel's language */
    reasonText: function(reason) {
        var name = String(reason).indexOf('rule:') === 0 ? 'rule' : reason;
        var entry = this.REASON_KEYS[name];
        return entry ? this._t(entry[0], entry[1]) : String(reason);
    },

    render: function() {
        // Tree candidates (tome, source, voice slot, copies, blacklist, plugin whitelist)
        var all = (state.lastSpellData && state.lastSpellData.spells) || [];
        var primed = function(filter) {
            return typeof getPrimedSpells === 'function' ? getPrimedSpells(filter) : all;
        };
        var spells = primed(typeof isTreeCandidate === 'function' ? isTreeCandidate : null);
        this.migrateKeys(all);
        var blacklist = settings.spellBlacklist, kept = settings.gateReviewed;
        var self = this;
        this._renderList('gate-review', this.pending(spells, blacklist, kept),
            ['modals.gateReview.empty', 'Nothing to look over'], [
                ['modals.gateReview.keep', 'Keep', function(spell) { self.keep(spell); }],
                ['modals.gateReview.exclude', 'Leave out', function(spell) { self.exclude(spell); }]
            ]);
        // Spells off by their source are not tree candidates: list them from the filter before it
        var tomeSpells = primed(typeof isTomeCandidate === 'function' ? isTomeCandidate : null);
        this._renderList('tome-source', this.offSource(tomeSpells, blacklist, kept),
            ['modals.gateReview.sourceEmpty', 'No spell is off the tree by its source'], [
                ['modals.gateReview.include', 'Include', function(spell) { self.keep(spell); }]
            ], true);
    },

    /** Why a row is listed: the source first when the list is the source one, then the gate's reasons */
    _reasons: function(spell, withSource) {
        var self = this, out = [];
        if (withSource && typeof TomeSourceSetting !== 'undefined') {
            var why = TomeSourceSetting.reasonText(spell);
            if (why) out.push(why);
        } else if (withSource && spell.questOnly === true) {
            out.push(this._t('settings.tomeSources.questOnly', 'only a quest hands out its tomes'));
        }
        if (!withSource && spell.source && spell.source.segment === 'review') {
            out.push(this._t('settings.tomeSources.review', 'source unclear'));
        }
        ((spell.gate && spell.gate.reasons) || []).forEach(function(r) { out.push(self.reasonText(r)); });
        return out;
    },

    /**
     * One list: rows of name, plugin and why it is listed (the source list's rows say the source,
     * then the gate's reasons), with its buttons ([key, fallback, onClick])
     */
    _renderList: function(prefix, list, emptyText, buttons, withSource) {
        var box = document.getElementById(prefix + '-entries');
        var count = document.getElementById(prefix + '-count');
        if (!box) return;
        if (count) count.textContent = list.length;
        box.innerHTML = '';
        if (list.length === 0) {
            var empty = document.createElement('div');
            empty.className = 'blacklist-empty';
            empty.textContent = this._t(emptyText[0], emptyText[1]);
            box.appendChild(empty);
            return;
        }
        var self = this;
        list.forEach(function(spell) {
            var row = document.createElement('div');
            row.className = 'blacklist-entry';
            var info = document.createElement('div');
            info.className = 'blacklist-entry-info';
            var name = document.createElement('div');
            name.className = 'blacklist-entry-name';
            name.textContent = spell.name || '???';
            var why = document.createElement('div');
            why.className = 'blacklist-entry-school';
            var reasons = self._reasons(spell, withSource === true);
            why.textContent = (spell.plugin || '') + (reasons.length ? ' - ' + reasons.join(', ') : '');
            info.appendChild(name);
            info.appendChild(why);
            row.appendChild(info);
            buttons.forEach(function(b) {
                var btn = document.createElement('button');
                btn.className = 'btn btn-secondary btn-sm';
                btn.textContent = self._t(b[0], b[1]);
                btn.addEventListener('click', function() { b[2](spell); });
                row.appendChild(btn);
            });
            box.appendChild(row);
        });
    },

    /** Keep (a flagged spell) or Include (one off by its source): the same list, read by the panel and the DLL */
    keep: function(spell) {
        if (!settings.gateReviewed) settings.gateReviewed = [];
        var key = this.key(spell);
        if (settings.gateReviewed.indexOf(key) === -1) settings.gateReviewed.push(key);
        if (typeof autoSaveSettings === 'function') autoSaveSettings();
        if (typeof updatePrimedCount === 'function') updatePrimedCount();
        this.render();
    },

    /** addToBlacklist repaints both lists (renderBlacklistEntries) */
    exclude: function(spell) {
        if (typeof addToBlacklist === 'function') addToBlacklist(spell);
    }
};

window.GateReview = GateReview;
