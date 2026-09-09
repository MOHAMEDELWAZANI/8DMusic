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
import com.eightd.music.audio.NowPlayingWatcher
import com.eightd.music.ui.Divider
import com.eightd.music.ui.EffectsScreen
import com.eightd.music.ui.LibraryScreen
import com.eightd.music.ui.LocalPalette
import com.eightd.music.ui.ModesScreen
import com.eightd.music.ui.Palette
import com.eightd.music.ui.PlayerScreen
import com.eightd.music.ui.PresetsScreen
import com.eightd.music.shizuku.ShizukuBridge
import com.eightd.music.ui.SettingsScreen
import com.eightd.music.ui.ShizukuScreen
import com.eightd.music.ui.SourcePickerScreen
import com.eightd.music.ui.Text
import com.eightd.music.ui.palette
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

private enum class Tab(val label: String) {
    Player("PLAYER"), Modes("MODES"), Presets("PRESETS"),
    Library("LIBRARY"), Settings("SETTINGS")
}

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
    val p = palette

    var tab by remember { mutableStateOf(Tab.Player) }
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
        while (true) {
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
            } else {
                mediaAccess = nowPlaying.hasAccess()
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

    if (!state.sourceChosen) {
        SourcePickerScreen(state) {
            when (state.source) {
                Source.Capture -> requestCapture(permissionLauncher)
                Source.SystemWide -> showShizuku = !ShizukuBridge.status(context).ready
                else -> Unit
            }
        }
        return
    }

    if (showShizuku || (state.source == Source.SystemWide && !shizuku.ready && !state.engineOn)) {
        ShizukuScreen(
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
            onSkip = { showShizuku = false; state.choose(Source.Local) },
        )
        return
    }

    val captureNote = when (captureState) {
        CaptureService.CaptureState.NoSignal -> "NO SIGNAL"
        CaptureService.CaptureState.Denied -> "CAPTURE REFUSED"
        else -> null
    }

    Column(Modifier.fillMaxSize()) {
        Box(Modifier.weight(1f)) {
            when (tab) {
                Tab.Player -> PlayerScreen(
                    state, angle, distance, peakL, peakR, trail, captureNote,
                    onEngineToggle = {
                        if (state.engineOn) {
                            stopCapture(context)
                            state.setEngine(false)
                            state.playing = false
                            EngineHolder.engine.pause()
                        } else when (state.source) {
                            Source.Capture, Source.SystemWide -> requestCapture(permissionLauncher)
                            Source.Local -> {
                                state.setEngine(true)
                                state.playing = EngineHolder.engine.start()
                            }
                        }
                    },
                    onPlayPause = {
                        if (state.source == Source.Local) {
                            if (EngineHolder.engine.isRunning()) {
                                EngineHolder.engine.pause()
                                state.playing = false
                            } else {
                                state.playing = EngineHolder.engine.start()
                            }
                        } else if (mediaAccess) {
                            nowPlaying.playPause()
                        } else {
                            context.startActivity(nowPlaying.settingsIntent())
                        }
                    },
                    onPrev = {
                        if (state.source == Source.Local) EngineHolder.engine.seek(0)
                        else nowPlaying.previous()
                    },
                    onNext = {
                        if (state.source == Source.Local) EngineHolder.engine.seek(0)
                        else nowPlaying.next()
                    },
                    onSeek = { f ->
                        if (state.source == Source.Local) {
                            EngineHolder.engine.seek((f * EngineHolder.engine.totalFrames()).toInt())
                        } else {
                            nowPlaying.seekTo((f * state.totalSeconds).toInt())
                        }
                    },
                )
                Tab.Modes -> ModesScreen(state)
                Tab.Presets -> PresetsScreen(state) { state.savePreset(nextPresetName(state)) }
                Tab.Library -> LibraryScreen(
                    state,
                    onPlay = { track -> playTrack(context, state, track) },
                    onGrant = { permissionLauncher.launch(arrayOf(readAudioPermission())) },
                    granted = mediaGranted,
                )
                Tab.Settings -> SettingsScreen(state, quality, { quality = it }, engineInfo)
            }
        }

        if (tab == Tab.Presets || tab == Tab.Modes) Unit
        BottomBar(tab) { tab = it }
    }
}

@Composable
private fun BottomBar(current: Tab, onPick: (Tab) -> Unit) {
    val p = palette
    Column {
        Divider()
        Row(Modifier.fillMaxWidth().background(p.panelSoft)) {
            Tab.entries.forEach { t ->
                val on = t == current
                Box(
                    Modifier.weight(1f).clickable { onPick(t) }.padding(vertical = 14.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(t.label, 10.sp, if (on) p.accent else p.faint,
                        if (on) FontWeight.SemiBold else FontWeight.Normal, letterSpacing = 1.2.sp)
                }
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
    )
    val found = mutableListOf<Track>()
    runCatching {
        context.contentResolver.query(
            MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, cols,
            "${MediaStore.Audio.Media.IS_MUSIC} != 0", null,
            "${MediaStore.Audio.Media.TITLE} ASC"
        )?.use { c ->
            while (c.moveToNext() && found.size < 500) {
                val id = c.getLong(0)
                found += Track(
                    id = id,
                    title = c.getString(1) ?: "Unknown",
                    artist = c.getString(2) ?: "Unknown artist",
                    format = (c.getString(4) ?: "").substringAfter('/').uppercase(),
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

private fun openShizukuApp(context: android.content.Context) {
    val launch = context.packageManager
        .getLaunchIntentForPackage("moe.shizuku.privileged.api")
        ?.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    if (launch != null) runCatching { context.startActivity(launch) }
    else openShizukuListing(context)
}
