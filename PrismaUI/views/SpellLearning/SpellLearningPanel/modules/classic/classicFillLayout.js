/**
 * ClassicFillLayout - slot-first layout for Classic Growth (experimental: settings.fillLayout)
 *
 * ClassicLayout's own layout walks the tree: each spell looks for a free point near its parent,
 * up to 12 hops away through taken points, which draws long lines across the tree. This one
 * walks the grid instead:
 *   1. The free slots are the grid points next to a placed spell (one hop from it).
 *   2. Slots are filled inside out: the one nearest the centre first.
 *   3. For a slot, every unplaced spell that could hang off a placed neighbour is scored as
 *      that pair (spell, parent), and the best pair fills it. Every line is then one hop.
 *      The score: the C++ builder's parent (a bonus; other parents are allowed), the slot's
 *      tier zone and theme sector for the spell, the parent's theme, the tier step, the
 *      parent's children and the parent's depth against the slot's ring.
 *   4. A slot whose best pair scores below MIN_SCORE stays empty for now; a spell placed next
 *      to it later offers it again. When no slot is left, a relaxed round fills the empty
 *      ones with any pair, and spells still left go to the nearest free point (force-placed).
 * The tier never steps down along a line. Spells stay in their root group (several physical
 * roots: ClassicLayout._buildGroups).
 *
 * Usage (from ClassicLayout._layoutOnGrid, once the grid graph, groups and spells are ready):
 *   return ClassicFillLayout.run(ClassicLayout, ctx);   // the placed nodes, as _layoutOnGrid's
 *
 * Depends on: ClassicLayout (tier zones, theme sectors, ring radius, _snapToNearest, _isOutward)
 */
var ClassicFillLayout = {

    TIERS: { Novice: 0, Apprentice: 1, Adept: 2, Expert: 3, Master: 4 },
    MAX_CHILDREN: 5,

    W_CPP_PARENT: 8,    // the parent is the C++ builder's
    W_CPP_WAIT: 4,      // the C++ parent is not placed yet: better to wait for it
    W_THEME: 3,         // the parent has the spell's theme
    W_TIER_JUMP: 2,     // per tier the spell skips past the parent's next one
    W_CHILD: 1,         // per child the parent already has
    W_DEPTH: 0.5,       // per ring the parent's depth + 1 is off the slot's ring
    MIN_SCORE: -6,      // a slot whose best pair scores below stays empty (until the relaxed round)
    ZONE_SLACK: 5,      // a spell goes no further in than its tier zone's start less this (%), until the relaxed round

    /**
     * @param {Object} L - ClassicLayout (its per-school state: _tierZones, _ringRadius, ...)
     * @param {Object} ctx - { gridGraph: {points, adj}, nodeLookup, rootFormId, groups, spellMap,
     *                         tierSpacing, ringRadius, isFlat, centerMaskR2 }
     * @returns {Array} positioned nodes { formId, x, y, parentFormId, tier, skillLevel, theme,
     *                  name, isRoot, gridIdx }
     */
    run: function (L, ctx) {
        var self = this;
        var pts = ctx.gridGraph.points, adj = ctx.gridGraph.adj;
        var nodes = ctx.nodeLookup, spellMap = ctx.spellMap || {};
        var gx = L._globeX || 0, gy = L._globeY || 0;

        var level = function (id) { var s = spellMap[id]; return s ? s.skillLevel : ''; };
        var tierOf = function (id) { var t = self.TIERS[level(id)]; return t === undefined ? 0 : t; };
        var themeOf = function (id) { return nodes[id] ? (nodes[id].theme || '') : ''; };

        // ---- each spell's C++ parent and root group ----
        var cppParent = {}, groupOf = {}, seedRoot = [];
        var all = [];
        for (var gi = 0; gi < ctx.groups.length; gi++) {
            var seeds = ctx.groups[gi].seedIds;
            if (seeds.length === 0) { seedRoot.push(null); continue; }
            seedRoot.push(seeds[0]);
            var stack = [];
            for (var si = 0; si < seeds.length; si++) {
                if (si > 0) cppParent[seeds[si]] = seeds[0];   // the group's other seeds hang off its first
                stack.push(seeds[si]);
            }
            while (stack.length) {
                var id = stack.pop();
                if (groupOf[id] !== undefined || !nodes[id]) continue;
                groupOf[id] = gi;
                all.push(id);
                var ch = nodes[id].children || [];
                for (var ci = 0; ci < ch.length; ci++) {
                    if (cppParent[ch[ci]] === undefined && ch[ci] !== ctx.rootFormId) cppParent[ch[ci]] = id;
                    stack.push(ch[ci]);
                }
            }
        }
        all.sort();

        // A group growing outward places nothing inside its ring (as ClassicLayout's phase 1)
        var minR2 = ctx.groups.map(function (g) {
            var m = 0;
            if (!ctx.isFlat && g.physicalRoot && L._isOutward(g.physicalRoot)) {
                var r = ctx.ringRadius - ctx.tierSpacing * 0.5;
                m = r * r;
            }
            return Math.max(m, ctx.centerMaskR2 || 0);
        });
        var globeR2 = function (idx) { var dx = pts[idx].x - gx, dy = pts[idx].y - gy; return dx * dx + dy * dy; };

        // ---- state ----
        var owner = {}, placed = {}, depth = {}, kids = {};
        var positioned = [], remaining = all.length;
        var heap = new this._Heap(), inHeap = {}, rejected = {};
        var radius = function (idx) { return Math.sqrt(pts[idx].x * pts[idx].x + pts[idx].y * pts[idx].y); };
        var offer = function (idx) {
            var n = adj[idx] || [];
            for (var i = 0; i < n.length; i++) {
                if (owner[n[i]] === undefined && !inHeap[n[i]]) { inHeap[n[i]] = true; heap.push(n[i], radius(n[i])); }
            }
        };
        var place = function (id, idx, parent) {
            owner[idx] = id;
            placed[id] = true;
            depth[id] = parent ? depth[parent] + 1 : 0;
            if (parent) kids[parent] = (kids[parent] || 0) + 1;
            remaining--;
            var s = spellMap[id];
            positioned.push({
                formId: id, x: pts[idx].x, y: pts[idx].y, parentFormId: parent || null,
                tier: nodes[id] ? (nodes[id].tier || 0) : 0,
                skillLevel: s ? s.skillLevel : '', theme: themeOf(id),
                name: s ? s.name : id, isRoot: !parent, gridIdx: idx
            });
            offer(idx);
        };

        // ---- 1. the roots ----
        for (var g = 0; g < ctx.groups.length; g++) {
            if (!seedRoot[g] || placed[seedRoot[g]]) continue;
            var pr = ctx.groups[g].physicalRoot;
            var at = L._snapToNearest(pts, pr.x, pr.y);
            if (at < 0 || owner[at] !== undefined) continue;
            place(seedRoot[g], at, null);
        }

        // ---- 2-4. the slots, inside out, a tier at a time: a tier's spells (and any left of a lower one)
        // fill the free slots, nearest the centre first, before the next tier may; so every tier finds
        // parents of its own tier or lower round the tree's edge ----
        var stats = { cpp: 0, relaxed: 0, forced: 0, rejectedSlots: 0 };
        var fill = function (maxTier, relaxed) {
            while (heap.size() > 0 && remaining > 0) {
                var idx = heap.pop();
                inHeap[idx] = false;
                if (owner[idx] !== undefined) continue;
                var best = self._bestPair(idx, relaxed, {
                    L: L, pts: pts, adj: adj, all: all, owner: owner, placed: placed, depth: depth, kids: kids,
                    cppParent: cppParent, groupOf: groupOf, minR2: minR2, r2: globeR2(idx), radius: radius(idx),
                    tierOf: tierOf, themeOf: themeOf, level: level, ctx: ctx, maxTier: maxTier
                });
                if (!best) { rejected[idx] = true; continue; }
                delete rejected[idx];
                if (cppParent[best.id] === best.parent) stats.cpp++;
                if (relaxed) stats.relaxed++;
                place(best.id, idx, best.parent);
            }
        };
        // The free slots next to every placed spell, offered again
        var reoffer = function () {
            heap = new self._Heap();
            inHeap = {};
            Object.keys(owner).map(Number).sort(function (a, b) { return a - b; }).forEach(offer);
        };
        var leftUpTo = function (t) {
            for (var i = 0; i < all.length; i++) if (!placed[all[i]] && tierOf(all[i]) <= t) return true;
            return false;
        };
        for (var tier = 0; tier <= 4 && remaining > 0; tier++) {
            reoffer();
            fill(tier, false);
            if (leftUpTo(tier)) {
                stats.rejectedSlots += Object.keys(rejected).length;
                reoffer();
                fill(tier, true);
            }
            rejected = {};
        }
        if (remaining > 0 && this.DEBUG) {
            var left = {};
            all.forEach(function (id) { if (!placed[id]) left[level(id)] = (left[level(id)] || 0) + 1; });
            console.log('[ClassicFillLayout] stalled: left ' + JSON.stringify(left) + ', placed ' + positioned.length);
        }
        if (remaining > 0) stats.forced = this._force(all, placed, owner, pts, cppParent, groupOf, seedRoot, minR2, globeR2, place);

        console.log('[ClassicFillLayout] ' + positioned.length + ' of ' + all.length + ' placed: ' + stats.cpp +
            ' on their C++ parent, ' + stats.relaxed + ' in the relaxed round, ' + stats.forced + ' force-placed (' +
            stats.rejectedSlots + ' slots left empty at first)');
        return positioned;
    },

    /** The best (spell, parent) pair for slot `idx`, or null (none, or below MIN_SCORE unless relaxed) */
    _bestPair: function (idx, relaxed, s) {
        var parents = [];
        var n = s.adj[idx] || [];
        for (var i = 0; i < n.length; i++) {
            var p = s.owner[n[i]];
            if (p !== undefined && (s.kids[p] || 0) < this.MAX_CHILDREN) parents.push(p);
        }
        if (parents.length === 0) return null;
        var L = s.L, pt = s.pts[idx];
        var ring = Math.max(0, (s.radius - (L._ringRadius || 0)) / (s.ctx.tierSpacing || 30));
        var best = null, bestScore = relaxed ? -Infinity : this.MIN_SCORE;
        var zoneCache = {}, themeCache = {};
        for (var a = 0; a < s.all.length; a++) {
            var id = s.all[a];
            if (s.placed[id]) continue;
            var grp = s.groupOf[id];
            if (s.r2 < s.minR2[grp]) continue;
            var lv = s.level(id), th = s.themeOf(id), ti = s.tierOf(id);
            if (ti > s.maxTier) continue;
            if (!relaxed && this._belowZone(L, lv, s.radius)) continue;
            if (zoneCache[lv] === undefined) zoneCache[lv] = this._zoneScore(L, lv, s.radius);
            if (themeCache[th] === undefined) themeCache[th] = this._themeScore(L, th, pt);
            var base = zoneCache[lv] + themeCache[th];
            var cpp = s.cppParent[id];
            if (cpp && !s.placed[cpp]) base -= this.W_CPP_WAIT;
            for (var q = 0; q < parents.length; q++) {
                var p = parents[q];
                if (s.groupOf[p] !== grp) continue;
                var tp = s.tierOf(p);
                if (ti < tp) continue;   // the tier never steps down along a line
                var score = base;
                if (p === cpp) score += this.W_CPP_PARENT;
                if (th && th === s.themeOf(p)) score += this.W_THEME;
                score -= this.W_TIER_JUMP * Math.max(0, ti - tp - 1);
                score -= this.W_CHILD * (s.kids[p] || 0);
                score -= this.W_DEPTH * Math.abs(s.depth[p] + 1 - ring);
                if (score > bestScore || (best && score === bestScore && (id < best.id || (id === best.id && p < best.parent)))) {
                    bestScore = score;
                    best = { id: id, parent: p, score: score };
                }
            }
        }
        return best;
    },

    /** Radius r as a percentage of the school's growth range (ClassicLayout's tier zones) */
    _pct: function (L, r) {
        var ringR = L._ringRadius || 0;
        var growR = (L._simulatedMaxRadius || L._maxGridRadius || 1) - ringR;
        var pct = growR > 1 ? ((r - ringR) / growR) * 100 : 50;
        return Math.max(0, Math.min(100, pct));
    },

    /** Radius r is further in than the start of the tier zone of `lv`, less ZONE_SLACK */
    _belowZone: function (L, lv, r) {
        var zone = L._tierZones && lv ? L._tierZones[lv] : null;
        return !!zone && this._pct(L, r) < zone.min - this.ZONE_SLACK;
    },

    /** The tier zone fit of radius r for a spell of `lv` (ClassicLayout._findSlots' scoring) */
    _zoneScore: function (L, lv, r) {
        var zone = L._tierZones && lv ? L._tierZones[lv] : null;
        if (!zone) return 0;
        var pct = this._pct(L, r);
        if (pct < zone.min) return -5 - (zone.min - pct) / 10;
        if (pct > zone.max) return -5 - (pct - zone.max) / 10;
        var half = (zone.max - zone.min) / 2;
        return half > 0 ? 3 * (1 - Math.abs(pct - (zone.min + zone.max) / 2) / half) : 3;
    },

    /** The theme sector fit of point p (ClassicLayout._findSlots' scoring) */
    _themeScore: function (L, theme, p) {
        var sector = L._currentThemeSectors && theme ? L._currentThemeSectors[theme] : null;
        if (!sector) return 0;
        var d = Math.atan2(p.y, p.x) - sector.center;
        while (d > Math.PI) d -= 2 * Math.PI;
        while (d < -Math.PI) d += 2 * Math.PI;
        return 2.5 - Math.abs(d) * (3.5 / Math.PI);
    },

    /** Spells no slot took: the nearest free point to their C++ parent (or group root) */
    _force: function (all, placed, owner, pts, cppParent, groupOf, seedRoot, minR2, globeR2, place) {
        var byId = {}, count = 0;
        Object.keys(owner).forEach(function (k) { byId[owner[k]] = +k; });
        for (var a = 0; a < all.length; a++) {
            var id = all[a];
            if (placed[id]) continue;
            var parent = placed[cppParent[id]] ? cppParent[id] : seedRoot[groupOf[id]];
            if (!parent || !placed[parent]) continue;
            var from = pts[byId[parent]], best = -1, bestD = Infinity;
            for (var i = 0; i < pts.length; i++) {
                if (owner[i] !== undefined || globeR2(i) < minR2[groupOf[id]]) continue;
                var dx = pts[i].x - from.x, dy = pts[i].y - from.y, d = dx * dx + dy * dy;
                if (d < bestD) { bestD = d; best = i; }
            }
            if (best < 0) break;
            place(id, best, parent);
            byId[id] = best;
            count++;
        }
        return count;
    },

    /** A min-heap of grid indices by key (ties: the lower index) */
    _Heap: function () {
        var a = [];
        var less = function (i, j) { return a[i].k < a[j].k || (a[i].k === a[j].k && a[i].v < a[j].v); };
        var swap = function (i, j) { var t = a[i]; a[i] = a[j]; a[j] = t; };
        this.size = function () { return a.length; };
        this.push = function (v, k) {
            a.push({ v: v, k: k });
            for (var i = a.length - 1; i > 0;) {
                var p = (i - 1) >> 1;
                if (!less(i, p)) break;
                swap(i, p); i = p;
            }
        };
        this.pop = function () {
            var top = a[0].v, last = a.pop();
            if (a.length > 0) {
                a[0] = last;
                for (var i = 0; ;) {
                    var l = 2 * i + 1, r = l + 1, m = i;
                    if (l < a.length && less(l, m)) m = l;
                    if (r < a.length && less(r, m)) m = r;
                    if (m === i) break;
                    swap(i, m); i = m;
                }
            }
            return top;
        };
    }
};

if (typeof window !== 'undefined') window.ClassicFillLayout = ClassicFillLayout;
