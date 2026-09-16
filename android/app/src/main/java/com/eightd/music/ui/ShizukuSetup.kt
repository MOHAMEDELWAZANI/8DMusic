package com.eightd.music.ui

import androidx.compose.foundation.Canvas
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
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.eightd.music.shizuku.ShizukuBridge

/**
 * System-wide setup, v2.
 *
 * A live checklist rather than a slideshow: every row reflects what the phone
 * reports right now, so someone who already did a step never wonders whether it
 * took. Come back from Shizuku and the tick is already there.
 */
@Composable
fun ShizukuSetupScreen(
    status: ShizukuBridge.Status,
    onInstall: () -> Unit,
    onOpenShizuku: () -> Unit,
    onRequestPermission: () -> Unit,
    onGrant: () -> Unit,
    onSkip: () -> Unit,
) {
    val p = palette
    val done = listOf(status.installed, status.running, status.permitted, status.dumpGranted)
    val completed = done.count { it }
    val current = done.indexOfFirst { !it }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            CircleIcon("‹") { onSkip() }
            T("Skip for now", 14.sp, p.faint, FontWeight.SemiBold,
                modifier = Modifier.clickable { onSkip() })
        }

        Box(Modifier.fillMaxWidth().padding(top = 18.dp), Alignment.Center) {
            Box(contentAlignment = Alignment.Center) {
                Canvas(Modifier.size(132.dp)) {
                    val stroke = 9.dp.toPx()
                    val inset = stroke / 2
                    drawArc(
                        p.well, -90f, 360f, false,
                        topLeft = Offset(inset, inset),
                        size = Size(size.width - stroke, size.height - stroke),
                        style = Stroke(stroke),
                    )
                    if (completed > 0) drawArc(
                        p.accent, -90f, 360f * completed / 4f, false,
                        topLeft = Offset(inset, inset),
                        size = Size(size.width - stroke, size.height - stroke),
                        style = Stroke(stroke, cap = StrokeCap.Round),
                    )
                }
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    T("$completed of 4", 18.sp, p.text, FontWeight.Bold)
                    T("done", 13.sp, p.faint)
                }
            }
        }

        Column(
            Modifier.fillMaxWidth().padding(horizontal = 28.dp).padding(top = 16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            T("Unlock every app", 26.sp, p.text, FontWeight.Bold, align = TextAlign.Center)
            T("Shizuku gives 8D Music one permission, once. It survives reboots and updates, " +
                "and you never need Shizuku again.",
                15.sp, p.dim, align = TextAlign.Center,
                modifier = Modifier.padding(top = 8.dp))
        }

        Spacer(Modifier.height(20.dp))

        Step(
            number = 1, title = "Install Shizuku", done = status.installed, current = current == 0,
            detail = if (status.installed) "Found on this phone"
            else "Get it from the Play Store or GitHub",
            action = if (status.installed) null else "Install" to onInstall,
        )
        Step(
            number = 2, title = "Start Shizuku", done = status.running, current = current == 1,
            detail = if (status.running) "Service is running"
            else "Open Shizuku and start it with wireless debugging",
            action = if (status.running || !status.installed) null
            else "Open Shizuku" to onOpenShizuku,
        )
        Step(
            number = 3, title = "Allow 8D Music", done = status.permitted, current = current == 2,
            detail = if (status.permitted) "Shizuku access granted"
            else "Shizuku asks once. Nothing leaves your phone.",
            action = if (status.permitted || !status.running) null
            else "Request" to onRequestPermission,
        )
        Step(
            number = 4, title = "Grant system audio access", done = status.dumpGranted,
            current = current == 3,
            detail = if (status.dumpGranted) "Ready — every app can be spatialised"
            else "Lets 8D Music quiet the original sound, so you only hear the 8D version",
            action = if (status.dumpGranted || !status.permitted) null else "Grant" to onGrant,
        )

        Spacer(Modifier.height(18.dp))

        if (status.ready) {
            Box(Modifier.padding(horizontal = 12.dp)) {
                PrimaryPill("Back to Live") { onSkip() }
            }
        } else {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 24.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Box(Modifier.size(16.dp).padding(top = 3.dp)) {
                    Canvas(Modifier.fillMaxSize()) {
                        drawCircle(p.ghost, size.width / 2, style = Stroke(2.dp.toPx()))
                    }
                }
                T("This page updates by itself. Come back from Shizuku and finished steps are " +
                    "already ticked.", 13.sp, p.faint)
            }
        }

        Spacer(Modifier.height(40.dp))
    }
}

@Composable
private fun Step(
    number: Int,
    title: String,
    detail: String,
    done: Boolean,
    current: Boolean,
    action: Pair<String, () -> Unit>?,
) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 5.dp)
            .background(if (current) p.presetTint else p.card, RoundedCornerShape(24.dp))
            .then(
                if (current) Modifier.border(1.5.dp, p.accent.copy(alpha = .7f),
                    RoundedCornerShape(24.dp)) else Modifier
            )
            .padding(16.dp)
            .alpha(if (!done && !current) .55f else 1f),
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Box(
            Modifier
                .size(34.dp)
                .background(if (done) p.accent else p.well, CircleShape)
                .then(
                    if (current) Modifier.border(2.dp, p.accent, CircleShape) else Modifier
                ),
            contentAlignment = Alignment.Center,
        ) {
            if (done) Canvas(Modifier.size(16.dp)) {
                val w = 2.4.dp.toPx()
                drawLine(p.presetTint, Offset(size.width * .2f, size.height * .55f),
                    Offset(size.width * .42f, size.height * .78f), w, cap = StrokeCap.Round)
                drawLine(p.presetTint, Offset(size.width * .42f, size.height * .78f),
                    Offset(size.width * .82f, size.height * .25f), w, cap = StrokeCap.Round)
            } else T("$number", 15.sp, if (current) p.deep else p.faint, FontWeight.Bold)
        }
        Column(Modifier.weight(1f)) {
            T(title, 17.sp, if (current) p.deep else p.text, FontWeight.Bold)
            T(detail, 14.sp, p.dim, modifier = Modifier.padding(top = 3.dp))
            if (action != null) {
                Box(
                    Modifier
                        .padding(top = 12.dp)
                        .background(p.accent, Pill)
                        .clickable { action.second() }
                        .padding(horizontal = 18.dp, vertical = 11.dp),
                ) { T(action.first, 14.sp, p.presetTint, FontWeight.Bold) }
            }
        }
    }
}

