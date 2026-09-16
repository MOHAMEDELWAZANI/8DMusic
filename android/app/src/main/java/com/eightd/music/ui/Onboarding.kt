package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.eightd.music.Character
import com.eightd.music.Mode

/*
 * The presentations: one Welcome flow at first launch, and a short tour the
 * first time each tab is opened. All four share the template from the canvas —
 * progress bars, skip, a picture drawn in the app's own language, two lines of
 * text and one clear action — so the second tour teaches nothing new about how
 * a tour works.
 */

@Composable
private fun TourPage(
    index: Int,
    count: Int,
    kicker: String,
    title: String,
    body: String,
    nextLabel: String,
    accentNext: Boolean = false,
    onSkip: (() -> Unit)? = null,
    onBack: (() -> Unit)? = null,
    onNext: () -> Unit,
    illustration: @Composable () -> Unit,
) {
    val p = palette
    Column(Modifier.fillMaxSize().background(p.ground).padding(top = 16.dp)) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Row(Modifier.weight(1f), horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                repeat(count) { i ->
                    Box(
                        Modifier
                            .weight(1f)
                            .height(4.dp)
                            .background(
                                if (i <= index) p.text else p.text.copy(alpha = .14f),
                                CircleShape,
                            )
                    )
                }
            }
            if (onSkip != null) {
                T("Skip", 14.sp, p.faint, FontWeight.SemiBold,
                    modifier = Modifier.clickable { onSkip() })
            }
        }

        Box(Modifier.weight(1f).fillMaxWidth(), Alignment.Center) {
            Glow(
                Modifier.matchParentSize(),
                cyan = .11f, magenta = .13f,
                cyanCentre = Offset(.26f, .52f), magentaCentre = Offset(.74f, .3f),
            )
            illustration()
        }

        Column(Modifier.padding(horizontal = 28.dp)) {
            Kicker(kicker)
            T(title, 30.sp, p.text, FontWeight.Bold, modifier = Modifier.padding(top = 10.dp))
            T(body, 16.sp, p.dim, modifier = Modifier.padding(top = 12.dp))
        }

        Row(
            Modifier.fillMaxWidth().padding(20.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (onBack != null) {
                Box(
                    Modifier.size(58.dp).background(p.card, CircleShape).clickable { onBack() },
                    contentAlignment = Alignment.Center,
                ) { T("‹", 26.sp, p.text, FontWeight.Bold) }
            }
            Box(Modifier.weight(1f)) {
                PrimaryPill(nextLabel, accent = accentNext) { onNext() }
            }
        }
    }
}

/* --------------------------------------------------------------- welcome */

/**
 * First launch. Ends on the account page, which is skippable by design: the
 * effect runs entirely on the phone, so an account is an offer, never a gate.
 */
@Composable
fun WelcomeFlow(onFinish: () -> Unit, onAccountsNotReady: () -> Unit) {
    val p = palette
    var page by remember { mutableStateOf(0) }
    val back: (() -> Unit)? = if (page == 0) null else ({ page -= 1 })

    when (page) {
        0 -> TourPage(
            0, 5, "WELCOME TO 8D MUSIC", "Sound that moves\naround you",
            "8D Music makes any song travel around your head, live, while it plays.",
            "Get started", onSkip = onFinish, onBack = back, onNext = { page += 1 },
        ) { WelcomeMark() }

        1 -> TourPage(
            1, 5, "ONE APP, EVERY SOUND", "Everything your\nphone plays",
            "Live brings in apps like YouTube and Spotify. Player plays your own files. " +
                "Studio shapes the sound for both.",
            "Next", onSkip = onFinish, onBack = back, onNext = { page += 1 },
        ) { SourcesMark() }

        2 -> TourPage(
            2, 5, "BEFORE YOU START", "Put your\nheadphones on",
            "8D gives each ear its own version of the sound. Speakers mix the two back " +
                "together, so the effect disappears.",
            "Next", onSkip = onFinish, onBack = back, onNext = { page += 1 },
        ) { HeadphonesMark() }

        3 -> TourPage(
            3, 5, "PRIVATE BY DESIGN", "Nothing leaves\nyour phone",
            "Every effect runs on your device, in the same C++ engine as the Linux and " +
                "Windows versions. Nothing is uploaded, and you don't need an account.",
            "Next", onSkip = onFinish, onBack = back, onNext = { page += 1 },
        ) { PrivacyMark() }

        else -> Column(Modifier.fillMaxSize().background(p.ground).padding(top = 16.dp)) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp),
                horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                repeat(5) {
                    Box(Modifier.weight(1f).height(4.dp).background(p.text, CircleShape))
                }
            }
            Box(Modifier.weight(1f).fillMaxWidth(), Alignment.Center) { DashboardMark() }
            Column(Modifier.padding(horizontal = 28.dp)) {
                Kicker("OPTIONAL")
                T("Help build\n8D Music", 30.sp, p.text, FontWeight.Bold,
                    modifier = Modifier.padding(top = 10.dp))
                T("Sign in to try early versions, send feedback and see your dashboard. " +
                    "The app works fully without an account.",
                    16.sp, p.dim, modifier = Modifier.padding(top = 12.dp))
            }
            Column(
                Modifier.fillMaxWidth().padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                AccountButton("Continue with Google", onAccountsNotReady)
                AccountButton("Continue with GitHub", onAccountsNotReady)
                Box(
                    Modifier.fillMaxWidth().height(44.dp).clickable { onFinish() },
                    contentAlignment = Alignment.Center,
                ) { T("Skip, take me to Studio", 15.5.sp, p.accent, FontWeight.Bold) }
            }
        }
    }
}

@Composable
private fun AccountButton(label: String, onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .fillMaxWidth()
            .height(56.dp)
            .background(p.card, Pill)
            .clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) { T(label, 16.sp, p.text, FontWeight.Bold) }
}

/* ----------------------------------------------------------- studio tour */

@Composable
fun StudioTour(onFinish: () -> Unit) {
    var page by remember { mutableStateOf(0) }
    // The knob page is a real knob: reading about turning it teaches less in a
    // paragraph than one second of turning it does.
    var demo by remember { mutableStateOf(0.8f) }
    val back: (() -> Unit)? = if (page == 0) null else ({ page -= 1 })
    val next: () -> Unit = { if (page == 6) onFinish() else page += 1 }

    when (page) {
        0 -> TourPage(0, 7, "STUDIO 8D · HOW IT WORKS", "The pink dot\nis the sound",
            "It travels around your head, and you hear it move. In Static mode you can drag " +
                "it to park the sound wherever you like.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { OrbitMark() }

        1 -> TourPage(1, 7, "MOVEMENT", "Eight ways\nto move",
            "Circle, ping-pong, pendulum, figure 8 and more. Tap one and the orbit changes " +
                "straight away. CW / CCW flips the direction.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { ModesMark() }

        2 -> TourPage(2, 7, "CONTROLS", "Turn a knob",
            "Hold a knob and move your finger around it, the way you would a real one. It " +
                "turns by as much as you turn, so it never jumps. Double-tap one to put it " +
                "back where it started.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) {
            KnobLesson(demo) { demo = it }
        }

        3 -> TourPage(3, 7, "SPACE", "Give the sound\na room",
            "Reverb adds the room, Room sets how big it is, and Damping softens the walls. " +
                "Width spreads the sound further apart.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { RoomMark() }

        4 -> TourPage(4, 7, "ECHO", "Echoes that\nfly around you",
            "Delay repeats the sound, and each repeat follows the orbit. Time sets the gap " +
                "between repeats, and Feedback sets how many you hear.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { EchoMark() }

        5 -> TourPage(5, 7, "CHARACTER & EQUALISER", "Colour and tone",
            "Character sets a mood: clean, slowed & sad, or old radio. The equaliser knobs " +
                "start in the middle — turn right to boost, left to cut.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { CharacterMark() }

        else -> TourPage(6, 7, "PRESETS & COMPARE", "Save it, then\ncompare",
            "A preset stores every knob at once. Tap + to save your own, and flip the 8D " +
                "switch to hear the difference.",
            "Start in Studio", accentNext = true, onBack = back, onNext = next) { CompareMark() }
    }
}

/* ------------------------------------------------------------- live tour */

@Composable
fun LiveTour(onFinish: () -> Unit) {
    var page by remember { mutableStateOf(0) }
    val back: (() -> Unit)? = if (page == 0) null else ({ page -= 1 })
    val next: () -> Unit = { if (page == 3) onFinish() else page += 1 }

    when (page) {
        0 -> TourPage(0, 4, "LIVE", "8D for the apps\nyou already use",
            "Live takes the sound other apps play, sends it through Studio, and passes it on " +
                "to your headphones. There's nothing to download or convert.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { AppsOrbitMark() }

        1 -> TourPage(1, 4, "ROUTE 1 · NO SETUP", "Direct capture",
            "Works right away with apps that allow their sound to be captured, like YouTube. " +
                "Some apps, like Spotify, don't allow it.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { CaptureListMark() }

        2 -> TourPage(2, 4, "ROUTE 2 · SHIZUKU", "System-wide:\nevery app",
            "Set up Shizuku once and Live reaches every app. It also quiets the original " +
                "sound, so you only hear the 8D version.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { StepsRingMark() }

        else -> TourPage(3, 4, "GOOD TO KNOW", "One source\nat a time",
            "Turning Live on stops Player, and the other way round. Some apps, like Discord " +
                "and most games, never say what's playing, even when you can hear them.",
            "Choose how Live listens", accentNext = true, onBack = back, onNext = next) {
            SwitchesMark()
        }
    }
}

/* ----------------------------------------------------------- player tour */

@Composable
fun PlayerTour(onFinish: () -> Unit) {
    var page by remember { mutableStateOf(0) }
    val back: (() -> Unit)? = if (page == 0) null else ({ page -= 1 })
    val next: () -> Unit = { if (page == 3) onFinish() else page += 1 }

    when (page) {
        0 -> TourPage(0, 4, "PLAYER", "Your music,\nin 8D",
            "Play the songs saved on your phone. Nothing streams and nothing gets converted: " +
                "they go straight through Studio to your headphones.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { CoversMark() }

        1 -> TourPage(1, 4, "PLAYLISTS", "Playlists that\nremember a preset",
            "Give a playlist its own 8D preset: night drives in Slow Orbit, workouts in " +
                "Extreme Spin. Studio switches for you when the playlist starts.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { PlaylistMark() }

        2 -> TourPage(2, 4, "NOW PLAYING", "8D is one\ntap away",
            "The 8D bar under every song shows the preset. Tap it to open Studio without " +
                "stopping the music.",
            "Next", onSkip = onFinish, onBack = back, onNext = next) { DockMark() }

        else -> TourPage(3, 4, "LAST STEP", "Let it see\nyour music",
            "8D Music reads the audio files already on this phone. Nothing is copied and " +
                "nothing is sent anywhere.",
            "Turn on Player", accentNext = true, onBack = back, onNext = next) { RecordTourMark() }
    }
}

/**
 * The knob lesson: a real knob, the way it moves, and the reset.
 *
 * Reading "turn it" teaches less than one second of turning it, so the knob
 * here is live and the arrow only says which way round.
 */
@Composable
private fun KnobLesson(value: Float, onChange: (Float) -> Unit) {
    val p = palette
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(contentAlignment = Alignment.Center) {
            Canvas(Modifier.size(148.dp)) { turnArrow(p.deep) }
            Knob("Intensity", value, 0f..1f, "${(value * 100).toInt()}%", resetTo = 0.8f,
                onChange = onChange)
        }
        Box(
            Modifier
                .padding(top = 22.dp)
                .background(p.card, Pill)
                .padding(horizontal = 14.dp, vertical = 7.dp),
        ) { T("Double-tap to reset", 12.5.sp, p.dim, FontWeight.SemiBold) }
    }
}

/** The way round, drawn as an arc outside the knob with a head on its end. */
private fun androidx.compose.ui.graphics.drawscope.DrawScope.turnArrow(tint: Color) {
    val s = size.width
    val r = s / 2f - 3.dp.toPx()
    val w = 2.6.dp.toPx()
    val start = 196f
    val sweep = 128f
    drawArc(
        tint.copy(alpha = .55f), startAngle = start, sweepAngle = sweep, useCenter = false,
        topLeft = Offset(s / 2f - r, s / 2f - r), size = Size(r * 2, r * 2),
        style = Stroke(w, cap = StrokeCap.Round),
    )
    // the head, pointing the way the arc travels
    val end = Math.toRadians((start + sweep).toDouble())
    val tip = Offset(
        s / 2f + (kotlin.math.cos(end) * r).toFloat(),
        s / 2f + (kotlin.math.sin(end) * r).toFloat(),
    )
    val along = Offset(-kotlin.math.sin(end).toFloat(), kotlin.math.cos(end).toFloat())
    val out = Offset(kotlin.math.cos(end).toFloat(), kotlin.math.sin(end).toFloat())
    val head = 7.dp.toPx()
    val path = androidx.compose.ui.graphics.Path().apply {
        moveTo(tip.x + along.x * head, tip.y + along.y * head)
        lineTo(tip.x - along.x * head * .2f + out.x * head * .8f,
               tip.y - along.y * head * .2f + out.y * head * .8f)
        lineTo(tip.x - along.x * head * .2f - out.x * head * .8f,
               tip.y - along.y * head * .2f - out.y * head * .8f)
        close()
    }
    drawPath(path, tint)
}

/**
 * The painted covers from the canvas.
 *
 * The presentation talks about album art before the app has any, so the three
 * covers from the doop frames are drawn rather than shipped as images: the same
 * gradients, in the same order, so the page reads as the design does.
 */
enum class CoverArt { Afterglow, NightDrive, Northern }

@Composable
fun PaintedCover(art: CoverArt, size: Dp, corner: Dp, modifier: Modifier = Modifier) {
    // Clipped, so the light can be painted as free shapes and still end at the
    // cover's rounded edge.
    Canvas(modifier.size(size).clip(RoundedCornerShape(corner))) {
        val w = this.size.width
        val h = this.size.height
        val radius = androidx.compose.ui.geometry.CornerRadius(corner.toPx())
        fun blob(color: Color, cx: Float, cy: Float, r: Float, alpha: Float = 1f) {
            drawRoundRect(
                brush = Brush.radialGradient(
                    colors = listOf(color.copy(alpha = alpha), Color.Transparent),
                    center = Offset(w * cx, h * cy),
                    radius = w * r,
                ),
                size = this.size,
                cornerRadius = radius,
            )
        }
        when (art) {
            CoverArt.Afterglow -> {
                drawRoundRect(Color(0xFF3A12B8), size = this.size, cornerRadius = radius)
                blob(Color(0xFF7A4CFF), .92f, .04f, .60f)
                blob(Color(0xFFFF4F8A), .62f, .42f, .52f)
                blob(Color(0xFFFF9A3C), .34f, .72f, .40f)
            }
            CoverArt.NightDrive -> {
                drawRoundRect(Color(0xFF07070A), size = this.size, cornerRadius = radius)
                // Two soft bands over black: headlights on a night road. Ovals,
                // not rectangles, so they fade out at the sides.
                fun band(color: Color, cy: Float, halfHeight: Float, spread: Float) {
                    val top = h * (cy - halfHeight)
                    val boxH = h * halfHeight * 2
                    drawOval(
                        brush = Brush.radialGradient(
                            colors = listOf(color, color.copy(alpha = .35f), Color.Transparent),
                            center = Offset(w * .48f, top + boxH / 2),
                            radius = w * spread,
                        ),
                        topLeft = Offset(-w * .25f, top),
                        size = androidx.compose.ui.geometry.Size(w * 1.5f, boxH),
                    )
                }
                band(Color(0xFF3A6AFF), .45f, .14f, .55f)
                band(Color(0xFFFF9A4A), .58f, .10f, .40f)
                blob(Color(0xFF1B2A66), .82f, .94f, .42f)
            }
            CoverArt.Northern -> {
                drawRoundRect(Color(0xFF1A20A0), size = this.size, cornerRadius = radius)
                blob(Color(0xFF20E080), .70f, .30f, .48f)
                blob(Color(0xFF5A9AFF), .22f, .78f, .52f)
            }
        }
    }
}

/* ------------------------------------------------------------ marks */

@Composable private fun WelcomeMark() {
    val p = palette
    Box(contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(300.dp)) {
            val c = Offset(size.width / 2, size.height / 2)
            val r = size.width / 2 - 2.dp.toPx()
            drawCircle(p.line.copy(alpha = .6f), r, c, style = Stroke(1.dp.toPx()))
            dashedOrbit(p, c, r * .72f)
            drawArc(
                p.motion.copy(alpha = .35f), 225f, 55f, false,
                topLeft = Offset(c.x - r * .72f, c.y - r * .72f),
                size = Size(r * 1.44f, r * 1.44f),
                style = Stroke(6.dp.toPx(), cap = StrokeCap.Round),
            )
            val dot = Offset(c.x + r * .72f * kotlin.math.cos(Math.toRadians(-80.0)).toFloat(),
                c.y + r * .72f * kotlin.math.sin(Math.toRadians(-80.0)).toFloat())
            drawCircle(p.motion.copy(alpha = .25f), 26.dp.toPx(), dot)
            drawCircle(p.motion, 11.dp.toPx(), dot)
        }
        Box(
            Modifier.size(104.dp).background(p.card, RoundedCornerShape(32.dp)),
            contentAlignment = Alignment.Center,
        ) { Logo(74.dp, p.text, p.accent, p.motion) }
    }
}

@Composable private fun SourcesMark() {
    val p = palette
    OrbitOfApps(diameter = 290.dp, showLocalTile = true) {
        Box(
            Modifier.size(104.dp).background(p.card, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Canvas(Modifier.size(52.dp)) { headphones(p, p.accent, p.motion) }
        }
    }
}

/**
 * The listener with other apps orbiting them, drawn with those apps' real
 * icons when they are installed.
 */
@Composable
private fun OrbitOfApps(
    diameter: Dp,
    showLocalTile: Boolean = false,
    centre: @Composable () -> Unit,
) {
    val p = palette
    val apps = rememberInstalledExampleApps(if (showLocalTile) 2 else 3)
    val slots = listOf(-130f, -20f, 95f)
    Box(Modifier.size(diameter), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(diameter)) {
            val c = Offset(size.width / 2, size.height / 2)
            val r = size.width / 2 - 2.dp.toPx()
            drawCircle(p.line.copy(alpha = .5f), r, c, style = Stroke(1.dp.toPx()))
            dashedOrbit(p, c, r * .78f)
            drawArc(
                p.motion.copy(alpha = .35f), 185f, 55f, false,
                topLeft = Offset(c.x - r * .78f, c.y - r * .78f),
                size = Size(r * 1.56f, r * 1.56f),
                style = Stroke(5.dp.toPx(), cap = StrokeCap.Round),
            )
        }
        val radius = diameter / 2 * 0.78f
        slots.forEachIndexed { i, deg ->
            val rad = Math.toRadians(deg.toDouble())
            val dx = radius * kotlin.math.cos(rad).toFloat()
            val dy = radius * kotlin.math.sin(rad).toFloat()
            Box(
                Modifier.offset(dx, dy).size(54.dp).background(p.well, CircleShape),
                contentAlignment = Alignment.Center,
            ) {
                // Installed apps show their own icon; the rest fall back to the
                // drawn mark, so the page never shows an empty circle.
                val pkg = apps.getOrNull(i) ?: LiveExampleApps.getOrNull(i)
                if (showLocalTile && i == slots.lastIndex) {
                    Canvas(Modifier.size(26.dp)) { noteGlyph(p.accent) }
                } else {
                    AppMark(pkg, 34.dp, 11.dp)
                }
            }
        }
        centre()
    }
}

/** A music note, for "your own files". */
private fun androidx.compose.ui.graphics.drawscope.DrawScope.noteGlyph(tint: Color) {
    val s = size.width
    val w = 2.dp.toPx()
    drawLine(tint, Offset(s * .36f, s * .82f), Offset(s * .36f, s * .18f), w, cap = StrokeCap.Round)
    drawLine(tint, Offset(s * .84f, s * .68f), Offset(s * .84f, s * .06f), w, cap = StrokeCap.Round)
    drawLine(tint, Offset(s * .36f, s * .18f), Offset(s * .84f, s * .06f), w, cap = StrokeCap.Round)
    drawCircle(tint, s * .15f, Offset(s * .21f, s * .82f))
    drawCircle(tint, s * .15f, Offset(s * .69f, s * .68f))
}

/** Any other app making sound. */
private fun androidx.compose.ui.graphics.drawscope.DrawScope.speakerGlyph(tint: Color) {
    val s = size.width
    val w = 2.dp.toPx()
    drawCircle(tint, s * .12f, Offset(s / 2, s / 2))
    listOf(.28f, .44f).forEach { r ->
        drawArc(tint, -50f, 100f, false,
            topLeft = Offset(s / 2 - s * r, s / 2 - s * r), size = Size(s * r * 2, s * r * 2),
            style = Stroke(w, cap = StrokeCap.Round))
        drawArc(tint, 130f, 100f, false,
            topLeft = Offset(s / 2 - s * r, s / 2 - s * r), size = Size(s * r * 2, s * r * 2),
            style = Stroke(w, cap = StrokeCap.Round))
    }
}

@Composable private fun HeadphonesMark() {
    val p = palette
    Box(Modifier.size(300.dp, 240.dp)) {
    Canvas(Modifier.fillMaxSize()) {
        val w = size.width
        val h = size.height
        val stroke = 14.dp.toPx()
        drawArc(
            p.raised, 180f, 180f, false,
            topLeft = Offset(w * .18f, h * .18f),
            size = Size(w * .64f, h * .62f),
            style = Stroke(stroke, cap = StrokeCap.Round),
        )
        drawRoundRect(
            p.accent, topLeft = Offset(w * .12f, h * .44f),
            size = Size(w * .14f, h * .34f),
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(w * .07f),
        )
        drawRoundRect(
            p.motion, topLeft = Offset(w * .74f, h * .44f),
            size = Size(w * .14f, h * .34f),
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(w * .07f),
        )

        listOf(p.accent to -1f, p.motion to 1f).forEach { (color, dir) ->
            (1..2).forEach { i ->
                val rr = w * (.10f + .07f * i)
                val cx = w * (if (dir < 0) .19f else .81f)
                drawArc(
                    color.copy(alpha = .30f / i), if (dir < 0) 120f else -60f, 120f, false,
                    topLeft = Offset(cx - rr, h * .61f - rr), size = Size(rr * 2, rr * 2),
                    style = Stroke(4.dp.toPx(), cap = StrokeCap.Round),
                )
            }
        }
    }
    // Which ear is which: the whole point of the page.
    T("L", 15.sp, Color(0xFF08222D), FontWeight.Bold,
        modifier = Modifier.align(Alignment.CenterStart).offset(x = 54.dp, y = 22.dp))
    T("R", 15.sp, Color(0xFF3D0A1F), FontWeight.Bold,
        modifier = Modifier.align(Alignment.CenterEnd).offset(x = (-54).dp, y = 22.dp))
    }
}

@Composable private fun PrivacyMark() {
    val p = palette
    Box(contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(290.dp)) {
            val c = Offset(size.width / 2, size.height / 2)
            val r = size.width / 2 - 6.dp.toPx()
            dashedOrbit(p, c, r)
            val rad = Math.toRadians(-52.0)
            val dot = Offset(
                c.x + (r * kotlin.math.cos(rad)).toFloat(),
                c.y + (r * kotlin.math.sin(rad)).toFloat(),
            )
            drawArc(
                p.motion.copy(alpha = .35f), 200f, 50f, false,
                topLeft = Offset(c.x - r, c.y - r), size = Size(r * 2, r * 2),
                style = Stroke(5.dp.toPx(), cap = StrokeCap.Round),
            )
            drawCircle(p.motion.copy(alpha = .22f), 22.dp.toPx(), dot)
            drawCircle(p.motion, 10.dp.toPx(), dot)
        }
        Box(
            Modifier.size(150.dp, 250.dp).background(p.card, RoundedCornerShape(34.dp)),
            contentAlignment = Alignment.Center,
        ) {
            Box(
                Modifier.size(86.dp).background(p.presetTint, CircleShape),
                contentAlignment = Alignment.Center,
            ) { Canvas(Modifier.size(38.dp)) { padlock(p.deep) } }
        }
    }
}

@Composable private fun DashboardMark() {
    val p = palette
    Column(
        Modifier.width(270.dp).background(p.card, RoundedCornerShape(26.dp)).padding(18.dp),
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            T("Your dashboard", 13.sp, p.text, FontWeight.Bold)
            Box(Modifier.background(p.motion.copy(alpha = .16f), Pill)
                .padding(horizontal = 9.dp, vertical = 3.dp)) {
                T("BETA", 10.5.sp, p.motion, FontWeight.Bold, 1.sp)
            }
        }
        Canvas(Modifier.fillMaxWidth().height(84.dp).padding(top = 16.dp)) {
            val bars = listOf(.48f, .64f, .38f, .78f, .56f, 1f, .68f)
            val gap = 10.dp.toPx()
            val w = (size.width - gap * (bars.size - 1)) / bars.size
            bars.forEachIndexed { i, v ->
                drawRoundRect(
                    if (i == 5) p.accent else p.raised,
                    topLeft = Offset(i * (w + gap), size.height * (1 - v)),
                    size = Size(w, size.height * v),
                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(w / 3),
                )
            }
        }
    }
}

@Composable private fun OrbitMark() {
    val p = palette
    Box(contentAlignment = Alignment.Center) {
    Canvas(Modifier.size(300.dp)) {
        val c = Offset(size.width / 2, size.height / 2)
        val r = size.width / 2 - 2.dp.toPx()
        drawCircle(p.card, r, c)
        drawCircle(p.line.copy(alpha = .6f), r, c, style = Stroke(1.dp.toPx()))
        dashedOrbit(p, c, r * .66f)
        drawArc(
            p.motion.copy(alpha = .35f), 250f, 55f, false,
            topLeft = Offset(c.x - r * .66f, c.y - r * .66f), size = Size(r * 1.32f, r * 1.32f),
            style = Stroke(6.dp.toPx(), cap = StrokeCap.Round),
        )
        val rad = Math.toRadians(-55.0)
        val dot = Offset(c.x + (r * .66f * kotlin.math.cos(rad)).toFloat(),
            c.y + (r * .66f * kotlin.math.sin(rad)).toFloat())
        drawCircle(p.motion.copy(alpha = .22f), 28.dp.toPx(), dot)
        drawCircle(p.motion, 12.dp.toPx(), dot)
        drawCircle(p.raised, r * .11f, c)
        drawCircle(p.raised, r * .035f, Offset(c.x - r * .11f, c.y))
        drawCircle(p.raised, r * .035f, Offset(c.x + r * .11f, c.y))
    }
    // The labels that make the circle mean something.
    Box(Modifier.size(300.dp)) {
        T("FRONT", 10.sp, p.ghost, FontWeight.Bold, 2.sp,
            modifier = Modifier.align(Alignment.TopCenter))
        T("BACK", 10.sp, p.ghost, FontWeight.Bold, 2.sp,
            modifier = Modifier.align(Alignment.BottomCenter))
        T("you", 12.sp, p.faint, FontWeight.SemiBold,
            modifier = Modifier.align(Alignment.Center).offset(y = 30.dp))
        Box(
            Modifier
                .align(Alignment.TopEnd)
                .offset(x = (-6).dp, y = 96.dp)
                .background(p.text, Pill)
                .padding(horizontal = 11.dp, vertical = 5.dp),
        ) { T("the sound", 12.5.sp, p.ground, FontWeight.Bold) }
    }
    }
}

@Composable private fun ModesMark() {
    Column(
        Modifier.width(320.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Mode.entries.chunked(4).forEach { row ->
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                row.forEach { m ->
                    ChoiceTile(modeShort(m), m == Mode.Circular, Modifier.weight(1f)) { tint ->
                        Canvas(Modifier.size(26.dp)) { modeGlyph(m, tint) }
                    }
                }
            }
        }
    }
}

@Composable private fun RoomMark() {
    val p = palette
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
    Canvas(Modifier.size(300.dp, 200.dp)) {
        drawRoundRect(
            p.card, size = size,
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(40.dp.toPx()),
        )
        val c = Offset(size.width / 2, size.height / 2)
        listOf(.30f, .52f, .76f).forEachIndexed { i, f ->
            drawCircle(
                p.accent.copy(alpha = .40f - i * .12f), size.height * f, c,
                style = Stroke(3.dp.toPx()),
            )
        }
        drawCircle(p.motion, 12.dp.toPx(), c)
    }
    Row(
        Modifier.padding(top = 20.dp),
        horizontalArrangement = Arrangement.spacedBy(36.dp),
    ) {
        Knob("Reverb", .28f, 0f..1f, "28%") {}
        Knob("Room", .72f, 0f..1f, "72%") {}
    }
    }
}

@Composable private fun EchoMark() {
    val p = palette
    Box(contentAlignment = Alignment.Center) {
    Canvas(Modifier.size(300.dp)) {
        val c = Offset(size.width / 2, size.height / 2)
        val r = size.width / 2 - 2.dp.toPx()
        drawCircle(p.card, r, c)
        dashedOrbit(p, c, r * .70f)
        listOf(-150.0 to .18f, -120.0 to .32f, -90.0 to .55f).forEach { (deg, alpha) ->
            val rad = Math.toRadians(deg)
            drawCircle(
                p.motion.copy(alpha = alpha),
                (6 + 4 * alpha).dp.toPx(),
                Offset(c.x + (r * .70f * kotlin.math.cos(rad)).toFloat(),
                    c.y + (r * .70f * kotlin.math.sin(rad)).toFloat()),
            )
        }
        val rad = Math.toRadians(-55.0)
        val dot = Offset(c.x + (r * .70f * kotlin.math.cos(rad)).toFloat(),
            c.y + (r * .70f * kotlin.math.sin(rad)).toFloat())
        drawCircle(p.motion.copy(alpha = .2f), 26.dp.toPx(), dot)
        drawCircle(p.motion, 12.dp.toPx(), dot)
        drawCircle(p.raised, r * .10f, c)
    }
    Box(Modifier.size(300.dp)) {
        T("echo", 12.sp, p.faint, FontWeight.SemiBold,
            modifier = Modifier.align(Alignment.TopCenter).offset(x = 30.dp, y = 34.dp))
        T("echo", 12.sp, p.ghost, FontWeight.SemiBold,
            modifier = Modifier.align(Alignment.TopStart).offset(x = 54.dp, y = 62.dp))
    }
    }
}

@Composable private fun CharacterMark() {
    val p = palette
    Column(Modifier.width(320.dp), verticalArrangement = Arrangement.spacedBy(18.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Character.entries.forEach { ch ->
                ChoiceTile(characterShort(ch), ch == Character.Clean, Modifier.weight(1f),
                    height = 88.dp) { tint ->
                    Canvas(Modifier.size(26.dp)) { characterGlyph(ch, tint) }
                }
            }
        }
        Row(
            Modifier.fillMaxWidth().background(p.card, CardShape).padding(vertical = 16.dp),
            horizontalArrangement = Arrangement.SpaceEvenly,
        ) {
            Knob("Bass", 3f, -12f..12f, "+3", bipolar = true) {}
            Knob("Mid", 0f, -12f..12f, "0", bipolar = true) {}
            Knob("Treble", -2f, -12f..12f, "−2", bipolar = true) {}
        }
    }
}

@Composable private fun CompareMark() {
    val p = palette
    Column(
        Modifier.width(320.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(30.dp),
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            PillChip("Classic 8D", false) {}
            PillChip("Slow Orbit", true) {}
        }
        Row(
            Modifier.background(p.card, Pill).padding(horizontal = 24.dp, vertical = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            T("8D", 20.sp, p.deep, FontWeight.Bold, 1.5.sp)
            PillSwitch(true) {}
        }
        T("Off = the original sound, on = 8D", 13.sp, p.faint)
    }
}

@Composable private fun AppsOrbitMark() {
    val p = palette
    OrbitOfApps(diameter = 290.dp) {
        Box(Modifier.size(78.dp).background(p.raised, CircleShape))
    }
}

@Composable private fun CaptureListMark() {
    // The apps on this phone, sorted the way capture actually treats them.
    val allowed = rememberInstalledExampleApps(4).firstOrNull { it != SPOTIFY } ?: PKG_YOUTUBE
    Column(Modifier.width(320.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        CaptureRow(pkg = allowed, title = appLabel(allowed), sub = "Allows capture", allowed = true)
        CaptureRow(
            pkg = null, title = "Most games & players", sub = "Usually allow capture",
            allowed = true,
        )
        CaptureRow(pkg = SPOTIFY, title = "Spotify", sub = "Blocks capture", allowed = false)
    }
}

private const val SPOTIFY = "com.spotify.music"

@Composable
private fun appLabel(pkg: String): String {
    val context = androidx.compose.ui.platform.LocalContext.current
    return remember(pkg) {
        runCatching {
            context.packageManager.getApplicationLabel(
                context.packageManager.getApplicationInfo(pkg, 0)
            ).toString()
        }.getOrDefault(pkg.substringAfterLast('.').replaceFirstChar { it.uppercase() })
    }
}

@Composable private fun CaptureRow(pkg: String?, title: String, sub: String, allowed: Boolean) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .background(if (allowed) p.presetTint else p.card, RoundedCornerShape(22.dp))
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Box(
            Modifier.size(38.dp).background(p.well, RoundedCornerShape(11.dp)),
            contentAlignment = Alignment.Center,
        ) { AppMark(pkg, 30.dp, 9.dp) }
        Column(Modifier.weight(1f)) {
            T(title, 16.sp, if (allowed) p.deep else p.text, FontWeight.Bold, maxLines = 1)
            T(sub, 13.sp, p.faint, maxLines = 1)
        }
        Box(
            Modifier
                .background(
                    if (allowed) p.accent.copy(alpha = .18f) else p.motion.copy(alpha = .16f),
                    Pill,
                )
                .padding(horizontal = 11.dp, vertical = 5.dp),
        ) {
            T(if (allowed) "In 8D" else "Blocked", 12.sp,
                if (allowed) p.deep else p.motion, FontWeight.Bold)
        }
    }
}

@Composable private fun StepsRingMark() {
    val p = palette
    Box(contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(240.dp)) {
            val c = Offset(size.width / 2, size.height / 2)
            val r = size.width / 2 - 10.dp.toPx()
            // Four arcs: the four steps, all of them ahead of you.
            repeat(4) { i ->
                drawArc(
                    p.accent, -85f + i * 90f, 80f, false,
                    topLeft = Offset(c.x - r, c.y - r), size = Size(r * 2, r * 2),
                    style = Stroke(16.dp.toPx(), cap = StrokeCap.Round),
                )
            }
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            T("4", 34.sp, palette.text, FontWeight.Bold)
            T("steps, once", 14.sp, palette.dim)
            Row(
                Modifier.padding(top = 12.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                listOf(PKG_SPOTIFY, PKG_BRAVE, PKG_YOUTUBE).forEach { pkg ->
                    AppMark(pkg, 26.dp, 8.dp)
                }
            }
        }
    }
}

@Composable private fun SwitchesMark() {
    val p = palette
    Column(Modifier.width(320.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        listOf("Live" to true, "Player" to false).forEach { (label, on) ->
            Row(
                Modifier.fillMaxWidth().background(p.card, RoundedCornerShape(22.dp)).padding(16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    T(label, 16.sp, p.text, FontWeight.Bold)
                    if (!on) T("Stopped when Live is on", 13.sp, p.faint)
                }
                PillSwitch(on) {}
            }
        }
        Row(
            Modifier
                .fillMaxWidth()
                .padding(top = 6.dp)
                .background(p.card, RoundedCornerShape(22.dp))
                .padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Box(
                Modifier.size(38.dp).background(p.well, CircleShape),
                contentAlignment = Alignment.Center,
            ) { Canvas(Modifier.size(15.dp)) { pauseGlyph(p.text) } }
            T("These buttons control the app, not the effect", 13.5.sp, p.dim)
        }
    }
}

private fun androidx.compose.ui.graphics.drawscope.DrawScope.pauseGlyph(tint: Color) {
    val s = size.width
    val w = s * .3f
    drawRoundRect(tint, size = Size(w, size.height),
        cornerRadius = androidx.compose.ui.geometry.CornerRadius(w / 3))
    drawRoundRect(tint, topLeft = Offset(s - w, 0f), size = Size(w, size.height),
        cornerRadius = androidx.compose.ui.geometry.CornerRadius(w / 3))
}

@Composable private fun CoversMark() {
    val p = palette
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.size(320.dp, 230.dp), contentAlignment = Alignment.Center) {
            Canvas(Modifier.fillMaxSize()) {
                val c = Offset(size.width / 2, size.height / 2)
                val r = size.height / 2 - 4.dp.toPx()
                drawCircle(
                    p.accent.copy(alpha = .40f), r, c,
                    style = Stroke(
                        1.5.dp.toPx(),
                        pathEffect = PathEffect.dashPathEffect(
                            floatArrayOf(1.dp.toPx(), 7.dp.toPx())
                        ),
                        cap = StrokeCap.Round,
                    ),
                )
                val rad = Math.toRadians(-52.0)
                drawCircle(
                    p.motion, 9.dp.toPx(),
                    Offset(
                        c.x + (r * kotlin.math.cos(rad)).toFloat(),
                        c.y + (r * kotlin.math.sin(rad)).toFloat(),
                    ),
                )
            }
            PaintedCover(
                CoverArt.NightDrive, 128.dp, 22.dp,
                Modifier.offset(x = (-72).dp).rotate(-12f),
            )
            PaintedCover(
                CoverArt.Northern, 128.dp, 22.dp,
                Modifier.offset(x = 72.dp).rotate(12f),
            )
            PaintedCover(CoverArt.Afterglow, 156.dp, 26.dp)
        }
        Row(
            Modifier.padding(top = 18.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            listOf("MP3", "FLAC", "WAV", "OGG").forEach { fmt ->
                Box(Modifier.background(p.card, Pill).padding(horizontal = 13.dp, vertical = 7.dp)) {
                    T(fmt, 13.sp, p.dim, FontWeight.Bold, .6.sp)
                }
            }
        }
    }
}

@Composable private fun PlaylistMark() {
    val p = palette
    Column(
        Modifier.width(280.dp).background(p.card, RoundedCornerShape(28.dp)).padding(20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        PaintedCover(CoverArt.NightDrive, 170.dp, 24.dp)
        T("Night drive", 21.sp, p.text, FontWeight.Bold,
            modifier = Modifier.padding(top = 14.dp))
        T("18 SONGS", 12.sp, p.faint, FontWeight.Bold, 1.2.sp)
        Row(
            Modifier
                .padding(top = 14.dp)
                .background(p.motion.copy(alpha = .14f), Pill)
                .padding(horizontal = 14.dp, vertical = 7.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Box(Modifier.size(8.dp).background(p.motion, CircleShape))
            T("Always plays in Slow Orbit", 13.sp, p.motion, FontWeight.Bold)
        }
    }
}

@Composable private fun DockMark() {
    val p = palette
    Column(Modifier.width(320.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        PaintedCover(CoverArt.Afterglow, 186.dp, 28.dp)
        T("Afterglow", 19.sp, p.text, FontWeight.Bold,
            modifier = Modifier.padding(top = 14.dp))
        T("Nour Sky", 14.sp, p.faint)
        Spacer(Modifier.height(20.dp))
        EightDDock("Slow Orbit", onOpenStudio = {})
    }
}

@Composable private fun RecordTourMark() {
    val p = palette
    Canvas(Modifier.size(260.dp)) {
        val c = Offset(size.width / 2, size.height / 2)
        val r = size.width / 2 - 2.dp.toPx()
        dashedOrbit(p, c, r * .88f)
        drawCircle(p.card, r * .62f, c)
        listOf(.50f, .40f, .30f).forEach {
            drawCircle(p.well, r * it, c, style = Stroke(2.dp.toPx()))
        }
        drawCircle(p.accent, r * .17f, c)
        drawCircle(p.ground, r * .04f, c)
    }
}

/* ------------------------------------------------------------- drawing */

private fun androidx.compose.ui.graphics.drawscope.DrawScope.dashedOrbit(
    p: Palette, c: Offset, radius: Float,
) = drawCircle(
    p.accent.copy(alpha = .45f), radius, c,
    style = Stroke(
        1.5.dp.toPx(),
        pathEffect = PathEffect.dashPathEffect(floatArrayOf(1.dp.toPx(), 7.dp.toPx())),
        cap = StrokeCap.Round,
    ),
)

private fun androidx.compose.ui.graphics.drawscope.DrawScope.headphones(
    p: Palette, left: Color, right: Color,
) {
    val w = size.width
    val h = size.height
    drawArc(
        p.text, 180f, 180f, false,
        topLeft = Offset(w * .1f, h * .12f), size = Size(w * .8f, h * .8f),
        style = Stroke(w * .10f, cap = StrokeCap.Round),
    )
    drawRoundRect(
        left, topLeft = Offset(w * .04f, h * .5f), size = Size(w * .2f, h * .42f),
        cornerRadius = androidx.compose.ui.geometry.CornerRadius(w * .1f),
    )
    drawRoundRect(
        right, topLeft = Offset(w * .76f, h * .5f), size = Size(w * .2f, h * .42f),
        cornerRadius = androidx.compose.ui.geometry.CornerRadius(w * .1f),
    )
}

private fun androidx.compose.ui.graphics.drawscope.DrawScope.padlock(tint: Color) {
    val w = size.width
    val h = size.height
    drawRoundRect(
        tint, topLeft = Offset(w * .16f, h * .44f), size = Size(w * .68f, h * .46f),
        cornerRadius = androidx.compose.ui.geometry.CornerRadius(w * .14f),
        style = Stroke(w * .1f),
    )
    drawArc(
        tint, 180f, 180f, false,
        topLeft = Offset(w * .30f, h * .16f), size = Size(w * .40f, h * .46f),
        style = Stroke(w * .1f, cap = StrokeCap.Round),
    )
}

private fun modeShort(m: Mode) = when (m) {
    Mode.Circular -> "Circle"
    Mode.PingPong -> "Ping-pong"
    Mode.Pendulum -> "Pendulum"
    Mode.Linear -> "Linear"
    Mode.Figure8 -> "Figure 8"
    Mode.Spiral -> "Spiral"
    Mode.Random -> "Random"
    Mode.Static -> "Static"
}

private fun characterShort(c: Character) = when (c) {
    Character.Clean -> "Clean"
    Character.Slowed -> "Slowed"
    Character.Radio -> "Old radio"
}
