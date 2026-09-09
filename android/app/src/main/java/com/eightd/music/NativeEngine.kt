package com.eightd.music

/**
 * Thin handle on the C++ engine in jni_bridge.cpp. Everything expensive
 * happens on the other side of this class; nothing here touches the audio
 * thread except by publishing a parameter snapshot.
 */
class NativeEngine : AutoCloseable {

    private var handle: Long = 0L
    private val telemetryScratch = FloatArray(5)

    val isOpen: Boolean get() = handle != 0L

    /** Opens the output stream. Returns false if the device refused it. */
    fun open(): Boolean {
        if (handle != 0L) return true
        handle = nativeCreate()
        return handle != 0L
    }

    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    /** The rate the device actually gave us, not the rate we asked for. */
    val sampleRate: Int get() = if (handle != 0L) nativeSampleRate(handle) else 0

    /**
     * Streamed source. Playback can begin as soon as the first chunk lands, so
     * a long track does not have to be fully decoded before anything is heard.
     */
    fun beginSource(estimatedFrames: Int, srcRate: Int, channels: Int) {
        if (handle != 0L) nativeBeginSource(handle, estimatedFrames, srcRate, channels)
    }

    fun appendSource(pcm: ShortArray, frames: Int) {
        if (handle != 0L) nativeAppendSource(handle, pcm, frames)
    }

    fun endSource() { if (handle != 0L) nativeEndSource(handle) }

    /**
     * Direct capture: the engine reads from the live ring instead of a decoded
     * file. Everything else -- the DSP, the parameters, the telemetry -- is
     * unchanged, so the two sources sound identical.
     */
    fun setLive(on: Boolean) { if (handle != 0L) nativeSetLive(handle, on) }

    /** Called from the capture thread with interleaved stereo floats. */
    fun pushCapture(pcm: FloatArray, frames: Int) {
        if (handle != 0L) nativePushCapture(handle, pcm, frames)
    }

    fun queued(): Int = if (handle != 0L) nativeQueued(handle) else 0

    fun start(): Boolean = handle != 0L && nativeStart(handle)
    fun pause() { if (handle != 0L) nativePause(handle) }

    /** The stream's own view of whether it is running. */
    fun isRunning(): Boolean = handle != 0L && nativeIsRunning(handle)

    fun setParams(p: Params) { if (handle != 0L) nativeSetParams(handle, p.toFloatArray()) }

    fun seek(frame: Int) { if (handle != 0L) nativeSeek(handle, frame) }
    fun position(): Int = if (handle != 0L) nativePosition(handle) else 0
    fun totalFrames(): Int = if (handle != 0L) nativeTotalFrames(handle) else 0
    fun setLoop(on: Boolean) { if (handle != 0L) nativeSetLoop(handle, on) }

    /** angle, distance, peakL, peakR, motion -- straight off the DSP. */
    fun telemetry(): FloatArray {
        if (handle != 0L) nativeTelemetry(handle, telemetryScratch)
        return telemetryScratch
    }

    /**
     * Runs the same probe as cpp/tests/dsp_probe.cpp and writes raw f32 into
     * [dir]. Compare the result against the desktop build's output to show the
     * two are the same effect, rather than asserting it.
     */
    fun selfTest(dir: String, mode: String): String = nativeSelfTest(dir, mode)

    private external fun nativeSelfTest(dir: String, mode: String): String
    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeSampleRate(handle: Long): Int
    private external fun nativeBeginSource(handle: Long, estFrames: Int, srcRate: Int, channels: Int)
    private external fun nativeAppendSource(handle: Long, pcm: ShortArray, frames: Int)
    private external fun nativeEndSource(handle: Long)
    private external fun nativeSetLive(handle: Long, on: Boolean)
    private external fun nativePushCapture(handle: Long, pcm: FloatArray, frames: Int)
    private external fun nativeQueued(handle: Long): Int
    private external fun nativeStart(handle: Long): Boolean
    private external fun nativePause(handle: Long)
    private external fun nativeIsRunning(handle: Long): Boolean
    private external fun nativeSetParams(handle: Long, values: FloatArray)
    private external fun nativeSeek(handle: Long, frame: Int)
    private external fun nativePosition(handle: Long): Int
    private external fun nativeTotalFrames(handle: Long): Int
    private external fun nativeSetLoop(handle: Long, on: Boolean)
    private external fun nativeTelemetry(handle: Long, out: FloatArray)

    companion object {
        init { System.loadLibrary("eightd") }
    }
}
