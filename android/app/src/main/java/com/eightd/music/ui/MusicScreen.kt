package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.eightd.music.AppState
import com.eightd.music.Track

/**
 * Player: the files on this phone, in 8D.
 *
 * Off until asked. A player that grabs the audio output the moment you open a
 * tab would fight Live for the engine, and the one-source rule is easier to
 * trust when turning it on is a deliberate act.
 */
@Composable
fun MusicScreen(
    state: AppState,
    on: Boolean,
    granted: Boolean,
    liveRunning: Boolean,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onGrant: () -> Unit,
    onPlay: (Track, List<Track>, String) -> Unit,
    onOpenPlaylist: (com.eightd.music.Playlist) -> Unit,
    bottomInset: Dp,
) {
    val p = palette

    if (on) {
        // The library scrolls itself; wrapping it in another scrolling column
        // measures it with unbounded height, which Compose refuses outright.
        LibraryScreenV2(
            state = state,
            onPlay = onPlay,
            onOpenPlaylist = onOpenPlaylist,
            onStop = onStop,
            bottomInset = bottomInset,
        )
        return
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            PageTitle("Player")
            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(9.dp)) {
                T(if (on) "ON" else "OFF", 12.sp, if (on) p.deep else p.faint,
                    FontWeight.Bold, 1.2.sp)
                PillSwitch(on) { if (on) onStop() else onStart() }
            }
        }

        run {
            Box(Modifier.fillMaxWidth().padding(top = 22.dp), Alignment.Center) {
                Glow(
                    Modifier.size(320.dp),
                    cyan = .14f, magenta = .10f,
                    cyanCentre = Offset(.5f, .5f), magentaCentre = Offset(.68f, .24f),
                )
                RecordMark()
            }
            Column(
                Modifier.fillMaxWidth().padding(horizontal = 30.dp).padding(top = 22.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                T("Your music, around you", 25.sp, p.text, FontWeight.Bold, align = TextAlign.Center)
                T("Play the songs saved on this phone in 8D. MP3, FLAC, WAV and OGG.",
                    15.5.sp, p.dim, align = TextAlign.Center,
                    modifier = Modifier.padding(top = 10.dp))
            }
            if (liveRunning) {
                Spacer(Modifier.height(20.dp))
                Notice("Live is on. Turning Player on stops it — one source plays in 8D at a time.")
            }
            Spacer(Modifier.height(if (liveRunning) 14.dp else 24.dp))
            Box(Modifier.padding(horizontal = 12.dp)) {
                PrimaryPill(if (granted) "Turn on Player" else "Allow access to my music") {
                    if (granted) onStart() else onGrant()
                }
            }
        }

        Spacer(Modifier.height(bottomInset))
    }
}

@Composable
private fun TrackRow(track: Track, playing: Boolean, onPlay: () -> Unit) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp)
            .background(if (playing) p.card else androidx.compose.ui.graphics.Color.Transparent,
                RoundedCornerShape(18.dp))
            .clickable { onPlay() }
            .padding(horizontal = 8.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Box(
            Modifier.size(52.dp).background(p.well, RoundedCornerShape(14.dp)),
            contentAlignment = Alignment.Center,
        ) { Mark(track.title.take(2), 15.sp, p.dim) }
        Column(Modifier.weight(1f)) {
            T(track.title, 16.sp, if (playing) p.accent else p.text, FontWeight.SemiBold,
                maxLines = 1)
            T("${track.artist} · ${track.format}", 13.5.sp, p.faint, maxLines = 1)
        }
        if (playing) EqualiserMark() else T(clock(track.seconds), 13.sp, p.faint)
    }
}

/** The three bars that mark the row currently playing. */
@Composable
private fun EqualiserMark() {
    val p = palette
    Canvas(Modifier.size(18.dp)) {
        val w = 2.6.dp.toPx()
        listOf(0.55f, 0.9f, 0.7f).forEachIndexed { i, h ->
            val x = w / 2 + i * (w * 2.2f)
            drawLine(
                p.accent, Offset(x, size.height), Offset(x, size.height * (1f - h)),
                w, cap = StrokeCap.Round,
            )
        }
    }
}

/** A record sitting inside the orbit: the Player off state's mark. */
@Composable
private fun RecordMark() {
    val p = palette
    Canvas(Modifier.size(210.dp)) {
        val c = Offset(size.width / 2, size.height / 2)
        val r = size.width / 2 - 4.dp.toPx()
        drawCircle(p.line.copy(alpha = .5f), r, c, style = Stroke(1.dp.toPx()))
        drawCircle(
            p.accent.copy(alpha = .45f), r * .78f, c,
            style = Stroke(
                1.5.dp.toPx(),
                pathEffect = androidx.compose.ui.graphics.PathEffect.dashPathEffect(
                    floatArrayOf(1.dp.toPx(), 7.dp.toPx())
                ),
                cap = StrokeCap.Round,
            ),
        )
        drawArc(
            p.motion.copy(alpha = .35f), 250f, 55f, false,
            topLeft = Offset(c.x - r * .78f, c.y - r * .78f),
            size = androidx.compose.ui.geometry.Size(r * 1.56f, r * 1.56f),
            style = Stroke(5.dp.toPx(), cap = StrokeCap.Round),
        )
        drawCircle(p.card, r * .5f, c)
        listOf(.40f, .32f, .24f).forEach { drawCircle(p.well, r * it, c, style = Stroke(1.5.dp.toPx())) }
        drawCircle(p.accent, r * .14f, c)
        drawCircle(p.ground, r * .03f, c)
    }
}
