/**
 * CanvasRenderer - the lines between the schools (their gradients kept until a
 * setting changes; a design may draw them as book rules instead) and the debug
 * grid of candidate positions.
 *
 * Split out of canvasRendererV2.js to keep the renderer's files under the size
 * limit; the methods are added to CanvasRenderer, so callers still write
 * CanvasRenderer.toggleDebugGrid() and so on. Load after canvasRendererV2.js.
 *
 * Depends on: CanvasRenderer, settings, state (globe position), TreeStyle, TreeGrid (the debug grid)
 */

(function() {
    var methods = {
        /** School dividers (world coordinates, rotated context). */
        renderSchoolDividers: function(ctx) {
            // Check if dividers are enabled
            if (!settings.showSchoolDividers) return;

            // LOD: Skip dividers entirely in MINIMAL tier
            if (this._lodTier === 'minimal') return;

            var schoolNames = Object.keys(this.schools);
            if (schoolNames.length < 2) return;

            var fallback = settings.dividerLength !== undefined ? settings.dividerLength : 800;
            var fade = (settings.dividerFade !== undefined ? settings.dividerFade : 50) / 100;  // Convert to 0-1
            var lineWidth = settings.dividerSpacing !== undefined ? settings.dividerSpacing : 3;
            var colorMode = settings.dividerColorMode || 'school';
            var customColor = settings.dividerCustomColor || '#ffffff';
            var gd = (state.treeData && state.treeData.globe) || { x: 0, y: 0 };
            // Each divider as long as the tree beside it
            var lengths = this._dividerLengths(schoolNames, gd, fallback);

            // A design preset can draw them as book rules instead
            if (TreeStyle.renderOrnamentDividers(ctx, this, lengths, gd)) return;

            // Build cache key from all settings that affect gradients
            var cacheKey = lengths.join(',') + '|' + fade + '|' + lineWidth + '|' + colorMode + '|' + customColor + '|' + gd.x + '|' + gd.y + '|' + schoolNames.length;
            for (var ci = 0; ci < schoolNames.length; ci++) {
                var sch = this.schools[schoolNames[ci]];
                cacheKey += '|' + (sch.startAngle || 0);
                if (colorMode === 'school') cacheKey += '|' + this._getSchoolColor(schoolNames[ci]);
            }

            // Rebuild gradient cache if settings changed
            if (this._dividerCacheKey !== cacheKey || !this._cachedDividerGradients) {
                this._dividerCacheKey = cacheKey;
                this._cachedDividerGradients = [];
                var startAlpha = 0.8;
                var endAlpha = startAlpha * (1 - fade);

                for (var i = 0; i < schoolNames.length; i++) {
                    var schoolName = schoolNames[i];
                    var rad = this._dividerAngle(schoolNames, i);
                    var length = lengths[i];

                    var color;
                    if (colorMode === 'custom') {
                        color = customColor;
                    } else {
                        color = this._getSchoolColor(schoolName) || '#ffffff';
                    }

                    var endX = gd.x + Math.cos(rad) * length;
                    var endY = gd.y + Math.sin(rad) * length;
                    var gradient = ctx.createLinearGradient(gd.x, gd.y, endX, endY);

                    var rgb = this._hexToRgb(color);
                    gradient.addColorStop(0, 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + startAlpha + ')');
                    gradient.addColorStop(0.5, 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + (startAlpha * 0.7 + endAlpha * 0.3) + ')');
                    gradient.addColorStop(1, 'rgba(' + rgb.r + ',' + rgb.g + ',' + rgb.b + ',' + endAlpha + ')');

                    this._cachedDividerGradients.push({
                        gradient: gradient,
                        startX: gd.x, startY: gd.y,
                        endX: endX, endY: endY
                    });
                }
            }

            ctx.lineWidth = lineWidth;
            ctx.lineCap = 'round';

            // Draw using cached gradients
            for (var i = 0; i < this._cachedDividerGradients.length; i++) {
                var cached = this._cachedDividerGradients[i];
                ctx.strokeStyle = cached.gradient;
                ctx.beginPath();
                ctx.moveTo(cached.startX, cached.startY);
                ctx.lineTo(cached.endX, cached.endY);
                ctx.stroke();
            }
        },

        // A spell this close to a divider's line (tree units) sets its length: 75% of the mean
        // edge length of the tree it was measured on (103, 990 edges, 2026-11-23)
        DIVIDER_BAND: 77,
        DIVIDER_MARGIN: 40,   // how far a divider runs past the furthest such spell

        /** The divider at school i: its angle in radians, from the school's start angle. */
        _dividerAngle: function(schoolNames, i) {
            var school = this.schools[schoolNames[i]];
            var angle = school.startAngle !== undefined ? school.startAngle : (i * (360 / schoolNames.length) - 90);
            return angle * Math.PI / 180;
        },

        /**
         * Each divider's length: a little past the furthest shown spell lying close
         * to its line, on the outward side of the globe. A divider no spell comes
         * near runs as far as the furthest spell of the tree; no spells at all, the
         * dividerLength setting. Rounded, so it keys the gradient cache.
         */
        _dividerLengths: function(schoolNames, gd, fallback) {
            var nodes = this.nodes || [];
            var band = this.DIVIDER_BAND, margin = this.DIVIDER_MARGIN;
            var dirs = [], near = [], furthest = 0;
            for (var i = 0; i < schoolNames.length; i++) {
                var rad = this._dividerAngle(schoolNames, i);
                dirs.push(Math.cos(rad), Math.sin(rad));
                near.push(0);
            }
            var hidden = settings.schoolVisibility;
            for (var n = 0; n < nodes.length; n++) {
                var node = nodes[n];
                if (hidden && hidden[node.school] === false) continue;
                var dx = node.x - gd.x, dy = node.y - gd.y;
                if (!isFinite(dx) || !isFinite(dy)) continue;
                var dist = Math.sqrt(dx * dx + dy * dy);
                if (dist > furthest) furthest = dist;
                for (var d = 0; d < near.length; d++) {
                    var ux = dirs[d * 2], uy = dirs[d * 2 + 1];
                    var along = dx * ux + dy * uy;
                    if (along > near[d] && Math.abs(dx * uy - dy * ux) <= band) near[d] = along;
                }
            }
            var out = [];
            for (var k = 0; k < near.length; k++) {
                var reach = near[k] || furthest;
                out.push(reach > 0 ? Math.round(reach + margin) : fallback);
            }
            return out;
        },

        /**
         * Render the grid edit mode snaps to (EditMode.gridPositions: the tree's own grid, or
         * the fallback square grid, TreeGrid), in world units
         * Called within the rotated context
         */
        renderDebugGrid: function(ctx) {
            if (!this.showDebugGrid) return;
            var pts = (typeof EditMode !== 'undefined' && EditMode.isActive && EditMode.gridPositions.length)
                ? EditMode.gridPositions
                : TreeGrid.positions(typeof state !== 'undefined' && state.treeData ? state.treeData.rawData : null).points;

            // One path: a point a frame each (up to about 20,000) was too slow as separate arcs
            ctx.fillStyle = 'rgba(184, 168, 120, 0.35)';
            ctx.beginPath();
            for (var i = 0; i < pts.length; i++) ctx.rect(pts[i].x - 2.5, pts[i].y - 2.5, 5, 5);
            ctx.fill();
        },

        /**
         * Toggle debug grid visibility
         */
        toggleDebugGrid: function() {
            this.showDebugGrid = !this.showDebugGrid;
            this._needsRender = true;
            console.log('[CanvasRenderer] Debug grid:', this.showDebugGrid ? 'ON' : 'OFF');
            return this.showDebugGrid;
        }
    };

    for (var key in methods) {
        if (methods.hasOwnProperty(key)) CanvasRenderer[key] = methods[key];
    }
})();
