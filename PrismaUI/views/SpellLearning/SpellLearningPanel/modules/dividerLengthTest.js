/**
 * DividerLengthTest - node tests for the school dividers' lengths (run-tests.js):
 * each divider runs DIVIDER_MARGIN past the furthest shown spell within
 * DIVIDER_BAND of its line on the outward side, a divider no spell comes near
 * runs as far as the furthest spell, and with no spells the dividerLength
 * setting holds.
 *
 * Depends on: CanvasRenderer (canvasRendererDividers.js), settings
 */

var DividerLengthTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        var g = (typeof global !== 'undefined') ? global : window;
        var R = g.CanvasRenderer;
        if (!R || typeof R._dividerLengths !== 'function') {
            this.check(false, 'canvasRendererDividers.js loaded');
            return { passed: this.passed, failed: this.failed };
        }
        var band = R.DIVIDER_BAND, margin = R.DIVIDER_MARGIN;
        // Dividers pointing right (0 deg), down (90) and left (180)
        var r = {
            schools: { A: { startAngle: 0 }, B: { startAngle: 90 }, C: { startAngle: 180 } },
            nodes: [
                { school: 'A', x: 500, y: band - 1 },     // close to the right divider
                { school: 'A', x: 900, y: band + 1 },     // further out, but too far from its line
                { school: 'B', x: 10, y: 300 },           // close to the down divider
                { school: 'B', x: -700, y: 5 },           // the furthest spell, close to the left divider...
                { school: 'X', x: -1200, y: 0 }           // ...but this one's school is hidden
            ],
            _dividerAngle: R._dividerAngle,
            _dividerLengths: R._dividerLengths,
            DIVIDER_BAND: band, DIVIDER_MARGIN: margin
        };
        var oldSettings = g.settings;
        try {
            g.settings = { schoolVisibility: { X: false } };
            var names = ['A', 'B', 'C'];
            var lengths = r._dividerLengths(names, { x: 0, y: 0 }, 800);
            this.check(lengths[0] === 500 + margin, 'a divider ends a margin past the furthest spell close to its line');
            this.check(lengths[1] === 300 + margin && lengths[2] === 700 + margin,
                'each divider has its own length; a hidden school\'s spells do not count');

            r.nodes = [{ school: 'A', x: 0, y: 400 }];   // near the down divider only
            lengths = r._dividerLengths(names, { x: 0, y: 0 }, 800);
            this.check(lengths[1] === 400 + margin && lengths[0] === 400 + margin && lengths[2] === 400 + margin,
                'a divider no spell comes near runs as far as the furthest spell');

            r.nodes = [];
            this.check(r._dividerLengths(names, { x: 0, y: 0 }, 800).join() === '800,800,800',
                'no spells: the dividerLength setting');

            r.nodes = [{ school: 'A', x: 600, y: 0 }];
            this.check(r._dividerLengths(names, { x: 100, y: 0 }, 800)[0] === 500 + margin,
                'lengths are measured from the globe');
        } finally {
            g.settings = oldSettings;
        }
        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.DividerLengthTest = DividerLengthTest;
if (typeof global !== 'undefined') global.DividerLengthTest = DividerLengthTest;
