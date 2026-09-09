package com.eightd.music

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicInteger

/**
 * Decodes a track into the engine as it goes.
 *
 * The old path decoded the whole file first, which for a four-minute song meant
 * several seconds of silence before anything played. Here the engine starts as
 * soon as roughly half a second exists and the decoder stays ahead of it.
 */
object StreamDecoder {

    private const val TAG = "8dmusic"

    /** Bumped on every new request so an in-flight decode knows to stop. */
    private val generation = AtomicInteger(0)

    fun cancel() { generation.incrementAndGet() }

    /**
     * Runs on the calling thread. [onPlayable] fires once, on that same thread,
     * as soon as there is enough decoded audio to start.
     */
    fun stream(
        context: Context,
        uri: Uri,
        engine: NativeEngine,
        onPlayable: () -> Unit,
        onFinished: (Boolean) -> Unit,
    ) {
        val mine = generation.incrementAndGet()
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null

        try {
            extractor.setDataSource(context, uri, null)

            var track = -1
            var fmt: MediaFormat? = null
            for (i in 0 until extractor.trackCount) {
                val f = extractor.getTrackFormat(i)
                if (f.getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true) {
                    track = i; fmt = f; break
                }
            }
            val format = fmt ?: run { onFinished(false); return }
            extractor.selectTrack(track)

            var rate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            var channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            val durationUs = if (format.containsKey(MediaFormat.KEY_DURATION))
                format.getLong(MediaFormat.KEY_DURATION) else 0L

            engine.beginSource((durationUs / 1_000_000.0 * rate).toInt().coerceAtLeast(rate), rate, channels)

            codec = MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME)!!)
            codec.configure(format, null, null, 0)
            codec.start()

            val info = MediaCodec.BufferInfo()
            var inputDone = false
            var outputDone = false
            var framesOut = 0L
            var started = false
            // Enough to cover the gap between "playing" and "still decoding".
            val playableAfter = rate / 2

            while (!outputDone) {
                if (generation.get() != mine) { onFinished(false); return }

                if (!inputDone) {
                    val idx = codec.dequeueInputBuffer(5_000)
                    if (idx >= 0) {
                        val buf = codec.getInputBuffer(idx)!!
                        val n = extractor.readSampleData(buf, 0)
                        if (n < 0) {
                            codec.queueInputBuffer(idx, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            inputDone = true
                        } else {
                            codec.queueInputBuffer(idx, 0, n, extractor.sampleTime, 0)
                            extractor.advance()
                        }
                    }
                }

                when (val idx = codec.dequeueOutputBuffer(info, 5_000)) {
                    MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        val out = codec.outputFormat
                        val newRate = out.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                        val newCh = out.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                        if (out.containsKey(MediaFormat.KEY_PCM_ENCODING) &&
                            out.getInteger(MediaFormat.KEY_PCM_ENCODING) !=
                            AudioFormat.ENCODING_PCM_16BIT
                        ) {
                            Log.e(TAG, "decoder is not 16-bit PCM")
                            onFinished(false); return
                        }
                        // The real format only becomes known here, so the
                        // engine is told again before any audio is appended.
                        if (newRate != rate || newCh != channels) {
                            rate = newRate; channels = newCh
                            engine.beginSource(
                                (durationUs / 1_000_000.0 * rate).toInt().coerceAtLeast(rate),
                                rate, channels)
                        }
                    }
                    MediaCodec.INFO_TRY_AGAIN_LATER -> Unit
                    else -> if (idx >= 0) {
                        if (info.size > 0) {
                            val buf = codec.getOutputBuffer(idx)!!
                            buf.position(info.offset)
                            buf.limit(info.offset + info.size)
                            val shorts = ShortArray(info.size / 2)
                            buf.order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(shorts)
                            val frames = shorts.size / channels
                            engine.appendSource(shorts, frames)
                            framesOut += frames

                            if (!started && framesOut >= playableAfter) {
                                started = true
                                onPlayable()
                            }
                        }
                        codec.releaseOutputBuffer(idx, false)
                        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) outputDone = true
                    }
                }
            }

            engine.endSource()
            if (!started) onPlayable()
            onFinished(true)
        } catch (t: Throwable) {
            Log.e(TAG, "decode failed: ${t.message}")
            onFinished(false)
        } finally {
            runCatching { codec?.stop() }
            runCatching { codec?.release() }
            runCatching { extractor.release() }
        }
    }
}
