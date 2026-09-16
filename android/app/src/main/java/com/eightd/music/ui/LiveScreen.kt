package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.eightd.music.AppState
import com.eightd.music.Source

/**
 * Live: 8D for whatever other apps are playing.
 *
 * Direct capture and Shizuku were two separate places in v1; to the person
 * using them they are one idea with two levels of reach, so they are one page
 * with a route switch, and the Shizuku setup is an upgrade offered from here.
 */
@Composable
fun LiveScreen(
    state: AppState,
    on: Boolean,
    captureNote: String?,
    shizukuReady: Boolean,
    mediaAccess: Boolean,
    playerRunning: Boolean,
    onRoute: (Source) -> Unit,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onSetupShizuku: () -> Unit,
    onPlayPause: () -> Unit,
    onPrev: () -> Unit,
    onNext: () -> Unit,
    onSeek: (Float) -> Unit,
    onOpenStudio: () -> Unit,
    onOpenNotificationAccess: () -> Unit,
    onOpenAppInfo: () -> Unit,
    apps: List<com.eightd.music.audio.NowPlayingWatcher.AppSound>,
    telemetry: String?,
    bottomInset: Dp,
) {
    val p = palette
    val route = if (state.source == Source.SystemWide) Source.SystemWide else Source.Capture

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            PageTitle("Live")
            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(9.dp)) {
                T(if (on) "ON" else "OFF", 12.sp, if (on) p.deep else p.faint,
                    FontWeight.Bold, 1.2.sp)
                PillSwitch(on) { if (on) onStop() else onStart() }
            }
        }

        if (!on) {
            /* ------------------------------------------------------- off */
            Box(Modifier.fillMaxWidth().padding(top = 18.dp), Alignment.Center) {
                Glow(
                    Modifier.size(320.dp),
                    cyan = .10f, magenta = .16f,
                    cyanCentre = Offset(.3f, .6f), magentaCentre = Offset(.62f, .34f),
                )
                LiveOrbitMark()
            }
            Column(Modifier.padding(horizontal = 24.dp).padding(top = 18.dp)) {
                T("Every app you play, in 8D", 25.sp, p.text, FontWeight.Bold)
                T("Pick how Live listens. You can switch any time.",
                    15.5.sp, p.dim, modifier = Modifier.padding(top = 8.dp))
            }
            Spacer(Modifier.height(18.dp))
            RouteCard(
                title = "Direct capture",
                tag = "NO SETUP",
                body = "Works with apps that allow playback capture: YouTube, games and most players.",
                apps = listOf(PKG_YOUTUBE, PKG_CHROME),
                selected = route == Source.Capture,
            ) { onRoute(Source.Capture) }
            Spacer(Modifier.height(10.dp))
            RouteCard(
                title = "System-wide",
                tag = if (shizukuReady) "READY" else "SHIZUKU · ONCE",
                body = if (shizukuReady)
                    "Set up on this phone. Every app can be spatialised, and the original stream is silenced."
                else
                    "Every app, including the ones that block capture. Four steps, done once, survives reboots.",
                apps = listOf(PKG_SPOTIFY, PKG_BRAVE, PKG_NETFLIX),
                selected = route == Source.SystemWide,
            ) { onRoute(Source.SystemWide) }

            if (playerRunning) {
                Spacer(Modifier.height(14.dp))
                Notice("Player is on. Turning Live on stops it — one source plays in 8D at a time.")
            }

            if (route == Source.SystemWide && !shizukuReady) {
                Spacer(Modifier.height(12.dp))
                Box(Modifier.padding(horizontal = 12.dp)) {
                    PrimaryPill("Set up System-wide") { onSetupShizuku() }
                }
            } else {
                Spacer(Modifier.height(18.dp))
                Box(Modifier.padding(horizontal = 12.dp)) {
                    PrimaryPill("Turn on Live") { onStart() }
                }
            }
            Spacer(Modifier.height(14.dp))
            T("Audio is processed on your phone. Nothing is uploaded.",
                13.sp, p.faint, align = androidx.compose.ui.text.style.TextAlign.Center,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 24.dp))
        } else {
            /* -------------------------------------------------------- on */
            Box(Modifier.padding(horizontal = 12.dp).padding(top = 14.dp)) {
                SegPill(
                    listOf("Direct capture", "System-wide"),
                    if (route == Source.SystemWide) "System-wide" else "Direct capture",
                    Modifier.fillMaxWidth(),
                ) { picked ->
                    onRoute(if (picked == "System-wide") Source.SystemWide else Source.Capture)
                }
            }
            Spacer(Modifier.height(12.dp))

            V2Card {
                SectionHeader(
                    "NOW PLAYING",
                    captureNote ?: if (state.source == Source.SystemWide) "System-wide" else "Captured",
                )
                NowPlayingBlock(
                    title = state.nowTitle,
                    artist = state.nowArtist,
                    coverLabel = if (mediaAccess) state.nowTitle.take(2) else null,
                    elapsed = state.nowSeconds,
                    total = state.totalSeconds,
                    playing = state.playing,
                    onPrev = onPrev,
                    onPlayPause = onPlayPause,
                    onNext = onNext,
                    onSeek = onSeek,
                )
                if (mediaAccess) T(
                    "These buttons control the app, not the effect",
                    12.5.sp, p.ghost,
                    align = androidx.compose.ui.text.style.TextAlign.Center,
                    modifier = Modifier.fillMaxWidth().padding(top = 10.dp),
                )
            }

            if (!mediaAccess) {
                Spacer(Modifier.height(12.dp))
                NotificationAccessHelp(onOpenNotificationAccess, onOpenAppInfo)
            }

            Spacer(Modifier.height(12.dp))
            EightDDock(state.presetName, telemetry, onOpenStudio = onOpenStudio)

            Spacer(Modifier.height(12.dp))
            AppsMakingSound(apps, captureNote == null, onSetupShizuku)

            if (route == Source.Capture) {
                Spacer(Modifier.height(12.dp))
                Box(
                    Modifier
                        .padding(horizontal = 12.dp)
                        .fillMaxWidth()
                        .background(p.motion.copy(alpha = .10f), RoundedCornerShape(22.dp))
                        .clickable { onSetupShizuku() }
                        .padding(16.dp),
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        Box(Modifier.size(8.dp).background(p.motion, CircleShape))
                        Column(Modifier.weight(1f)) {
                            T("Apps that block capture stay silent", 14.5.sp, p.text,
                                FontWeight.SemiBold)
                            T("Set up System-wide once to hear them too", 13.sp, p.dim)
                        }
                        T("›", 20.sp, p.dim)
                    }
                }
            }
            Spacer(Modifier.height(12.dp))
            T("Listening stats are coming in a later version.",
                12.5.sp, p.ghost, align = androidx.compose.ui.text.style.TextAlign.Center,
                modifier = Modifier.fillMaxWidth())
        }

        Spacer(Modifier.height(bottomInset))
    }
}

@Composable
private fun RouteCard(
    title: String,
    tag: String,
    body: String,
    apps: List<String>,
    selected: Boolean,
    onPick: () -> Unit,
) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .background(if (selected) p.presetTint else p.card, RoundedCornerShape(24.dp))
            .then(
                if (selected) Modifier.border(1.5.dp, p.accent.copy(alpha = .7f),
                    RoundedCornerShape(24.dp)) else Modifier
            )
            .clickable { onPick() }
            .padding(16.dp),
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Box(
            Modifier
                .padding(top = 2.dp)
                .size(22.dp)
                .border(2.dp, if (selected) p.accent else p.ghost, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            if (selected) Box(Modifier.size(10.dp).background(p.accent, CircleShape))
        }
        Column {
            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                T(title, 18.sp, if (selected) p.deep else p.text, FontWeight.Bold)
                Box(
                    Modifier.background(p.well, Pill).padding(horizontal = 9.dp, vertical = 3.dp)
                ) { T(tag, 10.5.sp, p.dim, FontWeight.Bold, 1.sp) }
            }
            T(body, 14.5.sp, p.dim, modifier = Modifier.padding(top = 5.dp))
            Row(
                Modifier.padding(top = 10.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                apps.forEach { AppMark(it, 22.dp, 7.dp) }
            }
        }
    }
}

/**
 * What has a media session right now, read from Android itself.
 *
 * "In 8D" means capture is receiving audio while that app plays; when the app
 * plays and capture hears nothing, that is what a blocked app looks like, and
 * the row offers the Shizuku upgrade rather than pretending otherwise.
 */
@Composable
private fun AppsMakingSound(
    apps: List<com.eightd.music.audio.NowPlayingWatcher.AppSound>,
    capturing: Boolean,
    onSetupShizuku: () -> Unit,
) {
    val p = palette
    V2Card {
        SectionHeader("APPS MAKING SOUND", if (apps.isEmpty()) "Nothing yet" else "Now")
        if (apps.isEmpty()) {
            T("Apps show up here once they start playing. Discord and most games never " +
                "publish what they play, even when you can hear them.",
                13.5.sp, p.faint, modifier = Modifier.padding(top = 10.dp))
        } else {
            Column(Modifier.padding(top = 6.dp)) {
                apps.forEach { app ->
                    val blocked = app.playing && !capturing
                    Row(
                        Modifier.fillMaxWidth().padding(vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        Box(
                            Modifier.size(34.dp).background(p.well, RoundedCornerShape(10.dp)),
                            contentAlignment = Alignment.Center,
                        ) { AppMark(app.packageName, 28.dp, 8.dp) }
                        Column(Modifier.weight(1f)) {
                            T(appLabelOf(app.packageName), 15.5.sp, p.text, FontWeight.SemiBold,
                                maxLines = 1)
                            T(
                                when {
                                    blocked -> "Playing, but nothing reaches capture"
                                    app.playing -> "Captured"
                                    else -> "Paused"
                                },
                                13.sp, p.faint, maxLines = 1,
                            )
                        }
                        when {
                            blocked -> Box(
                                Modifier
                                    .background(p.motion.copy(alpha = .16f), Pill)
                                    .clickable { onSetupShizuku() }
                                    .padding(horizontal = 12.dp, vertical = 6.dp),
                            ) { T("Unlock", 12.5.sp, p.motion, FontWeight.Bold) }
                            app.playing -> Box(
                                Modifier
                                    .background(p.presetTint, Pill)
                                    .padding(horizontal = 12.dp, vertical = 6.dp),
                            ) { T("In 8D", 12.5.sp, p.deep, FontWeight.Bold) }
                            else -> T("—", 14.sp, p.ghost)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun appLabelOf(pkg: String): String {
    val context = androidx.compose.ui.platform.LocalContext.current
    return androidx.compose.runtime.remember(pkg) {
        runCatching {
            context.packageManager.getApplicationLabel(
                context.packageManager.getApplicationInfo(pkg, 0)
            ).toString()
        }.getOrDefault(pkg.substringAfterLast('.').replaceFirstChar { it.uppercase() })
    }
}

/**
 * Why the notification-access switch refuses to move.
 *
 * Android 13 calls this a restricted setting and blocks it for anything that
 * did not come from an app store; MIUI shows it as "restricted setting" with no
 * explanation at all. The way through is App info, so the page says that
 * plainly and offers the button rather than leaving a dead end.
 */
@Composable
private fun NotificationAccessHelp(onOpenSetting: () -> Unit, onOpenAppInfo: () -> Unit) {
    val p = palette
    Column(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .background(p.card, CardShape)
            .padding(18.dp),
    ) {
        Kicker("TO SHOW WHAT IS PLAYING")
        T("Allow notification access", 18.sp, p.text, FontWeight.Bold,
            modifier = Modifier.padding(top = 8.dp))
        T("The effect already works — this is only how 8D Music reads the title and drives " +
            "play, pause and skip for the other app.",
            14.sp, p.dim, modifier = Modifier.padding(top = 6.dp))
        T("If the phone says the setting is restricted, that is Android blocking apps " +
            "installed outside the Play Store. Open App info, tap ⋮ in the corner, choose " +
            "\"Allow restricted settings\", then come back here.",
            14.sp, p.dim, modifier = Modifier.padding(top = 10.dp))
        Row(
            Modifier.fillMaxWidth().padding(top = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Box(Modifier.weight(1f)) { GhostPill("App info") { onOpenAppInfo() } }
            Box(Modifier.weight(1f)) { PrimaryPill("Allow access") { onOpenSetting() } }
        }
    }
}

@Composable
fun Notice(text: String) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .background(p.motion.copy(alpha = .10f), RoundedCornerShape(18.dp))
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Box(Modifier.size(8.dp).background(p.motion, CircleShape))
        T(text, 13.5.sp, p.dim)
    }
}

/** The bar that links whatever is playing back to Studio. */
@Composable
fun EightDDock(
    presetName: String,
    detail: String? = null,
    compact: Boolean = false,
    onOpenStudio: () -> Unit,
) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = if (compact) 0.dp else 12.dp)
            .background(p.card, Pill)
            .clickable { onOpenStudio() }
            .padding(horizontal = if (compact) 6.dp else 10.dp, vertical = if (compact) 8.dp else 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(if (compact) 8.dp else 12.dp),
    ) {
        Canvas(Modifier.size(if (compact) 38.dp else 44.dp)) {
            val c = Offset(size.width / 2, size.height / 2)
            drawCircle(p.ground, size.width / 2)
            drawCircle(p.accent.copy(alpha = .6f), size.width * .27f, c, style = Stroke(1.3.dp.toPx()))
            drawCircle(p.motion, 4.dp.toPx(), Offset(c.x + size.width * .19f, c.y - size.width * .19f))
        }
        Column(Modifier.weight(1f)) {
            T(presetName, if (compact) 14.5.sp else 15.5.sp, p.text, FontWeight.Bold, maxLines = 1)
            T(
                detail ?: if (compact) "open Studio" else "8D preset · open Studio",
                12.sp, p.faint, maxLines = 1,
            )
        }
        Box(
            Modifier
                .background(p.well, Pill)
                .padding(horizontal = if (compact) 12.dp else 16.dp, vertical = 9.dp)
        ) { T(if (compact) "8D" else "Studio ›", 13.sp, p.accent, FontWeight.Bold) }
    }
}

/** Transport plus progress, shared by Live and Player. */
@Composable
fun NowPlayingBlock(
    title: String,
    artist: String,
    coverLabel: String?,
    elapsed: Int,
    total: Int,
    playing: Boolean,
    onPrev: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onSeek: (Float) -> Unit,
) {
    val p = palette
    Row(
        Modifier.fillMaxWidth().padding(top = 16.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(84.dp).background(p.well, RoundedCornerShape(22.dp)),
            contentAlignment = Alignment.Center,
        ) {
            // Initials only for a real track: lettering a status message reads
            // as an album called "Al".
            if (coverLabel != null) T(coverLabel, 28.sp, p.dim, FontWeight.Bold)
            else Canvas(Modifier.size(28.dp)) {
                drawLine(
                    p.ghost, Offset(0f, size.height / 2), Offset(size.width, size.height / 2),
                    3.dp.toPx(), cap = StrokeCap.Round,
                )
            }
        }
        Column(Modifier.weight(1f)) {
            T(title, 19.sp, p.text, FontWeight.Bold, maxLines = 2)
            if (artist.isNotEmpty()) T(artist, 15.sp, p.dim, maxLines = 1)
        }
    }
    Column(Modifier.fillMaxWidth().padding(top = 10.dp)) {
        SeekBar(
            fraction = if (total > 0) (elapsed.toFloat() / total).coerceIn(0f, 1f) else 0f,
            onSeek = onSeek,
        )
        Row(Modifier.fillMaxWidth().padding(top = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween) {
            T(clock(elapsed), 12.5.sp, p.faint)
            T(clock(total), 12.5.sp, p.faint)
        }
    }
    Row(
        Modifier.fillMaxWidth().padding(top = 10.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TransportButton(kind = 0, onClick = onPrev)
        Spacer(Modifier.width(18.dp))
        Box(
            Modifier.size(68.dp).background(p.text, CircleShape).clickable { onPlayPause() },
            contentAlignment = Alignment.Center,
        ) {
            Canvas(Modifier.size(26.dp)) { transportGlyph(if (playing) 1 else 2, p.ground) }
        }
        Spacer(Modifier.width(18.dp))
        TransportButton(kind = 3, onClick = onNext)
    }
}

@Composable
private fun TransportButton(kind: Int, onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier.size(56.dp).background(p.well, CircleShape).clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.size(22.dp)) { transportGlyph(kind, p.text) }
    }
}

/** 0 = previous, 1 = pause, 2 = play, 3 = next. */
private fun androidx.compose.ui.graphics.drawscope.DrawScope.transportGlyph(kind: Int, tint: Color) {
    val s = size.width
    when (kind) {
        1 -> {
            val w = s * .28f
            drawRoundRect(tint, size = androidx.compose.ui.geometry.Size(w, s),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(w / 3))
            drawRoundRect(tint, topLeft = Offset(s - w, 0f),
                size = androidx.compose.ui.geometry.Size(w, s),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(w / 3))
        }
        2 -> {
            val path = androidx.compose.ui.graphics.Path().apply {
                moveTo(s * .12f, 0f); lineTo(s, s / 2f); lineTo(s * .12f, s); close()
            }
            drawPath(path, tint)
        }
        else -> {
            val forward = kind == 3
            val bar = s * .13f
            val path = androidx.compose.ui.graphics.Path().apply {
                if (forward) { moveTo(0f, 0f); lineTo(s - bar, s / 2f); lineTo(0f, s) }
                else { moveTo(s, 0f); lineTo(bar, s / 2f); lineTo(s, s) }
                close()
            }
            drawPath(path, tint)
            drawRoundRect(
                tint,
                topLeft = Offset(if (forward) s - bar else 0f, 0f),
                size = androidx.compose.ui.geometry.Size(bar, s),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(bar / 2),
            )
        }
    }
}

/** The mark on the Live off state: other apps orbiting the listener. */
@Composable
private fun LiveOrbitMark() {
    val p = palette
    val diameter = 230.dp
    Box(Modifier.size(diameter), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(diameter)) {
            val c = Offset(size.width / 2, size.height / 2)
            val r = size.width / 2 - 4.dp.toPx()
            drawCircle(p.line.copy(alpha = .55f), r, c, style = Stroke(1.dp.toPx()))
            drawCircle(
                p.accent.copy(alpha = .45f), r * .72f, c,
                style = Stroke(
                    1.5.dp.toPx(),
                    pathEffect = androidx.compose.ui.graphics.PathEffect.dashPathEffect(
                        floatArrayOf(1.dp.toPx(), 7.dp.toPx())
                    ),
                    cap = StrokeCap.Round,
                ),
            )
            drawArc(
                p.motion.copy(alpha = .4f), 180f, 60f, false,
                topLeft = Offset(c.x - r * .72f, c.y - r * .72f),
                size = androidx.compose.ui.geometry.Size(r * 1.44f, r * 1.44f),
                style = Stroke(5.dp.toPx(), cap = StrokeCap.Round),
            )
            drawCircle(p.raised, r * .16f, c)
            drawCircle(p.raised, r * .05f, Offset(c.x - r * .16f, c.y))
            drawCircle(p.raised, r * .05f, Offset(c.x + r * .16f, c.y))
        }
        // The apps themselves, on the ring: their own icon where installed,
        // the drawn mark where not.
        val radius = diameter / 2 * 0.72f
        listOf(PKG_YOUTUBE, PKG_SPOTIFY, PKG_BRAVE).forEachIndexed { i, pkg ->
            val rad = Math.toRadians(listOf(-135.0, -20.0, 100.0)[i])
            Box(
                Modifier
                    .offset(
                        radius * kotlin.math.cos(rad).toFloat(),
                        radius * kotlin.math.sin(rad).toFloat(),
                    )
                    .size(46.dp)
                    .background(p.well, CircleShape),
                contentAlignment = Alignment.Center,
            ) { AppMark(pkg, 30.dp, 9.dp) }
        }
    }
}
