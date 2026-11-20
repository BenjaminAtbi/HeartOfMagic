/**
 * TreeNodeScaleTest - node tests for TreeNodeScale (run-tests.js): Root Size to scale, the tree's
 * nodeScale clamped, the renderer's spell sizes set from it and back to 1 for a tree without one.
 *
 * Depends on: TreeNodeScale
 */

var TreeNodeScaleTest = {

    passed: 0,
    failed: 0,

    check: function(ok, name) {
        if (ok) { this.passed++; console.log('  [PASS] ' + name); }
        else { this.failed++; console.log('  [FAIL] ' + name); }
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        var S = TreeNodeScale;

        this.check(S.fromNodeSize(8) === 1, 'the default Root Size 8 is scale 1');
        this.check(S.fromNodeSize(16) === 2, 'Root Size 16 is scale 2');
        this.check(S.fromNodeSize(9999) === S.MAX && S.fromNodeSize(1) === S.MIN, 'Root Size is kept in MIN..MAX');
        this.check(S.fromNodeSize('x') === 1 && S.fromNodeSize(0) === 1, 'a size that is not a positive number is 1');

        this.check(S.fromTree({}) === 1 && S.fromTree(null) === 1, 'a tree without nodeScale is 1');
        this.check(S.fromTree({ nodeScale: 1.5 }) === 1.5, "a tree's nodeScale is used");
        this.check(S.fromTree({ nodeScale: 50 }) === S.MAX, "a tree's nodeScale is clamped");

        var r = { KNOWN_SIZE: 12, LEARNABLE_SIZE: 9, LOCKED_SIZE: 7 };
        S.apply({ nodeScale: 2 }, r);
        this.check(r.KNOWN_SIZE === 24 && r.LEARNABLE_SIZE === 18 && r.LOCKED_SIZE === 14,
            'apply scales the three spell sizes');
        S.apply({ nodeScale: 2 }, r);
        this.check(r.KNOWN_SIZE === 24, 'apply twice does not compound');
        S.apply({}, r);
        this.check(r.KNOWN_SIZE === 12 && r.LEARNABLE_SIZE === 9 && r.LOCKED_SIZE === 7,
            'a tree without nodeScale puts the sizes back');

        var savedPreview = typeof TreePreview !== 'undefined' ? TreePreview : undefined;
        var g = typeof global !== 'undefined' ? global : window;
        g.TreePreview = { activeMode: 'sun', modes: { sun: { settings: { nodeSize: 12 } }, flat: { settings: {} } } };
        this.check(S.fromPreview() === 1.5, "the sun preview's Root Size gives the scale");
        g.TreePreview.activeMode = 'flat';
        this.check(S.fromPreview() === 1, 'a mode without Root Size gives 1');
        g.TreePreview = savedPreview;

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.TreeNodeScaleTest = TreeNodeScaleTest;
if (typeof global !== 'undefined') global.TreeNodeScaleTest = TreeNodeScaleTest;
