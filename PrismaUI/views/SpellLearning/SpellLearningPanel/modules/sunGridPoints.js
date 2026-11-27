/**
 * SunGridPoints - the points of a SUN grid, from one set of parameters
 *
 * Every place a SUN grid appears takes its points from here: the dots the Root Base and Tree
 * Growth previews draw, the points ClassicLayout places spells on, and the grid the tree view's
 * edit mode shows (from the parameters a tree was applied with, its `grid`, TreeGrid). Before
 * (2026-11-26) each grid module drew its own dots and the preview collected the layout's
 * separately, both sized by the canvas (3 x its diagonal): a Fibonacci grid drawn on the Tree
 * Growth canvas missed the points the layout had used on the Root Base canvas by up to 46 px,
 * and Equal Area's drawing and collection used different formulas outright.
 *
 * Parameters (params(): all a grid depends on):
 *   type        'naive' | 'linear' | 'equalArea' | 'fibonacci' | 'square'
 *   spokes      angular divisions (the preview's Spoke Density)
 *   tiers       the preview's Tier Density; tierSpacing = min(40, 250 / tiers)
 *   ringTier    the ring the school roots sit on; ringRadius = ringTier x tierSpacing
 *   extent      how far out the grid reaches (EXTENT, whatever the canvas)
 *   maxDots     the point cap (MAX_DOTS_DENSE for fibonacci and equalArea)
 *   scale       every coordinate times this (1 in the previews; an applied tree's grid has the
 *               spread it was saved with, ClassicTreeOutput)
 *
 * Usage:
 *   var p = SunGridPoints.params(TreePreviewSun.settings);
 *   var pts = SunGridPoints.points(p);  // [{ x, y, _ptIdx, _moveDir, _neighbors }], cached: read only
 *   SunGridPoints.drawDots(ctx, cx, cy, pts, colorFn);
 */

var SunGridPoints = {

    /** World units a grid reaches out to (60 rings at the default spacing of 40) */
    EXTENT: 2400,
    MAX_DOTS: 20000,
    /** Fibonacci and Equal Area: irregular grids, their neighbours a K-nearest search */
    MAX_DOTS_DENSE: 5000,
    TYPES: ['naive', 'linear', 'equalArea', 'fibonacci', 'square'],
    CACHE_SIZE: 6,

    _cache: {},
    _cacheOrder: [],

    /** The ring spacing of a Tier Density */
    tierSpacing: function(tiers) {
        return Math.min(40, 250 / Math.max(1, tiers || 5));
    },

    /**
     * The parameters of the grid the SUN preview's settings describe.
     * @param {Object} s - TreePreviewSun.settings (gridType, gridDensity, tierDensity, ringTier)
     * @param {number} [scale] - default 1
     */
    params: function(s, scale) {
        var type = this.TYPES.indexOf(s.gridType) >= 0 ? s.gridType : 'naive';
        var tiers = s.tierDensity || 5, ringTier = s.ringTier || 3;
        var tierSpacing = this.tierSpacing(tiers);
        return {
            type: type,
            spokes: s.gridDensity || 10,
            tiers: tiers,
            tierSpacing: tierSpacing,
            ringTier: ringTier,
            ringRadius: ringTier * tierSpacing,
            extent: this.EXTENT,
            maxDots: (type === 'fibonacci' || type === 'equalArea') ? this.MAX_DOTS_DENSE : this.MAX_DOTS,
            scale: (typeof scale === 'number' && isFinite(scale) && scale > 0) ? scale : 1
        };
    },

    /** What a tree saves of a grid (output.grid): the parameters, nothing derived */
    toSaved: function(p) {
        return { type: p.type, spokes: p.spokes, tiers: p.tiers, tierSpacing: p.tierSpacing, ringTier: p.ringTier,
            extent: p.extent, maxDots: p.maxDots, scale: p.scale };
    },

    /**
     * A saved tree's grid (toSaved) as parameters, checked; null when it is none (a tree saved
     * before 2026-11-26, a flat one): the caller falls back on a grid of its own.
     */
    fromSaved: function(g) {
        if (!g || typeof g !== 'object' || this.TYPES.indexOf(g.type) < 0) return null;
        var ok = function(v) { return typeof v === 'number' && isFinite(v) && v > 0; };
        if (!ok(g.spokes) || !ok(g.tierSpacing) || !ok(g.extent) || !ok(g.maxDots)) return null;
        var ringTier = ok(g.ringTier) ? g.ringTier : 3;
        return {
            type: g.type,
            spokes: Math.min(Math.round(g.spokes), 360),
            tiers: ok(g.tiers) ? g.tiers : 5,
            tierSpacing: Math.max(g.tierSpacing, 1),
            ringTier: ringTier,
            ringRadius: ringTier * Math.max(g.tierSpacing, 1),
            extent: Math.min(g.extent, 1e5),
            maxDots: Math.min(g.maxDots, this.MAX_DOTS),
            scale: ok(g.scale) ? g.scale : 1
        };
    },

    /** Everything the points depend on */
    key: function(p) {
        return [p.type, p.spokes, p.tierSpacing, p.extent, p.maxDots, p.scale].join('|');
    },

    /** The grid's points (cached and shared: read only) */
    points: function(p) {
        var key = this.key(p);
        if (this._cache[key]) return this._cache[key];
        var pts;
        if (p.scale !== 1) {
            var s = p.scale;
            pts = this.points(this._atScale(p, 1)).map(function(b) {
                return { x: b.x * s, y: b.y * s, _ptIdx: b._ptIdx, _moveDir: b._moveDir, _neighbors: b._neighbors };
            });
        } else {
            pts = this._generate(p.type, p);
        }
        this._cache[key] = pts;
        this._cacheOrder.push(key);
        while (this._cacheOrder.length > this.CACHE_SIZE) delete this._cache[this._cacheOrder.shift()];
        return pts;
    },

    _atScale: function(p, scale) {
        var q = {};
        for (var k in p) if (p.hasOwnProperty(k)) q[k] = p[k];
        q.scale = scale;
        return q;
    },

    /**
     * The rings an Equal Area grid's points sit on (scale 1), for the circles the grid module
     * draws: the same numbers the points come from.
     */
    equalAreaRings: function(p) {
        var linearDensity = Math.ceil(p.extent / p.tierSpacing);
        var total = Math.min(500, linearDensity * 4);
        return {
            total: total,
            every: Math.max(1, Math.round(total / linearDensity)),
            radius: function(k) { return Math.sqrt(k / total) * p.extent; }
        };
    },

    /**
     * Draw points as the previews' 3 px dots, one path per colour.
     * @param {Function|null} colorFn - angle (radians) -> colour; none: the plain dot colour
     */
    drawDots: function(ctx, cx, cy, pts, colorFn) {
        var buckets = {}, plain = 'rgba(184, 168, 120, 0.15)';
        for (var i = 0; i < pts.length; i++) {
            var color = colorFn ? colorFn(Math.atan2(pts[i].y, pts[i].x)) : plain;
            (buckets[color] || (buckets[color] = [])).push(pts[i]);
        }
        for (var c in buckets) {
            var list = buckets[c];
            ctx.fillStyle = c;
            ctx.beginPath();
            for (var j = 0; j < list.length; j++) ctx.rect(cx + list[j].x - 1, cy + list[j].y - 1, 3, 3);
            ctx.fill();
        }
    },

    /** One grid type's points at scale 1, with their neighbours (moved from TreePreviewSun) */
    _generate: function(gridType, opts) {
        var points = [];
        var tierSpacing = opts.tierSpacing;
        var spokes = opts.spokes;
        var maxExtent = opts.extent;
        var maxDots = opts.maxDots || this.MAX_DOTS;
        var PI2 = Math.PI * 2;

        if (gridType === 'equalArea') {
            var maxRadius = maxExtent;
            var linearDensity = Math.ceil(maxExtent / tierSpacing);
            var totalRings = Math.min(500, linearDensity * 4);
            var ring1R = Math.sqrt(1 / totalRings) * maxRadius;
            var eaBs = (PI2 * ring1R) / Math.max(spokes, 4);
            var ga = Math.PI * (3 - Math.sqrt(5));
            var eaBase = points.length;
            for (var k = 1; k <= totalRings && points.length < maxDots; k++) {
                var eaR = Math.sqrt(k / totalRings) * maxRadius;
                var eaC = PI2 * eaR;
                var eaPc = Math.max(spokes, Math.round(eaC / eaBs));
                var eaAs = PI2 / eaPc;
                var rOff = k * ga;
                for (var ep = 0; ep < eaPc && points.length < maxDots; ep++) {
                    var eaA = ep * eaAs + rOff;
                    points.push({
                        x: Math.cos(eaA) * eaR, y: Math.sin(eaA) * eaR,
                        _ptIdx: points.length, _moveDir: eaA, _neighbors: []
                    });
                }
            }
            // KNN neighbors for equal area (irregular structure)
            SunGridPoints._addKNNNeighbors(points, eaBase, 8);
        } else if (gridType === 'fibonacci') {
            var fibGa = Math.PI * (3 - Math.sqrt(5));
            var fibR = maxExtent;
            var fibTiers = Math.ceil(maxExtent / tierSpacing);
            var fibTotal = Math.min(Math.round(0.5 * spokes * fibTiers * fibTiers), maxDots);
            var fibBase = points.length;
            for (var fi = 1; fi <= fibTotal; fi++) {
                var fr = fibR * Math.sqrt(fi / fibTotal);
                var fa = fi * fibGa;
                points.push({
                    x: Math.cos(fa) * fr, y: Math.sin(fa) * fr,
                    _ptIdx: points.length, _moveDir: fa, _neighbors: []
                });
            }
            // KNN neighbors for fibonacci (spiral, no regular grid)
            SunGridPoints._addKNNNeighbors(points, fibBase, 8);
        } else if (gridType === 'square') {
            // Square grid with 8-connected neighbor pre-computation
            var sqHc = Math.ceil(maxExtent / tierSpacing);
            var sqIdxMap = {}; // "gx,gy" → index
            for (var gx = -sqHc; gx <= sqHc && points.length < maxDots; gx++) {
                for (var gy = -sqHc; gy <= sqHc && points.length < maxDots; gy++) {
                    if (gx === 0 && gy === 0) continue;
                    var sqDx = gx * tierSpacing;
                    var sqDy = gy * tierSpacing;
                    var sqA = Math.atan2(sqDy, sqDx);
                    if (sqA < 0) sqA += PI2;
                    sqIdxMap[gx + ',' + gy] = points.length;
                    points.push({
                        x: sqDx, y: sqDy,
                        _ptIdx: points.length, _gx: gx, _gy: gy,
                        _moveDir: sqA, _neighbors: []
                    });
                }
            }
            // 8-connected neighbors
            var sqDirs = [[-1,-1],[-1,0],[-1,1],[0,-1],[0,1],[1,-1],[1,0],[1,1]];
            for (var sqi = 0; sqi < points.length; sqi++) {
                var sqPt = points[sqi];
                if (sqPt._gx === undefined) continue;
                for (var sd = 0; sd < sqDirs.length; sd++) {
                    var sqNk = (sqPt._gx + sqDirs[sd][0]) + ',' + (sqPt._gy + sqDirs[sd][1]);
                    if (sqIdxMap[sqNk] !== undefined) {
                        sqPt._neighbors.push(sqIdxMap[sqNk]);
                    }
                }
            }
        } else if (gridType === 'naive') {
            // Naive grid with 8-connected ring/spoke neighbor pre-computation
            var naiveDotsPerRing = spokes * 3;
            var naiveAngleStep = PI2 / naiveDotsPerRing;
            var naiveTotalTiers = Math.ceil(maxExtent / tierSpacing);
            var naiveDotTiers = Math.min(naiveTotalTiers, Math.floor(maxDots / Math.max(1, naiveDotsPerRing)));
            var naiveBase = points.length; // offset for index calculation
            for (var nr = 1; nr <= naiveDotTiers && points.length < maxDots; nr++) {
                var naiveR = nr * tierSpacing;
                for (var ng = 0; ng < naiveDotsPerRing && points.length < maxDots; ng++) {
                    var naiveA = ng * naiveAngleStep;
                    points.push({
                        x: Math.cos(naiveA) * naiveR,
                        y: Math.sin(naiveA) * naiveR,
                        _ptIdx: points.length,
                        _moveDir: naiveA, // radially outward
                        _neighbors: []
                    });
                }
            }
            // Build 8-connected neighbors: ring ±1, spoke ±1
            var dpr = naiveDotsPerRing;
            for (var npi = naiveBase; npi < points.length; npi++) {
                var npt = points[npi];
                var ri = Math.floor((npi - naiveBase) / dpr) + 1; // 1-indexed ring
                var si = (npi - naiveBase) % dpr;
                var prevS = (si - 1 + dpr) % dpr;
                var nextS = (si + 1) % dpr;
                // Same ring: prev/next spoke
                npt._neighbors.push(naiveBase + (ri - 1) * dpr + prevS);
                npt._neighbors.push(naiveBase + (ri - 1) * dpr + nextS);
                // Inner ring (ring - 1)
                if (ri > 1) {
                    npt._neighbors.push(naiveBase + (ri - 2) * dpr + si);
                    npt._neighbors.push(naiveBase + (ri - 2) * dpr + prevS);
                    npt._neighbors.push(naiveBase + (ri - 2) * dpr + nextS);
                }
                // Outer ring (ring + 1)
                if (ri < naiveDotTiers) {
                    npt._neighbors.push(naiveBase + ri * dpr + si);
                    npt._neighbors.push(naiveBase + ri * dpr + prevS);
                    npt._neighbors.push(naiveBase + ri * dpr + nextS);
                }
            }
        } else {
            // Linear (default) — variable dots per ring, with neighbor pre-computation
            var baseSpacing = (PI2 * tierSpacing) / spokes;
            var totalTiers = Math.ceil(maxExtent / tierSpacing);
            var linBase = points.length;
            var linRingStart = []; // ringIndex → start offset in points
            var linRingCount = []; // ringIndex → point count in that ring
            for (var lr = 1; lr <= totalTiers && points.length < maxDots; lr++) {
                var lrR = lr * tierSpacing;
                var lrC = PI2 * lrR;
                var lrPc = Math.max(spokes, Math.round(lrC / baseSpacing));
                var lrAs = PI2 / lrPc;
                linRingStart.push(points.length - linBase);
                linRingCount.push(lrPc);
                for (var lp = 0; lp < lrPc && points.length < maxDots; lp++) {
                    var lrA = lp * lrAs;
                    points.push({
                        x: Math.cos(lrA) * lrR,
                        y: Math.sin(lrA) * lrR,
                        _ptIdx: points.length,
                        _moveDir: lrA,
                        _ring: lr - 1, _spoke: lp,
                        _neighbors: []
                    });
                }
            }
            // Neighbors for linear: same ring ±1 spoke, adjacent rings nearest angle
            for (var lni = linBase; lni < points.length; lni++) {
                var lpt = points[lni];
                var lri = lpt._ring;
                var lsi = lpt._spoke;
                var lrc = linRingCount[lri];
                // Same ring neighbors
                lpt._neighbors.push(linBase + linRingStart[lri] + ((lsi - 1 + lrc) % lrc));
                lpt._neighbors.push(linBase + linRingStart[lri] + ((lsi + 1) % lrc));
                // Inner ring: find closest angular match
                if (lri > 0) {
                    var irc = linRingCount[lri - 1];
                    var irBase = linBase + linRingStart[lri - 1];
                    var irMap = Math.round(lsi * irc / lrc);
                    if (irMap >= irc) irMap = irc - 1;
                    lpt._neighbors.push(irBase + irMap);
                    if (irMap > 0) lpt._neighbors.push(irBase + irMap - 1);
                    if (irMap < irc - 1) lpt._neighbors.push(irBase + irMap + 1);
                }
                // Outer ring
                if (lri < linRingCount.length - 1) {
                    var orc = linRingCount[lri + 1];
                    var orBase = linBase + linRingStart[lri + 1];
                    var orMap = Math.round(lsi * orc / lrc);
                    if (orMap >= orc) orMap = orc - 1;
                    lpt._neighbors.push(orBase + orMap);
                    if (orMap > 0) lpt._neighbors.push(orBase + orMap - 1);
                    if (orMap < orc - 1) lpt._neighbors.push(orBase + orMap + 1);
                }
            }
        }


        return points;
    },

    /**
     * Add K-nearest-neighbor connections to points starting at startIdx.
     * Used for irregular grids (fibonacci, equal area) where structural
     * neighbors can't be pre-computed from ring/spoke indices.
     */
    _addKNNNeighbors: function(points, startIdx, K) {
        var count = points.length - startIdx;
        if (count < 2) return;

        // Bounding box for spatial hash
        var minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
        for (var bi = startIdx; bi < points.length; bi++) {
            if (points[bi].x < minX) minX = points[bi].x;
            if (points[bi].x > maxX) maxX = points[bi].x;
            if (points[bi].y < minY) minY = points[bi].y;
            if (points[bi].y > maxY) maxY = points[bi].y;
        }

        // Cell size: ~4 pts per cell on average → 5×5 search gives ~100 candidates
        var area = Math.max(1, (maxX - minX) * (maxY - minY));
        var cellSize = Math.max(1, Math.sqrt(area / count) * 2);

        // Build spatial hash buckets
        var buckets = {};
        for (var gi = startIdx; gi < points.length; gi++) {
            var gcx = Math.floor((points[gi].x - minX) / cellSize);
            var gcy = Math.floor((points[gi].y - minY) / cellSize);
            var gkey = gcx + '_' + gcy;
            if (!buckets[gkey]) buckets[gkey] = [];
            buckets[gkey].push(gi);
        }

        // For each point, find K nearest from nearby cells (5×5 neighborhood)
        var searchRing = 2;
        for (var i = startIdx; i < points.length; i++) {
            var pi = points[i];
            var pcx = Math.floor((pi.x - minX) / cellSize);
            var pcy = Math.floor((pi.y - minY) / cellSize);

            var dists = [];
            for (var dx = -searchRing; dx <= searchRing; dx++) {
                for (var dy = -searchRing; dy <= searchRing; dy++) {
                    var nkey = (pcx + dx) + '_' + (pcy + dy);
                    var bucket = buckets[nkey];
                    if (!bucket) continue;
                    for (var ci = 0; ci < bucket.length; ci++) {
                        var j = bucket[ci];
                        if (i === j) continue;
                        var ddx = pi.x - points[j].x;
                        var ddy = pi.y - points[j].y;
                        dists.push({ idx: j, d: ddx * ddx + ddy * ddy });
                    }
                }
            }

            dists.sort(function(a, b) { return a.d - b.d; });
            var nCount = Math.min(K, dists.length);
            for (var k = 0; k < nCount; k++) {
                pi._neighbors.push(dists[k].idx);
            }
        }
    }
};
