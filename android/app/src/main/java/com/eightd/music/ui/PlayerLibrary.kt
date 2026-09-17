package com.eightd.music.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.eightd.music.AppState
import com.eightd.music.Playlist
import com.eightd.music.Presets
import com.eightd.music.Track

/*
 * Player, the BitChord layout in 8D's colours: art-led lists, playlists that
 * carry a preset, a mini player above the tab bar and a full-screen Now Playing
 * with the 8D dock on it.
 */

/** Cover art with the lettered tile as the fallback, which is most files. */
@Composable
fun Cover(track: Track?, size: Dp, corner: Dp, px: Int = 256) {
    val p = palette
    val art = rememberAlbumArt(track?.uri, px)
    Box(
        Modifier.size(size).background(p.well, RoundedCornerShape(corner)),
        contentAlignment = Alignment.Center,
    ) {
        if (art != null) {
            Image(
                bitmap = art,
                contentDescription = track?.album?.ifEmpty { track.title },
                contentScale = ContentScale.Crop,
                modifier = Modifier.fillMaxSize().clip(RoundedCornerShape(corner)),
            )
        } else if (track != null) {
            Mark(track.title.take(2), (size.value / 3.4f).sp, p.dim)
        }
    }
}

/* --------------------------------------------------------------- library */

@Composable
fun LibraryScreenV2(
    state: AppState,
    onPlay: (Track, List<Track>, String) -> Unit,
    onOpenPlaylist: (Playlist) -> Unit,
    onStop: () -> Unit,
    bottomInset: Dp,
) {
    val p = palette
    var query by remember { mutableStateOf("") }
    var filter by remember { mutableStateOf("All") }
    var naming by remember { mutableStateOf(false) }
    var newName by remember { mutableStateOf("") }

    val matches = remember(query, state.tracks.size) {
        if (query.isBlank()) state.tracks.toList()
        else state.tracks.filter {
            it.title.contains(query, true) || it.artist.contains(query, true) ||
                it.album.contains(query, true)
        }
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            PageTitle("Player")
            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(9.dp)) {
                T("ON", 12.sp, p.deep, FontWeight.Bold, 1.2.sp)
                PillSwitch(true) { onStop() }
            }
        }

        /* search */
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Row(
                Modifier.weight(1f).height(46.dp).background(p.card, Pill)
                    .padding(horizontal = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Canvas(Modifier.size(18.dp)) {
                    val r = size.width * .36f
                    drawCircle(p.faint, r, Offset(size.width * .42f, size.height * .42f),
                        style = Stroke(2.dp.toPx()))
                    drawLine(p.faint, Offset(size.width * .68f, size.height * .68f),
                        Offset(size.width, size.height), 2.dp.toPx(), cap = StrokeCap.Round)
                }
                Box(Modifier.weight(1f)) {
                    if (query.isEmpty()) T("Songs, albums, playlists", 15.sp, p.ghost)
                    BasicTextField(
                        value = query,
                        onValueChange = { query = it },
                        singleLine = true,
                        cursorBrush = SolidColor(p.accent),
                        textStyle = TextStyle(
                            color = p.text, fontSize = 15.sp, fontFamily = FontFamily.SansSerif,
                        ),
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        }

        /* filters */
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())
                .padding(start = 20.dp, end = 20.dp, top = 14.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            listOf("All", "Playlists", "Songs").forEach { f ->
                PillChip(f, filter == f) { filter = f }
            }
        }

        /* playlists */
        if (filter != "Songs") {
            SectionTitle("Your playlists", "${state.playlists.size}")
            Row(
                Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())
                    .padding(start = 20.dp, end = 20.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                // Favourites first: it is the list the app keeps for you.
                val favourites = state.favourites()
                Column(
                    Modifier.width(148.dp).clickable { onOpenPlaylist(favourites) },
                ) {
                    Box {
                        val firstLiked = state.tracks.firstOrNull { it.id in state.liked }
                        Box(
                            Modifier.size(148.dp).background(
                                p.motion.copy(alpha = .16f), RoundedCornerShape(22.dp)
                            ),
                            contentAlignment = Alignment.Center,
                        ) {
                            if (firstLiked != null) Cover(firstLiked, 148.dp, 22.dp)
                            Box(
                                Modifier.size(56.dp).background(
                                    p.ground.copy(alpha = if (firstLiked != null) .55f else 0f),
                                    CircleShape,
                                ),
                                contentAlignment = Alignment.Center,
                            ) {
                                Canvas(Modifier.size(30.dp)) { heart(p.motion, filled = true) }
                            }
                        }
                    }
                    T("Favourites", 15.sp, p.text, FontWeight.SemiBold, maxLines = 1,
                        modifier = Modifier.padding(top = 9.dp))
                    T(
                        "${state.liked.size} " + if (state.liked.size == 1) "song" else "songs",
                        12.5.sp, p.faint,
                    )
                }
                Column(
                    Modifier.width(148.dp).clickable { newName = ""; naming = true },
                ) {
                    Box(
                        Modifier.size(148.dp).background(p.card, RoundedCornerShape(22.dp)),
                        contentAlignment = Alignment.Center,
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Box(
                                Modifier.size(48.dp).background(p.accent, CircleShape),
                                contentAlignment = Alignment.Center,
                            ) {
                                Canvas(Modifier.size(20.dp)) {
                                    val w = 2.2.dp.toPx()
                                    val c = Offset(size.width / 2, size.height / 2)
                                    drawLine(Color(0xFF08222D), Offset(c.x, 0f),
                                        Offset(c.x, size.height), w, cap = StrokeCap.Round)
                                    drawLine(Color(0xFF08222D), Offset(0f, c.y),
                                        Offset(size.width, c.y), w, cap = StrokeCap.Round)
                                }
                            }
                            Spacer(Modifier.height(10.dp))
                            T("New playlist", 13.sp, p.dim, FontWeight.SemiBold)
                        }
                    }
                }
                state.playlists.forEach { pl ->
                    val first = state.tracksOf(pl).firstOrNull()
                    Column(Modifier.width(148.dp).clickable { onOpenPlaylist(pl) }) {
                        Box {
                            Cover(first, 148.dp, 22.dp)
                            if (pl.preset != null) {
                                Row(
                                    Modifier
                                        .align(Alignment.BottomStart)
                                        .padding(8.dp)
                                        .background(p.ground.copy(alpha = .75f), Pill)
                                        .padding(horizontal = 9.dp, vertical = 5.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(5.dp),
                                ) {
                                    Box(Modifier.size(6.dp).background(p.motion, CircleShape))
                                    T(pl.preset, 11.sp, p.text, FontWeight.Bold, maxLines = 1)
                                }
                            }
                        }
                        val minutes = state.tracksOf(pl).sumOf { it.seconds } / 60
                        T(pl.name, 15.sp, p.text, FontWeight.SemiBold, maxLines = 1,
                            modifier = Modifier.padding(top = 9.dp))
                        T(
                            "${pl.trackIds.size} " +
                                (if (pl.trackIds.size == 1) "song" else "songs") +
                                if (minutes > 0) " · ${longClock(minutes)}" else "",
                            12.5.sp, p.faint, maxLines = 1,
                        )
                    }
                }
            }
        }

        /* recently added */
        if (filter == "All" && query.isBlank() && state.tracks.isNotEmpty()) {
            // MediaStore hands the library back newest first, so the first
            // albums in it are literally the most recent thing added.
            val albums = remember(state.tracks.size) {
                state.tracks
                    .filter { it.album.isNotBlank() }
                    .groupBy { it.album }
                    .entries.take(4)
                    .map { (album, tracks) -> album to tracks }
            }
            if (albums.isNotEmpty()) {
                SectionTitle("Recently added", "${albums.size}")
                Column(
                    Modifier.padding(horizontal = 20.dp),
                    verticalArrangement = Arrangement.spacedBy(18.dp),
                ) {
                    albums.chunked(2).forEach { row ->
                        Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                            row.forEach { (album, tracks) ->
                                Column(
                                    Modifier
                                        .weight(1f)
                                        .clickable { onPlay(tracks.first(), tracks, album) }
                                ) {
                                    Cover(tracks.first(), 168.dp, 20.dp, px = 384)
                                    T(album, 15.sp, p.text, FontWeight.SemiBold, maxLines = 1,
                                        modifier = Modifier.padding(top = 9.dp))
                                    T(
                                        "${tracks.first().artist} · ${tracks.size} " +
                                            if (tracks.size == 1) "song" else "songs",
                                        13.sp, p.faint, maxLines = 1,
                                    )
                                }
                            }
                            if (row.size == 1) Spacer(Modifier.weight(1f))
                        }
                    }
                }
            }
        }

        /* songs */
        if (filter != "Playlists") {
            SectionTitle(
                if (query.isBlank()) "Songs" else "Results",
                "${matches.size}",
            )
            if (matches.isEmpty()) {
                T(
                    if (state.tracks.isEmpty()) "No audio files found on this phone."
                    else "Nothing matches \"$query\".",
                    15.sp, p.dim,
                    modifier = Modifier.padding(horizontal = 20.dp).padding(top = 8.dp),
                )
            } else {
                matches.forEach { t ->
                    TrackRowV2(
                        t,
                        playing = t.uri == state.queue.getOrNull(state.queueIndex)?.uri,
                        liked = state.isLiked(t.id),
                        onToggleLike = { state.toggleLiked(t.id) },
                    ) { onPlay(t, matches, if (query.isBlank()) "Songs" else "Results") }
                }
            }
        }

        Spacer(Modifier.height(bottomInset))
    }

    if (naming) {
        NamePlaylistDialog(
            name = newName,
            onName = { newName = it },
            onCancel = { naming = false },
            onCreate = {
                val pl = state.createPlaylist(newName.ifBlank { "New playlist" })
                naming = false
                onOpenPlaylist(pl)
            },
        )
    }
}

/** Naming it up front, because a list called "Playlist 3" never gets renamed. */
@Composable
private fun NamePlaylistDialog(
    name: String,
    onName: (String) -> Unit,
    onCancel: () -> Unit,
    onCreate: () -> Unit,
) {
    val p = palette
    Box(
        Modifier.fillMaxSize().background(p.ground.copy(alpha = .86f)).clickable { onCancel() },
        contentAlignment = Alignment.Center,
    ) {
        Column(
            Modifier
                .padding(horizontal = 28.dp)
                .fillMaxWidth()
                .background(p.card, CardShape)
                .padding(22.dp),
        ) {
            T("New playlist", 20.sp, p.text, FontWeight.Bold)
            Box(
                Modifier
                    .fillMaxWidth()
                    .padding(top = 16.dp)
                    .height(52.dp)
                    .background(p.well, Pill)
                    .padding(horizontal = 18.dp),
                contentAlignment = Alignment.CenterStart,
            ) {
                if (name.isEmpty()) T("Night drive", 16.sp, p.ghost)
                BasicTextField(
                    value = name,
                    onValueChange = onName,
                    singleLine = true,
                    cursorBrush = SolidColor(p.accent),
                    textStyle = TextStyle(
                        color = p.text, fontSize = 16.sp, fontFamily = FontFamily.SansSerif,
                    ),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            Row(
                Modifier.fillMaxWidth().padding(top = 18.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Box(Modifier.weight(1f)) { GhostPill("Cancel") { onCancel() } }
                Box(Modifier.weight(1f)) { PrimaryPill("Create") { onCreate() } }
            }
        }
    }
}

@Composable
private fun SectionTitle(title: String, trailing: String) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 20.dp).padding(top = 24.dp, bottom = 12.dp),
        verticalAlignment = Alignment.Bottom,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        T(title, 20.sp, palette.text, FontWeight.Bold)
        T(trailing, 13.sp, palette.faint)
    }
}

@Composable
fun TrackRowV2(
    track: Track,
    playing: Boolean,
    liked: Boolean = false,
    onToggleLike: (() -> Unit)? = null,
    trailing: (@Composable () -> Unit)? = null,
    onClick: () -> Unit,
) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp)
            .background(if (playing) p.card else Color.Transparent, RoundedCornerShape(18.dp))
            .clickable { onClick() }
            .padding(horizontal = 8.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Cover(track, 52.dp, 14.dp, px = 128)
        Column(Modifier.weight(1f)) {
            T(track.title, 16.sp, if (playing) p.accent else p.text, FontWeight.SemiBold,
                maxLines = 1)
            T("${track.artist} · ${track.format}", 13.5.sp, p.faint, maxLines = 1)
        }
        if (onToggleLike != null) {
            Box(
                Modifier.size(38.dp).clickable { onToggleLike() },
                contentAlignment = Alignment.Center,
            ) {
                Canvas(Modifier.size(18.dp)) {
                    heart(if (liked) p.motion else p.ghost, filled = liked)
                }
            }
        }
        when {
            trailing != null -> trailing()
            playing -> PlayingBars()
            else -> T(clock(track.seconds), 13.sp, p.faint)
        }
    }
}

@Composable
fun PlayingBars() {
    val p = palette
    Canvas(Modifier.size(18.dp)) {
        val w = 2.6.dp.toPx()
        listOf(0.55f, 0.9f, 0.7f).forEachIndexed { i, h ->
            val x = w / 2 + i * (w * 2.2f)
            drawLine(p.accent, Offset(x, size.height), Offset(x, size.height * (1f - h)),
                w, cap = StrokeCap.Round)
        }
    }
}

/* -------------------------------------------------------- playlist page */

@Composable
fun PlaylistScreen(
    state: AppState,
    playlist: Playlist,
    onBack: () -> Unit,
    onPlay: (Track, List<Track>, String) -> Unit,
    onDelete: () -> Unit,
    bottomInset: Dp,
) {
    val p = palette
    val isFavourites = playlist.id == com.eightd.music.AppState.FAVOURITES_ID
    val tracks = if (isFavourites) state.tracks.filter { it.id in state.liked }
    else state.tracksOf(playlist)
    var picking by remember { mutableStateOf(false) }
    var choosingPreset by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf(false) }
    val art = rememberAlbumArt(tracks.firstOrNull()?.uri, 720)

    Box(Modifier.fillMaxSize()) {
    // The playlist's first cover, blurred behind the top of the page.
    if (art != null) {
        Image(
            bitmap = art,
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxWidth().height(560.dp).blur(60.dp).alpha(.6f),
        )
    } else {
        Glow(
            Modifier.fillMaxWidth().height(520.dp),
            cyan = .12f, magenta = .14f,
            cyanCentre = Offset(.25f, .4f), magentaCentre = Offset(.75f, .22f),
        )
    }
    Box(
        Modifier.fillMaxWidth().height(560.dp).background(
            Brush.verticalGradient(
                listOf(
                    p.ground.copy(alpha = .30f),
                    p.ground.copy(alpha = .70f),
                    p.ground,
                )
            )
        )
    )

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp).padding(top = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            CircleIcon("‹") { onBack() }
            // Favourites is not deletable: it is a view of the hearts.
            if (!isFavourites) CircleIcon("🗑", tint = p.faint) { confirmDelete = true }
            else Spacer(Modifier.size(42.dp))
        }

        Box(Modifier.fillMaxWidth().padding(top = 8.dp), Alignment.Center) {
            Cover(tracks.firstOrNull(), 236.dp, 28.dp, px = 512)
        }

        Column(
            Modifier.fillMaxWidth().padding(horizontal = 24.dp).padding(top = 20.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            T(playlist.name, 28.sp, p.text, FontWeight.Bold, align = TextAlign.Center)
            T("PLAYLIST · ${tracks.size} " +
                (if (tracks.size == 1) "SONG" else "SONGS") +
                " · ${clock(tracks.sumOf { it.seconds })}",
                12.5.sp, p.faint, FontWeight.Bold, 1.2.sp,
                modifier = Modifier.padding(top = 5.dp))

            // The 8D preset is the playlist's own: tapping it is how a night
            // drive stays a night drive.
            Row(
                Modifier
                    .padding(top = 14.dp)
                    .background(
                        if (playlist.preset != null) p.motion.copy(alpha = .14f) else p.card,
                        Pill,
                    )
                    .clickable { choosingPreset = !choosingPreset }
                    .padding(horizontal = 14.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Box(Modifier.size(8.dp).background(
                    if (playlist.preset != null) p.motion else p.ghost, CircleShape))
                T(
                    playlist.preset?.let { "Always plays in $it" } ?: "Set an 8D preset",
                    13.5.sp,
                    if (playlist.preset != null) p.motion else p.dim,
                    FontWeight.Bold,
                )
            }

            if (choosingPreset) {
                Row(
                    Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())
                        .padding(top = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    fun choose(name: String?) {
                        if (isFavourites) state.rememberFavouritePreset(name)
                        else state.setPlaylistPreset(playlist.id, name)
                        choosingPreset = false
                    }
                    PillChip("None", playlist.preset == null) { choose(null) }
                    (state.userPresets.map { it.name } + Presets.all.map { it.first })
                        .forEach { name ->
                            PillChip(name, playlist.preset == name) { choose(name) }
                        }
                }
            }
        }

        Row(
            Modifier.fillMaxWidth().padding(top = 20.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                Modifier
                    .height(52.dp)
                    .background(p.text, Pill)
                    .clickable(enabled = tracks.isNotEmpty()) {
                        tracks.firstOrNull()?.let { onPlay(it, tracks, playlist.name) }
                    }
                    .padding(horizontal = 34.dp),
                contentAlignment = Alignment.Center,
            ) { T("Play", 17.sp, p.ground, FontWeight.Bold) }
            if (!isFavourites) {
                Spacer(Modifier.width(14.dp))
                CircleIcon("+", size = 52.dp) { picking = true }
            }
        }

        Spacer(Modifier.height(18.dp))

        if (tracks.isEmpty()) {
            T(
                if (isFavourites) "No favourites yet. Tap the heart on a song to put it here."
                else "Nothing in here yet.",
                15.sp, p.dim, align = TextAlign.Center,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 30.dp),
            )
            if (!isFavourites) {
                Spacer(Modifier.height(14.dp))
                Box(Modifier.padding(horizontal = 40.dp)) {
                    PrimaryPill("Add songs") { picking = true }
                }
            }
        } else {
            tracks.forEach { t ->
                TrackRowV2(
                    t,
                    playing = t.uri == state.queue.getOrNull(state.queueIndex)?.uri,
                    liked = state.isLiked(t.id),
                    onToggleLike = if (isFavourites) ({ state.toggleLiked(t.id) }) else null,
                    trailing = if (isFavourites) null else ({
                        T("−", 22.sp, p.ghost, modifier = Modifier
                            .clickable { state.removeFromPlaylist(playlist.id, t.id) }
                            .padding(horizontal = 8.dp))
                    }),
                ) { onPlay(t, tracks, playlist.name) }
            }
        }

        Spacer(Modifier.height(bottomInset))
    }
    }

    if (confirmDelete) {
        ConfirmDialog(
            title = "Delete \"${playlist.name}\"?",
            body = "The playlist goes; the songs stay on your phone.",
            confirm = "Delete",
            onCancel = { confirmDelete = false },
            onConfirm = { confirmDelete = false; onDelete() },
        )
    }

    if (picking) {
        AddToPlaylistSheet(
            state = state,
            playlist = playlist,
            onClose = { picking = false },
        )
    }
}

@Composable
private fun ConfirmDialog(
    title: String,
    body: String,
    confirm: String,
    onCancel: () -> Unit,
    onConfirm: () -> Unit,
) {
    val p = palette
    Box(
        Modifier.fillMaxSize().background(p.ground.copy(alpha = .86f)).clickable { onCancel() },
        contentAlignment = Alignment.Center,
    ) {
        Column(
            Modifier
                .padding(horizontal = 28.dp)
                .fillMaxWidth()
                .background(p.card, CardShape)
                .padding(22.dp),
        ) {
            T(title, 20.sp, p.text, FontWeight.Bold)
            T(body, 15.sp, p.dim, modifier = Modifier.padding(top = 8.dp))
            Row(
                Modifier.fillMaxWidth().padding(top = 18.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Box(Modifier.weight(1f)) { GhostPill("Cancel") { onCancel() } }
                Box(Modifier.weight(1f)) {
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .height(50.dp)
                            .background(p.motion, Pill)
                            .clickable { onConfirm() },
                        contentAlignment = Alignment.Center,
                    ) { T(confirm, 16.sp, Color.White, FontWeight.Bold) }
                }
            }
        }
    }
}

@Composable
private fun AddToPlaylistSheet(state: AppState, playlist: Playlist, onClose: () -> Unit) {
    val p = palette
    Column(
        Modifier
            .fillMaxSize()
            .background(p.ground.copy(alpha = .97f))
            .padding(top = 16.dp),
    ) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            T("Add to ${playlist.name}", 20.sp, p.text, FontWeight.Bold)
            T("Done", 15.sp, p.accent, FontWeight.Bold,
                modifier = Modifier.clickable { onClose() })
        }
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(top = 12.dp)) {
            state.tracks.forEach { t ->
                val inList = t.id in playlist.trackIds
                TrackRowV2(t, playing = false, trailing = {
                    T(if (inList) "Added" else "Add", 13.sp,
                        if (inList) p.faint else p.accent, FontWeight.Bold)
                }) {
                    if (inList) state.removeFromPlaylist(playlist.id, t.id)
                    else state.addToPlaylist(playlist.id, t.id)
                }
            }
            Spacer(Modifier.height(40.dp))
        }
    }
}

@Composable
fun CircleIcon(glyph: String, size: Dp = 42.dp, tint: Color? = null, onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier.size(size).background(p.card, CircleShape).clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) { T(glyph, (size.value / 2.2f).sp, tint ?: p.text, FontWeight.Bold) }
}

/* ------------------------------------------------------- now playing */

@Composable
fun NowPlayingScreen(
    state: AppState,
    onBack: () -> Unit,
    onPlayPause: () -> Unit,
    onPrev: () -> Unit,
    onNext: () -> Unit,
    onSeek: (Float) -> Unit,
    onOpenStudio: () -> Unit,
    onOpenQueue: () -> Unit,
) {
    val p = palette
    val track = state.queue.getOrNull(state.queueIndex)
    val art = rememberAlbumArt(track?.uri, 720)

    Box(Modifier.fillMaxSize()) {
        // The artwork is the background, blurred behind a scrim: the canvas
        // takes its colour from the record rather than from the app.
        if (art != null) {
            Image(
                bitmap = art,
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier.fillMaxSize().blur(58.dp).alpha(.75f),
            )
        } else {
            Glow(
                Modifier.fillMaxSize(),
                cyan = .12f, magenta = .16f,
                cyanCentre = Offset(.25f, .35f), magentaCentre = Offset(.75f, .2f),
            )
        }
        Box(
            Modifier.fillMaxSize().background(
                Brush.verticalGradient(
                    listOf(
                        p.ground.copy(alpha = .35f),
                        p.ground.copy(alpha = .55f),
                        p.ground.copy(alpha = .96f),
                    )
                )
            )
        )

        Column(Modifier.fillMaxSize().padding(top = 8.dp)) {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                GlassCircle("⌄", onClick = onBack)
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    T("PLAYING FROM", 11.sp, p.dim, FontWeight.Bold, 1.4.sp)
                    T(state.queueName.ifEmpty { "Player" }, 15.sp, p.text, FontWeight.SemiBold)
                }
                GlassCircle("⋯", onClick = onOpenQueue)
            }

            Box(Modifier.fillMaxWidth().padding(top = 26.dp), Alignment.Center) {
                Cover(track, 320.dp, 30.dp, px = 720)
            }

            Row(
                Modifier.fillMaxWidth().padding(horizontal = 26.dp).padding(top = 26.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    T(state.nowTitle, 26.sp, p.text, FontWeight.Bold, maxLines = 2)
                    T(state.nowArtist, 18.sp, p.dim, maxLines = 1)
                }
                val liked = track != null && state.isLiked(track.id)
                Box(
                    Modifier
                        .size(44.dp)
                        .background(
                            if (liked) p.motion.copy(alpha = .22f) else p.text.copy(alpha = .12f),
                            CircleShape,
                        )
                        .clickable { track?.let { state.toggleLiked(it.id) } },
                    contentAlignment = Alignment.Center,
                ) {
                    Canvas(Modifier.size(20.dp)) { heart(if (liked) p.motion else p.text, liked) }
                }
            }

            Column(Modifier.fillMaxWidth().padding(horizontal = 26.dp).padding(top = 16.dp)) {
                SeekBar(
                    fraction = if (state.totalSeconds > 0)
                        (state.nowSeconds.toFloat() / state.totalSeconds).coerceIn(0f, 1f) else 0f,
                    onSeek = onSeek,
                )
                Row(
                    Modifier.fillMaxWidth().padding(top = 9.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    T(clock(state.nowSeconds), 12.5.sp, p.faint)
                    Box(
                        Modifier
                            .background(p.text.copy(alpha = .10f), Pill)
                            .padding(horizontal = 10.dp, vertical = 4.dp)
                    ) {
                        Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                            T(track?.format ?: "", 11.5.sp, p.dim, FontWeight.Bold)
                            T("8D", 11.5.sp, p.motion, FontWeight.Bold)
                        }
                    }
                    T("-${clock((state.totalSeconds - state.nowSeconds).coerceAtLeast(0))}",
                        12.5.sp, p.faint)
                }
            }

            Row(
                Modifier.fillMaxWidth().padding(horizontal = 30.dp).padding(top = 18.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // Shuffle and repeat are real: one reorders the queue, the other
                // loops the track in the engine.
                Canvas(
                    Modifier.size(24.dp).clickable { state.shuffle = !state.shuffle }
                ) { shuffleGlyph(if (state.shuffle) p.accent else p.faint) }
                Canvas(Modifier.size(40.dp).clickable { onPrev() }) { skip(p.text, forward = false) }
                Box(
                    Modifier.size(78.dp).background(p.text, CircleShape).clickable { onPlayPause() },
                    contentAlignment = Alignment.Center,
                ) {
                    Canvas(Modifier.size(28.dp)) {
                        if (state.playing) pause(p.ground) else play(p.ground)
                    }
                }
                Canvas(Modifier.size(40.dp).clickable { onNext() }) { skip(p.text, forward = true) }
                Canvas(
                    Modifier.size(24.dp).clickable {
                        state.repeatOne = !state.repeatOne
                        com.eightd.music.audio.EngineHolder.engine.setLoop(state.repeatOne)
                    }
                ) { repeatGlyph(if (state.repeatOne) p.accent else p.faint) }
            }

            Spacer(Modifier.weight(1f))

            Row(
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                GlassCircle(glyph = null, size = 44.dp, onClick = onOpenStudio) {
                    Canvas(Modifier.size(20.dp)) { headphonesGlyph(p.text) }
                }
                Box(Modifier.weight(1f)) {
                    EightDDock(state.presetName, compact = true, onOpenStudio = onOpenStudio)
                }
                GlassCircle(glyph = null, size = 44.dp, onClick = onOpenQueue) {
                    Canvas(Modifier.size(20.dp)) { queueGlyph(p.text) }
                }
            }
            Spacer(Modifier.height(26.dp))
        }
    }
}

/**
 * A bar you can scrub: tap anywhere to jump, or drag along it.
 *
 * The touch target is 28dp tall while the bar itself is 7dp, because a 7dp
 * target is a bar you fight rather than one you use.
 */
@Composable
fun SeekBar(fraction: Float, onSeek: (Float) -> Unit) {
    val p = palette
    var width by remember { mutableStateOf(1f) }
    var dragging by remember { mutableStateOf(false) }
    var dragFraction by remember { mutableStateOf(0f) }
    val shown = if (dragging) dragFraction else fraction

    Box(
        Modifier
            .fillMaxWidth()
            .height(28.dp)
            .onSizeChanged { width = it.width.toFloat().coerceAtLeast(1f) }
            .pointerInput(Unit) {
                detectTapGestures { offset -> onSeek((offset.x / width).coerceIn(0f, 1f)) }
            }
            .pointerInput(Unit) {
                detectHorizontalDragGestures(
                    onDragStart = { start ->
                        dragging = true
                        dragFraction = (start.x / width).coerceIn(0f, 1f)
                    },
                    onDragEnd = { dragging = false; onSeek(dragFraction) },
                    onDragCancel = { dragging = false },
                ) { change, _ ->
                    change.consume()
                    dragFraction = (change.position.x / width).coerceIn(0f, 1f)
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxWidth().height(if (dragging) 11.dp else 7.dp)) {
            val h = size.height
            val radius = androidx.compose.ui.geometry.CornerRadius(h / 2)
            drawRoundRect(p.well, size = size, cornerRadius = radius)
            if (shown > 0f) drawRoundRect(
                p.text,
                size = androidx.compose.ui.geometry.Size(size.width * shown, h),
                cornerRadius = radius,
            )
        }
    }
}

/** The round glass buttons that frame Now Playing. */
@Composable
fun GlassCircle(
    glyph: String?,
    size: Dp = 42.dp,
    onClick: () -> Unit,
    content: @Composable () -> Unit = {},
) {
    val p = palette
    Box(
        Modifier
            .size(size)
            .background(p.text.copy(alpha = .12f), CircleShape)
            .clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) {
        if (glyph != null) T(glyph, (size.value / 2.4f).sp, p.text, FontWeight.Bold) else content()
    }
}

/* ------------------------------------------------------------- glyphs */

internal fun DrawScope.heart(tint: Color, filled: Boolean) {
    val s = size.width
    val path = androidx.compose.ui.graphics.Path().apply {
        moveTo(s * .5f, s * .86f)
        cubicTo(s * .08f, s * .58f, s * .12f, s * .18f, s * .5f, s * .30f)
        cubicTo(s * .88f, s * .18f, s * .92f, s * .58f, s * .5f, s * .86f)
        close()
    }
    if (filled) drawPath(path, tint)
    else drawPath(path, tint, style = Stroke(2.dp.toPx(), cap = StrokeCap.Round))
}

private fun DrawScope.play(tint: Color) {
    val s = size.width
    val path = androidx.compose.ui.graphics.Path().apply {
        moveTo(s * .16f, 0f); lineTo(s, s / 2f); lineTo(s * .16f, s); close()
    }
    drawPath(path, tint)
}

private fun DrawScope.pause(tint: Color) {
    val s = size.width
    val w = s * .28f
    drawRoundRect(tint, size = androidx.compose.ui.geometry.Size(w, s),
        cornerRadius = androidx.compose.ui.geometry.CornerRadius(w / 3))
    drawRoundRect(tint, topLeft = Offset(s - w, 0f),
        size = androidx.compose.ui.geometry.Size(w, s),
        cornerRadius = androidx.compose.ui.geometry.CornerRadius(w / 3))
}

private fun DrawScope.skip(tint: Color, forward: Boolean) {
    val s = size.width
    val bar = s * .10f
    val path = androidx.compose.ui.graphics.Path().apply {
        if (forward) { moveTo(s * .1f, s * .2f); lineTo(s * .62f, s / 2f); lineTo(s * .1f, s * .8f) }
        else { moveTo(s * .9f, s * .2f); lineTo(s * .38f, s / 2f); lineTo(s * .9f, s * .8f) }
        close()
    }
    drawPath(path, tint)
    drawRoundRect(
        tint,
        topLeft = Offset(if (forward) s * .68f else s * .22f, s * .2f),
        size = androidx.compose.ui.geometry.Size(bar, s * .6f),
        cornerRadius = androidx.compose.ui.geometry.CornerRadius(bar / 2),
    )
}

private fun DrawScope.shuffleGlyph(tint: Color) {
    val s = size.width
    val w = 2.dp.toPx()
    drawLine(tint, Offset(s * .08f, s * .28f), Offset(s * .92f, s * .74f), w, cap = StrokeCap.Round)
    drawLine(tint, Offset(s * .08f, s * .74f), Offset(s * .92f, s * .28f), w, cap = StrokeCap.Round)
    listOf(s * .28f, s * .74f).forEach { y ->
        drawLine(tint, Offset(s * .72f, y), Offset(s * .92f, y), w, cap = StrokeCap.Round)
    }
}

private fun DrawScope.repeatGlyph(tint: Color) {
    val s = size.width
    val w = 2.dp.toPx()
    drawArc(tint, 20f, 300f, false, topLeft = Offset(s * .12f, s * .12f),
        size = androidx.compose.ui.geometry.Size(s * .76f, s * .76f),
        style = Stroke(w, cap = StrokeCap.Round))
    drawLine(tint, Offset(s * .74f, s * .06f), Offset(s * .92f, s * .24f), w, cap = StrokeCap.Round)
}

private fun DrawScope.headphonesGlyph(tint: Color) {
    val s = size.width
    val w = 2.dp.toPx()
    drawArc(tint, 180f, 180f, false, topLeft = Offset(s * .08f, s * .16f),
        size = androidx.compose.ui.geometry.Size(s * .84f, s * .84f),
        style = Stroke(w, cap = StrokeCap.Round))
    listOf(s * .08f, s * .72f).forEach { x ->
        drawRoundRect(tint, topLeft = Offset(x, s * .56f),
            size = androidx.compose.ui.geometry.Size(s * .2f, s * .34f),
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(s * .1f))
    }
}

private fun DrawScope.queueGlyph(tint: Color) {
    val s = size.width
    val w = 2.dp.toPx()
    listOf(.2f, .45f, .7f).forEachIndexed { i, y ->
        val end = if (i == 2) s * .55f else s * .92f
        drawLine(tint, Offset(s * .08f, s * y), Offset(end, s * y), w, cap = StrokeCap.Round)
    }
    drawCircle(tint, s * .1f, Offset(s * .78f, s * .8f))
    drawLine(tint, Offset(s * .88f, s * .8f), Offset(s * .88f, s * .42f), w, cap = StrokeCap.Round)
}

/** What plays next, and a way to jump straight to it. */
@Composable
fun QueueSheet(state: AppState, onClose: () -> Unit, onPlayAt: (Int) -> Unit) {
    val p = palette
    Column(Modifier.fillMaxSize().background(p.ground.copy(alpha = .97f)).padding(top = 16.dp)) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Column {
                T("Up next", 20.sp, p.text, FontWeight.Bold)
                T(state.queueName.ifEmpty { "Player" }, 13.sp, p.faint)
            }
            T("Done", 15.sp, p.accent, FontWeight.Bold, modifier = Modifier.clickable { onClose() })
        }
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(top = 12.dp)) {
            state.queue.forEachIndexed { i, t ->
                TrackRowV2(
                    t,
                    playing = i == state.queueIndex,
                    trailing = { T("${i + 1}", 13.sp, p.ghost) },
                ) { onPlayAt(i); onClose() }
            }
            Spacer(Modifier.height(40.dp))
        }
    }
}

/* -------------------------------------------------------- mini player */

@Composable
fun MiniPlayer(state: AppState, onOpen: () -> Unit, onPlayPause: () -> Unit, onNext: () -> Unit) {
    val p = palette
    val track = state.queue.getOrNull(state.queueIndex)
    Column {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 14.dp)
            .height(64.dp)
            .background(p.raised, RoundedCornerShape(22.dp))
            .clickable { onOpen() }
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Cover(track, 48.dp, 14.dp, px = 128)
        Column(Modifier.weight(1f)) {
            T(state.nowTitle, 15.sp, p.text, FontWeight.SemiBold, maxLines = 1)
            Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                T(state.nowArtist, 12.5.sp, p.faint, maxLines = 1,
                    modifier = Modifier.weight(1f, fill = false))
                T("· 8D ${state.presetName}", 12.5.sp, p.motion, FontWeight.SemiBold, maxLines = 1)
            }
        }
        val liked = track != null && state.isLiked(track.id)
        Box(
            Modifier.size(40.dp).clickable { track?.let { state.toggleLiked(it.id) } },
            contentAlignment = Alignment.Center,
        ) {
            Canvas(Modifier.size(18.dp)) {
                heart(if (liked) p.motion else p.faint, filled = liked)
            }
        }
        Box(
            Modifier.size(44.dp).clickable { onPlayPause() },
            contentAlignment = Alignment.Center,
        ) { T(if (state.playing) "❚❚" else "▶", 17.sp, p.text, FontWeight.Bold) }
        Box(
            Modifier.size(40.dp).clickable { onNext() },
            contentAlignment = Alignment.Center,
        ) { T("››", 20.sp, p.text, FontWeight.Bold) }
    }
    // The magenta hairline from the canvas: how far through the song you are.
    Box(Modifier.fillMaxWidth().padding(horizontal = 36.dp).offset(y = (-6).dp)) {
        Canvas(Modifier.fillMaxWidth().height(3.dp)) {
            val f = if (state.totalSeconds > 0)
                (state.nowSeconds.toFloat() / state.totalSeconds).coerceIn(0f, 1f) else 0f
            val radius = androidx.compose.ui.geometry.CornerRadius(size.height / 2)
            drawRoundRect(p.text.copy(alpha = .12f), size = size, cornerRadius = radius)
            if (f > 0f) drawRoundRect(
                p.motion,
                size = androidx.compose.ui.geometry.Size(size.width * f, size.height),
                cornerRadius = radius,
            )
        }
    }
    }
}

/** 92 minutes reads as "1 h 32 min", the way a playlist length should. */
internal fun longClock(minutes: Int): String =
    if (minutes >= 60) "${minutes / 60} h ${minutes % 60} min" else "$minutes min"
