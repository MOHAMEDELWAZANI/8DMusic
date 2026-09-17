package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/*
 * The v2 shell: rounded filled surfaces instead of outlined boxes, pills and
 * circles for anything tappable, and rotary knobs four to a row so the whole
 * studio fits on one page. Shapes and sizes follow the doop canvas
 * ("8D Music v2: structure & direction").
 */

val CardShape = RoundedCornerShape(26.dp)
val TileShape = RoundedCornerShape(18.dp)
val Pill = RoundedCornerShape(50)

/**
 * The soft light behind a hero.
 *
 * Every v2 page on the canvas sits on one or two of these: a cyan bloom where
 * the effect is, a magenta one where the sound is moving. They are the only
 * atmosphere in the design, which is why they belong to a shared composable
 * rather than being re-invented per screen.
 */
@Composable
fun Glow(
    modifier: Modifier = Modifier,
    cyan: Float = .12f,
    magenta: Float = .12f,
    cyanCentre: Offset = Offset(.28f, .42f),
    magentaCentre: Offset = Offset(.74f, .28f),
) {
    val p = palette
    if (!p.dark) return
    Canvas(modifier) {
        if (cyan > 0f) drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(p.accent.copy(alpha = cyan), Color.Transparent),
                center = Offset(size.width * cyanCentre.x, size.height * cyanCentre.y),
                radius = size.minDimension * .75f,
            ),
            radius = size.minDimension * .75f,
            center = Offset(size.width * cyanCentre.x, size.height * cyanCentre.y),
        )
        if (magenta > 0f) drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(p.motion.copy(alpha = magenta), Color.Transparent),
                center = Offset(size.width * magentaCentre.x, size.height * magentaCentre.y),
                radius = size.minDimension * .65f,
            ),
            radius = size.minDimension * .65f,
            center = Offset(size.width * magentaCentre.x, size.height * magentaCentre.y),
        )
    }
}

/*
 * The two faces the cover mark is drawn in.  They cover disjoint alphabets --
 * Pixelify Sans has all 52 Latin letters and no Arabic, KO Methlama 112 Arabic
 * letters and no Latin -- so the face has to follow the script rather than the
 * other way round.  Unlike the desktop build there is no per-word fallback here:
 * a custom typeface draws what it has and nothing else, which is why
 * `arabicOnly` refuses any string that mixes the two.
 */
private val Pixelify =
    FontFamily(Font(com.eightd.music.R.font.pixelify_sans_bold, FontWeight.Bold))
private val Methlama =
    FontFamily(Font(com.eightd.music.R.font.ko_methlama_medium, FontWeight.Medium))

/**
 * True when the string is Arabic and nothing else.  Digits, spaces and the
 * punctuation the interface uses are allowed through -- Methlama draws those --
 * but one Latin letter is enough to send the whole string back to the body face.
 */
fun arabicOnly(text: String): Boolean {
    var seen = false
    for (c in text) {
        if (c.code in 0x0600..0x06FF || c.code in 0x0750..0x077F ||
            c.code in 0xFB50..0xFEFF) { seen = true; continue }
        if (c.isLetter()) return false
    }
    return seen
}

/**
 * The cover mark: a track's opening letters, set in the face that can draw them.
 * Arabic sits shorter on the line than Latin at the same size, so it is given a
 * fifth more.
 */
@Composable
fun Mark(text: String, size: TextUnit, color: Color, modifier: Modifier = Modifier) {
    val arabic = arabicOnly(text)
    androidx.compose.material3.Text(
        text = text,
        color = color,
        maxLines = 1,
        // Trimmed and centred on the glyph band rather than on the line box:
        // the two faces have very different vertical metrics, and the default
        // padding would leave one of them sitting off the middle of the tile.
        style = TextStyle(
            fontSize = if (arabic) size * 1.05f else size,
            fontWeight = if (arabic) FontWeight.Medium else FontWeight.Bold,
            fontFamily = if (arabic) Methlama else Pixelify,
            lineHeight = if (arabic) size * 1.05f else size,
            lineHeightStyle = LineHeightStyle(
                alignment = LineHeightStyle.Alignment.Center,
                trim = LineHeightStyle.Trim.Both,
            ),
            platformStyle = PlatformTextStyle(includeFontPadding = false),
        ),
        modifier = modifier,
    )
}

/** Body face for v2. The serif is kept for page titles only, as in the design. */
@Composable
fun T(
    text: String,
    size: TextUnit = 15.sp,
    color: Color = palette.text,
    weight: FontWeight = FontWeight.Normal,
    letterSpacing: TextUnit = 0.sp,
    align: TextAlign? = null,
    maxLines: Int = Int.MAX_VALUE,
    modifier: Modifier = Modifier,
) = androidx.compose.material3.Text(
    text = text,
    color = color,
    fontSize = size,
    fontWeight = weight,
    // Figtree's stand-in has no Arabic, so an Arabic title goes to Methlama
    // rather than to whichever face the device happens to fall back on.
    fontFamily = if (arabicOnly(text)) Methlama else FontFamily.SansSerif,
    letterSpacing = letterSpacing,
    textAlign = align,
    maxLines = maxLines,
    lineHeight = size * 1.35f,
    overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
    modifier = modifier,
)

/** The page title: the one place the serif survives from v1. */
@Composable
fun PageTitle(text: String, modifier: Modifier = Modifier) = androidx.compose.material3.Text(
    text = text,
    color = palette.text,
    fontSize = 32.sp,
    fontWeight = FontWeight.SemiBold,
    fontFamily = FontFamily.Serif,
    modifier = modifier,
)

@Composable
fun Kicker(text: String, modifier: Modifier = Modifier) =
    T(text, 11.sp, palette.accent, FontWeight.Bold, 2.sp, modifier = modifier)

/** A section of the rack. Everything inside shares its 16dp inset. */
@Composable
fun V2Card(
    modifier: Modifier = Modifier,
    content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit,
) {
    Column(
        modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .background(palette.card, CardShape)
            .padding(horizontal = 16.dp, vertical = 18.dp),
        content = content,
    )
}

@Composable
fun SectionHeader(kicker: String, note: String? = null, trailing: (@Composable () -> Unit)? = null) {
    Row(
        Modifier.fillMaxWidth().defaultMinSize(minHeight = 28.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Kicker(kicker)
        if (trailing != null) trailing()
        else if (note != null) T(note, 12.sp, palette.faint, maxLines = 1)
    }
}

/** Full-width primary action. */
@Composable
fun PrimaryPill(label: String, accent: Boolean = true, onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .fillMaxWidth()
            .height(58.dp)
            .background(if (accent) p.accent else p.text, Pill)
            .clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) { T(label, 17.sp, if (p.dark) Color(0xFF08222D) else Color(0xFFF8F4F4), FontWeight.Bold) }
}

/** Quiet secondary action. */
@Composable
fun GhostPill(label: String, onClick: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(50.dp)
            .background(palette.card, Pill)
            .clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) { T(label, 15.sp, palette.dim, FontWeight.SemiBold) }
}

/** The 8D master switch, and every other on/off in v2. */
@Composable
fun PillSwitch(on: Boolean, onToggle: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .size(46.dp, 28.dp)
            .background(if (on) p.accent else p.well, Pill)
            .clickable { onToggle() },
        contentAlignment = if (on) Alignment.CenterEnd else Alignment.CenterStart,
    ) {
        Box(
            Modifier
                .padding(horizontal = 3.dp)
                .size(22.dp)
                .background(if (on) Color.White else p.ghost, CircleShape)
        )
    }
}

/** Segmented control, drawn as a pill with a raised pill inside it. */
@Composable
fun SegPill(options: List<String>, selected: String, modifier: Modifier = Modifier, onPick: (String) -> Unit) {
    val p = palette
    Row(
        modifier
            .background(p.card, Pill)
            .padding(4.dp),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        options.forEach { o ->
            val on = o == selected
            Box(
                Modifier
                    .weight(1f)
                    .height(38.dp)
                    .background(if (on) p.raised else Color.Transparent, Pill)
                    .clickable { onPick(o) },
                contentAlignment = Alignment.Center,
            ) {
                T(o, 14.sp, if (on) p.text else p.faint,
                    if (on) FontWeight.SemiBold else FontWeight.Normal, maxLines = 1)
            }
        }
    }
}

/** Preset / filter pill. Selected is solid accent, as on the canvas. */
@Composable
fun PillChip(label: String, selected: Boolean, onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .height(40.dp)
            .background(if (selected) p.accent else p.card, Pill)
            .clickable { onClick() }
            .padding(horizontal = 18.dp),
        contentAlignment = Alignment.Center,
    ) {
        T(label, 14.sp,
            if (selected) (if (p.dark) Color(0xFF08222D) else Color(0xFFF8F4F4)) else p.dim,
            if (selected) FontWeight.SemiBold else FontWeight.Normal, maxLines = 1)
    }
}

/**
 * A tile in a four-column grid: movement mode, character, anything picked from
 * a set. The glyph is drawn by the caller so the tile stays generic.
 */
@Composable
fun ChoiceTile(
    label: String,
    selected: Boolean,
    modifier: Modifier = Modifier,
    height: Dp = 70.dp,
    onClick: (() -> Unit)? = null,
    glyph: @Composable (Color) -> Unit,
) {
    val p = palette
    val tint = if (selected) p.deep else p.faint
    Column(
        modifier
            .height(height)
            .background(if (selected) p.presetTint else p.well, TileShape)
            .then(
                if (selected) Modifier.border(1.5.dp, p.accent.copy(alpha = .7f), TileShape)
                else Modifier
            )
            .then(if (onClick == null) Modifier else Modifier.clickable { onClick() }),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        glyph(tint)
        Spacer(Modifier.height(5.dp))
        T(label, 11.sp, tint, if (selected) FontWeight.SemiBold else FontWeight.Normal,
            align = TextAlign.Center, maxLines = 1)
    }
}

/**
 * The rotary knob the whole studio is built from.
 *
 * Vertical drag because a phone has far more of it than a 66dp control has
 * width: one screen height of travel covers the range twice over, which is
 * about right for a value you are tuning by ear. Double-tap returns the knob
 * to its default, so experimenting is never a one-way door.
 */
/**
 * A knob you turn.
 *
 * Grab it anywhere and move around the dial: the value follows how far your
 * finger travels round, not where it landed, so taking hold of one never makes
 * it jump. Three quarters of a turn covers the range, and the value lands on
 * whole steps -- a percent, a decibel -- rather than drifting, so the readout
 * stops flickering between neighbours. Double-tap puts it back to its default.
 *
 * The same behaviour as the desktop build, so a knob means the same thing on
 * both.
 */
@Composable
fun Knob(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    display: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    bipolar: Boolean = false,
    resetTo: Float? = null,
    step: Float = 0.01f,
    onChange: (Float) -> Unit,
) {
    val p = palette
    val span = range.endInclusive - range.start
    val latest by rememberUpdatedState(value)
    val onChangeNow by rememberUpdatedState(onChange)

    val fraction = ((value - range.start) / span).coerceIn(0f, 1f)
    val bipolarFraction = (value / range.endInclusive).coerceIn(-1f, 1f)

    Column(
        modifier.alpha(if (enabled) 1f else .4f),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(contentAlignment = Alignment.Center) {
            Canvas(
                Modifier
                    .size(66.dp)
                    // The gesture is read by hand rather than through a drag
                    // detector: the knob sits inside a vertically scrolling
                    // page, and only a child that consumes the touch from the
                    // first move keeps the page from scrolling out from under
                    // the finger.
                    .pointerInput(enabled, range, step, resetTo) {
                        if (!enabled) return@pointerInput
                        var lastTapMs = 0L
                        awaitEachGesture {
                            val centre = Offset(size.width / 2f, size.height / 2f)
                            val down = awaitFirstDown(requireUnconsumed = false)
                            down.consume()
                            var previous = angleOf(down.position - centre)
                            var carried = latest
                            var turned = false
                            while (true) {
                                val event = awaitPointerEvent()
                                val touch = event.changes.firstOrNull { it.id == down.id }
                                    ?: break
                                if (!touch.pressed) { touch.consume(); break }
                                val arm = touch.position - centre
                                // Too near the middle to aim with: a couple of
                                // millimetres either side of the centre is a
                                // different angle every frame.
                                if (arm.getDistance() > 14f) {
                                    val now = angleOf(arm)
                                    var moved = now - previous
                                    if (moved > 180f) moved -= 360f
                                    if (moved < -180f) moved += 360f
                                    previous = now
                                    if (kotlin.math.abs(moved) > 0.05f) turned = true
                                    carried = (carried + moved / 270f * span)
                                        .coerceIn(range.start, range.endInclusive)
                                    onChangeNow(landOn(carried, range, step))
                                }
                                touch.consume()
                            }
                            val at = System.currentTimeMillis()
                            if (!turned) {
                                if (at - lastTapMs < 320L) resetTo?.let { onChangeNow(it) }
                                lastTapMs = at
                            } else {
                                lastTapMs = 0L
                            }
                        }
                    }
            ) {
                val stroke = 6.dp.toPx()
                val inset = stroke / 2f + 1.dp.toPx()
                val arcSize = Size(size.width - inset * 2, size.height - inset * 2)
                val topLeft = Offset(inset, inset)

                // Track: 270 degrees, open at the bottom like a hardware knob.
                drawArc(
                    color = p.line, startAngle = 135f, sweepAngle = 270f, useCenter = false,
                    topLeft = topLeft, size = arcSize, style = Stroke(stroke, cap = StrokeCap.Round),
                )
                // Value: from the left for ordinary knobs, from 12 o'clock for EQ.
                val on = if (enabled) p.accent else p.ghost
                val head: Float
                if (bipolar) {
                    val sweep = 135f * bipolarFraction
                    head = 270f + sweep
                    if (kotlin.math.abs(sweep) > 0.5f) drawArc(
                        color = on,
                        startAngle = if (sweep >= 0) 270f else 270f + sweep,
                        sweepAngle = kotlin.math.abs(sweep), useCenter = false,
                        topLeft = topLeft, size = arcSize,
                        style = Stroke(stroke, cap = StrokeCap.Round),
                    )
                    drawLine(
                        p.faint,
                        Offset(size.width / 2f, 1.dp.toPx()),
                        Offset(size.width / 2f, 5.dp.toPx()),
                        1.5.dp.toPx(), cap = StrokeCap.Round,
                    )
                } else {
                    head = 135f + 270f * fraction
                    if (fraction > 0.001f) drawArc(
                        color = on, startAngle = 135f, sweepAngle = 270f * fraction,
                        useCenter = false, topLeft = topLeft, size = arcSize,
                        style = Stroke(stroke, cap = StrokeCap.Round),
                    )
                }
                // Cap.
                drawCircle(p.well, radius = size.width / 2f - 9.dp.toPx())

                // The head of the line: where the value has reached, and what
                // the thumb goes for.
                val radians = Math.toRadians(head.toDouble())
                val armLength = size.width / 2f - inset
                val at = Offset(
                    size.width / 2f + (kotlin.math.cos(radians) * armLength).toFloat(),
                    size.height / 2f + (kotlin.math.sin(radians) * armLength).toFloat(),
                )
                if (enabled) drawCircle(on.copy(alpha = .22f), stroke / 2f + 5.dp.toPx(), at)
                drawCircle(on, stroke / 2f + 2.5.dp.toPx(), at)
            }
            T(display, 13.sp, p.text, FontWeight.Bold, maxLines = 1)
        }
        if (label.isNotEmpty()) {
            Spacer(Modifier.height(5.dp))
            T(label, 12.sp, p.dim, align = TextAlign.Center, maxLines = 1)
        }
    }
}

/** Degrees clockwise from three o'clock, which is how the arcs are measured. */
private fun angleOf(arm: Offset): Float =
    Math.toDegrees(kotlin.math.atan2(arm.y.toDouble(), arm.x.toDouble())).toFloat()

/** The nearest whole step, so a value settles instead of drifting. */
private fun landOn(v: Float, range: ClosedFloatingPointRange<Float>, step: Float): Float {
    if (step <= 0f) return v
    val steps = kotlin.math.round((v - range.start) / step)
    return (range.start + steps * step).coerceIn(range.start, range.endInclusive)
}

/**
 * Four lanes to a row. Every knob takes Modifier.weight(1f); a row with three
 * knobs adds a Spacer with the same weight so the lanes stay aligned down the
 * page, which is what makes the rack read as one grid.
 */
@Composable
fun KnobRow(content: @Composable androidx.compose.foundation.layout.RowScope.() -> Unit) {
    Row(Modifier.fillMaxWidth().padding(top = 16.dp), content = content)
}

/** A labelled row inside a card: title on the left, control on the right. */
@Composable
fun SettingRow(label: String, sub: String? = null, trailing: @Composable () -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(top = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Column(Modifier.weight(1f)) {
            T(label, 15.sp, palette.text)
            if (sub != null) T(sub, 12.5.sp, palette.faint)
        }
        trailing()
    }
}

/** Floating pill navigation, inset from the edges and hovering over the page. */
@Composable
fun GlassTabBar(tabs: List<String>, current: String, onPick: (String) -> Unit) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 14.dp)
            .height(66.dp)
            // Opaque: the bar floats over the page, so anything showing through
            // it reads as a rendering fault rather than as depth.
            .shadow(18.dp, Pill)
            .background(if (p.dark) p.raised else p.card, Pill)
            .padding(6.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        tabs.forEach { t ->
            val on = t == current
            Column(
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .background(if (on) p.accent.copy(alpha = .16f) else Color.Transparent, Pill)
                    .clickable { onPick(t) },
                verticalArrangement = Arrangement.Center,
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                TabGlyph(t, if (on) p.deep else p.faint)
                Spacer(Modifier.height(3.dp))
                T(t, 11.sp, if (on) p.deep else p.faint,
                    if (on) FontWeight.Bold else FontWeight.Normal, maxLines = 1)
            }
        }
    }
}

@Composable
private fun TabGlyph(tab: String, tint: Color) {
    Canvas(Modifier.size(22.dp)) {
        val s = size.width
        val w = 1.8.dp.toPx()
        when (tab) {
            "Studio" -> {
                drawCircle(tint, s * .38f, style = Stroke(w))
                drawCircle(tint, s * .10f, style = Stroke(w))
                drawCircle(tint, s * .11f, center = Offset(s * .78f, s * .24f))
            }
            "Live" -> {
                drawCircle(tint, s * .09f, center = Offset(s / 2, s / 2))
                listOf(.24f, .40f).forEach { r ->
                    drawArc(tint, 135f, 90f, false,
                        topLeft = Offset(s / 2 - s * r, s / 2 - s * r),
                        size = Size(s * r * 2, s * r * 2),
                        style = Stroke(w, cap = StrokeCap.Round))
                    drawArc(tint, -45f, 90f, false,
                        topLeft = Offset(s / 2 - s * r, s / 2 - s * r),
                        size = Size(s * r * 2, s * r * 2),
                        style = Stroke(w, cap = StrokeCap.Round))
                }
            }
            "Player" -> {
                drawLine(tint, Offset(s * .38f, s * .78f), Offset(s * .38f, s * .18f), w,
                    cap = StrokeCap.Round)
                drawLine(tint, Offset(s * .82f, s * .66f), Offset(s * .82f, s * .10f), w,
                    cap = StrokeCap.Round)
                drawLine(tint, Offset(s * .38f, s * .18f), Offset(s * .82f, s * .10f), w,
                    cap = StrokeCap.Round)
                drawCircle(tint, s * .13f, center = Offset(s * .25f, s * .78f), style = Stroke(w))
                drawCircle(tint, s * .13f, center = Offset(s * .69f, s * .66f), style = Stroke(w))
            }
            else -> {
                drawCircle(tint, s * .42f, style = Stroke(w))
                drawLine(tint, Offset(s / 2, s * .44f), Offset(s / 2, s * .74f), w,
                    cap = StrokeCap.Round)
                drawCircle(tint, w * .9f, center = Offset(s / 2, s * .30f))
            }
        }
    }
}
