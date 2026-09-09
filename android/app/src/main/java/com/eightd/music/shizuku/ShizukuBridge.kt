package com.eightd.music.shizuku

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.ParcelFileDescriptor
import android.util.Log
import rikka.shizuku.Shizuku
import rikka.shizuku.ShizukuRemoteProcess
import rikka.shizuku.SystemServiceHelper
import java.io.FileInputStream
import java.io.InputStreamReader

/**
 * System-wide mode, one step at a time.
 *
 * Android will not tell an ordinary app which audio sessions other apps are
 * using, and without that there is nothing to silence — so the original keeps
 * playing underneath the processed copy. Shizuku can grant us
 * `android.permission.DUMP`, which lets us read the audio server's own state.
 *
 * Nothing here needs root, and nothing leaves the device.
 */
object ShizukuBridge {

    private const val TAG = "8dmusic"
    private const val SHIZUKU_PACKAGE = "moe.shizuku.privileged.api"
    const val REQUEST_CODE = 8802

    /** Every gate between "installed" and "system-wide works". */
    data class Status(
        val installed: Boolean,
        val running: Boolean,
        val permitted: Boolean,
        val dumpGranted: Boolean,
    ) {
        val ready: Boolean get() = dumpGranted
        /** How far along the four steps the phone actually is. */
        val step: Int get() = when {
            dumpGranted -> 4
            permitted -> 3
            running -> 2
            installed -> 1
            else -> 0
        }
    }

    fun status(context: Context): Status {
        val installed = runCatching {
            context.packageManager.getPackageInfo(SHIZUKU_PACKAGE, 0); true
        }.getOrDefault(false)

        val running = runCatching { Shizuku.pingBinder() }.getOrDefault(false)

        val permitted = running && runCatching {
            !Shizuku.isPreV11() && Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
        }.getOrDefault(false)

        val dump = context.checkSelfPermission(Manifest.permission.DUMP) ==
                PackageManager.PERMISSION_GRANTED

        return Status(installed, running, permitted, dump)
    }

    fun requestPermission() {
        runCatching {
            if (!Shizuku.isPreV11()) Shizuku.requestPermission(REQUEST_CODE)
        }.onFailure { Log.e(TAG, "shizuku: requestPermission failed: ${it.message}") }
    }

    /**
     * Grants ourselves DUMP through Shizuku's shell.
     *
     * `pm grant` rather than the hidden IPermissionManager: it needs no
     * hidden-API stubs and behaves the same across versions. DUMP is a
     * development permission, so the shell user is allowed to hand it over.
     */
    fun grantDump(context: Context): Boolean {
        val pkg = context.packageName
        val ok = exec("pm grant $pkg ${Manifest.permission.DUMP}")
        val granted = context.checkSelfPermission(Manifest.permission.DUMP) ==
                PackageManager.PERMISSION_GRANTED
        Log.i(TAG, "shizuku: grant DUMP exec=$ok granted=$granted")
        return granted
    }

    private fun exec(command: String): Boolean = runCatching {
        // newProcess is not public API; Shizuku exposes nothing else that runs
        // a shell command, and this is the call their own fallback path uses.
        val method = Shizuku::class.java.getDeclaredMethod(
            "newProcess", Array<String>::class.java, Array<String>::class.java, String::class.java
        ).apply { isAccessible = true }

        val process = method.invoke(null, arrayOf("sh"), null, null) as ShizukuRemoteProcess
        process.outputStream.use { it.write("$command\nexit\n".toByteArray()); it.flush() }
        process.waitFor() == 0
    }.onFailure { Log.e(TAG, "shizuku: exec failed: ${it.message}") }.getOrDefault(false)

    /**
     * Reads a system service's own dump. Requires DUMP, which is the whole
     * reason Shizuku is involved.
     */
    fun dumpService(context: Context, service: String): String? {
        if (context.checkSelfPermission(Manifest.permission.DUMP) !=
            PackageManager.PERMISSION_GRANTED) return null

        return runCatching {
            val pipe = ParcelFileDescriptor.createPipe()
            val read = pipe[0]
            val write = pipe[1]

            val binder = SystemServiceHelper.getSystemService(service) ?: return null
            binder.dumpAsync(write.fileDescriptor, emptyArray())
            write.close()

            val text = InputStreamReader(FileInputStream(read.fileDescriptor), "UTF-8")
                .use { it.readText() }
            read.close()
            text
        }.onFailure { Log.e(TAG, "shizuku: dump $service failed: ${it.message}") }.getOrNull()
    }
}
