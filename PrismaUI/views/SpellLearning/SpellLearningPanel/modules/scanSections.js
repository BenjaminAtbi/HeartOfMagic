/**
 * ScanSections - every section of the Scan page (Scan details, Spell Inclusion, Scaffold Preview, Tree Growth
 * Preview, Extra Settings) folds by its header bar: a click on the bar (not on a button or field in it) folds the
 * section to the bar alone, or opens it again. All start open.
 *
 * One listener on the page, so it covers the sections whose markup is written later (treePreview.js,
 * treeGrowth.js). A section is a .scan-section; its bar is its .tree-preview-header child. Folding sets
 * .scan-section-folded (panel-scan.css hides everything but the bar) and the bar's aria-expanded. Opening one
 * fires a window resize, so a canvas inside sizes itself to the box again.
 *
 * Called by: index.html (DOMContentLoaded). CSS: panel-scan.css (#contentSpellScan .scan-section).
 */

var ScanSections = {

    PAGE: 'contentSpellScan',
    FOLDED: 'scan-section-folded',

    /** The section a click folds: the bar's section, unless the click was on a control in the bar */
    sectionOf: function(target) {
        if (!target || !target.closest) return null;
        if (target.closest('button, input, select, textarea, a, label')) return null;
        var bar = target.closest('.tree-preview-header');
        var section = bar && bar.parentNode;
        return section && section.classList && section.classList.contains('scan-section') ? section : null;
    },

    isFolded: function(section) {
        return section.classList.contains(this.FOLDED);
    },

    /** Fold or open a section; folded: true / false, or left out to flip it */
    toggle: function(section, folded) {
        if (folded === undefined) folded = !this.isFolded(section);
        section.classList.toggle(this.FOLDED, folded);
        for (var i = 0; i < section.children.length; i++) {
            var bar = section.children[i];
            if (bar.classList && bar.classList.contains('tree-preview-header')) {
                bar.setAttribute('aria-expanded', folded ? 'false' : 'true');
            }
        }
        if (!folded && typeof window !== 'undefined' && typeof window.dispatchEvent === 'function' &&
            typeof Event === 'function') {
            window.dispatchEvent(new Event('resize'));
        }
        return folded;
    },

    init: function() {
        var page = typeof document !== 'undefined' ? document.getElementById(this.PAGE) : null;
        if (!page || this._wired) return;
        this._wired = true;
        var self = this;
        page.addEventListener('click', function(e) {
            var section = self.sectionOf(e.target);
            if (section) self.toggle(section);
        });
    }
};

if (typeof window !== 'undefined') window.ScanSections = ScanSections;
if (typeof document !== 'undefined' && document.addEventListener) {
    document.addEventListener('DOMContentLoaded', function() { ScanSections.init(); });
}
