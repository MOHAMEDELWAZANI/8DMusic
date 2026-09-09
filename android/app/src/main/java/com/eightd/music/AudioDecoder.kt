package com.eightd.music

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import java.nio.ByteBuffer
import java.nio.ByteOrder

class DecodedAudio(
    val samples: ShortArray,
    val frames: Int,
    val sampleRate: Int,
    val channels: Int,
)

/**
 * Decodes a whole file to PCM up front.
 *
 * Stage 1 plays one bundled track, so there is no reason to stream: decoding
 * everything once means the audio callback only ever reads from a flat array,
 * with no producer thread and no ring buffer anywhere near the DSP.
 */
object AudioDecoder {

    /** Decodes any content:// or file:// the device can open. */
    fun decodeUri(context: Context, uri: android.net.Uri): DecodedAudio {
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(context, uri, null)
            return decode(extractor)
        } finally {
            extractor.release()
        }
    }

    fun decodeAsset(context: Context, assetName: String): DecodedAudio {
        val afd = context.assets.openFd(assetName)
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(afd.fileDescriptor, afd.startOffset, afd.length)
            return decode(extractor)
        } finally {
            extractor.release()
            afd.close()
        }
    }

    private fun decode(extractor: MediaExtractor): DecodedAudio {
        var track = -1
        var format: MediaFormat? = null
        for (i in 0 until extractor.trackCount) {
            val f = extractor.getTrackFormat(i)
            if (f.getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true) {
                track = i; format = f; break
            }
        }
        val fmt = format ?: error("no audio track in the file")
        extractor.selectTrack(track)

        var rate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        var channels = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT)

        val codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME)!!)
        codec.configure(fmt, null, null, 0)
        codec.start()

        val chunks = ArrayList<ByteArray>()
        var totalBytes = 0
        val info = MediaCodec.BufferInfo()
        var inputDone = false
        var outputDone = false

        while (!outputDone) {
            if (!inputDone) {
                val idx = codec.dequeueInputBuffer(10_000)
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

            when (val idx = codec.dequeueOutputBuffer(info, 10_000)) {
                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                    val out = codec.outputFormat
                    rate = out.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                    channels = out.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                    if (out.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                        val enc = out.getInteger(MediaFormat.KEY_PCM_ENCODING)
                        require(enc == AudioFormat.ENCODING_PCM_16BIT) {
                            "decoder produced PCM encoding $enc; only 16-bit is handled"
                        }
                    }
                }
                MediaCodec.INFO_TRY_AGAIN_LATER -> Unit
                else -> if (idx >= 0) {
                    if (info.size > 0) {
                        val buf = codec.getOutputBuffer(idx)!!
                        val bytes = ByteArray(info.size)
                        buf.position(info.offset)
                        buf.get(bytes, 0, info.size)
                        chunks.add(bytes)
                        totalBytes += info.size
                    }
                    codec.releaseOutputBuffer(idx, false)
                    if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) outputDone = true
                }
            }
        }

        codec.stop()
        codec.release()

        val samples = ShortArray(totalBytes / 2)
        var off = 0
        for (i in chunks.indices) {
            val c = chunks[i]
            ByteBuffer.wrap(c).order(ByteOrder.LITTLE_ENDIAN)
                .asShortBuffer().get(samples, off, c.size / 2)
            off += c.size / 2
            chunks[i] = ByteArray(0)   // release each chunk as it is copied
        }

        return DecodedAudio(samples, samples.size / channels, rate, channels)
    }
}
