package com.eightd.music.ui

import androidx.compose.foundation.background
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
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * The written guides from About.
 *
 * Content, not chrome: every claim here matches what the engine actually does,
 * because a help page that oversells is worse than no help page.
 */
enum class Guide(val title: String, val kicker: String) {
    HowItWorks("How 8D works", "THE EFFECT"),
    Headphones("Why headphones", "LISTENING"),
    NothingPlaying("An app shows \"Nothing playing\"", "LIMITS"),
}

@Composable
fun GuideScreen(guide: Guide, onBack: () -> Unit, bottomInset: Dp) {
    val p = palette
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp).padding(top = 12.dp)) {
            CircleIcon("‹") { onBack() }
        }
        Column(Modifier.padding(horizontal = 24.dp).padding(top = 18.dp)) {
            Kicker(guide.kicker)
            T(guide.title, 30.sp, p.text, FontWeight.Bold,
                modifier = Modifier.padding(top = 10.dp))
        }
        Spacer(Modifier.height(18.dp))

        when (guide) {
            Guide.HowItWorks -> {
                Para("The sound is treated as a source orbiting your head. Rather than " +
                    "swinging the stereo balance left and right, the position is turned into " +
                    "the cues a real sound would produce.")
                Point("Time between the ears",
                    "The far ear hears the sound up to about 0.7 ms later. This is what pushes " +
                        "the image outside your head instead of leaving it stuck between your ears.")
                Point("Level",
                    "Constant-power panning, so loudness stays steady as the source travels.")
                Point("Head shadow",
                    "Your skull blocks high frequencies, so the far ear gets a gentle treble " +
                        "roll-off.")
                Point("Front and back",
                    "Positions behind you lose a little upper-mid, the way the outer ear shapes " +
                        "sound arriving from the rear.")
                Point("Distance",
                    "Level, air absorption and how much reverb is sent all follow the orbit " +
                        "radius — the Distance knob in Studio.")
                Para("Delay times and pan gains are interpolated for every sample, so nothing " +
                    "clicks or zippers while you turn a knob.")
            }

            Guide.Headphones -> {
                Para("8D works by giving each ear its own version of the sound: slightly " +
                    "different timing, level and tone.")
                Para("Speakers send both versions to both ears, which mixes them back together " +
                    "and undoes the effect. Any headphones or earbuds work — they don't need " +
                    "to be expensive, or to advertise spatial audio of their own.")
                Point("Turn other spatial effects off",
                    "Two effects fighting each other sound worse than either alone. If your " +
                        "phone or headphones have their own spatial mode, switch it off.")
            }

            Guide.NothingPlaying -> {
                Para("Live shows what is playing by reading the media session an app publishes. " +
                    "Spotify publishes one, and Chromium browsers report whatever the page says.")
                Para("Discord and most games publish nothing at all. They read as \"Nothing " +
                    "playing\" while you can plainly hear them. That is the boundary of " +
                    "Android's API, not a fault in the app.")
                Point("The effect still applies",
                    "Whether the title shows or not has nothing to do with whether the sound is " +
                        "spatialised — that depends only on whether the app allows capture, or " +
                        "whether you set up System-wide.")
            }
        }

        Spacer(Modifier.height(bottomInset))
    }
}

@Composable
private fun Para(text: String) {
    T(text, 16.sp, palette.dim,
        modifier = Modifier.padding(horizontal = 24.dp).padding(bottom = 16.dp))
}

@Composable
private fun Point(title: String, body: String) {
    val p = palette
    Column(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .padding(bottom = 10.dp)
            .background(p.card, CardShape)
            .padding(18.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Box(Modifier.size(8.dp).background(p.accent, CircleShape))
            T(title, 16.sp, p.text, FontWeight.Bold)
        }
        T(body, 14.5.sp, p.dim, modifier = Modifier.padding(top = 8.dp))
    }
}
