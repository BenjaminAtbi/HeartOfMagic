/**
 * GridAlignTest - one grid wherever it appears (sunGridPoints.js, treeGrid.js)
 *
 * - every SUN grid type draws its dots on exactly the points the layout gets, on a canvas of
 *   any size (they were sized by the canvas: Fibonacci missed by up to 46 px between the Root
 *   Base and Tree Growth canvases, Equal Area by its own formula at any size)
 * - an applied tree's grid (TreeGrid.forApply) comes back as the same points, scaled, with the
 *   layout's added points; a tree without one gets the fallback square grid
 * - TreeGrid.snap puts a spell the declutter moved on a free point, leaves roots and spells on
 *   points alone
 *
 * Depends on: colorUtils.js, treePreviewUtils.js, sunGridPoints.js, sunGridLinear.js,
 * sunGridEqualArea.js, sunGridFibonacci.js, sunGridSquare.js, treePreviewSun.js, treeGrid.js -
 * run in a sandbox of their own with a 2d context that records the dots drawn.
 */

var GridAlignTest = {

    passed: 0,
    failed: 0,

    check: function(ok, label) {
        if (ok) { this.passed++; console.log('  [PASS] ' + label); }
        else { this.failed++; console.log('  [FAIL] ' + label); }
    },

    _sandbox: function() {
        var fs = require('fs'), path = require('path'), vm = require('vm');
        var quiet = function() {};
        var box = { console: { log: quiet, warn: quiet, error: quiet }, document: { getElementById: function() { return null; } } };
        box.window = box;
        ['colorUtils.js', 'treePreviewUtils.js', 'sunGridPoints.js', 'sunGridLinear.js', 'sunGridEqualArea.js',
         'sunGridFibonacci.js', 'sunGridSquare.js', 'treePreviewSun.js', 'treeGrid.js'].forEach(function(f) {
            vm.runInNewContext(fs.readFileSync(path.join(__dirname, f), 'utf8'), box, { filename: f });
        });
        // The grid modules register themselves when TreePreviewSun is there; it loads after them
        box.TreePreviewSun.registerGrid('linear', box.SunGridLinear);
        box.TreePreviewSun.registerGrid('equalArea', box.SunGridEqualArea);
        box.TreePreviewSun.registerGrid('fibonacci', box.SunGridFibonacci);
        box.TreePreviewSun.registerGrid('square', box.SunGridSquare);
        return box;
    },

    /** A 2d context that draws nothing and records the 3 px dots (rect) by their centre */
    _ctx: function(dots) {
        var ctx = {
            rect: function(x, y) { dots.push({ x: x + 1, y: y + 1 }); },
            measureText: function(t) { return { width: String(t).length * 7 }; }
        };
        ['beginPath', 'moveTo', 'lineTo', 'arc', 'closePath', 'fill', 'stroke', 'fillRect', 'fillText', 'save',
         'restore', 'setLineDash', 'translate', 'rotate', 'scale'].forEach(function(n) { ctx[n] = function() {}; });
        return ctx;
    },

    /** Points of `pts` with no drawn dot within 0.01 */
    _missing: function(pts, dots, w, h) {
        var seen = {};
        dots.forEach(function(d) { seen[Math.round((d.x - w / 2) * 100) + ',' + Math.round((d.y - h / 2) * 100)] = true; });
        var missing = 0;
        pts.forEach(function(p) { if (!seen[Math.round(p.x * 100) + ',' + Math.round(p.y * 100)]) missing++; });
        return missing;
    },

    run: function() {
        this.passed = 0; this.failed = 0;
        console.log('GridAlignTest');
        if (typeof require !== 'function') return { passed: 0, failed: 0 };
        var box = this._sandbox();
        var S = box.TreePreviewSun, G = box.SunGridPoints, T = box.TreeGrid;
        var schoolData = { Alteration: 120, Conjuration: 170, Destruction: 280, Illusion: 160, Restoration: 180 };
        var self = this;

        // ---- the dots drawn are the layout's points, whatever the canvas ----
        G.TYPES.forEach(function(type) {
            S.settings.gridType = type;
            var dotsA = [], dotsB = [];
            S.render(self._ctx(dotsA), 485, 504, schoolData);
            var ptsA = S._lastRenderData.gridPoints;
            S.render(self._ctx(dotsB), 485, 798, schoolData);
            var ptsB = S._lastRenderData.gridPoints;
            self.check(ptsA.length > 100 && self._missing(ptsA, dotsA, 485, 504) === 0,
                type + ': every point the layout gets is a dot drawn (' + ptsA.length + ' points)');
            self.check(ptsB.length === ptsA.length && self._missing(ptsA, dotsB, 485, 798) === 0,
                type + ': drawn on a canvas of another size, the same points');
            self.check(ptsA.every(function(p) { return typeof p.school === 'string' && p.school; }),
                type + ': every point has its school');
        });
        S.settings.gridType = 'naive';

        // ---- an applied tree's grid, back as the same points ----
        var dots = [];
        S.render(this._ctx(dots), 485, 504, schoolData);
        var base = { mode: 'sun', grid: { params: S._lastRenderData.params } };
        var layout = { schools: { Destruction: { addedPoints: [{ x: 400, y: 10 }] }, Alteration: { addedPoints: [] } } };
        var saved = T.forApply(base, layout);
        this.check(saved && saved.type === 'naive' && saved.scale === T.APPLY_SCALE && saved.extra.length === 1 &&
            saved.extra[0][0] === 540 && saved.extra[0][1] === 13.5,
            'forApply: the preview grid at the applied scale, with the layout\'s added point scaled');
        this.check(T.forApply({ mode: 'flat', grid: {} }, layout) === null, 'forApply: a flat preview saves no grid');
        var json = JSON.parse(JSON.stringify(saved));
        var back = T.positions({ grid: json });
        var pts = G.points(G.params(S.settings));
        var same = back.baked && back.points.length === pts.length + 1 && pts.every(function(p, i) {
            return Math.abs(back.points[i].x - p.x * T.APPLY_SCALE) < 1e-6 && Math.abs(back.points[i].y - p.y * T.APPLY_SCALE) < 1e-6;
        });
        this.check(same, 'positions: a saved grid is the preview\'s points at its scale, plus the added ones');

        // ---- the fallback display grid ----
        var old = T.positions({ schools: {} });
        this.check(!old.baked && old.points.length === 53 * 53 && old.spacing === 50,
            'positions: a tree without a grid gets the fallback square grid (50 apart)');
        this.check(!T.positions({ grid: { type: 'hexagon', spokes: 10 } }).baked && !T.positions(null).baked,
            'positions: an unreadable grid or no tree falls back too');

        // ---- snap ----
        var grid = back.points;
        var on = grid[40], near = grid[41], root = { formId: 'r', isRoot: true, x: 3.3, y: 7.7 };
        var a = { formId: 'a', x: on.x, y: on.y };
        var b = { formId: 'b', x: on.x + 2, y: on.y + 1 };       // pushed off the point a holds
        var c = { formId: 'c', x: near.x + 0.4, y: near.y };      // on its point (within ON_POINT)
        var tree = { grid: json, schools: { X: { nodes: [root, a, b, c] } } };
        var moved = T.snap(tree);
        var onPoint = function(n) { return grid.some(function(p) { return Math.abs(p.x - n.x) < 0.01 && Math.abs(p.y - n.y) < 0.01; }); };
        this.check(moved === 1 && onPoint(b) && !(b.x === a.x && b.y === a.y) && !(b.x === c.x && b.y === c.y),
            'snap: a spell pushed off a taken point goes to a free one');
        this.check(root.x === 3.3 && root.y === 7.7 && a.x === on.x && c.x === near.x + 0.4,
            'snap: roots and spells on their points stay where they are');
        // A spell pushed just past its school's edge, next to a point outside the school, goes to
        // a point inside it
        var angleOf = function(p) { return Math.atan2(p.y, p.x) * 180 / Math.PI; };
        var q = grid.filter(function(p) { var r = Math.hypot(p.x, p.y); return r > 200 && r < 500; })[0];
        var stray = { formId: 's', x: q.x + 1, y: q.y };
        var edge = { startAngle: angleOf(q) + 0.5, endAngle: angleOf(q) + 90.5, nodes: [stray] };
        T.snap({ grid: json, globe: { x: 0, y: 0 }, schools: { Y: edge } });
        var into = ((angleOf(stray) - edge.startAngle) % 360 + 360) % 360;
        this.check(onPoint(stray) && into <= 90, 'snap: a spell goes to a point inside its school\'s segment, not the ' +
            'nearer one outside');
        var noGrid = { schools:{ X: { nodes: [{ formId: 'z', x: 1.234, y: 5.678 }] } } };
        this.check(T.snap(noGrid) === 0 && noGrid.schools.X.nodes[0].x === 1.234, 'snap: a tree without a grid is left alone');

        return { passed: this.passed, failed: this.failed };
    }
};

if (typeof window !== 'undefined') window.GridAlignTest = GridAlignTest;
if (typeof global !== 'undefined') global.GridAlignTest = GridAlignTest;
