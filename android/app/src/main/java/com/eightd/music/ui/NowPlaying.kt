package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

private val Square = RoundedCornerShape(2.dp)

/**
 * The now-playing panel from the mockup.
 *
 * The transport drives whatever is producing the sound, not the effect -- the
 * same split the desktop build makes between the player and the orbit.
 */
@Composable
fun NowPlayingCard(
    source: String,
    title: String,
    artist: String,
    cover: String,
    elapsed: Int,
    total: Int,
    playing: Boolean,
    engineOn: Boolean,
    engineEnabled: Boolean,
    onPrev: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onSeek: (Float) -> Unit,
    onEngine: () -> Unit,
) {
    val p = palette

    Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.Bottom) {
            Text("NOW PLAYING", 11.sp, p.accent, FontWeight.SemiBold, letterSpacing = 2.sp)
            Text(source, 13.sp, p.faint, maxLines = 1)
        }

        Row(Modifier.fillMaxWidth().padding(top = 14.dp), horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            Box(Modifier.size(64.dp).background(p.text, Square), contentAlignment = Alignment.Center) {
                Text(cover, 22.sp, p.ground)
            }
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(title, 19.sp, p.text, FontWeight.SemiBold, maxLines = 1)
                Text(artist, 15.sp, p.dim, maxLines = 1)
                Row(verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(clock(elapsed), 12.sp, p.faint)
                    ProgressBar(
                        fraction = if (total > 0) elapsed.toFloat() / total else 0f,
                        modifier = Modifier.weight(1f),
                        onSeek = onSeek,
                    )
                    Text(clock(total), 12.sp, p.faint)
                }
            }
        }

        Row(
            Modifier.fillMaxWidth().padding(top = 18.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TransportButton(onPrev) { PrevIcon(p.text) }
            TransportButton(onPlayPause) { if (playing) PauseIcon(p.text) else PlayIcon(p.text) }
            TransportButton(onNext) { NextIcon(p.text) }
            Spacer(Modifier.weight(1f))
            Box(
                Modifier
                    .defaultMinSize(minWidth = 108.dp, minHeight = 48.dp)
                    .background(if (engineOn) p.engineOnBg else p.engineBg, Square)
                    .then(if (engineOn) Modifier.border(1.dp, p.accent, Square) else Modifier)
                    .clickable(enabled = engineEnabled) { onEngine() }
                    .padding(horizontal = 18.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    if (engineOn) "Stop" else "Start", 17.sp,
                    if (engineOn) p.engineOnText else p.engineText, FontWeight.SemiBold,
                )
            }
        }
    }
}

@Composable
private fun ProgressBar(fraction: Float, modifier: Modifier, onSeek: (Float) -> Unit) {
    val p = palette
    Box(
        modifier
            .height(20.dp)
            .pointerInput(Unit) {
                detectTapGestures { off -> onSeek((off.x / size.width).coerceIn(0f, 1f)) }
            },
        contentAlignment = Alignment.CenterStart,
    ) {
        Canvas(Modifier.fillMaxWidth().height(20.dp)) {
            val y = size.height / 2f
            val h = 3.dp.toPx()
            drawRect(p.line, topLeft = Offset(0f, y - h / 2), size = Size(size.width, h))
            val w = size.width * fraction.coerceIn(0f, 1f)
            drawRect(p.motion, topLeft = Offset(0f, y - h / 2), size = Size(w, h))
            drawCircle(p.motion, radius = 6.5.dp.toPx(), center = Offset(w, y))
        }
    }
}

@Composable
private fun TransportButton(onClick: () -> Unit, icon: @Composable () -> Unit) {
    val p = palette
    Box(
        Modifier
            .width(52.dp)
            .height(48.dp)
            .border(1.dp, p.line, Square)
            .clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) { icon() }
}

@Composable private fun PlayIcon(c: Color) = Canvas(Modifier.size(16.dp)) {
    val p = Path().apply {
        moveTo(0f, 0f); lineTo(size.width, size.height / 2f); lineTo(0f, size.height); close()
    }
    drawPath(p, c)
}

@Composable private fun PauseIcon(c: Color) = Canvas(Modifier.size(14.dp)) {
    val w = size.width * 0.3f
    drawRect(c, size = Size(w, size.height))
    drawRect(c, topLeft = Offset(size.width - w, 0f), size = Size(w, size.height))
}

@Composable private fun PrevIcon(c: Color) = Canvas(Modifier.size(18.dp)) {
    drawRect(c, size = Size(2.4.dp.toPx(), size.height))
    val p = Path().apply {
        moveTo(size.width, 0f); lineTo(size.width, size.height); lineTo(4.dp.toPx(), size.height / 2f); close()
    }
    drawPath(p, c)
}

@Composable private fun NextIcon(c: Color) = Canvas(Modifier.size(18.dp)) {
    drawRect(c, topLeft = Offset(size.width - 2.4.dp.toPx(), 0f), size = Size(2.4.dp.toPx(), size.height))
    val p = Path().apply {
        moveTo(0f, 0f); lineTo(0f, size.height); lineTo(size.width - 4.dp.toPx(), size.height / 2f); close()
    }
    drawPath(p, c)
}

internal fun clock(seconds: Int): String =
    if (seconds < 0) "--:--" else "%d:%02d".format(seconds / 60, seconds % 60)
