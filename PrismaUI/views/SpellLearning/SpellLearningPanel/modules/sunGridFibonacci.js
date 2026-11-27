/**
 * SUN Grid Distribution — Fibonacci / Golden Angle
 *
 * Places points in a phyllotactic spiral using the golden angle (~137.508 deg).
 * Each point: radius = maxRadius * sqrt(i / totalPoints),
 *             angle  = i * goldenAngle.
 * No concentric ring structure; a dashed reference circle marks the
 * root ring position.
 *
 * Draws its own lines; the dots are the grid's points (SunGridPoints, opts.points), which also
 * generates them.
 *
 * Depends on: sunGridPoints.js
 */

var SunGridFibonacci = {
    name: 'Fibonacci',

    /**
     * Render a Fibonacci/golden-angle spiral grid.
     * @param {CanvasRenderingContext2D} ctx
     * @param {number} cx - Center X
     * @param {number} cy - Center Y
     * @param {object} opts - ringRadius, points (the rest of TreePreviewSun's grid options unused)
     */
    renderGrid: function(ctx, cx, cy, opts) {
        var ringRadius = opts.ringRadius;

        // --- Dashed reference circle at ringRadius ---
        ctx.beginPath();
        ctx.arc(cx, cy, ringRadius, 0, Math.PI * 2);
        ctx.strokeStyle = 'rgba(184, 168, 120, 0.35)';
        ctx.lineWidth = 1.5;
        ctx.setLineDash([6, 4]);
        ctx.stroke();
        ctx.setLineDash([]);

        // --- Spiral dots: the grid's own points ---
        SunGridPoints.drawDots(ctx, cx, cy, opts.points || [], opts.pointColorFn);

        // --- Center dot ---
        ctx.beginPath();
        ctx.arc(cx, cy, 3, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(184, 168, 120, 0.5)';
        ctx.fill();
    }
};
