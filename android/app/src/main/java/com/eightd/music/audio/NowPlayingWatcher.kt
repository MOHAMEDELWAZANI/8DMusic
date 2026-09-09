package com.eightd.music.audio

import android.content.ComponentName
import android.content.Context
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.provider.Settings
import android.util.Log

/**
 * What the phone is actually playing, when the source is another app.
 *
 * The local player knows its own track; direct capture and system-wide do not,
 * because all they receive is PCM. Android publishes the metadata separately
 * through MediaSessionManager, so the panel reads it from there.
 */
class NowPlayingWatcher(private val context: Context) {

    private companion object { const val TAG = "8dmusic" }

    data class Now(
        val title: String,
        val artist: String,
        val positionSeconds: Int,
        val durationSeconds: Int,
        val playing: Boolean,
    )

    private val component = ComponentName(context, MediaListener::class.java)
    private var controller: MediaController? = null

    /** Notification access is a single toggle in Settings, unlike Shizuku. */
    fun hasAccess(): Boolean {
        val flat = Settings.Secure.getString(
            context.contentResolver, "enabled_notification_listeners"
        ) ?: return false
        return flat.split(':').any { it.contains(context.packageName) }
    }

    fun settingsIntent() = android.content.Intent(
        Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS
    ).addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)

    fun poll(): Now? {
        if (!hasAccess()) return null

        val active = runCatching {
            context.getSystemService(MediaSessionManager::class.java)
                .getActiveSessions(component)
        }.onFailure { Log.w(TAG, "nowplaying: ${it.message}") }.getOrNull().orEmpty()

        // Whatever is actually playing wins; otherwise keep the last thing that was.
        val chosen = active.firstOrNull {
            it.playbackState?.state == PlaybackState.STATE_PLAYING
        } ?: active.firstOrNull { it.packageName != context.packageName }

        controller = chosen
        val md = chosen?.metadata ?: return null
        val state = chosen.playbackState

        val title = md.getString(MediaMetadata.METADATA_KEY_TITLE)
            ?: md.getString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE)
            ?: return null
        val artist = md.getString(MediaMetadata.METADATA_KEY_ARTIST)
            ?: md.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)
            ?: chosen.packageName.substringAfterLast('.')

        return Now(
            title = title,
            artist = artist,
            positionSeconds = ((state?.position ?: 0L) / 1000).toInt(),
            durationSeconds = (md.getLong(MediaMetadata.METADATA_KEY_DURATION) / 1000).toInt(),
            playing = state?.state == PlaybackState.STATE_PLAYING,
        )
    }

    /** The transport drives the player itself, not the effect. */
    fun playPause() = controller?.transportControls?.let {
        if (controller?.playbackState?.state == PlaybackState.STATE_PLAYING) it.pause() else it.play()
    }
    fun next() = controller?.transportControls?.skipToNext()
    fun previous() = controller?.transportControls?.skipToPrevious()
    fun seekTo(seconds: Int) = controller?.transportControls?.seekTo(seconds * 1000L)
}
