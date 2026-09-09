package com.eightd.music

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateListOf
import com.eightd.music.audio.EngineHolder
import org.json.JSONArray
import org.json.JSONObject

/** The three ways in, as the design document names them. */
enum class Source(val label: String, val note: String) {
    Capture("Direct capture", "NO SETUP"),
    Local("Local player", "OFFLINE"),
    SystemWide("System-wide (Shizuku)", "ADVANCED"),
}

data class SavedPreset(val name: String, val params: Params)

data class Track(
    val id: Long,
    val title: String,
    val artist: String,
    val format: String,
    val seconds: Int,
    val uri: String,
)

/**
 * Everything the screens share.
 *
 * Held in a ViewModel rather than the activity so a rotation does not stop the
 * music or throw away a decoded track -- the engine itself lives in
 * [EngineHolder] for the same reason.
 */
class AppState(app: Application) : AndroidViewModel(app) {

    private val prefs = app.getSharedPreferences("8dmusic", Context.MODE_PRIVATE)

    var source by mutableStateOf(readSource())
    var sourceChosen by mutableStateOf(prefs.contains(KEY_SOURCE))
    var params by mutableStateOf(Presets.all[0].second)
    var presetName by mutableStateOf("Classic 8D")
    var engineOn by mutableStateOf(false)
    var playing by mutableStateOf(false)

    val userPresets = mutableStateListOf<SavedPreset>().also { it.addAll(readPresets()) }
    val tracks = mutableStateListOf<Track>()

    var nowTitle by mutableStateOf("Nothing playing")
    var nowArtist by mutableStateOf("")
    var loading by mutableStateOf(false)
    var nowSeconds by mutableStateOf(0)
    var totalSeconds by mutableStateOf(0)

    fun choose(s: Source) {
        source = s
        sourceChosen = true
        prefs.edit().putString(KEY_SOURCE, s.name).apply()
    }

    fun apply(p: Params) {
        params = p
        EngineHolder.apply(p.copy(enabled = engineOn && !p.bypass))
    }

    fun setEngine(on: Boolean) {
        engineOn = on
        EngineHolder.apply(params.copy(enabled = on && !params.bypass))
    }

    fun savePreset(name: String) {
        userPresets.removeAll { it.name == name }
        userPresets.add(0, SavedPreset(name, params))
        writePresets()
    }

    fun deletePreset(name: String) {
        userPresets.removeAll { it.name == name }
        writePresets()
    }

    private fun readSource(): Source =
        runCatching { Source.valueOf(prefs.getString(KEY_SOURCE, null) ?: "") }
            .getOrDefault(Source.Local)

    private fun readPresets(): List<SavedPreset> = runCatching {
        val arr = JSONArray(prefs.getString(KEY_PRESETS, "[]"))
        (0 until arr.length()).map { i ->
            val o = arr.getJSONObject(i)
            SavedPreset(o.getString("name"), o.getJSONObject("p").toParams())
        }
    }.getOrDefault(emptyList())

    private fun writePresets() {
        val arr = JSONArray()
        userPresets.forEach { sp ->
            arr.put(JSONObject().put("name", sp.name).put("p", sp.params.toJson()))
        }
        prefs.edit().putString(KEY_PRESETS, arr.toString()).apply()
    }

    private companion object {
        const val KEY_SOURCE = "source"
        const val KEY_PRESETS = "presets"
    }
}

private fun Params.toJson() = JSONObject().apply {
    put("mode", mode.name); put("character", character.name)
    put("speed", speed.toDouble()); put("radius", radius.toDouble())
    put("depth", depth.toDouble()); put("smoothness", smoothness.toDouble())
    put("width", width.toDouble()); put("direction", direction)
    put("manualAngle", manualAngle.toDouble())
    put("characterAmount", characterAmount.toDouble())
    put("delayMix", delayMix.toDouble()); put("delayTime", delayTime.toDouble())
    put("delayFeedback", delayFeedback.toDouble())
    put("reverbMix", reverbMix.toDouble()); put("reverbSize", reverbSize.toDouble())
    put("reverbDamp", reverbDamp.toDouble()); put("outputGain", outputGain.toDouble())
    put("eqBass", eqBass.toDouble()); put("eqMid", eqMid.toDouble())
    put("eqTreble", eqTreble.toDouble())
}

private fun JSONObject.toParams(): Params {
    fun f(k: String, d: Float) = optDouble(k, d.toDouble()).toFloat()
    return Params(
        mode = runCatching { Mode.valueOf(getString("mode")) }.getOrDefault(Mode.Circular),
        character = runCatching { Character.valueOf(getString("character")) }
            .getOrDefault(Character.Clean),
        speed = f("speed", 0.12f), radius = f("radius", 1f), depth = f("depth", 0.85f),
        smoothness = f("smoothness", 0.35f), width = f("width", 1f),
        direction = optInt("direction", 1), manualAngle = f("manualAngle", 0f),
        characterAmount = f("characterAmount", 1f),
        delayMix = f("delayMix", 0f), delayTime = f("delayTime", 0.28f),
        delayFeedback = f("delayFeedback", 0.35f),
        reverbMix = f("reverbMix", 0.18f), reverbSize = f("reverbSize", 0.6f),
        reverbDamp = f("reverbDamp", 0.45f), outputGain = f("outputGain", 0.9f),
        eqBass = f("eqBass", 0f), eqMid = f("eqMid", 0f), eqTreble = f("eqTreble", 0f),
    )
}
