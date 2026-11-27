/**
 * SUN Grid Distribution — Equal Area
 *
 * Ring radii follow a square-root distribution: r_k = sqrt(k/N) * maxRadius.
 * This ensures each ring band encloses the same area. Rings get closer
 * together toward the outer edge. Point count per ring scales with
 * circumference for consistent density.
 *
 * Draws the ring circles and spokes; the dots are the grid's points (SunGridPoints, opts.points),
 * which also generates them, on the rings equalAreaRings() gives (the drawing used 6x the
 * linear ring count, the layout's points 4x, until 2026-11-26: they did not meet).
 *
 * Depends on: sunGridPoints.js
 */

var SunGridEqualArea = {
    name: 'Equal Area',

    /**
     * Render an equal-area radial grid.
     * @param {CanvasRenderingContext2D} ctx
     * @param {number} cx - Center X
     * @param {number} cy - Center Y
     * @param {object} opts - tierSpacing, ringTier, ringRadius, spokes, tiers, maxExtent, params, points
     */
    renderGrid: function(ctx, cx, cy, opts) {
        var ringRadius = opts.ringRadius;
        var spokes = opts.spokes;
        var maxExtent = opts.maxExtent;

        // Equal Area: ring radii follow sqrt(k/N) (SunGridPoints.equalAreaRings); a circle every
        // Nth ring to match other grid types, out to the last ring that has points
        var rings = SunGridPoints.equalAreaRings(opts.params);
        var pts = opts.points || [];
        var lastR = 0;
        for (var pi = 0; pi < pts.length; pi++) lastR = Math.max(lastR, pts[pi].x * pts[pi].x + pts[pi].y * pts[pi].y);
        lastR = Math.sqrt(lastR) + 0.5;

        // The root ring: the one nearest the roots' radius
        var rootRingK = -1;
        var bestRootDist = Infinity;
        for (var rk = 1; rk <= rings.total; rk++) {
            var dist = Math.abs(rings.radius(rk) - ringRadius);
            if (dist < bestRootDist) { bestRootDist = dist; rootRingK = rk; }
        }

        var eqInnerRings = [], eqOuterRings = [], eqRootRingR = -1;
        for (var k = 1; k <= rings.total; k++) {
            var radius = rings.radius(k);
            if (radius > lastR) break;
            if (k === rootRingK) eqRootRingR = radius;
            else if (k % rings.every === 0) (radius < ringRadius ? eqInnerRings : eqOuterRings).push(radius);
        }
        // Draw batched rings
        if (eqOuterRings.length > 0) {
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.05)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (var eoi = 0; eoi < eqOuterRings.length; eoi++) { ctx.moveTo(cx + eqOuterRings[eoi], cy); ctx.arc(cx, cy, eqOuterRings[eoi], 0, Math.PI * 2); }
            ctx.stroke();
        }
        if (eqInnerRings.length > 0) {
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.1)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (var eii = 0; eii < eqInnerRings.length; eii++) { ctx.moveTo(cx + eqInnerRings[eii], cy); ctx.arc(cx, cy, eqInnerRings[eii], 0, Math.PI * 2); }
            ctx.stroke();
        }
        if (eqRootRingR > 0) {
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.35)';
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(cx + eqRootRingR, cy); ctx.arc(cx, cy, eqRootRingR, 0, Math.PI * 2);
            ctx.stroke();
        }

        // Dots: the grid's own points
        SunGridPoints.drawDots(ctx, cx, cy, pts, opts.pointColorFn);

        // --- Radial spokes — single batched path ---
        var spokeStep = (Math.PI * 2) / spokes;
        ctx.strokeStyle = 'rgba(184, 168, 120, 0.08)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (var s = 0; s < spokes; s++) {
            var spokeAngle = s * spokeStep;
            ctx.moveTo(cx, cy);
            ctx.lineTo(cx + Math.cos(spokeAngle) * maxExtent, cy + Math.sin(spokeAngle) * maxExtent);
        }
        ctx.stroke();

        // --- Center dot ---
        ctx.beginPath();
        ctx.arc(cx, cy, 3, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(184, 168, 120, 0.5)';
        ctx.fill();
    }
};
