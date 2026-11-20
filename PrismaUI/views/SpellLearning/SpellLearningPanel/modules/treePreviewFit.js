/**
 * TreePreviewFit - the Scaffold Preview's settings box as tall as its tallest tab, so switching modes
 * moves nothing and no theme's fonts push a tab past the box.
 *
 * TreePreview builds only the active mode's settings. Beside them, this lays a copy of every other
 * mode's settings in the same grid cell, hidden (visibility, inert, aria-hidden) but kept in the layout:
 * the cell takes the tallest one's height, and follows the theme and width by itself. The copies carry
 * no id, name or for (built detached, so their radios never join the live groups), and nothing binds
 * to them.
 *
 * Used by: treePreview.js _loadModeSettings. CSS: panel-scan.css (.tree-preview-fit, .tp-page).
 */

var TreePreviewFit = {

    STRIP: ['id', 'name', 'for'],

    /** The live page's markup, wrapped for the shared grid cell */
    page: function(html) {
        return '<div class="tp-page">' + html + '</div>';
    },

    /** Append a hidden copy of every mode's settings but the active one's */
    addGhosts: function(settingsEl, modes, active) {
        if (!settingsEl || typeof document === 'undefined' || !document.createElement) return;
        for (var name in modes) {
            if (!modes.hasOwnProperty(name) || name === active) continue;
            var mode = modes[name];
            if (!mode || typeof mode.buildSettingsHTML !== 'function') continue;
            var ghost = document.createElement('div');
            ghost.className = 'tp-page tp-page-ghost';
            ghost.setAttribute('aria-hidden', 'true');
            ghost.setAttribute('inert', '');
            try { ghost.innerHTML = mode.buildSettingsHTML(); } catch (e) { continue; }
            this._strip(ghost);
            settingsEl.appendChild(ghost);
        }
    },

    _strip: function(root) {
        if (!root.querySelectorAll) return;
        var els = root.querySelectorAll('[id], [name], [for]');
        for (var i = 0; i < els.length; i++) {
            for (var a = 0; a < this.STRIP.length; a++) els[i].removeAttribute(this.STRIP[a]);
            if ('tabIndex' in els[i]) els[i].tabIndex = -1;
        }
    }
};
