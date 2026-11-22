/**
 * selectSkin.js - the panel's own drop-down list for <select class="setting-select">.
 *
 * Ultralight (the game's PrismaUI engine, 1.4.1) draws a <select>'s open list itself, white
 * with a small fixed font: no CSS on the select or its options reaches it (prismaui-lab test
 * 2026-11-19: font-size 28px on select and option, colours on option - the list stayed the
 * same), so the Font Size setting and the designs never applied to it.
 *
 * Each such select stays in the page, hidden, as the one place the value lives: every module
 * keeps reading select.value, setting it, filling its options and listening for 'change'.
 * A button shows its chosen option; a click opens a list drawn here at the button's own font
 * size and in the theme's colours; picking an item sets select.value and sends 'change' as
 * the browser would. Option changes (MutationObserver) and code setting value/selectedIndex
 * (the select's own accessors, wrapped) update the button.
 */
var SelectSkin = {
    _open: null,  // { select, button, list } while a list is open

    /** Wrap every select.setting-select once (more can be passed later). */
    enhanceAll: function(root) {
        var selects = (root || document).querySelectorAll('select.setting-select');
        for (var i = 0; i < selects.length; i++) this.enhance(selects[i]);
    },

    enhance: function(select) {
        if (!select || select._selectSkin) return;
        var self = this;
        var button = document.createElement('div');
        button.className = select.className + ' hom-select';  // the select's own look (.setting-select, designs' rules)
        button.setAttribute('role', 'button');
        button.tabIndex = 0;
        var label = document.createElement('span');
        label.className = 'hom-select-label';
        var arrow = document.createElement('span');
        arrow.className = 'hom-select-arrow';
        arrow.textContent = '▾';
        button.appendChild(label);
        button.appendChild(arrow);
        select.parentNode.insertBefore(button, select.nextSibling);
        select.classList.add('hom-select-native');
        select._selectSkin = { button: button, label: label };

        button.addEventListener('mousedown', function(e) {
            e.preventDefault();
            e.stopPropagation();
            if (select.disabled) return;
            if (self._open && self._open.select === select) self.close();
            else self.open(select);
        });
        button.addEventListener('keydown', function(e) {
            if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault();
                if (self._open && self._open.select === select) self.close();
                else if (!select.disabled) self.open(select);
            } else if (e.key === 'Escape' && self._open) {
                e.preventDefault();  // script.js then only notes the press: the panel stays open
                self.close();
            }
        });

        if (typeof MutationObserver !== 'undefined') {
            new MutationObserver(function() { self.refresh(select); })
                .observe(select, { childList: true, subtree: true, characterData: true, attributes: true });
        }
        this._watchValue(select);
        this.refresh(select);
    },

    /** Code that sets select.value or selectedIndex fires no event: wrap those setters on this element. */
    _watchValue: function(select) {
        var self = this;
        var proto = typeof HTMLSelectElement !== 'undefined' ? HTMLSelectElement.prototype : null;
        ['value', 'selectedIndex'].forEach(function(name) {
            var d = proto && Object.getOwnPropertyDescriptor(proto, name);
            if (!d || !d.get || !d.set) return;
            try {
                Object.defineProperty(select, name, {
                    configurable: true,
                    get: function() { return d.get.call(this); },
                    set: function(v) { d.set.call(this, v); self.refresh(this); }
                });
            } catch (e) {
                // Left unwrapped: the button still follows option changes, picks and each opening
            }
        });
    },

    refresh: function(select) {
        var skin = select && select._selectSkin;
        if (!skin) return;
        var option = select.options[select.selectedIndex];
        skin.label.textContent = option ? option.textContent : '';
        skin.button.classList.toggle('disabled', !!select.disabled);
        skin.button.title = select.title || '';
        if (this._open && this._open.select === select) this._fill(select, this._open.list);
    },

    open: function(select) {
        this.close();
        this.refresh(select);
        var button = select._selectSkin.button;
        var list = document.createElement('div');
        list.className = 'hom-select-list';
        // The button's own size (the Font Size setting and the row's em sizes are already in it)
        var style = window.getComputedStyle(button);
        list.style.fontSize = style.fontSize;
        list.style.fontFamily = style.fontFamily;
        this._fill(select, list);
        document.body.appendChild(list);
        this._open = { select: select, button: button, list: list };
        this._place(button, list);
        button.classList.add('open');
        var chosen = list.querySelector('.hom-select-item.selected');
        if (chosen && chosen.scrollIntoView) chosen.scrollIntoView({ block: 'nearest' });
    },

    _fill: function(select, list) {
        var self = this;
        list.innerHTML = '';
        for (var i = 0; i < select.options.length; i++) {
            var option = select.options[i];
            var item = document.createElement('div');
            item.className = 'hom-select-item' + (i === select.selectedIndex ? ' selected' : '') + (option.disabled ? ' disabled' : '');
            item.textContent = option.textContent;
            item._index = i;
            item.addEventListener('mousedown', function(e) {
                e.preventDefault();
                e.stopPropagation();
                if (select.options[this._index] && !select.options[this._index].disabled) self.pick(select, this._index);
            });
            list.appendChild(item);
        }
    },

    /** Under the button, or above it when the window has no room below; never past the window's edges. */
    _place: function(button, list) {
        var r = button.getBoundingClientRect();
        var winW = window.innerWidth, winH = window.innerHeight, gap = 2;
        list.style.minWidth = r.width + 'px';
        var below = winH - r.bottom - gap, above = r.top - gap;
        var height = list.offsetHeight;
        var downward = height <= below || below >= above;
        var room = Math.max(40, downward ? below : above);
        if (height > room) list.style.maxHeight = room + 'px';
        height = Math.min(height, room);
        list.style.top = (downward ? r.bottom + gap : r.top - gap - height) + 'px';
        var width = list.offsetWidth;
        list.style.left = Math.max(0, Math.min(r.left, winW - width)) + 'px';
    },

    pick: function(select, index) {
        var changed = select.selectedIndex !== index;
        select.selectedIndex = index;
        this.close();
        if (changed) {
            var ev;
            try {
                ev = new Event('change', { bubbles: true });
            } catch (e) {
                ev = document.createEvent('Event');
                ev.initEvent('change', true, false);
            }
            select.dispatchEvent(ev);
        }
        this.refresh(select);
    },

    close: function() {
        if (!this._open) return;
        this._open.button.classList.remove('open');
        if (this._open.list.parentNode) this._open.list.parentNode.removeChild(this._open.list);
        this._open = null;
    }
};

(function() {
    function start() {
        SelectSkin.enhanceAll();
        // A press anywhere else, the wheel outside the list, Escape or a resize closes an open list
        document.addEventListener('mousedown', function(e) {
            var open = SelectSkin._open;
            // its own button toggles it in the button's handler (this capture listener runs first)
            if (open && !open.list.contains(e.target) && !open.button.contains(e.target)) SelectSkin.close();
        }, true);
        document.addEventListener('wheel', function(e) {
            if (SelectSkin._open && !SelectSkin._open.list.contains(e.target)) SelectSkin.close();
        }, true);
        // Escape with a list open closes the list only: marked used (script.js's keydown then only notes
        // the press), and handleEscapePress closes it first too, for the plugin's onNativeEscape
        document.addEventListener('keydown', function(e) {
            if (e.key === 'Escape' && SelectSkin._open) {
                e.preventDefault();
                SelectSkin.close();
            }
        }, true);
        window.addEventListener('resize', function() { SelectSkin.close(); });
    }
    if (typeof document === 'undefined' || !document.addEventListener) return;
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
    else start();
})();
