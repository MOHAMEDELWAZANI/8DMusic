package com.eightd.music.audio

import android.content.Context
import android.util.Log
import com.eightd.music.shizuku.ShizukuBridge

/**
 * Which audio sessions other apps are currently using.
 *
 * There is no public API for this. The audio server prints its own session
 * table when asked to dump itself, so with DUMP granted we read that and parse
 * it. The table's shape changed between Android versions, so the header is
 * detected rather than assumed.
 */
object AudioSessions {

    private const val TAG = "8dmusic"
    private const val SERVICE = "media.audio_flinger"

    private var lastFingerprint = 0

    data class Session(val sessionId: Int, val pid: Int, val uid: Int?, val name: String)

    fun list(context: Context): List<Session> {
        val dump = ShizukuBridge.dumpService(context, SERVICE) ?: return emptyList()
        return parse(dump)
    }

    internal fun parse(dump: String): List<Session> {
        val head29 = Regex("""session\s+pid\s+count""")
        val head30 = Regex("""session\s+cnt\s+pid\s+uid""")
        val body29 = Regex("""(\d+)\s+(\d+)\s+(\d+)""")
        // The name column is what makes it possible to leave the launcher alone.
        val body30 = Regex("""(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*(\S*)""")

        val out = mutableListOf<Session>()
        var version = 0
        var inTable = false

        dump.lines().forEach { line ->
            when {
                line.contains("Global session refs") -> { inTable = true; version = 0 }

                inTable && version == 0 -> version = when {
                    head30.containsMatchIn(line) -> 30
                    head29.containsMatchIn(line) -> 29
                    else -> 0
                }.also { if (it == 0 && line.isNotBlank() && !line.contains("session")) inTable = false }

                inTable -> {
                    val m = if (version == 30) body30.find(line) else body29.find(line)
                    if (m == null) {
                        if (line.isBlank()) inTable = false
                    } else {
                        val g = m.groupValues
                        out += if (version == 30) {
                            Session(g[1].toInt(), g[3].toInt(), g[4].toIntOrNull(),
                                g.getOrElse(5) { "" })
                        } else {
                            Session(g[1].toInt(), g[2].toInt(), null, "")
                        }
                    }
                }
            }
        }

        val result = out.distinctBy { it.sessionId }
        val fingerprint = result.map { it.sessionId }.sorted().hashCode()
        if (fingerprint != lastFingerprint) {
            lastFingerprint = fingerprint
            Log.i(TAG, "sessions: ${result.size} (v$version) ${result.map { it.name }}")
        }
        return result
    }
}
