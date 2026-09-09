package com.eightd.music.audio

import com.eightd.music.NativeEngine
import com.eightd.music.Params

/**
 * One engine for the whole process.
 *
 * The activity and the capture service both need the same DSP instance -- the
 * service feeds it, the activity draws its telemetry -- and the activity can be
 * recreated at any time. Owning it here means a rotation cannot silently open a
 * second output stream, which is what happened when the activity held it.
 */
object EngineHolder {

    val engine = NativeEngine()

    @Volatile var params: Params = Params()
        private set

    @Synchronized
    fun open(): Boolean = engine.open()

    fun apply(p: Params) {
        params = p
        engine.setParams(p)
    }

    /** Torn down only when the process goes, not when a screen does. */
    @Synchronized
    fun shutdown() {
        engine.close()
    }
}
