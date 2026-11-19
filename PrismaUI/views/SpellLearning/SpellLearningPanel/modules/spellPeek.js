/**
 * SpellPeek - the Spells tab's "?" beside a spell's name: on hover, a small card with the spell's figures
 * and description, laid out like the tree's spell card: school, level, cost, type, an Effects table (power,
 * duration, size), the spell's tags (element, form, cast; the school is the badge), description.
 *
 * Everything comes from the scan's spell object (spell_scan_output.json), no call to the game: the
 * description is the effects' own text with <mag>, <dur> and <area> filled in from each effect, the way the
 * game writes it; effects hidden from the UI are left out.
 *
 * One card for the page (#spellPeekPopup), placed beside the "?" and kept inside the window.
 * Called by: spellLists.js (the lists' mouseover / mouseout). CSS: panel-scan.css (.spell-peek*).
 * Depends on: SpellCard (levelText, typeText, KNOWN_SCHOOLS; optional), tOr (i18n.js; optional).
 */

var SpellPeek = {

    ID: 'spellPeekPopup',
    GAP: 8,

    _t: function(key, params, fallback) {
        if (typeof tOr === 'function') return tOr(key, params, fallback);
        for (var p in (params || {})) fallback = fallback.split('{{' + p + '}}').join(params[p]);
        return fallback;
    },

    /** The effects a player sees: not hidden from the UI */
    _shownEffects: function(spell) {
        return (Array.isArray(spell.effects) ? spell.effects : []).filter(function(e) {
            return e && typeof e === 'object' && !(e.flags && e.flags.hideInUI);
        });
    },

    /** One effect's text with its figures in: <mag>, <dur>, <area>; any other tag dropped */
    fill: function(text, effect) {
        var num = function(v) { var n = Number(v) || 0; return String(Math.round(n * 10) / 10); };
        return String(text || '')
            .replace(/<mag>/gi, num(effect.magnitude))
            .replace(/<dur>/gi, num(effect.duration))
            .replace(/<area>/gi, num(effect.area))
            .replace(/<[^>]*>/g, '')
            .replace(/\s+/g, ' ').trim();
    },

    /** The spell's description, one text per shown effect, each text once (a spell has no text of its own) */
    descriptionParts: function(spell) {
        var self = this, seen = {}, out = [];
        this._shownEffects(spell).forEach(function(e) {
            var text = self.fill(e.description, e);
            if (text && !seen[text]) { seen[text] = true; out.push(text); }
        });
        return out;
    },

    description: function(spell) {
        return this.descriptionParts(spell).join(' ');
    },

    NONE: '—',

    /** One row per shown effect: [name, power, duration, size]; a figure the effect lacks is NONE */
    effectRows: function(spell) {
        var none = this.NONE;
        return this._shownEffects(spell).map(function(e) {
            var flags = e.flags || {};
            var mag = Math.round(Number(e.magnitude) || 0);
            var dur = Math.round(Number(e.duration) || 0);
            var area = Math.round(Number(e.area) || 0);
            return [
                e.name || '?',
                mag > 0 && !flags.noMagnitude ? String(mag) : none,
                dur > 0 && !flags.noDuration ? dur + 's' : none,
                area > 0 ? area + ' ft' : none
            ];
        });
    },

    /** The spell's tags other than its school (shown as the badge): element, form, cast, ... */
    chipIds: function(spell) {
        var list = Array.isArray(spell.chips) ? spell.chips : (Array.isArray(spell.traits) ? spell.traits : []);
        return list.map(String).filter(function(id) { return id && id.indexOf('school.') !== 0; });
    },

    _chips: function(spell) {
        var ids = this.chipIds(spell);
        if (!ids.length) return null;
        var C = typeof SpellCard !== 'undefined' ? SpellCard : null;
        var row = this._el('div', 'spell-chips spell-peek-chips');
        var self = this;
        ids.forEach(function(id) {
            var parts = id.split('.');
            // Same classes as the tree card's chips, so each tag keeps its colour
            row.appendChild(self._el('span', 'spell-chip chip-' + parts[0] + ' chip-' + parts.join('-'),
                C ? C.label(id) : id));
        });
        return row;
    },

    _effectsTable: function(spell) {
        var rows = this.effectRows(spell);
        if (!rows.length) return null;
        var box = this._el('div', 'spell-peek-effects');
        box.appendChild(this._el('div', 'spell-peek-section', this._t('details.effects', null, 'Effects')));
        var table = this._el('table', 'spell-peek-table');
        var head = this._el('tr');
        [['details.effect', 'Effect'], ['details.power', 'Power'], ['details.duration', 'Duration'],
            ['details.size', 'Size']].forEach(function(h) {
            head.appendChild(SpellPeek._el('th', null, SpellPeek._t(h[0], null, h[1])));
        });
        table.appendChild(head);
        rows.forEach(function(r) {
            var tr = SpellPeek._el('tr');
            r.forEach(function(v) { tr.appendChild(SpellPeek._el('td', null, v)); });
            table.appendChild(tr);
        });
        box.appendChild(table);
        return box;
    },

    _el: function(tag, cls, text) {
        var el = document.createElement(tag);
        if (cls) el.className = cls;
        if (text !== undefined) el.textContent = text;
        return el;
    },

    _row: function(label, value) {
        var row = this._el('div', 'spell-peek-row');
        row.appendChild(this._el('span', 'spell-peek-label', label));
        row.appendChild(this._el('span', 'spell-peek-value', value));
        return row;
    },

    /** The card's content for a spell */
    build: function(spell) {
        var C = typeof SpellCard !== 'undefined' ? SpellCard : null;
        var card = this._el('div', 'spell-peek-card');
        var head = this._el('div', 'spell-peek-head');
        head.appendChild(this._el('span', 'spell-peek-name', spell.name || spell.formId || '?'));
        var school = spell.school || '';
        if (school && (!C || C.KNOWN_SCHOOLS.indexOf(school) !== -1)) {
            var badge = this._el('span', 'school-badge ' + school.toLowerCase(),
                this._t('chips.school.' + school.toLowerCase(), null, school));
            head.appendChild(badge);
        }
        card.appendChild(head);
        var level = spell.skillLevel ? (C ? C.levelText(spell.skillLevel) : spell.skillLevel) : '?';
        var type = spell.castingType ? (C ? C.typeText(spell.castingType) : spell.castingType) : '?';
        card.appendChild(this._row(this._t('details.level', null, 'Level'), level));
        card.appendChild(this._row(this._t('details.cost', null, 'Cost'), String(spell.magickaCost !== undefined ? spell.magickaCost : '?')));
        card.appendChild(this._row(this._t('details.type', null, 'Type'), type + (spell.delivery ? ' · ' + spell.delivery : '')));
        var effects = this._effectsTable(spell);
        if (effects) card.appendChild(effects);
        var chips = this._chips(spell);
        if (chips) card.appendChild(chips);
        // Each effect's text in its own block, a thin line between them (spell-peek-desc-part + spell-peek-desc-part)
        var desc = this._el('div', 'spell-peek-desc');
        var parts = this.descriptionParts(spell);
        if (!parts.length) desc.textContent = this._t('details.noDescription', null, 'No description.');
        parts.forEach(function(text) { desc.appendChild(SpellPeek._el('div', 'spell-peek-desc-part', text)); });
        card.appendChild(desc);
        return card;
    },

    _popup: function() {
        var el = document.getElementById(this.ID);
        if (!el) {
            el = this._el('div', 'spell-peek hidden');
            el.id = this.ID;
            el.setAttribute('role', 'tooltip');
            document.body.appendChild(el);
        }
        return el;
    },

    /** Show the card for a spell beside an anchor (the "?"), inside the window */
    show: function(spell, anchor) {
        if (!spell || typeof document === 'undefined') return;
        var el = this._popup();
        el.innerHTML = '';
        el.appendChild(this.build(spell));
        el.classList.remove('hidden');
        var r = anchor.getBoundingClientRect();
        var w = el.offsetWidth, h = el.offsetHeight;
        var vw = window.innerWidth, vh = window.innerHeight;
        var left = r.right + this.GAP;
        if (left + w > vw - this.GAP) left = Math.max(this.GAP, r.left - this.GAP - w);
        var top = Math.min(Math.max(this.GAP, r.top - 4), vh - h - this.GAP);
        el.style.left = left + 'px';
        el.style.top = Math.max(this.GAP, top) + 'px';
    },

    hide: function() {
        var el = typeof document !== 'undefined' ? document.getElementById(this.ID) : null;
        if (el) el.classList.add('hidden');
    }
};

if (typeof window !== 'undefined') window.SpellPeek = SpellPeek;
