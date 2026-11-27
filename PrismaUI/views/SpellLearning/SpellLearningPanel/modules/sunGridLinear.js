/**
 * SUN Grid Distribution — Linear
 *
 * Points per ring scale linearly with radius. Each ring k has a point
 * count proportional to its circumference, derived from the base spoke
 * count on ring 1. Produces evenly spaced concentric rings with
 * increasing point density outward.
 *
 * Draws the rings and spokes; the dots are the grid's points (SunGridPoints, opts.points),
 * which also generates them.
 *
 * Depends on: sunGridPoints.js
 */

var SunGridLinear = {
    name: 'Linear',

    /**
     * Render a linear-distribution radial grid.
     * @param {CanvasRenderingContext2D} ctx
     * @param {number} cx - Center X
     * @param {number} cy - Center Y
     * @param {object} opts - tierSpacing, ringTier, ringRadius, spokes, tiers, maxExtent, points
     */
    renderGrid: function(ctx, cx, cy, opts) {
        var tierSpacing = opts.tierSpacing;
        var ringTier = opts.ringTier;
        var spokes = opts.spokes;
        var maxExtent = opts.maxExtent;
        var totalTiers = Math.ceil(maxExtent / tierSpacing);

        // --- Concentric tier rings — batch by style ---
        var linInner = [], linOuter = [], linRootR = -1;
        for (var r = 1; r <= totalTiers; r++) {
            var radius = r * tierSpacing;
            if (r === ringTier) { linRootR = radius; }
            else if (r < ringTier) { linInner.push(radius); }
            else { linOuter.push(radius); }
        }
        if (linOuter.length > 0) {
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.05)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (var loi = 0; loi < linOuter.length; loi++) { ctx.moveTo(cx + linOuter[loi], cy); ctx.arc(cx, cy, linOuter[loi], 0, Math.PI * 2); }
            ctx.stroke();
        }
        if (linInner.length > 0) {
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.1)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (var lii = 0; lii < linInner.length; lii++) { ctx.moveTo(cx + linInner[lii], cy); ctx.arc(cx, cy, linInner[lii], 0, Math.PI * 2); }
            ctx.stroke();
        }
        if (linRootR > 0) {
            ctx.strokeStyle = 'rgba(184, 168, 120, 0.35)';
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(cx + linRootR, cy); ctx.arc(cx, cy, linRootR, 0, Math.PI * 2);
            ctx.stroke();
        }

        // --- Grid dots: the grid's own points (SunGridPoints, opts.points) ---
        SunGridPoints.drawDots(ctx, cx, cy, opts.points, opts.pointColorFn);

        // --- Base spokes — single batched path ---
        var baseAngleStep = (Math.PI * 2) / spokes;
        ctx.strokeStyle = 'rgba(184, 168, 120, 0.08)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (var s = 0; s < spokes; s++) {
            var spokeAngle = s * baseAngleStep;
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
