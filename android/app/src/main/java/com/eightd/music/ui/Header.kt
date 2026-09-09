package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** The lockup from the mockup: four bars, the swoop, and the pink head. */
@Composable
fun Logo(size: androidx.compose.ui.unit.Dp, ink: Color, accent: Color, motion: Color) {
    Canvas(Modifier.size(size)) {
        val s = this.size.width / 512f
        fun bar(x: Float, y: Float, h: Float) = drawRoundRect(
            color = ink,
            topLeft = Offset(x * s, y * s),
            size = Size(44f * s, h * s),
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(22f * s),
        )
        bar(148f, 200f, 92f)
        bar(211f, 143f, 149f)
        bar(274f, 175f, 117f)
        bar(339f, 228f, 64f)

        val p = Path().apply {
            moveTo(100f * s, 316f * s)
            cubicTo(170f * s, 382f * s, 342f * s, 382f * s, 408f * s, 320f * s)
        }
        drawPath(p, accent, style = Stroke(width = 30f * s, cap = androidx.compose.ui.graphics.StrokeCap.Round))
        drawCircle(motion, radius = 30f * s, center = Offset(410f * s, 320f * s))
    }
}

@Composable
fun Header(onToggleTheme: () -> Unit) {
    val p = palette
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(bottom = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(9.dp)) {
            Logo(26.dp, p.text, p.accent, p.motion)
            Text("8D MUSIC", 11.sp, p.dim, androidx.compose.ui.text.font.FontWeight.SemiBold, letterSpacing = 2.sp)
        }
        Text(
            "SPATIAL AUDIO", 11.sp, p.ghost, letterSpacing = 1.8.sp,
            modifier = Modifier.clickable { onToggleTheme() }
        )
    }
}
