package com.eightd.music

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.provider.MediaStore
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.eightd.music.audio.CaptureService
import com.eightd.music.audio.EngineHolder
import com.eightd.music.audio.PlaybackBus
import com.eightd.music.audio.PlayerService
import com.eightd.music.audio.NowPlayingWatcher
import com.eightd.music.shizuku.ShizukuBridge
import com.eightd.music.ui.AboutScreen
import com.eightd.music.ui.GlassTabBar
import com.eightd.music.ui.Guide
import com.eightd.music.ui.GuideScreen
import com.eightd.music.ui.LiveScreen
import com.eightd.music.ui.LocalPalette
import com.eightd.music.ui.LiveTour
import com.eightd.music.ui.MiniPlayer
import com.eightd.music.ui.MusicScreen
import com.eightd.music.ui.NowPlayingScreen
import com.eightd.music.ui.PlaylistScreen
import com.eightd.music.ui.QueueSheet
import com.eightd.music.ui.PlayerTour
import com.eightd.music.ui.StudioTour
import com.eightd.music.ui.WelcomeFlow
import com.eightd.music.ui.Palette
import com.eightd.music.ui.ShizukuSetupScreen
import com.eightd.music.ui.StudioScreen
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/**
 * Four destinations, Studio first.
 *
 * Studio is where the effect lives, and Live and Player are only the two ways
 * to feed it, so the app opens on the thing it is for rather than on a source
 * picker the user has to answer before hearing anything.
 */
private enum class Tab(val label: String) {
    Studio("Studio"), Live("Live"), Player("Player"), About("About")
}

/** Room for the floating tab bar, which hovers over the page rather than sitting under it. */
private val TabBarInset = 122.dp

/** The mini player sits above the tab bar, so lists need to clear both. */
private val MiniPlayerInset = 74.dp

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { App() }
    }

    override fun onDestroy() {
        // The engine outlives the screen; only tear it down with the process.
        if (isFinishing) EngineHolder.shutdown()
        super.onDestroy()
    }
}

@Composable
private fun App() {
    // The app document is a dark ink design; the light paper theme stays
    // available for the phones that ask for it.
    val p = if (isSystemInDarkTheme()) Palette.Ink else Palette.Paper
    CompositionLocalProvider(LocalPalette provides p) {
        Box(Modifier.fillMaxSize().background(p.ground)) { Shell() }
    }
}

@Composable
private fun Shell() {
    val state: AppState = viewModel()
    val context = LocalContext.current

    var tab by remember { mutableStateOf(Tab.Studio) }
    // Player is armed by hand: the engine has one input, so a tab that grabbed
    // it on sight would fight Live for the output.
    var playerArmed by remember { mutableStateOf(false) }
    var openPlaylistId by remember { mutableStateOf<String?>(null) }
    var showNowPlaying by remember { mutableStateOf(false) }
    var showQueue by remember { mutableStateOf(false) }
    var openGuide by remember { mutableStateOf<Guide?>(null) }
    // Bumped by the telemetry loop when a local track reaches its end; the
    // decoder's own "finished" fires when decoding ends, which is much earlier.
    var trackEnded by remember { mutableStateOf(0) }
    // Which track the end has already been reported for, so the check below
    // fires once rather than thirty times a second.
    var endedFor by remember { mutableStateOf("") }
    var quality by remember { mutableStateOf("Safe") }
    var engineInfo by remember { mutableStateOf("") }

    var angle by remember { mutableStateOf(0f) }
    var distance by remember { mutableStateOf(1f) }
    var peakL by remember { mutableStateOf(0f) }
    var peakR by remember { mutableStateOf(0f) }
    val trail = remember { mutableStateListOf<Float>() }

    val nowPlaying = remember { NowPlayingWatcher(context) }
    var mediaAccess by remember { mutableStateOf(nowPlaying.hasAccess()) }
    var shizuku by remember { mutableStateOf(ShizukuBridge.status(context)) }
    var showShizuku by remember { mutableStateOf(false) }

    var mediaGranted by remember {
        mutableStateOf(context.checkSelfPermission(readAudioPermission()) ==
                PackageManager.PERMISSION_GRANTED)
    }

    val captureState by CaptureService.state.collectAsState()
    // What other apps are playing, polled with the rest of the telemetry.
    var soundingApps by remember {
        mutableStateOf(emptyList<NowPlayingWatcher.AppSound>())
    }

    // Ask for the runtime permissions direct capture needs, then take the
    // projection token; the service cannot start without both.
    val projectionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == android.app.Activity.RESULT_OK && result.data != null) {
            context.startForegroundService(
                Intent(context, CaptureService::class.java)
                    .putExtra(CaptureService.EXTRA_RESULT_CODE, result.resultCode)
                    .putExtra(CaptureService.EXTRA_RESULT_DATA, result.data)
                    .putExtra(
                        CaptureService.EXTRA_SYSTEM_WIDE,
                        state.source == Source.SystemWide && ShizukuBridge.status(context).ready
                    )
            )
            state.setEngine(true)
        }
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { grants ->
        mediaGranted = grants[readAudioPermission()] ?: mediaGranted
        if (grants[Manifest.permission.RECORD_AUDIO] == true) {
            val mgr = context.getSystemService(MediaProjectionManager::class.java)
            projectionLauncher.launch(mgr.createScreenCaptureIntent())
        }
        if (mediaGranted) Thread { loadLibrary(context, state) }.start()
    }

    LaunchedEffect(Unit) {
        EngineHolder.open()
        engineInfo = "${EngineHolder.engine.sampleRate} Hz output · C++ DSP shared with the desktop build"
        state.apply(state.params)
        if (mediaGranted) withContext(Dispatchers.IO) { loadLibrary(context, state) }
    }

    LaunchedEffect(Unit) {
        while (true) {
            shizuku = ShizukuBridge.status(context)
            delay(1500)
        }
    }

    LaunchedEffect(Unit) {
        var frame = 0
        while (true) {
            frame++
            val t = EngineHolder.engine.telemetry()
            angle = t[0]; distance = t[1]; peakL = t[2]; peakR = t[3]
            if (state.engineOn && state.playing) {
                trail.add(angle)
                while (trail.size > 14) trail.removeAt(0)
            } else if (trail.isNotEmpty()) trail.clear()

            if (state.source == Source.Local) {
                val rate = EngineHolder.engine.sampleRate
                if (rate > 0) state.nowSeconds = EngineHolder.engine.position() / rate
                val running = EngineHolder.engine.isRunning()
                if (running != state.playing) state.playing = running
                // End of track is measured against the file's own duration, not
                // against totalFrames(): that counts what has been DECODED so
                // far, so while a track is still streaming, playback catching up
                // with the decoder used to read as "finished" and skip the song.
                val here = state.queue.getOrNull(state.queueIndex)
                val duration = here?.seconds ?: 0
                if (here != null && duration > 0 && !state.loading &&
                    state.nowSeconds >= duration - 1
                ) {
                    if (endedFor != here.uri) {
                        endedFor = here.uri
                        trackEnded++
                    }
                } else if (duration > 0 && state.nowSeconds < duration / 2) {
                    endedFor = ""
                }
            } else {
                mediaAccess = nowPlaying.hasAccess()
                if (frame % 30 == 0) soundingApps = nowPlaying.activeApps()
                val now = nowPlaying.poll()
                if (now != null) {
                    state.nowTitle = now.title
                    state.nowArtist = now.artist
                    state.nowSeconds = now.positionSeconds
                    state.totalSeconds = now.durationSeconds
                    state.playing = now.playing
                } else if (mediaAccess) {
                    state.nowTitle = "Nothing playing"
                    state.nowArtist = ""
                } else {
                    state.nowTitle = "Allow notification access"
                    state.nowArtist = "so 8D Music can show what is playing"
                }
            }
            delay(33)
        }
    }

    /* ------------------------------------------------------------- actions */

    val liveOn = state.engineOn && state.source != Source.Local
    val playerOn = playerArmed && state.source == Source.Local

    fun stopLive() {
        stopCapture(context)
        state.setEngine(false)
        state.playing = false
    }

    fun startLive() {
        // One source at a time: whatever the local player is doing, it stops.
        EngineHolder.engine.pause()
        playerArmed = false
        state.playing = false
        if (state.source == Source.Local) state.choose(Source.Capture)
        if (state.source == Source.SystemWide && !ShizukuBridge.status(context).ready) {
            showShizuku = true
        } else {
            requestCapture(permissionLauncher)
        }
    }

    fun startPlayer() {
        stopLive()
        state.choose(Source.Local)
        playerArmed = true
        // Android 13+ hides the media notification unless this is granted, and
        // a player with no controls in the shade is half a player.
        if (Build.VERSION.SDK_INT >= 33 &&
            context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            permissionLauncher.launch(arrayOf(Manifest.permission.POST_NOTIFICATIONS))
        }
        if (mediaGranted && state.tracks.isEmpty()) Thread { loadLibrary(context, state) }.start()
    }

    /**
     * Start a track and remember the list it came from, so next and previous
     * mean something. A playlist with its own preset applies it here: that is
     * the whole point of attaching one.
     */
    fun playFrom(track: Track, list: List<Track>, name: String, preset: String?) {
        if (liveOn) stopLive()
        playerArmed = true
        state.queue = list
        state.queueIndex = list.indexOfFirst { it.id == track.id }.coerceAtLeast(0)
        state.queueName = name
        if (preset != null) {
            val params = state.userPresets.firstOrNull { it.name == preset }?.params
                ?: Presets.all.firstOrNull { it.first == preset }?.second
            if (params != null) {
                state.presetName = preset
                state.apply(params)
            }
        }
        playTrack(context, state, track)
    }

    fun togglePlayback() {
        if (EngineHolder.engine.isRunning()) {
            EngineHolder.engine.pause(); state.playing = false
        } else {
            // Play from the notification while Live is capturing would run both
            // sources into one engine; the rule holds wherever it is pressed.
            if (liveOn) stopLive()
            playerArmed = true
            state.playing = EngineHolder.engine.start()
        }
    }

    fun playAt(index: Int) {
        if (state.queue.isEmpty()) return
        val i = ((index % state.queue.size) + state.queue.size) % state.queue.size
        state.queueIndex = i
        playTrack(context, state, state.queue[i])
    }

    LaunchedEffect(state.shuffle) {
        if (state.shuffle && state.queue.size > 2) {
            val current = state.queue.getOrNull(state.queueIndex) ?: return@LaunchedEffect
            val rest = state.queue.filterIndexed { i, _ -> i != state.queueIndex }.shuffled()
            state.queue = listOf(current) + rest
            state.queueIndex = 0
        }
    }

    // Play the next track when one ends, so a playlist behaves like a playlist.
    LaunchedEffect(trackEnded) {
        if (trackEnded > 0 && state.queue.size > 1) playAt(state.queueIndex + 1)
    }

    fun stopPlayer() {
        PlayerService.stop(context)
        EngineHolder.engine.pause()
        state.setEngine(false)
        state.playing = false
        playerArmed = false
    }

    // The notification's buttons land here, where the queue lives.
    LaunchedEffect(Unit) {
        PlaybackBus.onToggle = { togglePlayback() }
        PlaybackBus.onNext = { playAt(state.queueIndex + 1) }
        PlaybackBus.onPrevious = {
            if (state.nowSeconds > 3) EngineHolder.engine.seek(0)
            else playAt(state.queueIndex - 1)
        }
        PlaybackBus.onSeekMs = { ms ->
            val rate = EngineHolder.engine.sampleRate
            if (rate > 0) EngineHolder.engine.seek((ms / 1000L * rate).toInt())
        }
        PlaybackBus.onStop = { stopPlayer() }
    }

    // Keep the shade in step: title, artist and the play/pause state.
    LaunchedEffect(playerOn, state.nowTitle, state.nowArtist, state.playing, state.totalSeconds) {
        if (playerOn && state.queue.isNotEmpty()) {
            PlayerService.update(
                context,
                title = state.nowTitle,
                artist = state.nowArtist,
                playing = state.playing,
                positionMs = state.nowSeconds * 1000L,
                durationMs = state.totalSeconds * 1000L,
            )
        } else {
            PlayerService.stop(context)
        }
    }

    val captureNote = when (captureState) {
        CaptureService.CaptureState.NoSignal -> "No signal"
        CaptureService.CaptureState.Denied -> "This app refuses capture"
        else -> null
    }

    /* ------------------------------------------------------- presentations */

    // Welcome once per install, then a short tour the first time each tab is
    // opened. Both are replayable from About, so nothing is lost by skipping.
    if (!state.seenWelcome) {
        WelcomeFlow(
            onFinish = { state.markSeen(AppState.KEY_SEEN_WELCOME) },
            onAccountsNotReady = {
                android.widget.Toast.makeText(
                    context, "Accounts are coming in a later version", android.widget.Toast.LENGTH_SHORT
                ).show()
            },
        )
        return
    }
    if (tab == Tab.Studio && !state.seenStudioTour) {
        StudioTour { state.markSeen(AppState.KEY_SEEN_STUDIO) }
        return
    }
    if (tab == Tab.Live && !state.seenLiveTour) {
        LiveTour { state.markSeen(AppState.KEY_SEEN_LIVE) }
        return
    }
    if (tab == Tab.Player && !state.seenPlayerTour) {
        PlayerTour {
            state.markSeen(AppState.KEY_SEEN_PLAYER)
            if (!mediaGranted) permissionLauncher.launch(arrayOf(readAudioPermission()))
        }
        return
    }

    /* ---------------------------------------------------------------- setup */

    if (showNowPlaying) {
        NowPlayingScreen(
            state = state,
            onBack = { showNowPlaying = false },
            onPlayPause = { togglePlayback() },
            // Under three seconds in, previous means the track before this one;
            // after that it means "start this one again", as every player does.
            onPrev = {
                if (state.nowSeconds > 3) EngineHolder.engine.seek(0)
                else playAt(state.queueIndex - 1)
            },
            onNext = { playAt(state.queueIndex + 1) },
            onSeek = { f ->
                EngineHolder.engine.seek((f * EngineHolder.engine.totalFrames()).toInt())
            },
            onOpenStudio = { showNowPlaying = false; tab = Tab.Studio },
            onOpenQueue = { showQueue = true },
        )
        if (showQueue) QueueSheet(
            state = state,
            onClose = { showQueue = false },
            onPlayAt = { playAt(it) },
        )
        return
    }

    if (showShizuku) {
        ShizukuSetupScreen(
            status = shizuku,
            onInstall = { openShizukuListing(context) },
            onOpenShizuku = { openShizukuApp(context) },
            onRequestPermission = { ShizukuBridge.requestPermission() },
            onGrant = {
                Thread {
                    ShizukuBridge.grantDump(context)
                    shizuku = ShizukuBridge.status(context)
                }.start()
            },
            onSkip = { showShizuku = false },
        )
        return
    }

    /* ----------------------------------------------------------------- tabs */

    Box(Modifier.fillMaxSize()) {
        when (tab) {
            Tab.Studio -> StudioScreen(
                state = state,
                angle = angle, distance = distance, peakL = peakL, peakR = peakR, trail = trail,
                sourceTitle = when {
                    liveOn -> "Live"
                    playerOn -> "Player"
                    else -> "Nothing on"
                },
                sourceDetail = when {
                    liveOn -> captureNote ?: state.nowTitle
                    playerOn -> state.nowTitle
                    else -> "turn on Live or Player"
                },
                sourceLive = liveOn || playerOn,
                quality = quality,
                // The rack header has one line: the long version belongs in About.
                engineInfo = engineInfo.substringBefore(" ·").ifEmpty { "" }
                    .let { if (it.isEmpty()) "" else "$it · C++ DSP" },
                onQuality = { quality = it },
                onSavePreset = {
                    val name = nextPresetName(state)
                    state.savePreset(name)
                    state.presetName = name
                },
                bottomInset = TabBarInset,
            )

            Tab.Live -> LiveScreen(
                state = state,
                on = liveOn,
                captureNote = captureNote,
                shizukuReady = shizuku.ready,
                mediaAccess = mediaAccess,
                playerRunning = playerOn,
                onRoute = { s ->
                    val wasOn = liveOn
                    if (wasOn) stopLive()
                    state.choose(s)
                    if (wasOn) startLive()
                },
                onStart = { startLive() },
                onStop = { stopLive() },
                onSetupShizuku = { showShizuku = true },
                onPlayPause = {
                    if (mediaAccess) nowPlaying.playPause()
                    else context.startActivity(nowPlaying.settingsIntent())
                },
                onPrev = { nowPlaying.previous() },
                onNext = { nowPlaying.next() },
                onSeek = { f ->
                    if (mediaAccess) nowPlaying.seekTo((f * state.totalSeconds).toInt())
                },
                onOpenStudio = { tab = Tab.Studio },
                onOpenNotificationAccess = {
                    runCatching { context.startActivity(nowPlaying.settingsIntent()) }
                },
                onOpenAppInfo = {
                    context.startActivity(
                        Intent(
                            android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                            android.net.Uri.parse("package:${'$'}{context.packageName}"),
                        ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    )
                },
                apps = soundingApps,
                telemetry = "%+d° · %.2f m · reverb %d%%".format(
                    Math.toDegrees(angle.toDouble()).toInt(),
                    distance,
                    (state.params.reverbMix * 100).toInt(),
                ),
                bottomInset = TabBarInset,
            )

            Tab.Player -> {
                // Lists have to clear the mini player too, or their last row is
                // stuck underneath it and can never be tapped.
                val playerInset =
                    if (playerOn && state.queue.isNotEmpty()) TabBarInset + MiniPlayerInset
                    else TabBarInset
                val open = when (openPlaylistId) {
                    null -> null
                    AppState.FAVOURITES_ID -> state.favourites()
                    else -> state.playlists.firstOrNull { it.id == openPlaylistId }
                }
                if (open != null) PlaylistScreen(
                    state = state,
                    playlist = open,
                    onBack = { openPlaylistId = null },
                    onPlay = { t, list, name -> playFrom(t, list, name, open.preset) },
                    onDelete = { state.deletePlaylist(open.id); openPlaylistId = null },
                    bottomInset = playerInset,
                ) else MusicScreen(
                    state = state,
                    on = playerOn,
                    granted = mediaGranted,
                    liveRunning = liveOn,
                    onStart = { startPlayer() },
                    onStop = { stopPlayer() },
                    onGrant = { permissionLauncher.launch(arrayOf(readAudioPermission())) },
                    onPlay = { t, list, name -> playFrom(t, list, name, null) },
                    onOpenPlaylist = { openPlaylistId = it.id },
                    bottomInset = playerInset,
                )
            }

            Tab.About -> {
                val guide = openGuide
                if (guide != null) GuideScreen(
                    guide = guide,
                    onBack = { openGuide = null },
                    bottomInset = TabBarInset,
                ) else AboutScreen(
                    version = "Version 1.0",
                    engineInfo = engineInfo,
                    shizukuReady = shizuku.ready,
                    onSetupShizuku = { showShizuku = true },
                    onOpenRepo = {
                        openUrl(context, "https://github.com/MOHAMEDELWAZANI/8DMusic")
                    },
                    onOpenUrl = { openUrl(context, it) },
                    onReplay = { key ->
                        state.replay(key)
                        // Send the user where that presentation lives, so
                        // replaying Live does not mean hunting for the tab.
                        tab = when (key) {
                            AppState.KEY_SEEN_STUDIO -> Tab.Studio
                            AppState.KEY_SEEN_LIVE -> Tab.Live
                            AppState.KEY_SEEN_PLAYER -> Tab.Player
                            else -> tab
                        }
                    },
                    onOpenGuide = { openGuide = it },
                    bottomInset = TabBarInset,
                )
            }
        }

        Column(
            Modifier.align(Alignment.BottomCenter).padding(bottom = 18.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (tab == Tab.Player && playerOn && state.queue.isNotEmpty()) {
                MiniPlayer(
                    state = state,
                    onOpen = { showNowPlaying = true },
                    onPlayPause = { togglePlayback() },
                    onNext = { playAt(state.queueIndex + 1) },
                )
            }
            GlassTabBar(Tab.entries.map { it.label }, tab.label) { picked ->
                tab = Tab.entries.first { it.label == picked }
                openPlaylistId = null
            }
        }
    }
}

/* ------------------------------------------------------------------ glue */

private fun readAudioPermission() =
    if (Build.VERSION.SDK_INT >= 33) Manifest.permission.READ_MEDIA_AUDIO
    else Manifest.permission.READ_EXTERNAL_STORAGE

private fun requestCapture(
    launcher: androidx.activity.result.ActivityResultLauncher<Array<String>>,
) {
    val perms = mutableListOf(Manifest.permission.RECORD_AUDIO)
    if (Build.VERSION.SDK_INT >= 33) perms += Manifest.permission.POST_NOTIFICATIONS
    launcher.launch(perms.toTypedArray())
}

private fun stopCapture(context: android.content.Context) {
    context.startService(
        Intent(context, CaptureService::class.java).setAction(CaptureService.ACTION_STOP)
    )
}

private fun nextPresetName(state: AppState): String {
    var i = 1
    while (state.userPresets.any { it.name == "My preset $i" }) i++
    return "My preset $i"
}

private fun loadLibrary(context: android.content.Context, state: AppState) {
    val cols = arrayOf(
        MediaStore.Audio.Media._ID,
        MediaStore.Audio.Media.TITLE,
        MediaStore.Audio.Media.ARTIST,
        MediaStore.Audio.Media.DURATION,
        MediaStore.Audio.Media.MIME_TYPE,
        MediaStore.Audio.Media.ALBUM,
        MediaStore.Audio.Media.DATE_ADDED,
    )
    val found = mutableListOf<Track>()
    runCatching {
        context.contentResolver.query(
            MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, cols,
            "${MediaStore.Audio.Media.IS_MUSIC} != 0", null,
            // Newest first: "recently added" is the shelf people actually use,
            // and the library sorts itself alphabetically where that matters.
            "${MediaStore.Audio.Media.DATE_ADDED} DESC"
        )?.use { c ->
            while (c.moveToNext() && found.size < 500) {
                val id = c.getLong(0)
                found += Track(
                    id = id,
                    title = c.getString(1) ?: "Unknown",
                    artist = c.getString(2) ?: "Unknown artist",
                    album = c.getString(5) ?: "",
                    format = prettyFormat(c.getString(4)),
                    seconds = (c.getLong(3) / 1000).toInt(),
                    uri = android.content.ContentUris.withAppendedId(
                        MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, id).toString(),
                )
            }
        }
    }
    state.tracks.clear()
    state.tracks.addAll(found)
}

/** "audio/mpeg" is what MediaStore says; "MP3" is what the file is called. */
private fun prettyFormat(mime: String?): String {
    val sub = (mime ?: "").substringAfter('/').lowercase()
    return when {
        sub.contains("mpeg") || sub == "mp3" -> "MP3"
        sub.contains("flac") -> "FLAC"
        sub.contains("wav") -> "WAV"
        sub.contains("ogg") || sub.contains("vorbis") -> "OGG"
        sub.contains("mp4") || sub.contains("m4a") || sub.contains("aac") -> "M4A"
        sub.contains("opus") -> "OPUS"
        else -> sub.uppercase()
    }
}

private fun playTrack(context: android.content.Context, state: AppState, track: Track) {
    state.choose(Source.Local)
    state.nowTitle = track.title
    state.nowArtist = track.artist
    state.totalSeconds = track.seconds
    state.loading = true

    StreamDecoder.cancel()
    EngineHolder.engine.setLive(false)
    EngineHolder.engine.setLoop(false)

    Thread(null, {
        StreamDecoder.stream(
            context, android.net.Uri.parse(track.uri), EngineHolder.engine,
            onPlayable = {
                state.loading = false
                state.setEngine(true)
                state.playing = EngineHolder.engine.start()
            },
            onFinished = { state.loading = false },
        )
    }, "8d-decode", 512 * 1024).start()
}

private fun openShizukuListing(context: android.content.Context) {
    val market = Intent(Intent.ACTION_VIEW,
        android.net.Uri.parse("market://details?id=moe.shizuku.privileged.api"))
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    val web = Intent(Intent.ACTION_VIEW,
        android.net.Uri.parse("https://shizuku.rikka.app/download/"))
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    runCatching { context.startActivity(market) }.onFailure {
        runCatching { context.startActivity(web) }
    }
}

private fun openUrl(context: android.content.Context, url: String) {
    val view = Intent(Intent.ACTION_VIEW, android.net.Uri.parse(url))
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    runCatching { context.startActivity(view) }
}

private fun openShizukuApp(context: android.content.Context) {
    val launch = context.packageManager
        .getLaunchIntentForPackage("moe.shizuku.privileged.api")
        ?.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    if (launch != null) runCatching { context.startActivity(launch) }
    else openShizukuListing(context)
}
