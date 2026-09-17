package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.eightd.music.AppState
import com.eightd.music.Character
import com.eightd.music.Mode
import com.eightd.music.Presets
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Studio: the orbit and every control on one page.
 *
 * The desktop build can afford two columns; a phone cannot, so the controls are
 * knobs four to a row instead of full-width sliders. That is the difference
 * between a page you scroll once and a page you scroll five times, and it keeps
 * reverb and movement close enough to tune against each other by ear.
 */
@Composable
fun StudioScreen(
    state: AppState,
    angle: Float,
    distance: Float,
    peakL: Float,
    peakR: Float,
    trail: List<Float>,
    sourceTitle: String,
    sourceDetail: String,
    sourceLive: Boolean,
    quality: String,
    engineInfo: String,
    onQuality: (String) -> Unit,
    onSavePreset: () -> Unit,
    bottomInset: androidx.compose.ui.unit.Dp,
) {
    val p = palette
    val v = state.params
    val running = state.engineOn && state.playing

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {

        /* ----------------------------------------------------- source + 8D */
        Row(
            Modifier.fillMaxWidth().padding(start = 20.dp, end = 20.dp, top = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Row(
                Modifier
                    .weight(1f, fill = false)
                    .padding(end = 12.dp)
                    .background(p.card, Pill)
                    .padding(horizontal = 14.dp, vertical = 9.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Box(
                    Modifier.size(8.dp).background(
                        if (sourceLive) p.deep else p.ghost, CircleShape
                    )
                )
                T(sourceTitle, 13.sp, p.text, FontWeight.SemiBold, maxLines = 1)
                if (sourceDetail.isNotEmpty()) {
                    T("·", 13.sp, p.faint)
                    T(sourceDetail, 13.sp, p.dim, maxLines = 1)
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(9.dp)) {
                T("8D", 12.sp, if (v.bypass) p.faint else p.deep, FontWeight.Bold, 1.4.sp)
                PillSwitch(!v.bypass) { state.apply(v.copy(bypass = !v.bypass)) }
            }
        }

        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 16.dp),
            verticalAlignment = Alignment.Bottom,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            PageTitle("Studio")
            T(state.presetName, 13.sp, p.faint, maxLines = 1)
        }

        /* ------------------------------------------------------------ orbit */
        OrbitV2(
            angle = angle,
            radius = v.radius,
            running = running,
            trail = trail,
            onScrub = if (v.mode == Mode.Static) { a -> state.apply(v.copy(manualAngle = a)) }
            else null,
        )

        ReadoutV2(angle, distance, peakL, peakR, running)

        /* ---------------------------------------------------------- presets */
        Row(
            Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState())
                .padding(start = 20.dp, end = 20.dp, top = 18.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            // Save sits with the presets: saving never means leaving the page
            // you are tuning on.
            Box(
                Modifier
                    .size(40.dp)
                    .background(p.well, CircleShape)
                    .clickable { onSavePreset() },
                contentAlignment = Alignment.Center,
            ) {
                Canvas(Modifier.size(16.dp)) {
                    val w = 1.8.dp.toPx()
                    drawLine(p.dim, Offset(size.width / 2, 0f),
                        Offset(size.width / 2, size.height), w, cap = StrokeCap.Round)
                    drawLine(p.dim, Offset(0f, size.height / 2),
                        Offset(size.width, size.height / 2), w, cap = StrokeCap.Round)
                }
            }
            state.userPresets.forEach { sp ->
                PillChip(sp.name, state.presetName == sp.name) {
                    state.presetName = sp.name; state.apply(sp.params)
                }
            }
            Presets.all.forEach { (name, prm) ->
                PillChip(name, state.presetName == name) {
                    state.presetName = name; state.apply(prm)
                }
            }
        }

        Spacer(Modifier.height(22.dp))

        /* --------------------------------------------------------- movement */
        V2Card {
            SectionHeader("MOVEMENT", trailing = {
                SegPill(
                    listOf("CW", "CCW"),
                    if (v.direction > 0) "CW" else "CCW",
                    Modifier.width(132.dp),
                ) { state.apply(v.copy(direction = if (it == "CW") 1 else -1)) }
            })
            Column(
                Modifier.padding(top = 14.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Mode.entries.chunked(4).forEach { row ->
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        row.forEach { m ->
                            ChoiceTile(
                                shortLabel(m), v.mode == m, Modifier.weight(1f),
                                onClick = { state.apply(v.copy(mode = m)) },
                            ) { tint -> Canvas(Modifier.size(26.dp)) { modeGlyph(m, tint) } }
                        }
                    }
                }
            }
            KnobRow {
                Knob("Speed rot/s", v.speed, 0.01f..1f, "%.2f".format(v.speed),
                    Modifier.weight(1f), resetTo = 0.12f) { state.apply(v.copy(speed = it)) }
                Knob("Distance", v.radius, 0.2f..3f, "%.2f m".format(v.radius),
                    Modifier.weight(1f), resetTo = 1f) { state.apply(v.copy(radius = it)) }
                Knob("Intensity", v.depth, 0f..1f, pct(v.depth),
                    Modifier.weight(1f), resetTo = 0.85f) { state.apply(v.copy(depth = it)) }
                Knob("Smooth", v.smoothness, 0f..1f, pct(v.smoothness),
                    Modifier.weight(1f), resetTo = 0.35f) { state.apply(v.copy(smoothness = it)) }
            }
            if (v.mode == Mode.Static) {
                T("Drag the dot on the orbit to place the sound.",
                    13.sp, p.faint, modifier = Modifier.padding(top = 12.dp))
            }
        }

        Spacer(Modifier.height(12.dp))

        /* ------------------------------------------------------------ space */
        V2Card {
            SectionHeader("SPACE", "Reverb & stereo")
            KnobRow {
                Knob("Reverb", v.reverbMix, 0f..1f, pct(v.reverbMix),
                    Modifier.weight(1f), resetTo = 0.18f) { state.apply(v.copy(reverbMix = it)) }
                Knob("Room", v.reverbSize, 0f..1f, pct(v.reverbSize), Modifier.weight(1f),
                    enabled = v.reverbMix > 0f, resetTo = 0.6f) {
                    state.apply(v.copy(reverbSize = it))
                }
                Knob("Damping", v.reverbDamp, 0f..1f, pct(v.reverbDamp), Modifier.weight(1f),
                    enabled = v.reverbMix > 0f, resetTo = 0.45f) {
                    state.apply(v.copy(reverbDamp = it))
                }
                Knob("Width", v.width, 0f..2f, pct(v.width), Modifier.weight(1f),
                    resetTo = 1f) { state.apply(v.copy(width = it)) }
            }
        }

        Spacer(Modifier.height(12.dp))

        /* ------------------------------------------------------------- echo */
        V2Card {
            SectionHeader(
                "ECHO",
                if (v.delayMix == 0f) "Off · raise Delay" else "Repeats travel with the orbit",
            )
            KnobRow {
                Knob("Delay", v.delayMix, 0f..1f, pct(v.delayMix),
                    Modifier.weight(1f), resetTo = 0f) { state.apply(v.copy(delayMix = it)) }
                Knob("Time ms", v.delayTime, 0.02f..0.9f, "${(v.delayTime * 1000).roundToInt()}",
                    Modifier.weight(1f), enabled = v.delayMix > 0f, resetTo = 0.28f,
                    step = 0.005f) {
                    state.apply(v.copy(delayTime = it))
                }
                Knob("Feedback", v.delayFeedback, 0f..0.95f, pct(v.delayFeedback),
                    Modifier.weight(1f), enabled = v.delayMix > 0f, resetTo = 0.35f) {
                    state.apply(v.copy(delayFeedback = it))
                }
                Spacer(Modifier.weight(1f))
            }
        }

        Spacer(Modifier.height(12.dp))

        /* -------------------------------------------------------- character */
        V2Card {
            SectionHeader("CHARACTER", "Colour on the source")
            Row(
                Modifier.padding(top = 14.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Character.entries.forEach { c ->
                    ChoiceTile(
                        shortLabel(c), v.character == c, Modifier.weight(1f),
                        onClick = { state.apply(v.copy(character = c)) },
                    ) { tint -> Canvas(Modifier.size(26.dp)) { characterGlyph(c, tint) } }
                }
                Knob("Amount", v.characterAmount, 0f..1f, pct(v.characterAmount),
                    Modifier.weight(1f), enabled = v.character != Character.Clean,
                    resetTo = 1f) { state.apply(v.copy(characterAmount = it)) }
            }
        }

        Spacer(Modifier.height(12.dp))

        /* -------------------------------------------------------------- eq */
        V2Card {
            SectionHeader("EQUALISER", "±12 dB")
            KnobRow {
                Knob("Bass", v.eqBass, -12f..12f, db(v.eqBass), Modifier.weight(1f),
                    bipolar = true, resetTo = 0f, step = 1f) { state.apply(v.copy(eqBass = it)) }
                Knob("Mid", v.eqMid, -12f..12f, db(v.eqMid), Modifier.weight(1f),
                    bipolar = true, resetTo = 0f, step = 1f) { state.apply(v.copy(eqMid = it)) }
                Knob("Treble", v.eqTreble, -12f..12f, db(v.eqTreble), Modifier.weight(1f),
                    bipolar = true, resetTo = 0f, step = 1f) { state.apply(v.copy(eqTreble = it)) }
                Knob("Output", v.outputGain, 0f..1.5f, pct(v.outputGain),
                    Modifier.weight(1f), resetTo = 0.9f) { state.apply(v.copy(outputGain = it)) }
            }
        }

        Spacer(Modifier.height(12.dp))

        /* ----------------------------------------------------------- engine */
        V2Card {
            SectionHeader("ENGINE", engineInfo.ifEmpty { "C++ DSP" })
            SettingRow("Quality") {
                SegPill(listOf("Safe", "Balanced", "Max"), quality, Modifier.width(210.dp)) {
                    onQuality(it)
                }
            }
            SettingRow("Pause orbit when silent") {
                PillSwitch(v.pauseWhenSilent) {
                    state.apply(v.copy(pauseWhenSilent = !v.pauseWhenSilent))
                }
            }
            SettingRow("Reset studio to defaults") {
                Box(
                    Modifier
                        .background(p.well, Pill)
                        .clickable {
                            state.presetName = "Classic 8D"
                            state.apply(Presets.all[0].second)
                        }
                        .padding(horizontal = 16.dp, vertical = 9.dp),
                    contentAlignment = Alignment.Center,
                ) { T("Reset", 13.sp, p.accent, FontWeight.Bold) }
            }
        }

        Spacer(Modifier.height(bottomInset))
    }
}

/* ------------------------------------------------------------------ orbit */

/**
 * The orbit, drawn from the DSP's telemetry, with rings at one, two and three
 * metres so the Distance knob reads as a place rather than a number.
 */
@Composable
private fun OrbitV2(
    angle: Float,
    radius: Float,
    running: Boolean,
    trail: List<Float>,
    onScrub: ((Float) -> Unit)?,
) {
    val p = palette
    Box(Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 6.dp)) {
        // The light behind the orbit, as on the canvas: cyan where the effect
        // lives, magenta where the source is travelling.
        Glow(
            Modifier.matchParentSize(),
            cyan = .13f, magenta = .15f,
            cyanCentre = Offset(.24f, .46f), magentaCentre = Offset(.74f, .30f),
        )
        Canvas(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .then(
                    if (onScrub == null) Modifier else Modifier.pointerInput(Unit) {
                        detectDragGestures { change, _ ->
                            change.consume()
                            val cx = size.width / 2f
                            val cy = size.height / 2f
                            onScrub(atan2(change.position.x - cx, cy - change.position.y))
                        }
                    }
                )
        ) {
            val cx = size.width / 2f
            val cy = size.height / 2f
            val r = min(cx, cy) - 4.dp.toPx()

            drawCircle(p.line.copy(alpha = .7f), r, Offset(cx, cy), style = Stroke(1.dp.toPx()))
            drawCircle(p.line.copy(alpha = .45f), r * 2f / 3f, Offset(cx, cy),
                style = Stroke(1.dp.toPx()))
            drawCircle(p.line.copy(alpha = .45f), r / 3f, Offset(cx, cy),
                style = Stroke(1.dp.toPx()))
            drawLine(p.line.copy(alpha = .35f), Offset(cx, cy - r), Offset(cx, cy + r),
                1.dp.toPx())
            drawLine(p.line.copy(alpha = .35f), Offset(cx - r, cy), Offset(cx + r, cy),
                1.dp.toPx())

            // The orbit the current radius describes: 3 m is the outer ring.
            val orbitR = r * (radius.coerceIn(0.2f, 3f) / 3f)
            drawCircle(
                color = if (running) p.accent.copy(alpha = .55f) else p.orbitOff,
                radius = orbitR, center = Offset(cx, cy),
                style = Stroke(
                    1.5.dp.toPx(),
                    pathEffect = PathEffect.dashPathEffect(
                        floatArrayOf(1.dp.toPx(), 7.dp.toPx())
                    ),
                    cap = StrokeCap.Round,
                ),
            )

            // Where the source has just been: the trail is telemetry, not an
            // animation, so it stops the moment the audio does.
            trail.forEachIndexed { i, a ->
                val t = (i + 1f) / (trail.size + 1f)
                drawCircle(
                    color = p.motion.copy(alpha = .08f + .35f * t),
                    radius = (2f + 4f * t).dp.toPx(),
                    center = polar(cx, cy, orbitR, a),
                )
            }

            val dot = polar(cx, cy, orbitR, angle)
            val dotColor = if (running) p.motion else p.dotOff
            drawCircle(dotColor.copy(alpha = .16f), 22.dp.toPx(), dot)
            drawCircle(dotColor, 8.5.dp.toPx(), dot)

            // The listener.
            drawCircle(p.raised, r * .10f, Offset(cx, cy))
            drawCircle(p.raised, r * .028f, Offset(cx - r * .10f, cy))
            drawCircle(p.raised, r * .028f, Offset(cx + r * .10f, cy))
        }
        listOf(
            "FRONT" to Alignment.TopCenter, "BACK" to Alignment.BottomCenter,
            "L" to Alignment.CenterStart, "R" to Alignment.CenterEnd,
        ).forEach { (label, align) ->
            T(label, 10.sp, p.ghost, FontWeight.Bold, 2.sp,
                modifier = Modifier.align(align))
        }
    }
}

private fun polar(cx: Float, cy: Float, r: Float, angle: Float) =
    Offset(cx + r * sin(angle), cy - r * cos(angle))

@Composable
private fun ReadoutV2(angle: Float, distance: Float, peakL: Float, peakR: Float, running: Boolean) {
    val p = palette
    val deg = Math.toDegrees(angle.toDouble()).let { d ->
        var x = d % 360.0
        if (x > 180) x -= 360.0
        if (x < -180) x += 360.0
        x.roundToInt()
    }
    val side = when {
        abs(deg) < 12 -> "front"
        abs(deg) > 168 -> "behind"
        deg > 0 -> if (abs(deg) < 100) "front-right" else "behind right"
        else -> if (abs(deg) < 100) "front-left" else "behind left"
    }
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(Modifier.weight(1f), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            T("${if (deg >= 0) "+" else ""}$deg°", 14.sp, p.motion, FontWeight.Bold)
            T("%.2f m".format(distance), 14.sp, p.dim)
            T(side, 14.sp, p.faint, maxLines = 1)
        }
        Column(Modifier.width(110.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
            MeterV2("L", peakL, running)
            MeterV2("R", peakR, running)
        }
    }
}

/**
 * Where a peak sits on the bar.
 *
 * The peak itself is a plain amplitude, and a bar drawn straight from it reads
 * as broken: music mixed to peak at -20 dBFS would fill a tenth of it and never
 * appear to move. Meters are read in decibels for that reason, so this is a
 * 60 dB scale — silence at the left, full scale at the right, and ordinary
 * listening in the top third where it can actually be seen. The same scale as
 * the desktop and Windows builds.
 */
private fun meterScale(peak: Float): Float {
    if (peak <= 1e-4f) return 0f
    val db = 20f * kotlin.math.log10(peak)
    return ((db + 60f) / 60f).coerceIn(0f, 1f)
}

@Composable
private fun MeterV2(label: String, level: Float, running: Boolean) {
    val p = palette
    Row(verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        T(label, 9.sp, p.ghost, FontWeight.Bold)
        Canvas(Modifier.weight(1f).height(4.dp)) {
            val h = size.height
            drawRoundRect(p.well, size = size,
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(h / 2))
            val w = meterScale(level) * size.width
            if (w > 1f) drawRoundRect(
                if (running) p.accent else p.meterOff,
                size = Size(w, h),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(h / 2),
            )
        }
    }
}

/* ----------------------------------------------------------------- glyphs */

private fun shortLabel(m: Mode) = when (m) {
    Mode.Circular -> "Circle"
    Mode.PingPong -> "Ping-pong"
    Mode.Pendulum -> "Pendulum"
    Mode.Linear -> "Linear"
    Mode.Figure8 -> "Figure 8"
    Mode.Spiral -> "Spiral"
    Mode.Random -> "Random"
    Mode.Static -> "Static"
}

private fun shortLabel(c: Character) = when (c) {
    Character.Clean -> "Clean"
    Character.Slowed -> "Slowed"
    Character.Radio -> "Old radio"
}

/** Each mode draws the path it actually takes, so the grid reads at a glance. */
internal fun DrawScope.modeGlyph(mode: Mode, tint: Color) {
    val s = size.width
    val w = 1.7.dp.toPx()
    val stroke = Stroke(w, cap = StrokeCap.Round)
    fun p(x: Float, y: Float) = Offset(s * x, s * y)
    when (mode) {
        Mode.Circular -> {
            drawCircle(tint, s * .34f, style = stroke)
            drawCircle(tint, s * .08f, center = p(.5f, .16f))
        }
        Mode.PingPong -> {
            drawLine(tint, p(.12f, .5f), p(.88f, .5f), w, cap = StrokeCap.Round)
            drawLine(tint, p(.28f, .34f), p(.12f, .5f), w, cap = StrokeCap.Round)
            drawLine(tint, p(.28f, .66f), p(.12f, .5f), w, cap = StrokeCap.Round)
            drawLine(tint, p(.72f, .34f), p(.88f, .5f), w, cap = StrokeCap.Round)
            drawLine(tint, p(.72f, .66f), p(.88f, .5f), w, cap = StrokeCap.Round)
        }
        Mode.Pendulum -> {
            drawArc(tint, 0f, 180f, false, topLeft = p(.12f, .12f),
                size = Size(s * .76f, s * .56f), style = stroke)
            drawCircle(tint, s * .08f, center = p(.88f, .4f))
        }
        Mode.Linear -> {
            drawLine(tint, p(.14f, .78f), p(.86f, .24f), w, cap = StrokeCap.Round)
            drawCircle(tint, s * .08f, center = p(.86f, .24f))
        }
        Mode.Figure8 -> {
            drawCircle(tint, s * .20f, center = p(.30f, .5f), style = stroke)
            drawCircle(tint, s * .20f, center = p(.70f, .5f), style = stroke)
        }
        Mode.Spiral -> {
            drawCircle(tint, s * .36f, style = stroke)
            drawCircle(tint, s * .22f, style = stroke)
            drawCircle(tint, s * .09f, style = stroke)
        }
        Mode.Random -> {
            val pts = listOf(p(.12f, .70f), p(.32f, .38f), p(.48f, .62f),
                p(.66f, .24f), p(.78f, .54f), p(.9f, .36f))
            pts.zipWithNext { a, b -> drawLine(tint, a, b, w, cap = StrokeCap.Round) }
        }
        Mode.Static -> {
            drawCircle(tint, s * .34f, style = Stroke(
                w, pathEffect = PathEffect.dashPathEffect(
                    floatArrayOf(2.dp.toPx(), 3.dp.toPx())
                )
            ))
            drawCircle(tint, s * .10f, center = p(.74f, .26f))
        }
    }
}

internal fun DrawScope.characterGlyph(c: Character, tint: Color) {
    val s = size.width
    val w = 1.7.dp.toPx()
    fun p(x: Float, y: Float) = Offset(s * x, s * y)
    when (c) {
        Character.Clean -> drawLine(tint, p(.12f, .5f), p(.88f, .5f), w, cap = StrokeCap.Round)
        Character.Slowed -> {
            listOf(.38f, .62f).forEach { y ->
                drawArc(tint, 180f, 180f, false, topLeft = p(.12f, y - .14f),
                    size = Size(s * .38f, s * .28f), style = Stroke(w, cap = StrokeCap.Round))
                drawArc(tint, 0f, 180f, false, topLeft = p(.50f, y - .14f),
                    size = Size(s * .38f, s * .28f), style = Stroke(w, cap = StrokeCap.Round))
            }
        }
        Character.Radio -> {
            drawRoundRect(
                tint, topLeft = p(.12f, .34f), size = Size(s * .76f, s * .52f),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(s * .14f),
                style = Stroke(w),
            )
            drawLine(tint, p(.34f, .34f), p(.68f, .12f), w, cap = StrokeCap.Round)
            drawCircle(tint, s * .11f, center = p(.66f, .60f), style = Stroke(w))
            drawLine(tint, p(.26f, .52f), p(.40f, .52f), w, cap = StrokeCap.Round)
            drawLine(tint, p(.26f, .68f), p(.40f, .68f), w, cap = StrokeCap.Round)
        }
    }
}

private fun pct(v: Float) = "${(v * 100).roundToInt()}%"
private fun db(v: Float) = if (v > 0) "+${v.roundToInt()}" else "${v.roundToInt()}"
