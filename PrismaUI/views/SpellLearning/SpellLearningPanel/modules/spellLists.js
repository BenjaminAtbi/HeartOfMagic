/**
 * SpellLists - the Spells tab of Where Spells Come From (the right half): three columns, left to right
 * Spell list, Blacklisted and Whitelisted.
 *
 * - Spell list: every spell a tome teaches (isTomeCandidate), from the plugins switched on, on neither list.
 *   The ones with something to say (the gate's look-over reasons, a source switched off) come first.
 * - Blacklisted: settings.spellBlacklist (left out of the tree).
 * - Whitelisted: settings.gateReviewed, the Keep / Include list (GateReview), named by the scan's spell.
 *
 * Every row: the name, and under it the plugin, the category (TomeSourceSetting: where its tomes come
 * from) and any notes. On its right, a small button per other column, in the columns' left-to-right
 * order: X (red) back to the Spell list, B (black) to the blacklist, W (white) to the whitelist. A move
 * saves at once (addToBlacklist / removeFromBlacklist, autoSaveSettings) and repaints the page.
 *
 * The lists are painted again only when what they show changed (the scan, both lists, the switches,
 * the plugin list, the language): ScanStage.refresh calls render on every change. One click listener
 * per list (the Spell list can hold thousands of rows).
 *
 * Depends on: setI18n, tOr (i18n.js); isTomeCandidate (proceduralTreeBuilder.js), filterWhitelistedSpells,
 *   addToBlacklist, removeFromBlacklist, autoSaveSettings (settingsPanel.js), foldPluginName (constants.js),
 *   getLocalFormId, updatePrimedCount (uiHelpers.js), GateReview, TomeSourceSetting - each optional.
 * Called by: scanStage.js refresh. CSS: panel-scan.css (.spell-list-*, .spell-move-*).
 */

var SpellLists = {

    /** The columns, left to right: list id, title id, title [key, English] */
    COLUMNS: [
        { name: 'spells', list: 'scanSpellListPreview', title: 'scanSpellListTitle', text: ['scan.spellList', 'Spell list ({{count}})'] },
        { name: 'blacklisted', list: 'scanBlacklistPreview', title: 'scanBlacklistTitle', text: ['scan.blacklisted', 'Blacklisted ({{count}})'] },
        { name: 'whitelisted', list: 'scanWhitelistPreview', title: 'scanWhitelistTitle', text: ['scan.whitelisted', 'Whitelisted ({{count}})'] }
    ],

    /** Each column's move button: its mark, class and tooltip */
    MOVES: {
        spells: { mark: 'X', cls: 'spell-move-spells', title: ['scan.moveToSpells', 'Back to the spell list'] },
        blacklisted: { mark: 'B', cls: 'spell-move-blacklisted', title: ['scan.moveToBlacklist', 'Blacklist'] },
        whitelisted: { mark: 'W', cls: 'spell-move-whitelisted', title: ['scan.moveToWhitelist', 'Whitelist'] }
    },

    /** The last painted state, and each column's rows by index (the click listener reads them) */
    _sig: null,
    _items: {},
    _wired: {},

    _doc: function() { return typeof document !== 'undefined' ? document : null; },

    _t: function(key, fallback) {
        return typeof tOr === 'function' ? tOr(key, null, fallback) : fallback;
    },

    _fold: function(plugin) {
        return typeof foldPluginName === 'function' ? foldPluginName(plugin || '') : String(plugin || '').toLowerCase();
    },

    _local: function(formId) {
        return typeof getLocalFormId === 'function' ? getLocalFormId(formId) : String(formId || '').replace(/^0x/i, '').toLowerCase().slice(-6);
    },

    /** A spell's Include key (GateReview.key) */
    keyOf: function(spell) {
        if (typeof GateReview !== 'undefined' && GateReview.key) return GateReview.key(spell);
        return this._fold(spell.plugin) + ':' + this._local(spell.formId);
    },

    /** A blacklist entry's key: its stable plugin:local id, else its spell's */
    _entryKey: function(entry) {
        if (entry.plugin && entry.localFormId) return this._fold(entry.plugin) + ':' + entry.localFormId;
        return entry.plugin && entry.formId ? this._fold(entry.plugin) + ':' + this._local(entry.formId) : null;
    },

    /** The row's lines under the name: the plugin, the category, then the notes */
    describe: function(spell, plugin) {
        var parts = [];
        if (plugin) parts.push(plugin);
        if (!spell) return parts;
        var T = typeof TomeSourceSetting !== 'undefined' ? TomeSourceSetting : null;
        var category = T ? T.categoryOf(spell) : null;
        if (category && T.NAMES[category]) parts.push(this._t(T.NAMES[category][0], T.NAMES[category][1]));
        else parts.push(this._t('scan.noCategory', 'source not known'));
        if (typeof GateReview !== 'undefined' && GateReview.flagged && GateReview.flagged(spell)) {
            parts = parts.concat(GateReview._reasons(spell, false));
        }
        if (T && T.offTree(spell)) parts.push(this._t('scan.offBySource', 'its source is switched off'));
        return parts;
    },

    /**
     * The three columns' rows: { spells, blacklisted, whitelisted }, each row
     * { name, plugin, key, spell (the scan's, or null), entry (the blacklist's), notes (beyond plugin and category) }
     */
    model: function(scanSpells, blacklist, kept) {
        var self = this, byKey = {}, byFormId = {};
        (scanSpells || []).forEach(function(s) {
            if (!s || !s.formId) return;
            if (s.plugin) byKey[self.keyOf(s)] = s;
            byFormId[s.formId] = s;
        });
        var listed = {};
        var row = function(spell, name, plugin, key, entry) {
            var lines = self.describe(spell, plugin);
            return { name: name, plugin: plugin, key: key, spell: spell || null, entry: entry || null,
                lines: lines, notes: Math.max(0, lines.length - 2) };
        };
        var blacklisted = (blacklist || []).map(function(e) {
            var key = self._entryKey(e);
            var spell = (key && byKey[key]) || byFormId[e.formId] || null;
            if (spell) listed[self.keyOf(spell)] = true;
            if (key) listed[key] = true;
            var name = e.name || (spell && spell.name) || e.formId || key || '?';
            return row(spell, name, e.plugin || (spell && spell.plugin) || '', key || (spell ? self.keyOf(spell) : null), e);
        });
        var whitelisted = (kept || []).map(function(k) {
            var spell = byKey[k] || null;
            listed[k] = true;
            return row(spell, spell ? (spell.name || spell.formId) : k, spell ? spell.plugin : String(k).split(':')[0], k);
        });
        var tome = (scanSpells || []).filter(function(s) {
            return s && (typeof isTomeCandidate === 'function' ? isTomeCandidate(s) : s.taughtByTome !== false);
        });
        if (typeof filterWhitelistedSpells === 'function') tome = filterWhitelistedSpells(tome);
        var spells = tome.filter(function(s) { return !listed[self.keyOf(s)]; }).map(function(s) {
            return row(s, s.name || s.formId, s.plugin || '', self.keyOf(s));
        });
        var byName = function(a, b) { return String(a.name).localeCompare(String(b.name)); };
        spells.sort(function(a, b) { return (b.notes > 0) - (a.notes > 0) || byName(a, b); });
        blacklisted.sort(byName);
        whitelisted.sort(byName);
        return { spells: spells, blacklisted: blacklisted, whitelisted: whitelisted };
    },

    /** What the lists show depends on; the same signature paints nothing */
    _signature: function(scanSpells, opts) {
        var doc = this._doc();
        var lang = (doc && doc.documentElement && doc.documentElement.lang) || '';
        return [(scanSpells || []).length, scanSpells && scanSpells[0] && scanSpells[0].formId, lang,
            JSON.stringify(opts.spellBlacklist || []), JSON.stringify(opts.gateReviewed || []),
            JSON.stringify(opts.tomeSources || {}), JSON.stringify(opts.pluginWhitelist || [])].join('|');
    },

    /** Paint the three columns (ScanStage.refresh); force repaints an unchanged state */
    render: function(scanSpells, opts, force) {
        opts = opts || {};
        var sig = this._signature(scanSpells, opts);
        if (!force && sig === this._sig) return false;
        this._sig = sig;
        var rows = this.model(scanSpells, opts.spellBlacklist, opts.gateReviewed);
        for (var i = 0; i < this.COLUMNS.length; i++) this._paint(this.COLUMNS[i], rows[this.COLUMNS[i].name]);
        return true;
    },

    _el: function(doc, tag, cls, text) {
        var el = doc.createElement(tag);
        if (cls) el.setAttribute('class', cls);
        if (text !== undefined) el.textContent = text;
        return el;
    },

    _paint: function(col, rows) {
        var doc = this._doc();
        if (!doc) return;
        var title = doc.getElementById(col.title);
        if (title && typeof setI18n === 'function') setI18n(title, col.text[0], { count: rows.length }, col.text[1]);
        var list = doc.getElementById(col.list);
        if (!list) return;
        this._wire(list, col.name);
        this._items[col.name] = rows;
        list.innerHTML = '';
        if (!rows.length) {
            var empty = this._el(doc, 'li', 'scan-list-empty');
            if (typeof setI18n === 'function') setI18n(empty, 'scan.listEmpty', null, 'None');
            else empty.textContent = 'None';
            list.appendChild(empty);
            return;
        }
        var targets = this.COLUMNS.filter(function(c) { return c.name !== col.name; });
        var into = typeof doc.createDocumentFragment === 'function' ? doc.createDocumentFragment() : list;
        for (var r = 0; r < rows.length; r++) {
            var li = this._el(doc, 'li', 'spell-list-item');
            li.setAttribute('data-index', r);
            var info = this._el(doc, 'div', 'spell-list-info');
            info.appendChild(this._el(doc, 'div', 'spell-list-name', rows[r].name));
            info.appendChild(this._el(doc, 'div', 'spell-list-desc', rows[r].lines.join(' - ')));
            li.appendChild(info);
            var moves = this._el(doc, 'div', 'spell-list-moves');
            for (var t = 0; t < targets.length; t++) {
                var m = this.MOVES[targets[t].name];
                var btn = this._el(doc, 'button', 'spell-move ' + m.cls, m.mark);
                btn.setAttribute('type', 'button');
                btn.setAttribute('data-move', targets[t].name);
                btn.setAttribute('title', this._t(m.title[0], m.title[1]));
                // A whitelisted key the scan has no spell for cannot be blacklisted (no form id)
                if (targets[t].name === 'blacklisted' && !rows[r].spell && !rows[r].entry) btn.disabled = true;
                moves.appendChild(btn);
            }
            li.appendChild(moves);
            into.appendChild(li);
        }
        if (into !== list) list.appendChild(into);
    },

    /** One click listener per list: the button's data-move, the row's data-index */
    _wire: function(list, column) {
        if (this._wired[column] === list || !list.addEventListener) return;
        this._wired[column] = list;
        var self = this;
        list.addEventListener('click', function(e) {
            var btn = e && e.target && e.target.closest ? e.target.closest('button[data-move]') : null;
            if (!btn || btn.disabled) return;
            var li = btn.closest('li[data-index]');
            var row = li ? (self._items[column] || [])[parseInt(li.getAttribute('data-index'), 10)] : null;
            if (row) self.move(row, btn.getAttribute('data-move'));
        });
    },

    /** The row as a spell addToBlacklist takes: the scan's, else the blacklist entry's fields */
    _spellOf: function(row) {
        if (row.spell) return row.spell;
        var e = row.entry;
        return e ? { formId: e.formId, name: e.name, school: e.school, plugin: e.plugin } : null;
    },

    /** Move a row to a column: off the other list first, then onto the target's (none for the Spell list) */
    move: function(row, to) {
        if (typeof settings === 'undefined' || !settings) return;
        var spell = this._spellOf(row);
        if (to !== 'whitelisted' && row.key && settings.gateReviewed) {
            settings.gateReviewed = settings.gateReviewed.filter(function(k) { return k !== row.key; });
        }
        // Only a Blacklisted row has an entry: the other columns' spells are on no blacklist
        if (to !== 'blacklisted' && row.entry && typeof removeFromBlacklist === 'function') {
            removeFromBlacklist(row.entry.plugin, row.entry.localFormId, row.entry.formId);
        }
        if (to === 'blacklisted' && spell && typeof addToBlacklist === 'function') addToBlacklist(spell);
        if (to === 'whitelisted' && row.key) {
            if (!settings.gateReviewed) settings.gateReviewed = [];
            if (settings.gateReviewed.indexOf(row.key) === -1) settings.gateReviewed.push(row.key);
        }
        if (typeof autoSaveSettings === 'function') autoSaveSettings();
        if (typeof updatePrimedCount === 'function') updatePrimedCount();
        if (typeof GateReview !== 'undefined' && GateReview.render && typeof state !== 'undefined') {
            try { GateReview.render(); } catch (err) { /* the window is not on the page */ }
        }
        if (typeof ScanStage !== 'undefined' && ScanStage.refresh) ScanStage.refresh();
    }
};

if (typeof window !== 'undefined') window.SpellLists = SpellLists;
