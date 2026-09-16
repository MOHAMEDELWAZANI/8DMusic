package com.eightd.music.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * About: what the app is, how to set up the hard part, and where the code is.
 *
 * Only rows that actually do something are here. The story, the guides and the
 * replayable presentations from the design belong with the onboarding work and
 * land with it, rather than as buttons that lead nowhere.
 */
@Composable
fun AboutScreen(
    version: String,
    engineInfo: String,
    shizukuReady: Boolean,
    onSetupShizuku: () -> Unit,
    onOpenRepo: () -> Unit,
    onOpenUrl: (String) -> Unit,
    onReplay: (String) -> Unit,
    onOpenGuide: (Guide) -> Unit,
    bottomInset: Dp,
) {
    val p = palette

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 14.dp)) {
            PageTitle("About")
        }

        /* ------------------------------------------------------------- hero */
        Box(contentAlignment = Alignment.TopCenter) {
        Glow(
            Modifier.fillMaxWidth().height(320.dp),
            cyan = .12f, magenta = .14f,
            cyanCentre = androidx.compose.ui.geometry.Offset(.25f, .55f),
            magentaCentre = androidx.compose.ui.geometry.Offset(.72f, .3f),
        )
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp)
                .padding(top = 16.dp)
                .background(p.card, RoundedCornerShape(30.dp))
                .padding(horizontal = 20.dp, vertical = 26.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Box(
                Modifier.size(92.dp).background(p.well, RoundedCornerShape(28.dp)),
                contentAlignment = Alignment.Center,
            ) { Logo(64.dp, p.text, p.accent, p.motion) }
            T("8D MUSIC", 13.sp, p.dim, FontWeight.Bold, 3.sp,
                modifier = Modifier.padding(top = 16.dp))
            T("Real-time 8D for everything your phone plays",
                20.sp, p.text, FontWeight.Bold, align = TextAlign.Center,
                modifier = Modifier.padding(top = 8.dp))
            Row(
                Modifier.padding(top = 14.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Tag(version)
                Tag("Android 10+")
                Tag("Open source", accent = true)
            }
        }
        }

        /* ------------------------------------------------------------ story */
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp)
                .padding(top = 12.dp)
                .background(p.card, CardShape)
                .padding(18.dp),
        ) {
            Kicker("OUR STORY")
            T("One effect, written once in C++, sounding the same on Linux, Windows and Android.",
                17.sp, p.text, FontWeight.SemiBold, modifier = Modifier.padding(top = 8.dp))
            T("No uploading, no converting. What other apps play, your own files and games all " +
                    "go through the same engine on their way to your headphones.",
                14.sp, p.dim, modifier = Modifier.padding(top = 6.dp))
        }

        /* -------------------------------------------------- presentations */
        Spacer(Modifier.height(22.dp))
        T("Presentations", 20.sp, p.text, FontWeight.Bold,
            modifier = Modifier.padding(horizontal = 20.dp))
        Row(
            Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 12.dp)
                .padding(top = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            TourCard("Welcome", "5 pages", p.motion) {
                onReplay(com.eightd.music.AppState.KEY_SEEN_WELCOME)
            }
            TourCard("Studio 8D", "7 pages", p.accent) {
                onReplay(com.eightd.music.AppState.KEY_SEEN_STUDIO)
            }
            TourCard("Live", "4 pages", p.deep) {
                onReplay(com.eightd.music.AppState.KEY_SEEN_LIVE)
            }
            TourCard("Player", "4 pages", p.raised) {
                onReplay(com.eightd.music.AppState.KEY_SEEN_PLAYER)
            }
        }

        /* ----------------------------------------------------------- guides */
        Spacer(Modifier.height(22.dp))
        T("Set up & help", 20.sp, p.text, FontWeight.Bold,
            modifier = Modifier.padding(horizontal = 20.dp))
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp)
                .padding(top = 12.dp)
                .background(p.card, CardShape),
        ) {
            AboutRow(
                title = "Set up Shizuku",
                sub = if (shizukuReady) "Ready on this phone — every app can be spatialised"
                else "Hear every app in 8D, four steps, once",
                onClick = onSetupShizuku,
            )
            Guide.entries.forEach { g ->
                AboutRow(
                    title = g.title,
                    sub = when (g) {
                        Guide.HowItWorks -> "The cues that move sound around you"
                        Guide.Headphones -> "8D needs each ear to hear its own side"
                        Guide.NothingPlaying -> "Why Discord and most games don't appear"
                    },
                    onClick = { onOpenGuide(g) },
                )
            }
        }

        Spacer(Modifier.height(22.dp))
        T("Get involved", 20.sp, p.text, FontWeight.Bold,
            modifier = Modifier.padding(horizontal = 20.dp))
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp)
                .padding(top = 12.dp)
                .background(p.card, CardShape),
        ) {
            AboutRow(
                title = "Source code",
                sub = "github.com/MOHAMEDELWAZANI/8DMusic",
                onClick = onOpenRepo,
            )
            // Rows appear only once their address exists: a button that goes
            // nowhere is worse than no button.
            if (WEBSITE_URL.isNotEmpty()) AboutRow(
                title = "Website", sub = WEBSITE_URL.removePrefix("https://"),
                onClick = { onOpenUrl(WEBSITE_URL) },
            )
            if (DONATE_URL.isNotEmpty()) AboutRow(
                title = "Support 8D Music",
                sub = "Free, no ads, open source — a donation keeps it that way",
                onClick = { onOpenUrl(DONATE_URL) },
            )
        }

        /* ---------------------------------------------------------- footers */
        Spacer(Modifier.height(22.dp))
        Column(
            Modifier.fillMaxWidth().padding(horizontal = 24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            T("Your audio never leaves your phone", 13.sp, p.faint, align = TextAlign.Center)
            T(engineInfo.ifEmpty { "C++20 DSP engine · Kotlin UI" },
                12.5.sp, p.ghost, align = TextAlign.Center,
                modifier = Modifier.padding(top = 4.dp))
            T("8D needs stereo separation — headphones are strongly recommended.",
                12.5.sp, p.ghost, align = TextAlign.Center,
                modifier = Modifier.padding(top = 10.dp))
        }

        Spacer(Modifier.height(bottomInset))
    }
}

/** Fill these in and the rows appear in About. */
const val WEBSITE_URL = ""
const val DONATE_URL = ""

@Composable
private fun TourCard(title: String, pages: String, tint: Color, onClick: () -> Unit) {
    val p = palette
    Column(
        Modifier
            .width(136.dp)
            .height(150.dp)
            .background(p.card, RoundedCornerShape(24.dp))
            .clickable { onClick() }
            .padding(14.dp),
        verticalArrangement = Arrangement.SpaceBetween,
    ) {
        Box(
            Modifier.size(34.dp).background(tint.copy(alpha = .22f), CircleShape),
            contentAlignment = Alignment.Center,
        ) { T("▶", 12.sp, tint, FontWeight.Bold) }
        Column {
            T(title, 16.sp, p.text, FontWeight.Bold, maxLines = 1)
            T(pages, 12.5.sp, p.faint)
        }
    }
}

@Composable
private fun Tag(label: String, accent: Boolean = false) {
    val p = palette
    Box(
        Modifier
            .background(if (accent) p.motion.copy(alpha = .16f) else p.well, Pill)
            .padding(horizontal = 11.dp, vertical = 5.dp)
    ) {
        T(label, 12.sp, if (accent) p.motion else p.dim, FontWeight.SemiBold)
    }
}

@Composable
private fun AboutRow(title: String, sub: String, onClick: () -> Unit) {
    val p = palette
    Row(
        Modifier.fillMaxWidth().clickable { onClick() }.padding(16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Column(Modifier.weight(1f)) {
            T(title, 16.sp, p.text, FontWeight.SemiBold)
            T(sub, 13.sp, p.faint, maxLines = 2)
        }
        T("›", 20.sp, p.ghost)
    }
}
