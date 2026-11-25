/**
 * TreeNodeScale - the preview size settings applied to the real tree:
 *   - Tree Growth's Node Size sizes every spell but the roots (nodeScale: Node Size / NODE_DEFAULT, 5 is 1);
 *   - the Scaffold Preview's Root Size sizes the school roots (rootScale: Root Size / ROOT_DEFAULT, 8 is 1).
 *
 * The build writes both scales into the tree JSON. Loading a tree sets the renderer's spell sizes
 * (KNOWN_SIZE, LEARNABLE_SIZE, LOCKED_SIZE) from nodeScale, and NODE_SCALE / ROOT_SCALE, from which
 * CanvasRenderer._spellSize gives a root its own size (canvasRendererNodes.js). A tree without a scale
 * (built before this) draws that part at 1.
 *
 * Spacing is not scaled: the declutter's minimum distance (LayoutDeclutter NODE_RADIUS, and the plugin's
 * kNodeRadius) is for size 1, so large scales can draw neighbouring spells over each other.
 *
 * Called by: classicTreeOutput.js (build: fromSettings), treeLoad.js (loadTreeData: apply).
 * Depends on: TreePreview, TreeGrowthClassic, CanvasRenderer (all optional).
 */

var TreeNodeScale = {

    NODE_DEFAULT: 5,                // Tree Growth's default Node Size: scale 1
    ROOT_DEFAULT: 8,                // the Scaffold Preview's default Root Size: scale 1
    MIN: 0.2,
    MAX: 4,
    BASE: { KNOWN_SIZE: 12, LEARNABLE_SIZE: 9, LOCKED_SIZE: 7 },

    /** A scale kept inside MIN..MAX; anything not a positive number is 1 */
    clamp: function(scale) {
        var n = Number(scale);
        if (!isFinite(n) || n <= 0) return 1;
        return Math.min(this.MAX, Math.max(this.MIN, n));
    },

    /** A size setting as a scale of its default; a size that is not a positive number is 1 */
    fromSize: function(size, def) {
        var n = Number(size);
        return (isFinite(n) && n > 0) ? this.clamp(n / def) : 1;
    },

    /** Root Size of the Scaffold Preview's active mode, as a scale (1 when it has none) */
    rootFromPreview: function() {
        if (typeof TreePreview === 'undefined' || !TreePreview.modes) return 1;
        var mode = TreePreview.modes[TreePreview.activeMode];
        var size = mode && mode.settings ? mode.settings.nodeSize : undefined;
        return size === undefined ? 1 : this.fromSize(size, this.ROOT_DEFAULT);
    },

    /** Tree Growth's Node Size (TreeGrowthClassic.settings.nodeRadius), as a scale */
    nodeFromGrowth: function() {
        if (typeof TreeGrowthClassic === 'undefined' || !TreeGrowthClassic.settings) return 1;
        var size = TreeGrowthClassic.settings.nodeRadius;
        return size === undefined ? 1 : this.fromSize(size, this.NODE_DEFAULT);
    },

    /** Both scales for the tree JSON */
    fromSettings: function() {
        return { nodeScale: this.nodeFromGrowth(), rootScale: this.rootFromPreview() };
    },

    /** A tree's scales: its nodeScale and rootScale, 1 for one it lacks */
    fromTree: function(data) {
        var d = data || {};
        return {
            nodeScale: d.nodeScale !== undefined ? this.clamp(d.nodeScale) : 1,
            rootScale: d.rootScale !== undefined ? this.clamp(d.rootScale) : 1
        };
    },

    /** Set the renderer's spell sizes for a tree; returns the scales used */
    apply: function(data, renderer) {
        var s = this.fromTree(data);
        var r = renderer || (typeof CanvasRenderer !== 'undefined' ? CanvasRenderer : null);
        if (r) {
            for (var k in this.BASE) r[k] = this.BASE[k] * s.nodeScale;
            r.NODE_SCALE = s.nodeScale;
            r.ROOT_SCALE = s.rootScale;
        }
        return s;
    }
};

if (typeof window !== 'undefined') window.TreeNodeScale = TreeNodeScale;
if (typeof global !== 'undefined') global.TreeNodeScale = TreeNodeScale;
