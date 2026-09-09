package com.eightd.music.audio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioPlaybackConfiguration
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.Process
import android.util.Log
import com.eightd.music.MainActivity
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlin.concurrent.thread

/**
 * Direct capture.
 *
 * Android hands us a *copy* of what other apps are playing; the original keeps
 * going to the speaker. That is fine for the local player but wrong here, so
 * this route is only honest on headphones — see [CaptureState] for how a
 * refusal is surfaced rather than swallowed.
 *
 * Apps may decline capture entirely (`ALLOW_CAPTURE_BY_NONE`); when they do,
 * the OS simply gives us silence, so silence-with-something-playing is the
 * signal we watch for.
 */
class CaptureService : Service() {

    enum class CaptureState { Idle, Starting, Running, NoSignal, Denied }

    companion object {
        private const val TAG = "8dmusic"
        private const val CHANNEL = "capture"
        private const val NOTIFICATION_ID = 8801

        const val EXTRA_RESULT_CODE = "resultCode"
        /** System-wide mode also silences the original stream. */
        const val EXTRA_SYSTEM_WIDE = "systemWide"
        const val EXTRA_RESULT_DATA = "resultData"
        const val ACTION_STOP = "com.eightd.music.STOP"

        private val _state = MutableStateFlow(CaptureState.Idle)
        val state: StateFlow<CaptureState> = _state

        /** Seconds of unbroken silence before we call it a refusal. */
        private const val SILENCE_BEFORE_WARNING = 3.0
    }

    private var projection: MediaProjection? = null
    private var muter: SessionMuter? = null
    private var systemWide = false
    private var playbackCallback: AudioManager.AudioPlaybackCallback? = null
    private var muteThread: HandlerThread? = null
    private var muteHandler: Handler? = null
    private var record: AudioRecord? = null
    @Volatile private var running = false
    private var worker: Thread? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            stopCapture()
            stopSelf()
            return START_NOT_STICKY
        }

        // Android 14+ insists the service is already foreground, with the
        // mediaProjection type declared, before the projection is taken.
        startForegroundCompat()

        val code = intent?.getIntExtra(EXTRA_RESULT_CODE, 0) ?: 0
        val data: Intent? = if (Build.VERSION.SDK_INT >= 33) {
            intent?.getParcelableExtra(EXTRA_RESULT_DATA, Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent?.getParcelableExtra(EXTRA_RESULT_DATA)
        }

        if (code == 0 || data == null) {
            Log.e(TAG, "capture: no projection token")
            _state.value = CaptureState.Denied
            stopSelf()
            return START_NOT_STICKY
        }

        systemWide = intent?.getBooleanExtra(EXTRA_SYSTEM_WIDE, false) ?: false
        startCapture(code, data)
        return START_STICKY
    }

    private fun startForegroundCompat() {
        val nm = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= 26 && nm.getNotificationChannel(CHANNEL) == null) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL, "8D engine", NotificationManager.IMPORTANCE_LOW)
                    .apply { setShowBadge(false) }
            )
        }

        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val stop = PendingIntent.getService(
            this, 1, Intent(this, CaptureService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        val n: Notification = Notification.Builder(this, CHANNEL)
            .setContentTitle("8D Music")
            .setContentText("Spatialising system audio")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Stop", stop).build())
            .setOngoing(true)
            .build()

        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
        } else {
            startForeground(NOTIFICATION_ID, n)
        }
    }

    private fun startCapture(code: Int, data: Intent) {
        _state.value = CaptureState.Starting

        val mgr = getSystemService(MediaProjectionManager::class.java)
        val proj = mgr.getMediaProjection(code, data)
        if (proj == null) {
            _state.value = CaptureState.Denied
            stopSelf()
            return
        }
        projection = proj
        proj.registerCallback(object : MediaProjection.Callback() {
            override fun onStop() {
                Log.i(TAG, "capture: projection stopped by the system")
                stopCapture()
                stopSelf()
            }
        }, null)

        val engine = EngineHolder.engine
        if (!EngineHolder.open()) {
            _state.value = CaptureState.Denied
            stopSelf()
            return
        }
        val rate = engine.sampleRate

        val config = AudioPlaybackCaptureConfiguration.Builder(proj)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .addMatchingUsage(AudioAttributes.USAGE_GAME)
            .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
            // Without this we capture our own output and the effect feeds back.
            .excludeUid(Process.myUid())
            .build()

        val format = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
            .setSampleRate(rate)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
            .build()

        val minBytes = AudioRecord.getMinBufferSize(
            rate, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT
        ).coerceAtLeast(rate / 10 * 2 * 4)

        val rec = try {
            AudioRecord.Builder()
                .setAudioFormat(format)
                .setBufferSizeInBytes(minBytes * 2)
                .setAudioPlaybackCaptureConfig(config)
                .build()
        } catch (t: Throwable) {
            Log.e(TAG, "capture: AudioRecord refused: ${t.message}")
            _state.value = CaptureState.Denied
            stopSelf()
            return
        }

        record = rec
        engine.setLive(true)
        engine.start()
        rec.startRecording()
        running = true
        _state.value = CaptureState.Running

        if (systemWide) {
            muter = SessionMuter(this).also { it.refresh() }

            // A new track means a new audio session, and polling for it is the
            // difference between "instant" and a couple of seconds of the
            // original leaking through. The system will just tell us instead.
            muteThread = HandlerThread("8d-mute").apply { start() }
            muteHandler = Handler(muteThread!!.looper)

            val am = getSystemService(AudioManager::class.java)
            playbackCallback = object : AudioManager.AudioPlaybackCallback() {
                override fun onPlaybackConfigChanged(configs: MutableList<AudioPlaybackConfiguration>) {
                    muteHandler?.post { runCatching { muter?.refresh() } }
                }
            }.also { am.registerAudioPlaybackCallback(it, muteHandler) }

            Log.i(TAG, "capture: system-wide, muting original streams")
        }

        worker = thread(name = "8d-capture", priority = Thread.MAX_PRIORITY) {
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
            val frames = 1024
            val buf = FloatArray(frames * 2)
            var silentFor = 0.0
            var sinceRescan = 0.0

            while (running) {
                val read = rec.read(buf, 0, buf.size, AudioRecord.READ_BLOCKING)
                if (read <= 0) continue
                val n = read / 2
                engine.pushCapture(buf, n)

                // Everything silent for a while, with the user expecting sound,
                // is what a blocked app looks like from here.
                var peak = 0f
                for (i in 0 until read) {
                    val a = kotlin.math.abs(buf[i])
                    if (a > peak) peak = a
                }
                silentFor = if (peak < 1e-5f) silentFor + n.toDouble() / rate else 0.0

                // A session that appears after we started still needs silencing.
                sinceRescan += n.toDouble() / rate
                if (systemWide && sinceRescan > 5.0) {
                    sinceRescan = 0.0
                    runCatching { muter?.refresh() }
                }
                val quiet = silentFor > SILENCE_BEFORE_WARNING
                val want = if (quiet) CaptureState.NoSignal else CaptureState.Running
                if (_state.value != want && running) _state.value = want
            }
        }

        Log.i(TAG, "capture: running at $rate Hz")
    }

    private fun stopCapture() {
        running = false
        playbackCallback?.let { cb ->
            runCatching { getSystemService(AudioManager::class.java).unregisterAudioPlaybackCallback(cb) }
        }
        playbackCallback = null
        muteThread?.quitSafely()
        muteThread = null
        muteHandler = null
        // Before anything else: a muted session left behind is a silent phone.
        runCatching { muter?.releaseAll() }
        muter = null
        worker?.join(500)
        worker = null
        try { record?.stop() } catch (_: Throwable) {}
        record?.release()
        record = null
        projection?.stop()
        projection = null
        EngineHolder.engine.setLive(false)
        EngineHolder.engine.pause()
        _state.value = CaptureState.Idle
    }

    override fun onDestroy() {
        stopCapture()
        super.onDestroy()
    }
}
