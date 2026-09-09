package com.eightd.music.audio

import android.content.Context
import android.media.audiofx.AudioEffect
import android.media.audiofx.DynamicsProcessing
import android.os.Process
import android.util.Log

/**
 * Silences the original stream so only the spatialised copy is heard.
 *
 * Playback capture hands us a copy; the app we captured keeps playing to the
 * speaker. Two versions of the same song at once is not "half the effect" —
 * an 8D image is built from a sub-millisecond delay between the ears, and an
 * undelayed duplicate on top of it destroys exactly that.
 *
 * The trick is to attach an effect at the highest priority to the other app's
 * session and turn its gain all the way down. Another app can take control of
 * an effect at any time, so every effect is watched and re-applied.
 */
class SessionMuter(private val context: Context) {

    private companion object { const val TAG = "8dmusic" }

    private val active = mutableMapOf<Int, AudioEffect>()

    /** Sessions we must never touch: our own, and anything we were told to skip. */
    private val ourUid = Process.myUid()

    /**
     * Silencing everything also silences the launcher and the system UI, which
     * costs the user their notification and keyboard sounds for no benefit --
     * none of it is music, and none of it is what they asked to spatialise.
     */
    private val neverMute = setOf(
        context.packageName,
        "com.miui.home",
        "com.android.systemui",
        "com.android.launcher3",
        "com.google.android.googlequicksearchbox",
        "com.google.android.as",
        "android",
    )

    fun refresh(excludedUids: Set<Int> = emptySet()) {
        val sessions = AudioSessions.list(context)
        val wanted = sessions
            .filter { it.uid == null || (it.uid != ourUid && it.uid !in excludedUids) }
            .filter { it.name.isEmpty() || it.name !in neverMute }
            .map { it.sessionId }
            .toSet()

        (active.keys - wanted).forEach { release(it) }
        wanted.forEach { if (it !in active) mute(it) }
    }

    private fun mute(sessionId: Int) {
        val effect = runCatching {
            DynamicsProcessing(Int.MAX_VALUE, sessionId, null).apply {
                setInputGainAllChannelsTo(-200f)
                enabled = true
            }
        }.onFailure {
            Log.w(TAG, "mute: session $sessionId refused: ${it.message}")
        }.getOrNull() ?: return

        effect.setEnableStatusListener { e, on ->
            if (!on) runCatching {
                (e as? DynamicsProcessing)?.setInputGainAllChannelsTo(-200f)
                e.enabled = true
            }
        }
        effect.setControlStatusListener { e, granted ->
            if (granted) runCatching {
                (e as? DynamicsProcessing)?.setInputGainAllChannelsTo(-200f)
                e.enabled = true
            } else {
                Log.w(TAG, "mute: lost control of session $sessionId")
            }
        }

        active[sessionId] = effect
        Log.i(TAG, "mute: session $sessionId silenced")
    }

    private fun release(sessionId: Int) {
        active.remove(sessionId)?.let { e ->
            runCatching { e.enabled = false; e.release() }
        }
        Log.i(TAG, "mute: session $sessionId released")
    }

    /** Always call this: a muted session left behind is a silent phone. */
    fun releaseAll() {
        active.keys.toList().forEach { release(it) }
    }
}
