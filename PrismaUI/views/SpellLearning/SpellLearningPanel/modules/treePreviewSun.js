/**
 * Tree Preview — SUN Mode
 *
 * Root base settings and renderer for the SUN (radial/wheel) layout.
 * Grid rendering is delegated to pluggable grid modules (Naive, Linear,
 * Equal Area, Fibonacci) that register via registerGrid().
 * The root ring sits on a specific tier. School root nodes are placed
 * centered within their arc sections.
 *
 * Self-registers with TreePreview via registerMode().
 *
 * Depends on: treePreviewUtils.js (drag inputs, label place), treePreview.js (registers into it),
 *             schoolText (uiHelpers.js, optional: the school names in the panel's language)
 * Grid modules: sunGridLinear.js, sunGridEqualArea.js, sunGridFibonacci.js
 */

var TreePreviewSun = {

    LABEL_FONT: '12px sans-serif',   // the school names (the panel's 12px readable floor)
    LABEL_HEIGHT: 12,                // css px, for the fit
    LABEL_GAP: 6,                    // css px between an arrow's tip and its school's name
    ARROW_REACH: 18,                 // css px an outward arrow reaches past its root (_drawArrow: 2 + 16)

    settings: {
        ringTier: 3,
        nodeSize: 8,
        rootsPerSchool: 1,
        gridDensity: 10,
        tierDensity: 5,
        rootClumping: 0,
        rootRandomness: 0,
        gridType: 'naive',
        proportional: false,
        invertGrowth: false
    },

    // Pluggable grid renderers
    _grids: {},
    _gridOrder: ['naive', 'linear', 'equalArea', 'fibonacci', 'square'],

    // Grid point cache (avoids recomputing KNN every frame)
    _gridPointCache: null,
    _gridPointCacheKey: '',

    // Offscreen canvas cache (avoids redrawing thousands of dots every frame)
    _gridCanvas: null,
    _gridCanvasKey: '',
    _gridCanvasW: 0,
    _gridCanvasH: 0,

    registerGrid: function(name, module) {
        this._grids[name] = module;
        console.log('[TreePreviewSun] Registered grid: ' + name);
    },

    // School colors matching CSS variables
    _schoolColors: {
        'Destruction': '#C85050',
        'Restoration': '#C8B850',
        'Alteration': '#50A868',
        'Conjuration': '#9068C8',
        'Illusion': '#5098C8'
    },

    /**
     * Build the settings panel HTML for SUN mode
     */
    buildSettingsHTML: function() {
        var s = this.settings;
        var H = TreePreviewUtils.settingHTML;
        return '' +
            '<div class="tree-preview-settings-title">' + t('preview.sun.title') + '</div>' +

            // --- Toggles at top ---
            '<div class="tree-preview-setting">' +
                '<label class="tree-preview-label">' + t('preview.sun.gridType') + '</label>' +
                '<div class="tree-preview-toggle-row tree-preview-toggle-wrap">' +
                    '<button class="tree-preview-toggle-btn' + (s.gridType === 'naive' ? ' active' : '') + '" ' +
                        'id="tpSunGridNaive" data-grid="naive">' + t('preview.sun.naive') + '</button>' +
                    '<button class="tree-preview-toggle-btn' + (s.gridType === 'linear' ? ' active' : '') + '" ' +
                        'id="tpSunGridLinear" data-grid="linear">' + t('preview.sun.linear') + '</button>' +
                    '<button class="tree-preview-toggle-btn' + (s.gridType === 'equalArea' ? ' active' : '') + '" ' +
                        'id="tpSunGridEqArea" data-grid="equalArea">' + t('preview.sun.equalArea') + '</button>' +
                    '<button class="tree-preview-toggle-btn' + (s.gridType === 'fibonacci' ? ' active' : '') + '" ' +
                        'id="tpSunGridFib" data-grid="fibonacci">' + t('preview.sun.fibonacci') + '</button>' +
                    '<button class="tree-preview-toggle-btn' + (s.gridType === 'square' ? ' active' : '') + '" ' +
                        'id="tpSunGridSquare" data-grid="square">' + t('preview.sun.square') + '</button>' +
                '</div>' +
            '</div>' +

            '<div class="tree-preview-setting">' +
                '<label class="tree-preview-label">' + t('preview.sectionSplit') + '</label>' +
                '<div class="tree-preview-toggle-row">' +
                    '<button class="tree-preview-toggle-btn' + (!s.proportional ? ' active' : '') + '" ' +
                        'id="tpSunSplitEqual">' + t('preview.equal') + '</button>' +
                    '<button class="tree-preview-toggle-btn' + (s.proportional ? ' active' : '') + '" ' +
                        'id="tpSunSplitProp">' + t('preview.proportional') + '</button>' +
                '</div>' +
            '</div>' +

            '<div class="tree-preview-setting">' +
                '<label class="tree-preview-label">' + t('preview.growthDirection') + '</label>' +
                '<div class="tree-preview-toggle-row tree-preview-toggle-wrap">' +
                    '<button class="tree-preview-toggle-btn' + (!s.invertGrowth ? ' active' : '') + '" ' +
                        'id="tpSunGrowNormal">' + t('preview.sun.outward') + '</button>' +
                    '<button class="tree-preview-toggle-btn' + (s.invertGrowth ? ' active' : '') + '" ' +
                        'id="tpSunGrowInvert">' + t('preview.sun.inward') + '</button>' +
                '</div>' +
            '</div>' +

            // --- Numeric inputs in responsive grid ---
            // Each with a "?" saying what it does to the build (read from render() and ClassicLayout)
            '<div class="tree-preview-settings-grid">' +
                H(t('preview.sun.ringTier'), 'tpSunRingTier', 1, 9999, 1, s.ringTier, '', t('preview.sun.ringTierHint')) +
                H(t('preview.rootSize'), 'tpSunNodeSize', 1, 9999, 1, s.nodeSize, '', t('preview.sun.nodeSizeHint')) +
                H(t('preview.rootsPerSchool'), 'tpSunRoots', 1, 9999, 1, s.rootsPerSchool, '', t('preview.sun.rootsPerSchoolHint')) +
                H(t('preview.clumping'), 'tpSunClump', 0, 100, 1, s.rootClumping, '%', t('preview.sun.clumpingHint')) +
                H(t('preview.randomness'), 'tpSunRand', 0, 100, 1, s.rootRandomness, '%', t('preview.sun.randomnessHint')) +
                H(t('preview.sun.spokeDensity'), 'tpSunGrid', 5, 9999, 5, s.gridDensity, '', t('preview.sun.spokeDensityHint')) +
                H(t('preview.sun.tierDensity'), 'tpSunTiers', 5, 9999, 5, s.tierDensity, '', t('preview.sun.tierDensityHint')) +
            '</div>';
    },

    /**
     * Bind change events to settings inputs
     */
    bindEvents: function() {
        var self = this;
        var B = TreePreviewUtils.bindInput;
        var dirty = function() { if (typeof TreePreview !== 'undefined') TreePreview._markDirty(); };

        B('tpSunRingTier', function(v) { self.settings.ringTier = v; dirty(); });
        B('tpSunNodeSize', function(v) { self.settings.nodeSize = v; dirty(); });
        B('tpSunRoots', function(v) { self.settings.rootsPerSchool = v; dirty(); });
        B('tpSunGrid', function(v) { self.settings.gridDensity = v; dirty(); });
        B('tpSunTiers', function(v) { self.settings.tierDensity = v; dirty(); });
        B('tpSunClump', function(v) { self.settings.rootClumping = v; dirty(); });
        B('tpSunRand', function(v) { self.settings.rootRandomness = v; dirty(); });

        // Growth direction toggle
        var growBtnIds = ['tpSunGrowNormal', 'tpSunGrowInvert'];
        growBtnIds.forEach(function(btnId) {
            var btn = document.getElementById(btnId);
            if (!btn) return;
            btn.addEventListener('click', function() {
                self.settings.invertGrowth = (btnId === 'tpSunGrowInvert');
                growBtnIds.forEach(function(id) {
                    var b = document.getElementById(id);
                    if (b) b.classList.remove('active');
                });
                this.classList.add('active');
                if (typeof TreePreview !== 'undefined') TreePreview._markDirty();
            });
        });

        // Grid type toggle
        var gridBtnIds = ['tpSunGridNaive', 'tpSunGridLinear', 'tpSunGridEqArea', 'tpSunGridFib', 'tpSunGridSquare'];
        gridBtnIds.forEach(function(btnId) {
            var btn = document.getElementById(btnId);
            if (!btn) return;
            btn.addEventListener('click', function() {
                self.settings.gridType = this.getAttribute('data-grid');
                gridBtnIds.forEach(function(id) {
                    var b = document.getElementById(id);
                    if (b) b.classList.remove('active');
                });
                this.classList.add('active');
                if (typeof TreePreview !== 'undefined') TreePreview._markDirty();
            });
        });

        // Section split toggle
        var btnEqual = document.getElementById('tpSunSplitEqual');
        var btnProp = document.getElementById('tpSunSplitProp');
        if (btnEqual) {
            btnEqual.addEventListener('click', function() {
                self.settings.proportional = false;
                btnEqual.classList.add('active');
                if (btnProp) btnProp.classList.remove('active');
                if (typeof TreePreview !== 'undefined') TreePreview._markDirty();
            });
        }
        if (btnProp) {
            btnProp.addEventListener('click', function() {
                self.settings.proportional = true;
                btnProp.classList.add('active');
                if (btnEqual) btnEqual.classList.remove('active');
                if (typeof TreePreview !== 'undefined') TreePreview._markDirty();
            });
        }
    },

    /**
     * Render the SUN radial grid and root ring onto the canvas.
     * Delegates grid drawing to the active grid module, or uses built-in naive grid.
     */
    render: function(ctx, w, h, schoolData) {
        var s = this.settings;
        var cx = w / 2;
        var cy = h / 2;
        var schools = Object.keys(schoolData || {});
        if (schools.length === 0) schools = Object.keys(this._schoolColors);
        var schoolCount = schools.length;

        // The grid: one set of parameters, the same points wherever it is drawn or used
        // (SunGridPoints), whatever the canvas's size
        var gridParams = SunGridPoints.params(s);
        var spokes = gridParams.spokes;
        var tiers = gridParams.tiers;
        var tierSpacing = gridParams.tierSpacing;
        var ringRadius = gridParams.ringRadius;
        var maxExtent = gridParams.extent;
        var gridPts = SunGridPoints.points(gridParams);

        // --- Calculate school arcs early (needed for grid point coloring) ---
        var totalAngle = Math.PI * 2;
        var arcSizes = [];
        var arcStarts = [];

        if (schoolCount > 0) {
            if (s.proportional && schoolData) {
                var totalSpells = 0;
                for (var si = 0; si < schoolCount; si++) {
                    totalSpells += (schoolData[schools[si]] || 1);
                }
                for (var si2 = 0; si2 < schoolCount; si2++) {
                    arcSizes.push(totalAngle * (schoolData[schools[si2]] || 1) / totalSpells);
                }
            } else {
                var equalArc = totalAngle / schoolCount;
                for (var si3 = 0; si3 < schoolCount; si3++) {
                    arcSizes.push(equalArc);
                }
            }
            // Offset so first school is centered at the top (-π/2)
            var cumAngle = -Math.PI / 2 - arcSizes[0] / 2;
            for (var ai = 0; ai < schoolCount; ai++) {
                arcStarts.push(((cumAngle % totalAngle) + totalAngle) % totalAngle);
                cumAngle += arcSizes[ai];
            }
        }

        // Build school-colored point function for grid modules
        var schoolRgba = [];
        for (var ci = 0; ci < schoolCount; ci++) {
            schoolRgba.push(this._hexToRgba(this._schoolColors[schools[ci]] || '#888888', 0.25));
        }

        var pointColorFn = schoolCount > 0 ? function(angle) {
            var a = ((angle % totalAngle) + totalAngle) % totalAngle;
            for (var pi = 0; pi < schoolCount; pi++) {
                var start = arcStarts[pi];
                var end = start + arcSizes[pi];
                if (end > totalAngle) {
                    // Arc wraps around 2π
                    if (a >= start || a < end - totalAngle) return schoolRgba[pi];
                } else {
                    if (a >= start && a < end) return schoolRgba[pi];
                }
            }
            return schoolRgba[schoolCount - 1];
        } : null;

        // Cache for getGridData()
        this._lastRenderData = {
            schools: schools,
            arcStarts: arcStarts,
            arcSizes: arcSizes,
            tierSpacing: tierSpacing,
            ringRadius: ringRadius,
            spokes: spokes,
            tiers: tiers
        };

        // --- Draw grid (delegate or built-in naive): its own lines, the shared points as dots ---
        var gridOpts = {
            tierSpacing: tierSpacing,
            ringTier: gridParams.ringTier,
            ringRadius: ringRadius,
            spokes: spokes,
            tiers: tiers,
            maxExtent: maxExtent,
            pointColorFn: pointColorFn,
            params: gridParams,
            points: gridPts
        };

        // Render grid directly (batched paths, no offscreen canvas needed)
        var gridModule = this._grids[gridParams.type];
        if (gridModule && typeof gridModule.renderGrid === 'function') {
            gridModule.renderGrid(ctx, cx, cy, gridOpts);
        } else {
            this._renderNaiveGrid(ctx, cx, cy, gridOpts);
        }
        // Clean up old offscreen canvas from previous caching approach
        if (this._gridCanvas) { this._gridCanvas = null; this._gridCacheKey = null; }

        // The grid's points with the school each falls in, for downstream modules (cached on
        // all they depend on: the grid's parameters and the schools' arcs)
        this._lastRenderData.params = gridParams;
        var gpCacheKey = SunGridPoints.key(gridParams) + '|' + schools.join(',') + '|' +
            arcStarts.join(',') + '|' + arcSizes.join(',');
        if (this._gridPointCache && this._gridPointCacheKey === gpCacheKey) {
            this._lastRenderData.gridPoints = this._gridPointCache;
        } else {
            this._lastRenderData.gridPoints = this._schoolPoints(gridPts, schools, arcStarts, arcSizes);
            this._gridPointCache = this._lastRenderData.gridPoints;
            this._gridPointCacheKey = gpCacheKey;
        }

        if (schoolCount === 0) return;

        // --- School divider spokes + root nodes + labels ---
        var arcStart = -Math.PI / 2 - arcSizes[0] / 2;
        var rootNodes = [];
        var extentX = 0, extentY = 0;

        for (var i = 0; i < schoolCount; i++) {
            var school = schools[i];
            var color = this._schoolColors[school] || '#888888';
            var arcSize = arcSizes[i];

            // School divider spoke (slightly brighter, extends to edge)
            ctx.beginPath();
            ctx.moveTo(cx, cy);
            ctx.lineTo(
                cx + Math.cos(arcStart) * maxExtent,
                cy + Math.sin(arcStart) * maxExtent
            );
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.2)';
            ctx.lineWidth = 1;
            ctx.stroke();

            // Place roots within this school's arc on the ring tier
            var roots = s.rootsPerSchool;
            var clumpF = s.rootClumping / 100;
            var randF = s.rootRandomness / 100;
            for (var j = 0; j < roots; j++) {
                var tEven = (j + 0.5) / roots;
                var t = tEven + (0.5 - tEven) * clumpF;
                if (randF > 0) {
                    var rnd = this._pseudoRandom(i * 17 + j * 31);
                    t = t + (rnd - 0.5) * randF * (1 / Math.max(1, roots));
                }
                t = Math.max(0.02, Math.min(0.98, t));
                var nodeAngle = arcStart + t * arcSize;
                var nx = cx + Math.cos(nodeAngle) * ringRadius;
                var ny = cy + Math.sin(nodeAngle) * ringRadius;

                // Growth direction: outward (default) or inward (inverted)
                var dirAngle = s.invertGrowth ? nodeAngle + Math.PI : nodeAngle;

                rootNodes.push({ x: nx - cx, y: ny - cy, school: school, color: color, dir: dirAngle, rootIndex: j });

                // Node glow
                ctx.beginPath();
                ctx.arc(nx, ny, s.nodeSize + 3, 0, Math.PI * 2);
                ctx.fillStyle = this._hexToRgba(color, 0.15);
                ctx.fill();

                // Node body
                ctx.beginPath();
                ctx.arc(nx, ny, s.nodeSize, 0, Math.PI * 2);
                ctx.fillStyle = color;
                ctx.fill();

                // Node border
                ctx.strokeStyle = 'rgba(255, 255, 255, 0.3)';
                ctx.lineWidth = 1;
                ctx.stroke();

                // Growth direction arrow
                this._drawArrow(ctx, nx, ny, dirAngle, s.nodeSize, color);
            }

            // School name in the panel's language, upright, past the roots and their
            // outward arrows (TreePreviewUtils.schoolLabelPlace); its reach is the extent the fit uses
            var labelText = (typeof schoolText === 'function') ? schoolText(school) : school;
            ctx.save();
            ctx.font = this.LABEL_FONT;
            var outward = s.nodeSize + (s.invertGrowth ? 3 : this.ARROW_REACH);
            var place = TreePreviewUtils.schoolLabelPlace(arcStart + arcSize / 2, ringRadius + outward + this.LABEL_GAP,
                ctx.measureText(labelText).width, this.LABEL_HEIGHT);
            ctx.fillStyle = 'rgba(184, 168, 120, 0.85)';
            ctx.textAlign = place.align;
            ctx.textBaseline = place.baseline;
            ctx.fillText(labelText, cx + place.x, cy + place.y);
            ctx.restore();
            extentX = Math.max(extentX, place.reachX, ringRadius + outward);
            extentY = Math.max(extentY, place.reachY, ringRadius + outward);

            arcStart += arcSize;
        }

        this._lastRenderData.rootNodes = rootNodes;
        this._lastRenderData.extent = { x: extentX, y: extentY };
    },

    /**
     * The grid's points (SunGridPoints, shared and read only) as the layout takes them: copies
     * with the school whose arc each falls in. Each point: { x, y, school, _ptIdx, _moveDir,
     * _neighbors } (coords relative to the centre; the neighbour lists are the shared ones).
     */
    _schoolPoints: function(pts, schools, arcStarts, arcSizes) {
        var PI2 = Math.PI * 2;
        var schoolCount = schools.length;
        function getSchool(angle) {
            if (schoolCount === 0) return '';
            var a = ((angle % PI2) + PI2) % PI2;
            for (var i = 0; i < schoolCount; i++) {
                var start = arcStarts[i];
                var end = start + arcSizes[i];
                if (end > PI2) {
                    if (a >= start || a < end - PI2) return schools[i];
                } else {
                    if (a >= start && a < end) return schools[i];
                }
            }
            return schools[schoolCount - 1];
        }
        var out = new Array(pts.length);
        for (var i = 0; i < pts.length; i++) {
            var p = pts[i];
            out[i] = { x: p.x, y: p.y, school: getSchool(Math.atan2(p.y, p.x)), _ptIdx: p._ptIdx,
                _moveDir: p._moveDir, _neighbors: p._neighbors };
        }
        return out;
    },

    /**
     * Built-in naive grid: fixed angular divisions, same point count per ring
     */
    _renderNaiveGrid: function(ctx, cx, cy, opts) {
        var tierSpacing = opts.tierSpacing;
        var spokes = opts.spokes;
        var maxExtent = opts.maxExtent;
        var ringTier = opts.ringTier;
        var ringRadius = opts.ringRadius;
        var totalTiers = Math.ceil(maxExtent / tierSpacing);
        var angleStep = (Math.PI * 2) / spokes;

        // Concentric tier rings — batch by style
        var innerRings = [];
        var outerRings = [];
        var rootRingR = -1;
        for (var r = 1; r <= totalTiers; r++) {
            var ringR = r * tierSpacing;
            if (r === ringTier) {
                rootRingR = ringR;
            } else if (r < ringTier) {
                innerRings.push(ringR);
            } else {
                outerRings.push(ringR);
            }
        }
        if (outerRings.length > 0) {
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.05)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (var ori = 0; ori < outerRings.length; ori++) { ctx.moveTo(cx + outerRings[ori], cy); ctx.arc(cx, cy, outerRings[ori], 0, Math.PI * 2); }
            ctx.stroke();
        }
        if (innerRings.length > 0) {
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.1)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (var iri = 0; iri < innerRings.length; iri++) { ctx.moveTo(cx + innerRings[iri], cy); ctx.arc(cx, cy, innerRings[iri], 0, Math.PI * 2); }
            ctx.stroke();
        }
        if (rootRingR > 0) {
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.35)';
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(cx + rootRingR, cy);
            ctx.arc(cx, cy, rootRingR, 0, Math.PI * 2);
            ctx.stroke();
        }

        // Radial spokes — single batched path
        ctx.strokeStyle = 'rgba(184, 168, 120, 0.08)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (var g = 0; g < spokes; g++) {
            var angle = g * angleStep;
            ctx.moveTo(cx, cy);
            ctx.lineTo(cx + Math.cos(angle) * maxExtent, cy + Math.sin(angle) * maxExtent);
        }
        ctx.stroke();

        // Grid dots: the grid's own points (SunGridPoints)
        SunGridPoints.drawDots(ctx, cx, cy, opts.points, opts.pointColorFn);

        // Center dot
        ctx.beginPath();
        ctx.arc(cx, cy, 3, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(184, 168, 120, 0.5)';
        ctx.fill();
    },

    /**
     * Draw a direction arrow from node edge outward
     */
    _drawArrow: function(ctx, nx, ny, angle, nodeSize, color) {
        var arrowLen = 16;
        var headLen = 6;
        var headAngle = Math.PI / 6;
        var sx = nx + Math.cos(angle) * (nodeSize + 2);
        var sy = ny + Math.sin(angle) * (nodeSize + 2);
        var ex = sx + Math.cos(angle) * arrowLen;
        var ey = sy + Math.sin(angle) * arrowLen;

        ctx.beginPath();
        ctx.moveTo(sx, sy);
        ctx.lineTo(ex, ey);
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.stroke();

        ctx.beginPath();
        ctx.moveTo(ex, ey);
        ctx.lineTo(ex - Math.cos(angle - headAngle) * headLen, ey - Math.sin(angle - headAngle) * headLen);
        ctx.lineTo(ex - Math.cos(angle + headAngle) * headLen, ey - Math.sin(angle + headAngle) * headLen);
        ctx.closePath();
        ctx.fillStyle = color;
        ctx.fill();
    },

    /**
     * Deterministic pseudo-random from seed (consistent per frame)
     */
    _pseudoRandom: function(seed) {
        var x = Math.sin(seed * 127.1 + 311.7) * 43758.5453;
        return x - Math.floor(x);
    },

    /**
     * Convert hex color to rgba string
     */
    _hexToRgba: function(hex, alpha) { return hexToRgba(hex, alpha); },

    /**
     * Return current settings for external consumption
     */
    getSettings: function() {
        return JSON.parse(JSON.stringify(this.settings));
    },

    /**
     * Return grid data with school assignments for the next build section.
     * Contains school boundaries and grid structure from the last render.
     */
    getGridData: function() {
        var d = this._lastRenderData;
        if (!d) return null;
        var self = this;
        var totalAngle = Math.PI * 2;
        return {
            mode: 'sun',
            gridType: self.settings.gridType,
            gridPoints: d.gridPoints || [],
            grid: {
                spokes: d.spokes,
                tiers: d.tiers,
                tierSpacing: d.tierSpacing,
                ringRadius: d.ringRadius,
                ringTier: self.settings.ringTier,
                // All the grid's points depend on (SunGridPoints): what an applied tree saves
                params: d.params || null
            },
            schools: d.schools.map(function(name, i) {
                var a1 = d.arcStarts[i];
                var a2 = a1 + d.arcSizes[i];
                var r = d.ringRadius;
                return {
                    name: name,
                    color: self._schoolColors[name] || '#888888',
                    arcStart: a1,
                    arcSize: d.arcSizes[i],
                    rootLine: {
                        p1: { x: Math.cos(a1) * r, y: Math.sin(a1) * r },
                        p2: { x: Math.cos(a2) * r, y: Math.sin(a2) * r },
                        angle1: a1,
                        angle2: a2
                    }
                };
            }),
            getSchoolAtAngle: function(angle) {
                var a = ((angle % totalAngle) + totalAngle) % totalAngle;
                for (var i = 0; i < d.schools.length; i++) {
                    var start = d.arcStarts[i];
                    var end = start + d.arcSizes[i];
                    if (end > totalAngle) {
                        if (a >= start || a < end - totalAngle) return d.schools[i];
                    } else {
                        if (a >= start && a < end) return d.schools[i];
                    }
                }
                return d.schools[d.schools.length - 1];
            }
        };
    }
};

// Register grid modules that loaded before us
if (typeof SunGridLinear !== 'undefined') {
    TreePreviewSun.registerGrid('linear', SunGridLinear);
}
if (typeof SunGridEqualArea !== 'undefined') {
    TreePreviewSun.registerGrid('equalArea', SunGridEqualArea);
}
if (typeof SunGridFibonacci !== 'undefined') {
    TreePreviewSun.registerGrid('fibonacci', SunGridFibonacci);
}
if (typeof SunGridSquare !== 'undefined') {
    TreePreviewSun.registerGrid('square', SunGridSquare);
}

// Self-register when TreePreview is available
if (typeof TreePreview !== 'undefined') {
    TreePreview.registerMode('sun', TreePreviewSun);
}
