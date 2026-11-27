/**
 * TreeGrid - the grid of an applied tree: what Apply saves of it, the points the tree view's edit
 * mode shows and snaps to, and the snap that keeps an applied tree's spells on them
 *
 * A tree applied from Tree Growth (TreeGrowthClassic.applyTree) is the preview's layout at
 * APPLY_SCALE, on the preview's grid at the same scale. It saves that grid as `grid`: the SUN
 * grid's parameters (SunGridPoints.toSaved, scale included) and `extra`, the points the layout
 * added to it (ClassicLayout._densifyGrid). Every place that grid appears rebuilds it from
 * those parameters (SunGridPoints), so it is the same points everywhere.
 *
 * A tree without a grid of its own (applied before 2026-11-26, a flat layout, an import) gets
 * the fallback display grid: the square grid edit mode always showed (FALLBACK_SPACING apart,
 * out to FALLBACK_EXTENT).
 *
 * Usage:
 *   output.grid = TreeGrid.forApply(baseData, layoutData);    // ClassicTreeOutput.build
 *   TreeGrid.snap(output);                                    // after LayoutDeclutter, before the save
 *   var g = TreeGrid.positions(tree);   // { points: [{x, y}], baked: true|false } (edit mode)
 *
 * Depends on: sunGridPoints.js
 */

var TreeGrid = {

    /**
     * An applied tree is the preview at this scale: the room LayoutDeclutter's SPREAD made (it
     * spread the tree 1.35 before it untangled it) is now the grid's scale, and the declutter
     * runs without its own spread, so the spells it does not move stay on grid points.
     */
    APPLY_SCALE: 1.35,

    /** The fallback display grid: a square grid this far apart, out to this far */
    FALLBACK_SPACING: 50,
    FALLBACK_EXTENT: 1300,

    /** A spell this close to a point is on it */
    ON_POINT: 0.5,
    /** A free point at least this far from every other spell is preferred (two drawn spells, 12 + 12) */
    CLEAR: 24,
    /** Points searched round a spell, in grid spacings */
    SEARCH_SPACINGS: 4,
    /** Nothing free that near: the search widens this many times (a crowded inner ring) */
    FAR_FACTOR: 4,

    _positionsCache: null,

    /**
     * The grid an applied tree saves (output.grid): null without a SUN preview grid (flat mode).
     * @param {Object|null} baseData - TreePreview.getOutput() (grid.params: SunGridPoints.params)
     * @param {Object|null} layoutData - ClassicLayout.layoutAllSchools() (schools[].addedPoints)
     * @param {number} [scale] - default APPLY_SCALE
     */
    forApply: function(baseData, layoutData, scale) {
        var params = baseData && baseData.mode === 'sun' && baseData.grid ? baseData.grid.params : null;
        if (!params || typeof SunGridPoints === 'undefined') return null;
        var s = (typeof scale === 'number' && scale > 0) ? scale : this.APPLY_SCALE;
        var saved = SunGridPoints.toSaved(params);
        saved.scale = Math.round(params.scale * s * 10000) / 10000;
        var extra = [];
        var schools = (layoutData && layoutData.schools) || {};
        Object.keys(schools).sort().forEach(function(name) {
            var added = schools[name].addedPoints || [];
            for (var i = 0; i < added.length; i++) {
                extra.push([Math.round(added[i].x * s * 100) / 100, Math.round(added[i].y * s * 100) / 100]);
            }
        });
        saved.extra = extra;
        return saved;
    },

    /**
     * The points of a tree's grid: its own (baked: true) or the fallback display grid.
     * @param {Object|null} tree - a saved tree (its `grid`), or null
     * @returns {{points: Array<{x:number,y:number}>, baked: boolean, spacing: number}}
     */
    positions: function(tree) {
        var g = tree && tree.grid;
        var c = this._positionsCache;
        if (c && c.grid === g && c.tree === tree) return c.result;
        var result = this._fromSaved(g) || this._fallback();
        this._positionsCache = { tree: tree, grid: g, result: result };
        return result;
    },

    _fromSaved: function(g) {
        var params = (g && typeof SunGridPoints !== 'undefined') ? SunGridPoints.fromSaved(g) : null;
        if (!params) return null;
        var points = SunGridPoints.points(params).map(function(p) { return { x: p.x, y: p.y }; });
        var extra = Array.isArray(g.extra) ? g.extra : [];
        for (var i = 0; i < extra.length && points.length < SunGridPoints.MAX_DOTS * 2; i++) {
            var e = extra[i];
            if (Array.isArray(e) && isFinite(e[0]) && isFinite(e[1])) points.push({ x: +e[0], y: +e[1] });
        }
        return { points: points, baked: true, spacing: params.tierSpacing * params.scale };
    },

    _fallback: function() {
        var points = [], s = this.FALLBACK_SPACING, ext = this.FALLBACK_EXTENT;
        for (var gx = -ext; gx <= ext; gx += s) {
            for (var gy = -ext; gy <= ext; gy += s) points.push({ x: gx, y: gy });
        }
        return { points: points, baked: false, spacing: s };
    },

    /**
     * Put every spell of `output` that is off its grid (moved by LayoutDeclutter) on the
     * nearest free point inside its school's segment (startAngle..endAngle round the globe),
     * one well clear of the other spells where there is one in reach. Roots stay where they
     * are; nothing happens for a tree without a grid of its own.
     * Deterministic: schools by name, spells in their order.
     * @returns {number} spells moved onto a point
     */
    snap: function(output) {
        if (!output || !output.schools || !output.grid) return 0;
        var grid = this._fromSaved(output.grid);
        if (!grid || grid.points.length === 0) return 0;
        var pts = grid.points, cell = Math.max(grid.spacing, 1);
        var hash = {};
        var key = function(x, y) { return Math.floor(x / cell) + ',' + Math.floor(y / cell); };
        for (var i = 0; i < pts.length; i++) (hash[key(pts[i].x, pts[i].y)] || (hash[key(pts[i].x, pts[i].y)] = [])).push(i);

        var taken = {}, nodes = [], arcs = [], self = this;
        var globe = output.globe || {};
        Object.keys(output.schools).sort().forEach(function(name) {
            var arc = self._arc(output.schools[name], globe);
            (output.schools[name].nodes || []).forEach(function(n) {
                if (typeof n.x === 'number' && typeof n.y === 'number' && isFinite(n.x) && isFinite(n.y)) {
                    nodes.push(n);
                    arcs.push(arc);
                }
            });
        });
        // Placed spells: a hash of their positions, for the clearance check
        var placed = {};
        var place = function(n) { (placed[key(n.x, n.y)] || (placed[key(n.x, n.y)] = [])).push(n); };
        var off = [], offArcs = [];
        nodes.forEach(function(n, i) {
            var at = self._nearest(pts, hash, cell, n.x, n.y, 0, taken, null, null, null);
            if (at >= 0 && self._dist(pts[at], n) <= self.ON_POINT && self._inArc(arcs[i], n)) taken[at] = true;
            else if (!n.isRoot) { off.push(n); offArcs.push(arcs[i]); return; }
            place(n);
        });

        var reach = this.SEARCH_SPACINGS;
        var moved = 0;
        for (var k = 0; k < off.length; k++) {
            var n = off[k], arc = offArcs[k];
            var best = this._nearest(pts, hash, cell, n.x, n.y, reach, taken, placed, this.CLEAR, arc);
            if (best < 0) best = this._nearest(pts, hash, cell, n.x, n.y, reach, taken, null, null, arc);
            if (best < 0) best = this._nearest(pts, hash, cell, n.x, n.y, reach * this.FAR_FACTOR, taken, null, null, arc);
            if (best >= 0) {
                taken[best] = true;
                n.x = Math.round(pts[best].x * 100) / 100;
                n.y = Math.round(pts[best].y * 100) / 100;
                moved++;
            }
            place(n);
        }
        console.log('[TreeGrid] ' + moved + ' of ' + off.length + ' spells off the grid put on a free point (' +
            pts.length + ' points)');
        return moved;
    },

    _dist: function(a, b) {
        var dx = a.x - b.x, dy = a.y - b.y;
        return Math.sqrt(dx * dx + dy * dy);
    },

    /** A school's segment round the globe (degrees, as ClassicTreeOutput saves it), or null */
    _arc: function(school, globe) {
        var a = school.startAngle, b = school.endAngle;
        if (typeof a !== 'number' || typeof b !== 'number' || !isFinite(a) || !isFinite(b) || b <= a) return null;
        return { x: +globe.x || 0, y: +globe.y || 0, start: a, size: b - a };
    },

    _inArc: function(arc, p) {
        if (!arc || arc.size >= 360) return true;
        var deg = Math.atan2(p.y - arc.y, p.x - arc.x) * 180 / Math.PI;
        return ((deg - arc.start) % 360 + 360) % 360 <= arc.size;
    },

    /**
     * The nearest point to (x, y) within `reach` cells round it (0: its cell and the ones next
     * to it), not taken, inside `arc` when given and, with `placed`, at least `clear` from
     * every placed spell; -1 if none.
     */
    _nearest: function(pts, hash, cell, x, y, reach, taken, placed, clear, arc) {
        var cx = Math.floor(x / cell), cy = Math.floor(y / cell), r = Math.max(1, reach);
        var best = -1, bestD = Infinity;
        for (var dx = -r; dx <= r; dx++) {
            for (var dy = -r; dy <= r; dy++) {
                var list = hash[(cx + dx) + ',' + (cy + dy)];
                if (!list) continue;
                for (var i = 0; i < list.length; i++) {
                    var idx = list[i];
                    if (taken[idx]) continue;
                    var p = pts[idx], ddx = p.x - x, ddy = p.y - y, d = ddx * ddx + ddy * ddy;
                    if (d >= bestD) continue;
                    if (arc && !this._inArc(arc, p)) continue;
                    if (placed && !this._clearOf(placed, cell, p, clear)) continue;
                    best = idx;
                    bestD = d;
                }
            }
        }
        return best;
    },

    _clearOf: function(placed, cell, p, clear) {
        var cx = Math.floor(p.x / cell), cy = Math.floor(p.y / cell), r = Math.ceil(clear / cell), c2 = clear * clear;
        for (var dx = -r; dx <= r; dx++) {
            for (var dy = -r; dy <= r; dy++) {
                var list = placed[(cx + dx) + ',' + (cy + dy)];
                if (!list) continue;
                for (var i = 0; i < list.length; i++) {
                    var ddx = list[i].x - p.x, ddy = list[i].y - p.y;
                    if (ddx * ddx + ddy * ddy < c2) return false;
                }
            }
        }
        return true;
    }
};
