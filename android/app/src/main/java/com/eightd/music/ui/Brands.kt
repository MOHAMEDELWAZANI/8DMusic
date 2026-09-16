package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/*
 * The apps Live talks about.
 *
 * The phone's own launcher icon is always preferred — it is the real thing and
 * costs nothing. These drawn marks are the fallback for a phone that does not
 * have the app installed, so the presentation still shows what it is talking
 * about. They are simplified shapes in each brand's colour, used to refer to
 * the apps 8D Music works with; the marks themselves remain their owners'.
 */

const val PKG_YOUTUBE = "com.google.android.youtube"
const val PKG_SPOTIFY = "com.spotify.music"
const val PKG_BRAVE = "com.brave.browser"
const val PKG_CHROME = "com.android.chrome"
const val PKG_NETFLIX = "com.netflix.mediaclient"

/** The logo files Mohamed supplied, used ahead of anything drawn by hand. */
private fun logoRes(pkg: String): Int? = when (pkg) {
    PKG_YOUTUBE -> com.eightd.music.R.drawable.logo_youtube
    PKG_SPOTIFY -> com.eightd.music.R.drawable.logo_spotify
    PKG_BRAVE -> com.eightd.music.R.drawable.logo_brave
    else -> null
}

/** True when this package has a mark to fall back on: a file, or a drawn one. */
fun hasBrandMark(pkg: String) =
    logoRes(pkg) != null || pkg in setOf(PKG_CHROME, PKG_NETFLIX)

/**
 * The app's icon if it is installed, the drawn mark if not, and a plain speaker
 * when it is an app we have no mark for.
 */
@Composable
fun AppMark(pkg: String?, size: Dp, corner: Dp = size / 4) {
    val p = palette
    val icon = if (pkg != null) rememberAppIcon(pkg, (size.value * 2).toInt()) else null
    Box(Modifier.size(size), contentAlignment = Alignment.Center) {
        val logo = pkg?.let { logoRes(it) }
        when {
            icon != null -> Image(
                bitmap = icon,
                contentDescription = pkg,
                modifier = Modifier.size(size).clip(RoundedCornerShape(corner)),
            )
            // The supplied logos keep their own shape: YouTube's badge is wider
            // than it is tall, so it is fitted rather than cropped square.
            logo != null -> Image(
                painter = androidx.compose.ui.res.painterResource(logo),
                contentDescription = pkg,
                contentScale = ContentScale.Fit,
                modifier = Modifier.size(size).padding(size * .08f),
            )
            pkg != null && hasBrandMark(pkg) -> Canvas(Modifier.size(size)) { brandMark(pkg) }
            else -> Canvas(Modifier.size(size * .62f)) { speaker(p.faint) }
        }
    }
}

private fun DrawScope.brandMark(pkg: String) {
    val s = size.width
    when (pkg) {
        PKG_CHROME -> {
            drawCircle(Color(0xFFEA4335), s / 2f, Offset(s / 2, s / 2))
            drawArc(
                Color(0xFF34A853), 30f, 120f, true,
                topLeft = Offset(0f, 0f), size = Size(s, s),
            )
            drawArc(
                Color(0xFFFBBC05), 150f, 120f, true,
                topLeft = Offset(0f, 0f), size = Size(s, s),
            )
            drawCircle(Color.White, s * .26f, Offset(s / 2, s / 2))
            drawCircle(Color(0xFF4285F4), s * .19f, Offset(s / 2, s / 2))
        }
        PKG_NETFLIX -> {
            drawRoundRect(
                Color(0xFF141414), size = size,
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(s * .22f),
            )
            val n = Path().apply {
                moveTo(s * .30f, s * .20f)
                lineTo(s * .42f, s * .20f)
                lineTo(s * .70f, s * .80f)
                lineTo(s * .58f, s * .80f)
                close()
            }
            drawPath(n, Color(0xFFE50914))
            drawRect(Color(0xFFE50914), Offset(s * .30f, s * .20f), Size(s * .12f, s * .60f))
            drawRect(Color(0xFFE50914), Offset(s * .58f, s * .20f), Size(s * .12f, s * .60f))
        }
    }
}

/** Any app we have no mark for: something is making sound, that is all we know. */
private fun DrawScope.speaker(tint: Color) {
    val s = size.width
    val w = 2.dp.toPx()
    drawCircle(tint, s * .12f, Offset(s / 2, s / 2))
    listOf(.28f, .44f).forEach { r ->
        drawArc(
            tint, -50f, 100f, false,
            topLeft = Offset(s / 2 - s * r, s / 2 - s * r), size = Size(s * r * 2, s * r * 2),
            style = Stroke(w, cap = StrokeCap.Round),
        )
        drawArc(
            tint, 130f, 100f, false,
            topLeft = Offset(s / 2 - s * r, s / 2 - s * r), size = Size(s * r * 2, s * r * 2),
            style = Stroke(w, cap = StrokeCap.Round),
        )
    }
}
