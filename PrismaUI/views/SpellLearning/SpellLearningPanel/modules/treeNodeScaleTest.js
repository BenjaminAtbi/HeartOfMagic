/**
 * TreeNodeScaleTest - node tests for TreeNodeScale (run-tests.js): Node Size and Root Size to scales,
 * the tree's scales clamped, the renderer's spell sizes set from nodeScale, a root sized by rootScale
 * (CanvasRenderer._spellSize, copied onto a stand-in renderer), and back to 1 for a tree without them.
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

    // The renderer's helpers (canvasRendererNodes.js), as the renderer has them
    _renderer: function() {
        return {
            KNOWN_SIZE: 12, LEARNABLE_SIZE: 9, LOCKED_SIZE: 7, NODE_SCALE: 1, ROOT_SCALE: 1,
            _spellSize: function(node, size) {
                return node && node.isRoot ? size * (this.ROOT_SCALE || 1) / (this.NODE_SCALE || 1) : size;
            },
            _largestSpellSize: function() {
                return this.KNOWN_SIZE * Math.max(1, (this.ROOT_SCALE || 1) / (this.NODE_SCALE || 1));
            }
        };
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        var S = TreeNodeScale;

        this.check(S.fromSize(5, S.NODE_DEFAULT) === 1 && S.fromSize(8, S.ROOT_DEFAULT) === 1,
            'the default Node Size 5 and Root Size 8 are scale 1');
        this.check(S.fromSize(10, S.NODE_DEFAULT) === 2 && S.fromSize(16, S.ROOT_DEFAULT) === 2, 'twice the default is 2');
        this.check(S.fromSize(9999, 8) === S.MAX && S.fromSize(1, 8) === S.MIN, 'sizes are kept in MIN..MAX');
        this.check(S.fromSize('x', 8) === 1 && S.fromSize(0, 8) === 1, 'a size that is not a positive number is 1');

        var none = S.fromTree({});
        this.check(none.nodeScale === 1 && none.rootScale === 1 && S.fromTree(null).nodeScale === 1,
            'a tree without scales is 1 and 1');
        var t = S.fromTree({ nodeScale: 1.5, rootScale: 50 });
        this.check(t.nodeScale === 1.5 && t.rootScale === S.MAX, "a tree's scales are used, clamped");

        var r = this._renderer();
        S.apply({ nodeScale: 2, rootScale: 0.5 }, r);
        this.check(r.KNOWN_SIZE === 24 && r.LEARNABLE_SIZE === 18 && r.LOCKED_SIZE === 14,
            'nodeScale scales the three spell sizes');
        this.check(r._spellSize({}, r.KNOWN_SIZE) === 24, 'a spell that is not a root keeps the Node Size');
        this.check(r._spellSize({ isRoot: true }, r.KNOWN_SIZE) === 6 && r._spellSize({ isRoot: true }, r.LOCKED_SIZE) === 3.5,
            'a root is drawn at rootScale, whatever nodeScale is');
        this.check(r._largestSpellSize() === 24, 'smaller roots: the largest spell is KNOWN_SIZE');
        S.apply({ nodeScale: 1, rootScale: 3 }, r);
        this.check(r._largestSpellSize() === 36 && r._spellSize({ isRoot: true }, r.KNOWN_SIZE) === 36,
            'bigger roots: the largest spell is a root');
        S.apply({}, r);
        this.check(r.KNOWN_SIZE === 12 && r.LOCKED_SIZE === 7 && r._spellSize({ isRoot: true }, 12) === 12,
            'a tree without scales puts every size back');

        var g = typeof global !== 'undefined' ? global : window;
        var savedPreview = g.TreePreview, savedGrowth = g.TreeGrowthClassic;
        g.TreePreview = { activeMode: 'sun', modes: { sun: { settings: { nodeSize: 12 } }, flat: { settings: {} } } };
        g.TreeGrowthClassic = { settings: { nodeRadius: 10 } };
        var both = S.fromSettings();
        this.check(both.rootScale === 1.5 && both.nodeScale === 2,
            "Root Size comes from the Scaffold Preview, Node Size from Tree Growth");
        g.TreePreview.activeMode = 'flat';
        this.check(S.rootFromPreview() === 1, 'a mode without Root Size gives 1');
        g.TreePreview = savedPreview; g.TreeGrowthClassic = savedGrowth;

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.TreeNodeScaleTest = TreeNodeScaleTest;
if (typeof global !== 'undefined') global.TreeNodeScaleTest = TreeNodeScaleTest;
