package com.nikhil.yt.utils

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

object ApkInstaller {
    fun canInstall(context: Context): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.O ||
            context.packageManager.canRequestPackageInstalls()

    fun openInstallPermissionSettings(context: Context) {
        val intent = Intent(
            Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
            Uri.parse("package:${context.packageName}")
        ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(intent)
    }

    private fun directory(context: Context) = File(context.filesDir, "updates")

    suspend fun cleanupInstalledUpdates(context: Context) = withContext(Dispatchers.IO) {
        val manager = context.packageManager
        val installed = manager.getPackageInfo(context.packageName, 0)
        fun version(info: android.content.pm.PackageInfo): Long =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) info.longVersionCode
            else info.versionCode.toLong()
        directory(context).listFiles()?.filter { it.extension == "apk" }?.forEach { file ->
            // Keep uninstalled packages. Installation can be dismissed or fail.
            val archive = runCatching { manager.getPackageArchiveInfo(file.absolutePath, 0) }.getOrNull()
            if (archive != null && archive.packageName == installed.packageName &&
                version(archive) <= version(installed)) {
                if (file.delete()) {
                    File(file.path.removeSuffix(".apk") + ".part").delete()
                    File(file.path.removeSuffix(".apk") + ".validator").delete()
                }
            }
        }
    }

    fun downloaded(context: Context, url: String): File? =
        ResumableUpdateDownload.completed(directory(context), url)

    suspend fun download(context: Context, url: String, onProgress: (Int) -> Unit): File =
        ResumableUpdateDownload.download(directory(context), url) { progress ->
            withContext(Dispatchers.Main) { onProgress(progress) }
        }

    fun install(context: Context, file: File) {
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.FileProvider", file)
        val intent = Intent(Intent.ACTION_VIEW)
            .setDataAndType(uri, "application/vnd.android.package-archive")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(intent)
    }
}
