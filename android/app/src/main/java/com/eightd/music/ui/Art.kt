package com.eightd.music.ui

import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import android.util.LruCache
import android.util.Size
import androidx.core.graphics.drawable.toBitmap
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Album art, straight from the file.
 *
 * MediaStore.loadThumbnail reads whatever artwork is embedded in the track, so
 * the library shows real covers rather than two letters. Files with no artwork
 * return null and keep the lettered tile, which is most of the point of drawing
 * the tile in the first place.
 */
private object ArtCache {
    // A few dozen covers at thumbnail size: enough for a scrolling library,
    // small enough that it never becomes the reason the app is remembered.
    private val cache = LruCache<String, ImageBitmap>(64)
    private val missing = mutableSetOf<String>()

    fun cached(uri: String): ImageBitmap? = cache.get(uri)

    fun load(context: Context, uri: String, px: Int): ImageBitmap? {
        cache.get(uri)?.let { return it }
        if (uri in missing) return null
        val bmp: Bitmap? = runCatching {
            context.contentResolver.loadThumbnail(Uri.parse(uri), Size(px, px), null)
        }.getOrNull()
        if (bmp == null) {
            synchronized(missing) { missing += uri }
            return null
        }
        val image = bmp.asImageBitmap()
        cache.put(uri, image)
        return image
    }
}

/**
 * The launcher icon of another app, read from the phone itself.
 *
 * The presentations talk about YouTube and Spotify, and shipping their logos
 * would mean bundling other people's trademarks; asking the package manager
 * shows the real icon when the app is installed and nothing when it is not,
 * which is also more honest about what Live can actually reach.
 */
@Composable
fun rememberAppIcon(pkg: String, px: Int = 96): ImageBitmap? {
    val context = LocalContext.current
    val state = produceState<ImageBitmap?>(null, pkg) {
        value = withContext(Dispatchers.IO) {
            runCatching {
                context.packageManager.getApplicationIcon(pkg).toBitmap(px, px).asImageBitmap()
            }.getOrNull()
        }
    }
    return state.value
}

/** Apps worth showing in the presentations, in the order we would pick them. */
val LiveExampleApps = listOf(
    "com.google.android.youtube",
    "com.spotify.music",
    "com.brave.browser",
    "com.android.chrome",
    "com.netflix.mediaclient",
    "com.instagram.android",
    "com.zhiliaoapp.musically",
    "com.google.android.apps.youtube.music",
)

/** The first few of [LiveExampleApps] that are actually on this phone. */
@Composable
fun rememberInstalledExampleApps(count: Int = 3): List<String> {
    val context = LocalContext.current
    val state = produceState(emptyList<String>(), count) {
        value = withContext(Dispatchers.IO) {
            LiveExampleApps.filter { pkg ->
                runCatching { context.packageManager.getApplicationInfo(pkg, 0) }.isSuccess
            }.take(count)
        }
    }
    return state.value
}

/** Null until it has loaded, and null for good when the file carries no art. */
@Composable
fun rememberAlbumArt(uri: String?, px: Int = 256): ImageBitmap? {
    if (uri == null) return null
    val context = LocalContext.current
    val state = produceState(ArtCache.cached(uri), uri, px) {
        if (value == null) value = withContext(Dispatchers.IO) { ArtCache.load(context, uri, px) }
    }
    return state.value
}
