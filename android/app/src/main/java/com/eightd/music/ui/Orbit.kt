package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * The orbit, drawn from the DSP's own telemetry.
 *
 * Everything here is read from Processor::angle/distance/peak -- the view never
 * runs its own clock, so what you see is literally where the source is, not an
 * animation that happens to look similar.
 */
@Composable
fun OrbitView(
    angle: Float,
    distance: Float,
    radius: Float,
    running: Boolean,
    trail: List<Float>,
    onScrub: ((Float) -> Unit)?,
    modifier: Modifier = Modifier,
) {
    val p = palette
    val scrub = onScrub

    Canvas(
        modifier
            .fillMaxWidth()
            .aspectRatio(1f)
            .then(
                if (scrub == null) Modifier else Modifier.pointerInput(Unit) {
                    detectDragGestures { change, _ ->
                        val cx = size.width / 2f
                        val cy = size.height / 2f
                        change.consume()
                        scrub(atan2(change.position.x - cx, cy - change.position.y))
                    }
                }
            )
    ) {
        val cx = size.width / 2f
        val cy = size.height / 2f
        val r = min(cx, cy) - 2.dp.toPx()

        drawCircle(p.line, radius = r, center = Offset(cx, cy), style = Stroke(1.dp.toPx()))
        drawCircle(p.lineSoft, radius = r * 0.69f, center = Offset(cx, cy), style = Stroke(1.dp.toPx()))
        drawLine(p.lineSoft, Offset(cx, cy - r), Offset(cx, cy + r), 1.dp.toPx())
        drawLine(p.lineSoft, Offset(cx - r, cy), Offset(cx + r, cy), 1.dp.toPx())

        // The orbit the current radius describes, dashed the way the mockup has it.
        val orbitR = r * (radius.coerceIn(0.2f, 3f) / 3f) * 0.92f
        drawCircle(
            color = if (running) p.orbitOn else p.orbitOff,
            radius = orbitR,
            center = Offset(cx, cy),
            style = Stroke(
                width = 1.dp.toPx(),
                pathEffect = PathEffect.dashPathEffect(floatArrayOf(6.dp.toPx(), 6.dp.toPx()))
            )
        )

        // The listener, at the centre.
        drawCircle(p.line, radius = r * 0.115f, center = Offset(cx, cy), style = Stroke(1.dp.toPx()))
        drawCircle(p.line, radius = r * 0.075f, center = Offset(cx, cy), style = Stroke(1.dp.toPx()))

        // Where the source has just been, oldest faintest.
        trail.forEachIndexed { i, a ->
            val t = (i + 1f) / (trail.size + 1f)
            val pos = polar(cx, cy, orbitR, a)
            drawCircle(
                color = (if (running) p.orbitOn else p.orbitOff).copy(alpha = 0.10f + 0.22f * t),
                radius = (2f + 4f * t).dp.toPx(),
                center = pos
            )
        }

        val dot = polar(cx, cy, orbitR, angle)
        val dotColor = if (running) p.dotOn else p.dotOff
        drawCircle(dotColor.copy(alpha = 0.16f), radius = 15.dp.toPx(), center = dot)
        drawCircle(dotColor, radius = 7.dp.toPx(), center = dot)

        drawLabels(p.ghost, cx, cy, r)
    }
}

private fun polar(cx: Float, cy: Float, r: Float, angle: Float) =
    Offset(cx + r * sin(angle), cy - r * cos(angle))

private fun DrawScope.drawLabels(color: Color, cx: Float, cy: Float, r: Float) {
    // FRONT / BACK / L / R are drawn as ticks; the text sits outside the canvas
    // so it can use the real type stack rather than Canvas text.
    val tick = 5.dp.toPx()
    drawLine(color, Offset(cx, cy - r), Offset(cx, cy - r + tick), 1.dp.toPx())
    drawLine(color, Offset(cx, cy + r - tick), Offset(cx, cy + r), 1.dp.toPx())
    drawLine(color, Offset(cx - r, cy), Offset(cx - r + tick, cy), 1.dp.toPx())
    drawLine(color, Offset(cx + r - tick, cy), Offset(cx + r, cy), 1.dp.toPx())
}

/** +0° · 1.00 m · centre, with the L/R meters the desktop rail also carries. */
@Composable
fun Readout(angle: Float, distance: Float, peakL: Float, peakR: Float, running: Boolean) {
    val p = palette
    val deg = Math.toDegrees(angle.toDouble()).let { d ->
        var x = d % 360.0
        if (x > 180) x -= 360.0
        if (x < -180) x += 360.0
        x.roundToInt()
    }
    val side = when {
        abs(deg) < 12 -> "centre"
        abs(deg) > 168 -> "behind"
        deg > 0 -> "right"
        else -> "left"
    }

    Row(
        Modifier.fillMaxWidth().padding(horizontal = 20.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Row(Modifier.weight(1f), horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            Text("${if (deg >= 0) "+" else ""}$deg°", 15.sp, p.accent, FontWeight.SemiBold)
            Text("%.2f m".format(distance), 15.sp, p.dim)
            Text(side, 15.sp, p.faint)
        }
        Column(Modifier.width(120.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Meter("L", peakL, running)
            Meter("R", peakR, running)
        }
    }
}

@Composable
private fun Meter(label: String, level: Float, running: Boolean) {
    val p = palette
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(label, 10.sp, p.ghost)
        Canvas(Modifier.weight(1f).height(3.dp)) {
            drawRect(p.lineSoft, size = size)
            // Peak is linear; a decibel-ish curve makes quiet music visible.
            val w = (kotlin.math.sqrt(level.coerceIn(0f, 1f)) * size.width)
            drawRect(if (running) p.meterOn else p.meterOff, size = Size(w, size.height))
        }
    }
}

@Composable
internal fun Text(
    text: String,
    size: androidx.compose.ui.unit.TextUnit,
    color: Color,
    weight: FontWeight = FontWeight.Normal,
    letterSpacing: androidx.compose.ui.unit.TextUnit = 0.sp,
    modifier: Modifier = Modifier,
    maxLines: Int = Int.MAX_VALUE,
) = androidx.compose.material3.Text(
    text = text,
    color = color,
    fontSize = size,
    fontWeight = weight,
    fontFamily = FontFamily.Serif,
    letterSpacing = letterSpacing,
    maxLines = maxLines,
    overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
    modifier = modifier,
)
