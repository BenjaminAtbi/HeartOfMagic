/**
 * SpellPeek - the Spells tab's "?" beside a spell's name: on hover, a small card with the spell's figures
 * and description, laid out like the tree's spell card (school, level, cost, type, effects, description).
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

    /** The spell's description: its shown effects' texts, each once */
    description: function(spell) {
        var self = this, seen = {}, out = [];
        this._shownEffects(spell).forEach(function(e) {
            var text = self.fill(e.description, e);
            if (text && !seen[text]) { seen[text] = true; out.push(text); }
        });
        return out.join(' ');
    },

    /** "Flames (8) 1s, 5 ft" for each shown effect */
    effectLines: function(spell) {
        return this._shownEffects(spell).map(function(e) {
            var text = e.name || '?';
            var mag = Math.round(Number(e.magnitude) || 0);
            if (mag > 0 && !(e.flags && e.flags.noMagnitude)) text += ' (' + mag + ')';
            if (Number(e.duration) > 0 && !(e.flags && e.flags.noDuration)) text += ' ' + e.duration + 's';
            if (Number(e.area) > 0) text += ', ' + e.area + ' ft';
            return text;
        });
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
        var effects = this.effectLines(spell);
        if (effects.length) {
            var list = this._el('ul', 'spell-peek-effects');
            effects.forEach(function(t) { list.appendChild(SpellPeek._el('li', null, t)); });
            card.appendChild(list);
        }
        card.appendChild(this._el('div', 'spell-peek-desc',
            this.description(spell) || this._t('details.noDescription', null, 'No description.')));
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
