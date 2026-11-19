/**
 * ScanSectionsTest - node tests for the Scan page's folding sections (scanSections.js).
 *
 * Checks: a click on a section's bar finds the section, a click on a control in the bar or outside any bar finds
 * none; folding sets the class and the bar's aria-expanded, a second toggle opens it again; one page listener.
 *
 * Depends on: scanSections.js, run in a vm sandbox with stand-in elements.
 */

var ScanSectionsTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    /** A stand-in element: classes, attributes, children, closest by class or tag list */
    _el: function(tag, cls, parent) {
        var classes = {}, attrs = {};
        (cls || '').split(' ').forEach(function(c) { if (c) classes[c] = true; });
        var e = {
            tagName: tag.toUpperCase(), parentNode: parent || null, children: [], listeners: {},
            classList: {
                contains: function(c) { return !!classes[c]; },
                toggle: function(c, on) { if (on === undefined) on = !classes[c]; if (on) classes[c] = true; else delete classes[c]; return on; }
            },
            setAttribute: function(n, v) { attrs[n] = String(v); },
            getAttribute: function(n) { return attrs.hasOwnProperty(n) ? attrs[n] : null; },
            addEventListener: function(t, fn) { e.listeners[t] = (e.listeners[t] || 0) + 1; },
            closest: function(sel) {
                var parts = sel.split(',').map(function(s) { return s.trim(); });
                for (var n = e; n; n = n.parentNode) {
                    for (var i = 0; i < parts.length; i++) {
                        var p = parts[i];
                        if (p.charAt(0) === '.' ? n.classList.contains(p.slice(1)) : n.tagName === p.toUpperCase()) return n;
                    }
                }
                return null;
            }
        };
        if (parent) parent.children.push(e);
        return e;
    },

    run: function() {
        this.passed = 0;
        this.failed = 0;
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var vm = require('vm'), fs = require('fs'), path = require('path');
        try {
            var page = this._el('div');
            var section = this._el('div', 'scan-section', page);
            var bar = this._el('div', 'tree-preview-header settings-block-header', section);
            var title = this._el('span', 'tree-preview-title', bar);
            var tab = this._el('button', 'tree-preview-tab', bar);
            var body = this._el('div', 'tree-preview-split', section);
            var resized = 0;
            var box = {
                document: { getElementById: function(id) { return id === 'contentSpellScan' ? page : null; } },
                window: { dispatchEvent: function() { resized++; } },
                Event: function(type) { this.type = type; }
            };
            vm.createContext(box);
            var file = path.join(__dirname, 'scanSections.js');
            vm.runInContext(fs.readFileSync(file, 'utf8'), box, { filename: file });
            var S = box.ScanSections;
            this.check(S.sectionOf(title) === section && S.sectionOf(bar) === section,
                'A click on the bar or its title finds the section');
            this.check(S.sectionOf(tab) === null && S.sectionOf(body) === null,
                'A click on a control in the bar, or on the content, folds nothing');
            this.check(S.toggle(section) === true && section.classList.contains('scan-section-folded') &&
                bar.getAttribute('aria-expanded') === 'false' && resized === 0,
                'Folding: the class on, the bar says collapsed, no resize');
            this.check(S.toggle(section) === false && !section.classList.contains('scan-section-folded') &&
                bar.getAttribute('aria-expanded') === 'true' && resized === 1,
                'Opening: the class off, the bar says expanded, a resize so a canvas fits again');
            S.init(); S.init();
            this.check(page.listeners.click === 1, 'One click listener on the page, however often init runs');
        } catch (e) {
            this.check(false, 'ScanSections threw: ' + (e && e.stack ? e.stack : e));
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.ScanSectionsTest = ScanSectionsTest;
if (typeof global !== 'undefined') global.ScanSectionsTest = ScanSectionsTest;
