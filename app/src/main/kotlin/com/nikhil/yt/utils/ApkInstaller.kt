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

    private fun version(info: android.content.pm.PackageInfo): Long =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) info.longVersionCode else info.versionCode.toLong()

    private fun validate(context: Context, file: File) {
        val manager = context.packageManager
        val installed = manager.getPackageInfo(context.packageName, 0)
        val archive = manager.getPackageArchiveInfo(file.absolutePath, 0)
        if (archive == null || !isCompatibleUpdatePackage(
                installed.packageName, version(installed), archive.packageName, version(archive))) {
            throw InvalidUpdatePackageException()
        }
    }

    suspend fun cleanupInstalledUpdates(context: Context) = withContext(Dispatchers.IO) {
        val manager = context.packageManager
        val installed = manager.getPackageInfo(context.packageName, 0)
        directory(context).listFiles()?.filter { it.extension == "apk" }?.forEach { file ->
            // Keep valid pending updates when installation was dismissed or failed.
            val archive = runCatching { manager.getPackageArchiveInfo(file.absolutePath, 0) }.getOrNull()
            if (archive == null || archive.packageName != installed.packageName || version(archive) <= version(installed)) {
                if (file.delete()) {
                    File(file.path.removeSuffix(".apk") + ".part").delete()
                    File(file.path.removeSuffix(".apk") + ".validator").delete()
                }
            }
        }
    }

    fun downloaded(context: Context, url: String): File? {
        val file = ResumableUpdateDownload.completed(directory(context), url) ?: return null
        return try { validate(context, file); file } catch (_: InvalidUpdatePackageException) { file.delete(); null }
    }

    suspend fun download(context: Context, url: String, onProgress: (Int) -> Unit): File =
        ResumableUpdateDownload.download(directory(context), url) { progress ->
            withContext(Dispatchers.Main) { onProgress(progress) }
        }.also { file ->
            withContext(Dispatchers.IO) {
                try { validate(context, file) } catch (e: InvalidUpdatePackageException) { file.delete(); throw e }
            }
        }

    fun install(context: Context, file: File) {
        try { validate(context, file) } catch (e: InvalidUpdatePackageException) {
            if (file.parentFile == directory(context)) file.delete()
            throw e
        }
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.FileProvider", file)
        val intent = Intent(Intent.ACTION_VIEW)
            .setDataAndType(uri, "application/vnd.android.package-archive")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(intent)
    }
}
