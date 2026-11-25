/**
 * SpellLists - the Spells tab of Spell Inclusion (the right half): three columns, left to right
 * Spell list, Blacklisted and Whitelisted.
 *
 * - Spell list: every spell a tome teaches (isTomeCandidate), from the plugins switched on, on neither list.
 *   All / Flagged (right of the filter field) pick the view (Flagged: the spells to look over,
 *   GateReview.flagged, with why in their own colour); the category table's View spells shows one category
 *   (showCategory). The filter field above the lists narrows all three columns by name or plugin.
 * - Blacklisted: settings.spellBlacklist (left out of the tree).
 * - Whitelisted: settings.gateReviewed, the Keep / Include list (GateReview), named by the scan's spell.
 *
 * Every row: the name, and under it the plugin, the category (TomeSourceSetting: where its tomes come
 * from) and whether that source is switched off. On its right, a small button per other column, in the columns' left-to-right
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

    /** The row's description under the name: the plugin, the category, then a source switched off */
    describe: function(spell, plugin) {
        var parts = [];
        if (plugin) parts.push(plugin);
        if (!spell) return parts;
        var T = typeof TomeSourceSetting !== 'undefined' ? TomeSourceSetting : null;
        var category = T ? T.categoryOf(spell) : null;
        if (category && T.NAMES[category]) parts.push(this._t(T.NAMES[category][0], T.NAMES[category][1]));
        else parts.push(this._t('scan.noCategory', 'source not known'));
        if (T && T.offTree(spell)) parts.push(this._t('scan.offBySource', 'its source is switched off'));
        return parts;
    },

    /** Flagged for a look (GateReview.flagged: the gate's bands, or no clear source) */
    _flagged: function(spell) {
        return !!spell && typeof GateReview !== 'undefined' && !!GateReview.flagged && GateReview.flagged(spell);
    },

    /** Why a flagged spell is to be looked over (Gate Review's reasons), shown in every column in the Flagged view */
    reasonsOf: function(spell) {
        return this._flagged(spell) && GateReview._reasons ? GateReview._reasons(spell, false) : [];
    },

    // =========================================================================
    // THE SPELL LIST'S VIEW: All, Flagged (the spells to look over), or one category (View spells)
    // =========================================================================

    view: 'all',
    category: null,
    _last: null,

    /** Whether the Spell list shows a spell in the current view */
    _inView: function(spell) {
        if (this.view === 'flagged') return this._flagged(spell);
        if (this.view === 'category') {
            return typeof TomeSourceSetting !== 'undefined' && TomeSourceSetting.categoryOf(spell) === this.category;
        }
        return true;
    },

    /** Switch the view ('all', 'flagged', or 'category' with its segment) and paint again */
    setView: function(view, category) {
        this.view = view === 'flagged' || view === 'category' ? view : 'all';
        this.category = this.view === 'category' ? category : null;
        this._syncViewButtons();
        if (this._last) this.render(this._last[0], this._last[1], true);
    },

    /** Category table's View spells: that category in the Spell list, on the Spells tab */
    showCategory: function(segment) {
        if (typeof SourceListTabs !== 'undefined' && SourceListTabs.show) SourceListTabs.show('spells');
        this.setView('category', segment);
    },

    _syncViewButtons: function() {
        var doc = this._doc();
        var btns = doc && doc.querySelectorAll ? doc.querySelectorAll('#spellListViews [data-spell-view]') : [];
        for (var i = 0; i < btns.length; i++) btns[i].classList.toggle('active', btns[i].getAttribute('data-spell-view') === this.view);
    },

    // The filter field above the lists: every column shows the rows whose name or plugin holds the text
    query: '',
    FILTER_DELAY_MS: 120,
    _filterTimer: null,

    /** Whether a row's name or plugin holds the filter text (any case) */
    _matches: function(name, plugin) {
        if (!this.query) return true;
        return String(name || '').toLowerCase().indexOf(this.query) !== -1 ||
            String(plugin || '').toLowerCase().indexOf(this.query) !== -1;
    },

    /** Filter every column by a text and paint again */
    setQuery: function(text) {
        this.query = String(text || '').trim().toLowerCase();
        if (this._last) this.render(this._last[0], this._last[1], true);
    },

    /** The tools row: the filter field, then All / Flagged */
    initViews: function() {
        var doc = this._doc();
        var btns = doc && doc.querySelectorAll ? doc.querySelectorAll('#spellListViews [data-spell-view]') : [];
        var self = this;
        for (var i = 0; i < btns.length; i++) {
            (function(btn) {
                btn.addEventListener('click', function() { self.setView(btn.getAttribute('data-spell-view')); });
            })(btns[i]);
        }
        var search = doc && doc.getElementById ? doc.getElementById('spellListSearch') : null;
        if (search) {
            // Typing paints once it pauses (the Spell list can hold thousands of rows)
            search.addEventListener('input', function() {
                var value = search.value;
                clearTimeout(self._filterTimer);
                self._filterTimer = setTimeout(function() { self.setQuery(value); }, self.FILTER_DELAY_MS);
            });
        }
        this._syncViewButtons();
    },

    /** The Spell list's title with its view: "Spell list: All", "Flagged" or the category's name */
    _spellsTitle: function() {
        var name;
        if (this.view === 'all') name = this._t('scan.viewAll', 'All');
        else if (this.view === 'flagged') name = this._t('scan.viewFlagged', 'Flagged');
        else if (typeof TomeSourceSetting === 'undefined') return null;
        else {
            var n = TomeSourceSetting.NAMES[this.category];
            name = n ? this._t(n[0], n[1]) : String(this.category);
        }
        return ['scan.spellListCategory', 'Spell list: {{category}} ({{count}})', { category: name }];
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
            return { name: name, plugin: plugin, key: key, spell: spell || null, entry: entry || null,
                lines: self.describe(spell, plugin), reasons: self.reasonsOf(spell) };
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
        var spells = tome.filter(function(s) {
            return !listed[self.keyOf(s)] && self._inView(s) && self._matches(s.name || s.formId, s.plugin);
        }).map(function(s) {
            return row(s, s.name || s.formId, s.plugin || '', self.keyOf(s));
        });
        var shown = function(r) { return self._matches(r.name, r.plugin); };
        blacklisted = blacklisted.filter(shown);
        whitelisted = whitelisted.filter(shown);
        var byName = function(a, b) { return String(a.name).localeCompare(String(b.name)); };
        spells.sort(byName);
        blacklisted.sort(byName);
        whitelisted.sort(byName);
        return { spells: spells, blacklisted: blacklisted, whitelisted: whitelisted };
    },

    /** What the lists show depends on; the same signature paints nothing */
    _signature: function(scanSpells, opts) {
        var doc = this._doc();
        var lang = (doc && doc.documentElement && doc.documentElement.lang) || '';
        return [(scanSpells || []).length, scanSpells && scanSpells[0] && scanSpells[0].formId, lang, this.view, this.category, this.query,
            JSON.stringify(opts.spellBlacklist || []), JSON.stringify(opts.gateReviewed || []),
            JSON.stringify(opts.tomeSources || {}), JSON.stringify(opts.pluginWhitelist || [])].join('|');
    },

    /** Paint the three columns (ScanStage.refresh); force repaints an unchanged state */
    render: function(scanSpells, opts, force) {
        opts = opts || {};
        this._last = [scanSpells, opts];
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
        var text = (col.name === 'spells' && this._spellsTitle()) || col.text;
        var params = { count: rows.length };
        for (var p in (text[2] || {})) params[p] = text[2][p];
        if (title && typeof setI18n === 'function') setI18n(title, text[0], params, text[1]);
        // The Flagged view shows why in every column, so a listed spell's flag stays visible
        var showReasons = this.view === 'flagged';
        var list = doc.getElementById(col.list);
        if (!list) return;
        this._wire(list, col.name);
        this._items[col.name] = rows;
        if (typeof SpellPeek !== 'undefined' && SpellPeek.hide) SpellPeek.hide();
        // Same view and filter (a move, a switch, a rescan): keep the rows loaded so far and the scroll place;
        // a new view or filter starts at the top with one page
        var scope = [this.view, this.category, this.query].join('|');
        var same = this._scope[col.name] === scope;
        this._scope[col.name] = scope;
        var top = same ? list.scrollTop || 0 : 0;
        var count = Math.min(rows.length, Math.max(this.PAGE_ROWS, same ? this._shown[col.name] || 0 : 0));
        list.innerHTML = '';
        this._shown[col.name] = 0;
        if (!rows.length) {
            var empty = this._el(doc, 'li', 'scan-list-empty');
            if (typeof setI18n === 'function') setI18n(empty, 'scan.listEmpty', null, 'None');
            else empty.textContent = 'None';
            list.appendChild(empty);
            return;
        }
        this._more(col.name, count, showReasons);
        list.scrollTop = top;
    },

    // Rows are put on the page a page at a time: the first PAGE_ROWS, then the next whenever the list is scrolled
    // near its end (_wire). A list of thousands then costs a page to paint and to lay out (a move, Easy / Complex)
    PAGE_ROWS: 60,
    MORE_AT_PX: 200,
    _shown: {},
    _scope: {},

    /** Append the column's next rows (count of them, else a page) after those already on the page */
    _more: function(column, count, showReasons) {
        var doc = this._doc();
        var col = this.COLUMNS.filter(function(c) { return c.name === column; })[0];
        var list = doc && col ? doc.getElementById(col.list) : null;
        var rows = this._items[column] || [];
        var from = this._shown[column] || 0;
        var to = Math.min(rows.length, from + (count || this.PAGE_ROWS));
        if (!list || from >= to) return 0;
        if (showReasons === undefined) showReasons = this.view === 'flagged';
        var targets = this.COLUMNS.filter(function(c) { return c.name !== column; });
        var into = typeof doc.createDocumentFragment === 'function' ? doc.createDocumentFragment() : list;
        for (var r = from; r < to; r++) into.appendChild(this._rowEl(doc, rows[r], r, targets, showReasons));
        if (into !== list) list.appendChild(into);
        this._shown[column] = to;
        return to - from;
    },

    /** One row: the name with its "?", the description, the move buttons */
    _rowEl: function(doc, row, index, targets, showReasons) {
        var li = this._el(doc, 'li', 'spell-list-item');
        li.setAttribute('data-index', index);
        var info = this._el(doc, 'div', 'spell-list-info');
        var nameEl = info.appendChild(this._el(doc, 'div', 'spell-list-name', row.name));
        // "?": the spell's card on hover (spellPeek.js); only for a spell the scan has
        if (row.spell) {
            var help = nameEl.appendChild(this._el(doc, 'span', 'spell-list-help', '?'));
            help.setAttribute('aria-label', this._t('scan.spellPeek', "Show the spell's figures and description"));
        }
        var desc = info.appendChild(this._el(doc, 'div', 'spell-list-desc', row.lines.join(' - ')));
        // Why each is to be looked over, in its own colour
        if (showReasons && row.reasons.length) {
            desc.appendChild(this._el(doc, 'span', 'spell-list-reasons', ' - ' + row.reasons.join(', ')));
        }
        li.appendChild(info);
        var moves = this._el(doc, 'div', 'spell-list-moves');
        for (var t = 0; t < targets.length; t++) {
            var m = this.MOVES[targets[t].name];
            var btn = this._el(doc, 'button', 'spell-move ' + m.cls, m.mark);
            btn.setAttribute('type', 'button');
            btn.setAttribute('data-move', targets[t].name);
            btn.setAttribute('title', this._t(m.title[0], m.title[1]));
            // A whitelisted key the scan has no spell for cannot be blacklisted (no form id)
            if (targets[t].name === 'blacklisted' && !row.spell && !row.entry) btn.disabled = true;
            moves.appendChild(btn);
        }
        li.appendChild(moves);
        return li;
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
        // The "?" tags: the spell's card while the pointer is on one
        var helpOf = function(e) { return e && e.target && e.target.closest ? e.target.closest('.spell-list-help') : null; };
        list.addEventListener('mouseover', function(e) {
            var help = helpOf(e);
            if (!help || typeof SpellPeek === 'undefined') return;
            var li = help.closest('li[data-index]');
            var row = li ? (self._items[column] || [])[parseInt(li.getAttribute('data-index'), 10)] : null;
            if (row && row.spell) SpellPeek.show(row.spell, help);
        });
        list.addEventListener('mouseout', function(e) {
            if (helpOf(e) && typeof SpellPeek !== 'undefined') SpellPeek.hide();
        });
        // A repaint or a scroll takes the "?" away from under the pointer; near the end, the next page of rows
        list.addEventListener('scroll', function() {
            if (typeof SpellPeek !== 'undefined') SpellPeek.hide();
            if (list.scrollHeight - list.scrollTop - list.clientHeight < self.MORE_AT_PX) self._more(column);
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
if (typeof document !== 'undefined' && document.addEventListener) {
    document.addEventListener('DOMContentLoaded', function() { SpellLists.initViews(); });
}
