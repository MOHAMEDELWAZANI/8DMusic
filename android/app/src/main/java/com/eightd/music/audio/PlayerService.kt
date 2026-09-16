package com.eightd.music.audio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Bitmap
import android.media.MediaMetadata
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.Build
import android.os.IBinder
import com.eightd.music.MainActivity
import com.eightd.music.R

/**
 * The notification for the local player.
 *
 * Playing music with nothing in the shade is how a media app gets killed in the
 * background and how a user loses the controls the moment they leave the
 * screen. A MediaSession also puts play/pause on the lock screen and makes
 * headset buttons work, which is what people expect from anything that plays a
 * song.
 */
class PlayerService : Service() {

    private var session: MediaSession? = null
    private var title = ""
    private var artist = ""
    private var playing = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        session = MediaSession(this, "8dmusic").apply {
            setCallback(object : MediaSession.Callback() {
                override fun onPlay() = PlaybackBus.toggle()
                override fun onPause() = PlaybackBus.toggle()
                override fun onSkipToNext() = PlaybackBus.next()
                override fun onSkipToPrevious() = PlaybackBus.previous()
                override fun onSeekTo(pos: Long) = PlaybackBus.seekToMs(pos)
                override fun onStop() = PlaybackBus.stop()
            })
            isActive = true
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_TOGGLE -> PlaybackBus.toggle()
            ACTION_NEXT -> PlaybackBus.next()
            ACTION_PREV -> PlaybackBus.previous()
            ACTION_STOP -> {
                PlaybackBus.stop()
                stopSelf()
                return START_NOT_STICKY
            }
        }
        title = intent?.getStringExtra(EXTRA_TITLE) ?: title
        artist = intent?.getStringExtra(EXTRA_ARTIST) ?: artist
        playing = intent?.getBooleanExtra(EXTRA_PLAYING, playing) ?: playing

        val elapsed = intent?.getLongExtra(EXTRA_POSITION_MS, -1L) ?: -1L
        val duration = intent?.getLongExtra(EXTRA_DURATION_MS, -1L) ?: -1L
        publish(elapsed, duration)
        return START_STICKY
    }

    private fun publish(elapsedMs: Long, durationMs: Long) {
        val s = session ?: return
        if (durationMs > 0) {
            s.setMetadata(
                MediaMetadata.Builder()
                    .putString(MediaMetadata.METADATA_KEY_TITLE, title)
                    .putString(MediaMetadata.METADATA_KEY_ARTIST, artist)
                    .putLong(MediaMetadata.METADATA_KEY_DURATION, durationMs)
                    .build()
            )
        }
        s.setPlaybackState(
            PlaybackState.Builder()
                .setActions(
                    PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or
                        PlaybackState.ACTION_PLAY_PAUSE or PlaybackState.ACTION_SEEK_TO or
                        PlaybackState.ACTION_SKIP_TO_NEXT or
                        PlaybackState.ACTION_SKIP_TO_PREVIOUS or PlaybackState.ACTION_STOP
                )
                .setState(
                    if (playing) PlaybackState.STATE_PLAYING else PlaybackState.STATE_PAUSED,
                    if (elapsedMs >= 0) elapsedMs else 0L,
                    1f,
                )
                .build()
        )
        startForegroundCompat(build())
    }

    private fun build(): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= 26 && nm.getNotificationChannel(CHANNEL) == null) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL, "Player", NotificationManager.IMPORTANCE_LOW).apply {
                    setShowBadge(false)
                    description = "What 8D Music is playing"
                }
            )
        }
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE,
        )
        fun action(icon: Int, label: String, act: String) = Notification.Action.Builder(
            android.graphics.drawable.Icon.createWithResource(this, icon), label,
            PendingIntent.getService(
                this, act.hashCode(), Intent(this, PlayerService::class.java).setAction(act),
                PendingIntent.FLAG_IMMUTABLE,
            ),
        ).build()

        val style = Notification.MediaStyle()
            .setMediaSession(session?.sessionToken)
            .setShowActionsInCompactView(0, 1, 2)

        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat_8d)
            .setContentTitle(title.ifEmpty { "8D Music" })
            .setContentText(
                if (artist.isEmpty()) "Playing in 8D" else "$artist · 8D"
            )
            .setContentIntent(open)
            .setOngoing(playing)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .addAction(action(android.R.drawable.ic_media_previous, "Previous", ACTION_PREV))
            .addAction(
                if (playing) action(android.R.drawable.ic_media_pause, "Pause", ACTION_TOGGLE)
                else action(android.R.drawable.ic_media_play, "Play", ACTION_TOGGLE)
            )
            .addAction(action(android.R.drawable.ic_media_next, "Next", ACTION_NEXT))
            .setStyle(style)
            .build()
    }

    private fun startForegroundCompat(n: Notification) {
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        } else {
            startForeground(NOTIFICATION_ID, n)
        }
    }

    override fun onDestroy() {
        session?.isActive = false
        session?.release()
        session = null
        super.onDestroy()
    }

    companion object {
        private const val CHANNEL = "8d_player"
        private const val NOTIFICATION_ID = 8442

        const val ACTION_TOGGLE = "com.eightd.music.PLAYER_TOGGLE"
        const val ACTION_NEXT = "com.eightd.music.PLAYER_NEXT"
        const val ACTION_PREV = "com.eightd.music.PLAYER_PREV"
        const val ACTION_STOP = "com.eightd.music.PLAYER_STOP"

        private const val EXTRA_TITLE = "title"
        private const val EXTRA_ARTIST = "artist"
        private const val EXTRA_PLAYING = "playing"
        private const val EXTRA_POSITION_MS = "position"
        private const val EXTRA_DURATION_MS = "duration"

        /** Called whenever what is playing, or whether it is playing, changes. */
        fun update(
            context: Context,
            title: String,
            artist: String,
            playing: Boolean,
            positionMs: Long,
            durationMs: Long,
        ) {
            val i = Intent(context, PlayerService::class.java)
                .putExtra(EXTRA_TITLE, title)
                .putExtra(EXTRA_ARTIST, artist)
                .putExtra(EXTRA_PLAYING, playing)
                .putExtra(EXTRA_POSITION_MS, positionMs)
                .putExtra(EXTRA_DURATION_MS, durationMs)
            context.startForegroundService(i)
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, PlayerService::class.java))
        }
    }
}

/**
 * Where the notification's buttons land.
 *
 * The queue lives in the UI, so the service cannot skip tracks by itself; it
 * hands the tap back to whoever is running the player.
 */
object PlaybackBus {
    @Volatile var onToggle: () -> Unit = {}
    @Volatile var onNext: () -> Unit = {}
    @Volatile var onPrevious: () -> Unit = {}
    @Volatile var onSeekMs: (Long) -> Unit = {}
    @Volatile var onStop: () -> Unit = {}

    fun toggle() = onToggle()
    fun next() = onNext()
    fun previous() = onPrevious()
    fun seekToMs(ms: Long) = onSeekMs(ms)
    fun stop() = onStop()
}
