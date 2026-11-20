/**
 * TreeNodeScale - the Scaffold Preview's Root Size applied to the real tree.
 *
 * The build writes the size as a scale into the tree JSON (nodeScale: Root Size / DEFAULT_SIZE, so the
 * default 8 is 1); loading a tree sets the renderer's spell sizes from it (KNOWN_SIZE, LEARNABLE_SIZE,
 * LOCKED_SIZE; the book style's chapter titles keep clear of KNOWN_SIZE), so every use of those sizes
 * follows. A tree without nodeScale (built before this) draws at 1.
 *
 * Spacing is not scaled: the declutter's minimum distance (LayoutDeclutter NODE_RADIUS, and the plugin's
 * kNodeRadius) is for size 1, so large scales can draw neighbouring spells over each other.
 *
 * Called by: classicTreeOutput.js (build: fromPreview), treeLoad.js (loadTreeData: apply).
 * Depends on: TreePreview (optional), CanvasRenderer (optional).
 */

var TreeNodeScale = {

    DEFAULT_SIZE: 8,                // the preview's default Root Size: scale 1
    MIN: 0.25,
    MAX: 4,
    BASE: { KNOWN_SIZE: 12, LEARNABLE_SIZE: 9, LOCKED_SIZE: 7 },

    /** A scale kept inside MIN..MAX; anything not a positive number is 1 */
    clamp: function(scale) {
        var n = Number(scale);
        if (!isFinite(n) || n <= 0) return 1;
        return Math.min(this.MAX, Math.max(this.MIN, n));
    },

    fromNodeSize: function(size) {
        var n = Number(size);
        return (isFinite(n) && n > 0) ? this.clamp(n / this.DEFAULT_SIZE) : 1;
    },

    /** The scale of the preview's active mode (TreePreview), 1 when it has no Root Size */
    fromPreview: function() {
        if (typeof TreePreview === 'undefined' || !TreePreview.modes) return 1;
        var mode = TreePreview.modes[TreePreview.activeMode];
        var size = mode && mode.settings ? mode.settings.nodeSize : undefined;
        return size === undefined ? 1 : this.fromNodeSize(size);
    },

    /** A tree's scale: its nodeScale, 1 without one */
    fromTree: function(data) {
        return data && data.nodeScale !== undefined ? this.clamp(data.nodeScale) : 1;
    },

    /** Set the renderer's spell sizes for a tree (or a scale); returns the scale used */
    apply: function(dataOrScale, renderer) {
        var scale = typeof dataOrScale === 'number' ? this.clamp(dataOrScale) : this.fromTree(dataOrScale);
        var r = renderer || (typeof CanvasRenderer !== 'undefined' ? CanvasRenderer : null);
        if (r) for (var k in this.BASE) r[k] = this.BASE[k] * scale;
        return scale;
    }
};

if (typeof window !== 'undefined') window.TreeNodeScale = TreeNodeScale;
if (typeof global !== 'undefined') global.TreeNodeScale = TreeNodeScale;
