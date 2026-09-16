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
    val album: String,
    val format: String,
    val seconds: Int,
    val uri: String,
)

/**
 * A playlist, and the 8D preset it plays in.
 *
 * The preset is the part no other player can do: a night-drive list that always
 * comes back in Slow Orbit is the reason to keep your music here rather than in
 * whatever player you already have.
 */
data class Playlist(
    val id: String,
    val name: String,
    val trackIds: List<Long>,
    val preset: String? = null,
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
    val playlists = mutableStateListOf<Playlist>().also { it.addAll(readPlaylists()) }

    /** What plays next: set when a track is started from a list or a playlist. */
    var queue by mutableStateOf<List<Track>>(emptyList())
    var queueIndex by mutableStateOf(0)
    var queueName by mutableStateOf("")

    /** Tracks marked with the heart on Now Playing. */
    val liked = mutableStateListOf<Long>().also { it.addAll(readLiked()) }
    var shuffle by mutableStateOf(false)
    var repeatOne by mutableStateOf(false)

    /** First-run flags. Each presentation is shown once and replayable from About. */
    var seenWelcome by mutableStateOf(prefs.getBoolean(KEY_SEEN_WELCOME, false))
    var seenStudioTour by mutableStateOf(prefs.getBoolean(KEY_SEEN_STUDIO, false))
    var seenLiveTour by mutableStateOf(prefs.getBoolean(KEY_SEEN_LIVE, false))
    var seenPlayerTour by mutableStateOf(prefs.getBoolean(KEY_SEEN_PLAYER, false))

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

    /* ------------------------------------------------------------ playlists */

    fun createPlaylist(name: String): Playlist {
        val pl = Playlist(java.util.UUID.randomUUID().toString().take(8), name, emptyList())
        playlists.add(0, pl)
        writePlaylists()
        return pl
    }

    fun deletePlaylist(id: String) {
        playlists.removeAll { it.id == id }
        writePlaylists()
    }

    fun addToPlaylist(id: String, trackId: Long) = update(id) {
        if (trackId in it.trackIds) it else it.copy(trackIds = it.trackIds + trackId)
    }

    fun removeFromPlaylist(id: String, trackId: Long) = update(id) {
        it.copy(trackIds = it.trackIds - trackId)
    }

    /** Null clears it, and the playlist then plays in whatever Studio is set to. */
    fun setPlaylistPreset(id: String, preset: String?) = update(id) { it.copy(preset = preset) }

    private fun update(id: String, f: (Playlist) -> Playlist) {
        val i = playlists.indexOfFirst { it.id == id }
        if (i >= 0) {
            playlists[i] = f(playlists[i])
            writePlaylists()
        }
    }

    fun tracksOf(pl: Playlist): List<Track> =
        pl.trackIds.mapNotNull { id -> tracks.firstOrNull { it.id == id } }

    fun toggleLiked(id: Long) {
        if (!liked.remove(id)) liked.add(id)
        prefs.edit().putString(KEY_LIKED, liked.joinToString(",")).apply()
    }

    fun isLiked(id: Long) = id in liked

    /**
     * The one playlist nobody has to create.
     *
     * Synthetic: it has no entry in storage, its contents are whatever carries
     * a heart, and it cannot be deleted. [FAVOURITES_ID] marks it so the
     * playlist screen knows to hide the bin and the add button.
     */
    fun favourites() = Playlist(FAVOURITES_ID, "Favourites", liked.toList(), favouritePreset)

    var favouritePreset by mutableStateOf(prefs.getString(KEY_FAV_PRESET, null))
        private set

    fun rememberFavouritePreset(preset: String?) {
        favouritePreset = preset
        prefs.edit().putString(KEY_FAV_PRESET, preset).apply()
    }

    private fun readLiked(): List<Long> =
        (prefs.getString(KEY_LIKED, "") ?: "").split(',').mapNotNull { it.toLongOrNull() }

    /* ----------------------------------------------------------- first runs */

    fun markSeen(key: String) {
        when (key) {
            KEY_SEEN_WELCOME -> seenWelcome = true
            KEY_SEEN_STUDIO -> seenStudioTour = true
            KEY_SEEN_LIVE -> seenLiveTour = true
            KEY_SEEN_PLAYER -> seenPlayerTour = true
        }
        prefs.edit().putBoolean(key, true).apply()
    }

    fun replay(key: String) {
        when (key) {
            KEY_SEEN_WELCOME -> seenWelcome = false
            KEY_SEEN_STUDIO -> seenStudioTour = false
            KEY_SEEN_LIVE -> seenLiveTour = false
            KEY_SEEN_PLAYER -> seenPlayerTour = false
        }
        prefs.edit().putBoolean(key, false).apply()
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

    private fun readPlaylists(): List<Playlist> = runCatching {
        val arr = JSONArray(prefs.getString(KEY_PLAYLISTS, "[]"))
        (0 until arr.length()).map { i ->
            val o = arr.getJSONObject(i)
            val ids = o.getJSONArray("tracks")
            Playlist(
                id = o.getString("id"),
                name = o.getString("name"),
                trackIds = (0 until ids.length()).map { ids.getLong(it) },
                preset = if (o.isNull("preset")) null else o.getString("preset"),
            )
        }
    }.getOrDefault(emptyList())

    private fun writePlaylists() {
        val arr = JSONArray()
        playlists.forEach { pl ->
            arr.put(JSONObject()
                .put("id", pl.id).put("name", pl.name)
                .put("tracks", JSONArray().also { a -> pl.trackIds.forEach(a::put) })
                .put("preset", pl.preset ?: JSONObject.NULL))
        }
        prefs.edit().putString(KEY_PLAYLISTS, arr.toString()).apply()
    }

    private fun writePresets() {
        val arr = JSONArray()
        userPresets.forEach { sp ->
            arr.put(JSONObject().put("name", sp.name).put("p", sp.params.toJson()))
        }
        prefs.edit().putString(KEY_PRESETS, arr.toString()).apply()
    }

    companion object {
        private const val KEY_SOURCE = "source"
        private const val KEY_PRESETS = "presets"
        private const val KEY_PLAYLISTS = "playlists"
        private const val KEY_LIKED = "liked"
        private const val KEY_FAV_PRESET = "fav_preset"
        const val FAVOURITES_ID = "__favourites__"
        const val KEY_SEEN_WELCOME = "seen_welcome"
        const val KEY_SEEN_STUDIO = "seen_studio"
        const val KEY_SEEN_LIVE = "seen_live"
        const val KEY_SEEN_PLAYER = "seen_player"
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
