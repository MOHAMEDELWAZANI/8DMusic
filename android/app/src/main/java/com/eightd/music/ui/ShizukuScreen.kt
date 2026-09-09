package com.eightd.music.ui

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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.eightd.music.shizuku.ShizukuBridge

private val Square = RoundedCornerShape(2.dp)

/**
 * 1g — system-wide setup.
 *
 * A live checklist rather than a slideshow: every row reflects what the phone
 * actually reports right now, so a user who already did a step never has to
 * wonder whether it took.
 */
@Composable
fun ShizukuScreen(
    status: ShizukuBridge.Status,
    onInstall: () -> Unit,
    onOpenShizuku: () -> Unit,
    onRequestPermission: () -> Unit,
    onGrant: () -> Unit,
    onSkip: () -> Unit,
) {
    val p = palette

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp)) {
        Header2("ADVANCED MODE", "System-wide audio")

        Spacer(Modifier.height(10.dp))
        Text(
            "Four steps, once. After this every app on the phone can be spatialised — " +
                "including the ones that block capture.",
            16.sp, p.dim,
        )

        Spacer(Modifier.height(8.dp))
        Step(1, "Install Shizuku", status.installed,
            if (status.installed) "Found on this device" else "Get it from the Play Store or GitHub",
            action = if (status.installed) null else "Install" to onInstall)

        Step(2, "Start Shizuku", status.running,
            if (status.running) "Service is running"
            else "Open Shizuku and start it with wireless debugging",
            action = if (status.running || !status.installed) null else "Open Shizuku" to onOpenShizuku)

        Step(3, "Allow 8D Music", status.permitted,
            if (status.permitted) "Shizuku access granted"
            else "Shizuku will ask once. Nothing leaves the device.",
            action = if (status.permitted || !status.running) null else "Request" to onRequestPermission)

        Step(4, "Grant system audio access", status.dumpGranted,
            if (status.dumpGranted) "Ready — every app can be spatialised"
            else "Lets 8D Music see which apps are playing, so the original can be silenced",
            action = if (status.dumpGranted || !status.permitted) null else "Grant" to onGrant)

        Spacer(Modifier.height(22.dp))

        if (status.ready) {
            Box(
                Modifier.fillMaxWidth().border(1.dp, p.accent, Square)
                    .background(p.panelSoft, Square).padding(16.dp)
            ) {
                Text(
                    "System-wide is ready. Direct capture will now silence the original " +
                        "stream, so you hear only the spatialised version.",
                    15.sp, p.accent,
                )
            }
        } else {
            Text("Don't want to do this?", 17.sp, p.text, FontWeight.SemiBold)
            Spacer(Modifier.height(6.dp))
            Text(
                "Direct capture and the local player need none of it — you keep full 8D " +
                    "on your own files and on the apps that allow capture.",
                15.sp, p.dim,
            )
            Spacer(Modifier.height(14.dp))
            Box(
                Modifier.fillMaxWidth().border(1.dp, p.line, Square)
                    .clickable { onSkip() }.padding(vertical = 15.dp),
                contentAlignment = Alignment.Center,
            ) { Text("Skip for now", 16.sp, p.text) }
        }

        Spacer(Modifier.height(30.dp))
    }
}

@Composable
private fun Step(
    number: Int,
    title: String,
    done: Boolean,
    detail: String,
    action: Pair<String, () -> Unit>?,
) {
    val p = palette
    Row(
        Modifier.fillMaxWidth().padding(top = 18.dp),
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Box(
            Modifier.size(26.dp)
                .background(if (done) p.accent else Color.Transparent, Square)
                .border(1.dp, if (done) p.accent else p.boxBorder, Square),
            contentAlignment = Alignment.Center,
        ) {
            Text(if (done) "✓" else "$number", 13.sp, if (done) p.mark else p.faint)
        }
        Column(Modifier.weight(1f)) {
            Text(title, 18.sp, if (done) p.text else p.text, FontWeight.SemiBold)
            Spacer(Modifier.height(4.dp))
            Text(detail, 14.sp, p.dim)
            if (action != null) {
                Spacer(Modifier.height(10.dp))
                Box(
                    Modifier.border(1.dp, p.accent, Square)
                        .clickable { action.second() }
                        .padding(horizontal = 18.dp, vertical = 10.dp)
                ) { Text(action.first, 15.sp, p.accent, FontWeight.SemiBold) }
            }
        }
    }
}
