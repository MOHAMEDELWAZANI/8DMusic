package com.eightd.music.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.eightd.music.AppState
import com.eightd.music.Character
import com.eightd.music.Mode
import com.eightd.music.Params
import com.eightd.music.Presets
import com.eightd.music.SavedPreset
import com.eightd.music.Source
import com.eightd.music.Track
import kotlin.math.roundToInt

private val Square = RoundedCornerShape(2.dp)

/* ------------------------------------------------------------------ 1a */

/** Where should the sound come from? Setup cost stated up front. */
@Composable
fun SourcePickerScreen(state: AppState, onContinue: () -> Unit) {
    val p = palette
    var picked by remember { mutableStateOf(state.source) }

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp)
    ) {
        Spacer(Modifier.height(12.dp))
        Row(verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(9.dp)) {
            Logo(28.dp, p.text, p.accent, p.motion)
            Text("8D MUSIC", 11.sp, p.dim, FontWeight.SemiBold, letterSpacing = 2.sp)
        }
        Spacer(Modifier.height(26.dp))
        Text("Where should the sound come from?", 34.sp, p.text, FontWeight.SemiBold)
        Spacer(Modifier.height(10.dp))
        Text("Pick one now, change it any time. Two of the three need no setup at all.",
            16.sp, p.dim)

        Spacer(Modifier.height(22.dp))
        Source.entries.forEach { s ->
            SourceCard(s, s == picked, describe(s), examples(s)) { picked = s }
            Spacer(Modifier.height(12.dp))
        }

        Spacer(Modifier.height(10.dp))
        Primary("Continue with ${picked.label.lowercase()}") { state.choose(picked); onContinue() }
        Spacer(Modifier.height(10.dp))
        Box(Modifier.fillMaxWidth().clickable { state.choose(Source.Local); onContinue() },
            contentAlignment = Alignment.Center) {
            Text("Decide later", 16.sp, p.faint, modifier = Modifier.padding(12.dp))
        }
        Spacer(Modifier.height(30.dp))
    }
}

private fun describe(s: Source) = when (s) {
    Source.Capture -> "Spatialises whatever app is playing, when that app permits playback capture."
    Source.Local -> "Play files from this device and build playlists. MP3, FLAC, WAV and more."
    Source.SystemWide -> "Every app, no exceptions. Needs a one-time pairing through Developer options."
}

private fun examples(s: Source) = when (s) {
    Source.Capture -> listOf("YouTube", "Games", "Most players")
    Source.Local -> listOf("MP3", "FLAC", "WAV")
    Source.SystemWide -> listOf("Spotify", "Brave", "Everything")
}

@Composable
private fun SourceCard(
    s: Source, selected: Boolean, body: String, tags: List<String>, onPick: () -> Unit,
) {
    val p = palette
    Column(
        Modifier
            .fillMaxWidth()
            .border(1.dp, if (selected) p.accent else p.line, Square)
            .background(if (selected) p.panelSoft else Color.Transparent, Square)
            .clickable { onPick() }
            .padding(16.dp)
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically) {
            Text(s.label, 19.sp, if (selected) p.accent else p.text, FontWeight.SemiBold)
            Text(s.note, 10.sp, p.faint, letterSpacing = 1.6.sp)
        }
        Spacer(Modifier.height(8.dp))
        Text(body, 15.sp, p.dim)
        Spacer(Modifier.height(12.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            tags.forEach { t ->
                Box(Modifier.border(1.dp, p.lineSoft, Square).padding(horizontal = 8.dp, vertical = 4.dp)) {
                    Text(t, 12.sp, p.faint)
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ 1b */

@Composable
fun PlayerScreen(
    state: AppState,
    angle: Float, distance: Float, peakL: Float, peakR: Float,
    trail: List<Float>,
    captureNote: String?,
    onEngineToggle: () -> Unit,
    onPlayPause: () -> Unit,
    onPrev: () -> Unit,
    onNext: () -> Unit,
    onSeek: (Float) -> Unit,
) {
    val p = palette
    val v = state.params

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        StatusStrip(state, captureNote)

        Box(Modifier.padding(horizontal = 20.dp).padding(top = 8.dp)) {
            OrbitView(
                angle = angle, distance = distance, radius = v.radius,
                running = state.engineOn && state.playing, trail = trail,
                onScrub = if (v.mode == Mode.Static) { a -> state.apply(v.copy(manualAngle = a)) } else null,
            )
        }
        Spacer(Modifier.height(10.dp))
        Readout(angle, distance, peakL, peakR, state.engineOn && state.playing)

        Spacer(Modifier.height(18.dp))
        Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp),
            horizontalArrangement = Arrangement.SpaceBetween) {
            Text(v.mode.label, 15.sp, p.accent, FontWeight.SemiBold)
            Text("%.1f s".format(1f / v.speed.coerceAtLeast(0.001f)), 15.sp, p.faint)
        }

        Divider(Modifier.padding(horizontal = 20.dp).padding(top = 16.dp))
        Spacer(Modifier.height(18.dp))

        NowPlayingCard(
            source = state.source.label,
            title = state.nowTitle,
            artist = state.nowArtist,
            cover = state.nowTitle.take(2),
            elapsed = state.nowSeconds,
            total = state.totalSeconds,
            playing = state.playing,
            engineOn = state.engineOn,
            engineEnabled = true,
            onPrev = onPrev,
            onPlayPause = onPlayPause,
            onNext = onNext,
            onSeek = onSeek,
            onEngine = onEngineToggle,
        )

        Column(Modifier.padding(horizontal = 20.dp)) {
            ParamSlider("Speed", v.speed, 0.01f..1f, "%.2f rot/s".format(v.speed)) {
                state.apply(v.copy(speed = it))
            }
            ParamSlider("Intensity", v.depth, 0f..1f, "${(v.depth * 100).roundToInt()}%") {
                state.apply(v.copy(depth = it))
            }
        }
        Spacer(Modifier.height(24.dp))
    }
}

@Composable
private fun StatusStrip(state: AppState, note: String?) {
    val p = palette
    val on = state.engineOn
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 14.dp, bottom = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Box(Modifier.size(7.dp).background(
                if (note != null) p.motion else if (on) p.statusOn else p.statusOff,
                RoundedCornerShape(50)))
            Text(note ?: if (on) "PROCESSING" else "READY", 11.sp,
                if (note != null) p.motion else if (on) p.statusOn else p.statusOff,
                letterSpacing = 1.8.sp)
        }
        Text(state.source.label, 13.sp, p.faint)
    }
}

/* ------------------------------------------------------------------ 1c */

@Composable
fun ModesScreen(state: AppState) {
    val p = palette
    val v = state.params
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp)) {
        Header2("8D MODES", "How it moves")

        Mode.entries.forEach { m ->
            ModeRow(m, m == v.mode) { state.apply(v.copy(mode = m)) }
        }

        Spacer(Modifier.height(8.dp))
        ParamSlider("Speed", v.speed, 0.01f..1f,
            "%.2f rot/s · %.1f s".format(v.speed, 1f / v.speed.coerceAtLeast(0.001f))) {
            state.apply(v.copy(speed = it))
        }
        ParamSlider("Distance", v.radius, 0.2f..3f, "%.2f m".format(v.radius),
            "Virtual distance — changes level, tone and room") { state.apply(v.copy(radius = it)) }
        ParamSlider("Smoothness", v.smoothness, 0f..1f, "${(v.smoothness * 100).roundToInt()}%") {
            state.apply(v.copy(smoothness = it))
        }
        ParamSlider("Manual position", v.manualAngle, -Math.PI.toFloat()..Math.PI.toFloat(),
            "${Math.toDegrees(v.manualAngle.toDouble()).roundToInt()}°",
            "Used by the Static position mode", enabled = v.mode == Mode.Static) {
            state.apply(v.copy(manualAngle = it))
        }
        Box(Modifier.fillMaxWidth().padding(top = 14.dp)) {
            Field(v.direction, listOf(1, -1),
                { if (it > 0) "Clockwise" else "Counter-clockwise" }) { state.apply(v.copy(direction = it)) }
        }
        CheckRow("Pause the orbit when nothing plays", v.pauseWhenSilent) {
            state.apply(v.copy(pauseWhenSilent = !v.pauseWhenSilent))
        }
        Spacer(Modifier.height(30.dp))
    }
}

@Composable
private fun ModeRow(m: Mode, selected: Boolean, onPick: () -> Unit) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(top = 10.dp)
            .border(1.dp, if (selected) p.accent else p.line, Square)
            .background(if (selected) p.panelSoft else Color.Transparent, Square)
            .clickable { onPick() }
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(Modifier.weight(1f)) {
            Text(m.label, 18.sp, if (selected) p.accent else p.text, FontWeight.SemiBold)
            Text(modeBlurb(m), 14.sp, p.dim)
        }
        if (selected) Text("✓", 16.sp, p.accent)
    }
}

private fun modeBlurb(m: Mode) = when (m) {
    Mode.Circular -> "Full circle around your head"
    Mode.PingPong -> "Swings left to right"
    Mode.Pendulum -> "Eases at the edges, quickest through the middle"
    Mode.Linear -> "A straight sweep across the field"
    Mode.Figure8 -> "Crosses through the centre"
    Mode.Spiral -> "Drifts closer, then away"
    Mode.Random -> "Wanders without repeating"
    Mode.Static -> "Parked where you put it"
}

/* ------------------------------------------------------------------ 1d */

@Composable
fun EffectsScreen(state: AppState, onSavePreset: () -> Unit) {
    val p = palette
    val v = state.params
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp)) {
        Header2("EFFECTS", "Room and colour")

        Row(Modifier.horizontalScroll(rememberScrollState()).padding(top = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Character.entries.forEach { c ->
                Chip(c.label, v.character == c) { state.apply(v.copy(character = c)) }
            }
        }
        ParamSlider("Character amount", v.characterAmount, 0f..1f,
            "${(v.characterAmount * 100).roundToInt()}%",
            if (v.character == Character.Clean) "The source passes through untouched"
            else "How much of the ${v.character.label.lowercase()} colour is printed on the source",
            enabled = v.character != Character.Clean) { state.apply(v.copy(characterAmount = it)) }

        ParamSlider("Reverb", v.reverbMix, 0f..1f, "${(v.reverbMix * 100).roundToInt()}%") {
            state.apply(v.copy(reverbMix = it))
        }
        ParamSlider("Room size", v.reverbSize, 0f..1f, "${(v.reverbSize * 100).roundToInt()}%",
            enabled = v.reverbMix > 0f) { state.apply(v.copy(reverbSize = it)) }
        ParamSlider("Damping", v.reverbDamp, 0f..1f, "${(v.reverbDamp * 100).roundToInt()}%",
            enabled = v.reverbMix > 0f) { state.apply(v.copy(reverbDamp = it)) }

        ParamSlider("Delay", v.delayMix, 0f..1f, "${(v.delayMix * 100).roundToInt()}%",
            if (v.delayMix == 0f) "Off — raise to hear the repeats travel with the source" else null) {
            state.apply(v.copy(delayMix = it))
        }
        ParamSlider("Delay time", v.delayTime, 0.02f..0.9f, "${(v.delayTime * 1000).roundToInt()} ms",
            enabled = v.delayMix > 0f) { state.apply(v.copy(delayTime = it)) }
        ParamSlider("Delay feedback", v.delayFeedback, 0f..0.95f,
            "${(v.delayFeedback * 100).roundToInt()}%",
            enabled = v.delayMix > 0f) { state.apply(v.copy(delayFeedback = it)) }

        ParamSlider("Stereo width", v.width, 0f..2f, "${(v.width * 100).roundToInt()}%") {
            state.apply(v.copy(width = it))
        }

        SectionLabel("EQUALISER")
        Row(Modifier.fillMaxWidth().padding(top = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            EqBand("Bass", v.eqBass, Modifier.weight(1f)) { state.apply(v.copy(eqBass = it)) }
            EqBand("Mid", v.eqMid, Modifier.weight(1f)) { state.apply(v.copy(eqMid = it)) }
            EqBand("Treble", v.eqTreble, Modifier.weight(1f)) { state.apply(v.copy(eqTreble = it)) }
        }

        Spacer(Modifier.height(22.dp))
        Primary("Save as preset", onClick = onSavePreset)
        Spacer(Modifier.height(30.dp))
    }
}

@Composable
private fun EqBand(label: String, db: Float, modifier: Modifier, onChange: (Float) -> Unit) {
    val p = palette
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(if (db > 0) "+${db.roundToInt()}" else "${db.roundToInt()}", 18.sp,
            if (db == 0f) p.faint else p.accent, FontWeight.SemiBold)
        ParamSlider("", db, -12f..12f, "", steps = 23) { onChange(it) }
        Text(label, 13.sp, p.dim)
    }
}

/* ------------------------------------------------------------------ 1e */

@Composable
fun PresetsScreen(state: AppState, onSave: () -> Unit) {
    val p = palette
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp)) {
        Header2("PRESETS", "Your settings, kept")

        if (state.userPresets.isNotEmpty()) {
            SectionLabel("YOURS")
            state.userPresets.forEach { sp ->
                PresetRow(sp.name, summarise(sp.params), state.presetName == sp.name,
                    onPick = { state.presetName = sp.name; state.apply(sp.params) },
                    onDelete = { state.deletePreset(sp.name) })
            }
        }

        SectionLabel("BUILT IN")
        Presets.all.forEach { (name, prm) ->
            PresetRow(name, summarise(prm), state.presetName == name,
                onPick = { state.presetName = name; state.apply(prm) }, onDelete = null)
        }

        Spacer(Modifier.height(20.dp))
        Primary("Save current", onClick = onSave)
        Spacer(Modifier.height(30.dp))
    }
}

private fun summarise(p: Params): String {
    val bits = mutableListOf(p.mode.label, "%.2f rot/s".format(p.speed), "%.1f m".format(p.radius))
    if (p.reverbMix > 0f) bits += "reverb ${(p.reverbMix * 100).roundToInt()}%"
    if (p.character != Character.Clean) bits += p.character.label.lowercase()
    return bits.joinToString(" · ")
}

@Composable
private fun PresetRow(
    name: String, detail: String, selected: Boolean,
    onPick: () -> Unit, onDelete: (() -> Unit)?,
) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(top = 10.dp)
            .border(1.dp, if (selected) p.accent else p.line, Square)
            .background(if (selected) p.panelSoft else Color.Transparent, Square)
            .clickable { onPick() }
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(name, 18.sp, if (selected) p.accent else p.text, FontWeight.SemiBold)
            Text(detail, 13.sp, p.dim, maxLines = 1)
        }
        if (selected) Text("LOADED", 10.sp, p.accent, letterSpacing = 1.4.sp)
        if (onDelete != null) {
            Spacer(Modifier.width(12.dp))
            Box(Modifier.clickable { onDelete() }) { Text("✕", 15.sp, p.faint) }
        }
    }
}

/* ------------------------------------------------------------------ 1f */

@Composable
fun LibraryScreen(state: AppState, onPlay: (Track) -> Unit, onGrant: () -> Unit, granted: Boolean) {
    val p = palette
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp)) {
        Header2("LIBRARY", "On this device")

        if (!granted) {
            Spacer(Modifier.height(12.dp))
            Text("8D Music needs permission to see the audio files on this phone.",
                15.sp, p.dim)
            Spacer(Modifier.height(14.dp))
            Primary("Allow access to my music", onClick = onGrant)
        } else if (state.tracks.isEmpty()) {
            Spacer(Modifier.height(16.dp))
            Text("No audio files found on this device.", 15.sp, p.dim)
        } else {
            state.tracks.forEach { t ->
                Row(
                    Modifier.fillMaxWidth().padding(top = 12.dp).clickable { onPlay(t) },
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(14.dp),
                ) {
                    Box(Modifier.size(44.dp).background(p.panel, Square),
                        contentAlignment = Alignment.Center) {
                        Text(t.title.take(2), 15.sp, p.dim)
                    }
                    Column(Modifier.weight(1f)) {
                        Text(t.title, 17.sp, p.text, maxLines = 1)
                        Text(t.artist, 13.sp, p.dim, maxLines = 1)
                    }
                    Text(t.format, 11.sp, p.faint)
                    Text(clock(t.seconds), 12.sp, p.faint)
                }
            }
        }
        Spacer(Modifier.height(30.dp))
    }
}

/* ------------------------------------------------------------------ 1h */

@Composable
fun SettingsScreen(state: AppState, quality: String, onQuality: (String) -> Unit, engineInfo: String) {
    val p = palette
    val v = state.params
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp)) {
        Header2("SETTINGS", "Source & output")

        SectionLabel("AUDIO SOURCE")
        Box(Modifier.fillMaxWidth().padding(top = 6.dp)) {
            Field(state.source, Source.entries.toList(), { it.label }) { state.choose(it) }
        }

        SectionLabel("PROCESSING QUALITY")
        Row(Modifier.fillMaxWidth().padding(top = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf("Safe", "Balanced", "Max").forEach { q ->
                Box(Modifier.weight(1f)) { Chip(q, q == quality) { onQuality(q) } }
            }
        }
        Text(engineInfo, 12.sp, p.faint, modifier = Modifier.padding(top = 8.dp))

        SectionLabel("ENGINE")
        CheckRow("Pause orbit when nothing plays", v.pauseWhenSilent) {
            state.apply(v.copy(pauseWhenSilent = !v.pauseWhenSilent))
        }
        CheckRow("Bypass all processing", v.bypass) { state.apply(v.copy(bypass = !v.bypass)) }
        ParamSlider("Output volume", v.outputGain, 0f..1.5f,
            "${(v.outputGain * 100).roundToInt()}%") { state.apply(v.copy(outputGain = it)) }

        Text("8D needs stereo separation — headphones are strongly recommended.",
            13.sp, p.faint, modifier = Modifier.padding(top = 14.dp))

        Spacer(Modifier.height(22.dp))
        Primary("Reset everything to defaults") {
            state.presetName = "Classic 8D"
            state.apply(Presets.all[0].second)
        }
        Spacer(Modifier.height(18.dp))
        Text("8D Music 1.0 · DSP engine C++/NDK · Kotlin UI", 12.sp, p.ghost)
        Spacer(Modifier.height(30.dp))
    }
}

/* ------------------------------------------------------------------ shared */

@Composable
fun Header2(kicker: String, title: String) {
    val p = palette
    Spacer(Modifier.height(18.dp))
    Text(kicker, 11.sp, p.accent, FontWeight.SemiBold, letterSpacing = 2.sp)
    Spacer(Modifier.height(6.dp))
    Text(title, 30.sp, p.text, FontWeight.SemiBold)
}

@Composable
fun Primary(label: String, onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = 52.dp)
            .background(p.engineBg, Square)
            .clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) { Text(label, 17.sp, p.engineText, FontWeight.SemiBold) }
}
