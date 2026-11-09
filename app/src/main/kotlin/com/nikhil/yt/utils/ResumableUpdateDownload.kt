package com.nikhil.yt.utils

import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/** Durable, URL-scoped downloads. Only complete responses are promoted to APKs. */
object ResumableUpdateDownload {
    private val mutex = Mutex()

    private fun base(directory: File, url: String): File {
        val key = MessageDigest.getInstance("SHA-256").digest(url.toByteArray())
            .joinToString("") { "%02x".format(it) }
        return File(directory, key)
    }

    fun completed(directory: File, url: String): File? =
        File(base(directory, url).path + ".apk").takeIf { it.isFile && it.length() > 0 }

    suspend fun download(directory: File, url: String, onProgress: suspend (Int) -> Unit): File =
        mutex.withLock {
            withContext(Dispatchers.IO) {
                completed(directory, url)?.let { return@withContext it }
                check(directory.isDirectory || directory.mkdirs())
                val base = base(directory, url)
                val partial = File(base.path + ".part")
                val validatorFile = File(base.path + ".validator")
                val validator = validatorFile.takeIf { it.isFile }?.readText().orEmpty()
                // Without a validator we cannot safely combine responses across attempts.
                val offset = if (validator.isNotBlank()) partial.length() else 0L
                val connection = URL(url).openConnection() as HttpURLConnection
                try {
                    connection.connectTimeout = 15_000
                    connection.readTimeout = 30_000
                    connection.setRequestProperty("Accept-Encoding", "identity")
                    if (offset > 0) {
                        connection.setRequestProperty("Range", "bytes=$offset-")
                        connection.setRequestProperty("If-Range", validator)
                    }
                    connection.connect()
                    val code = connection.responseCode
                    // A stale/oversized partial must be discarded, not mistaken for completion.
                    if (code == 416) {
                        partial.delete()
                        validatorFile.delete()
                        throw IOException("Download changed; retry to restart")
                    }
                    if (code != 200 && code != 206) throw IOException("HTTP $code")
                    val range = Regex("bytes (\\d+)-(\\d+)/(\\d+)")
                        .matchEntire(connection.getHeaderField("Content-Range").orEmpty())
                    val append = code == 206 && offset > 0
                    if (code == 206 && (range == null || range.groupValues[1].toLong() != offset)) {
                        throw IOException("Invalid download range")
                    }
                    val start = if (append) offset else 0L
                    val total = if (code == 206) range!!.groupValues[3].toLong()
                        else connection.contentLengthLong
                    val etag = connection.getHeaderField("ETag")?.takeUnless { it.startsWith("W/") }
                    val newValidator = etag ?: connection.getHeaderField("Last-Modified").orEmpty()
                    if (append && newValidator.isNotEmpty() && newValidator != validator) {
                        partial.delete()
                        validatorFile.delete()
                        throw IOException("Download changed; retry to restart")
                    }
                    // Truncate before replacing metadata when a server ignores Range.
                    if (!append) partial.writeBytes(byteArrayOf())
                    validatorFile.writeText(newValidator.ifEmpty { if (append) validator else "" })
                    var done = start
                    var lastProgress = -2
                    suspend fun report() {
                        val value = if (total > 0) ((done * 100 / total).toInt()).coerceIn(0, 99) else -1
                        if (value != lastProgress) { lastProgress = value; onProgress(value) }
                    }
                    report()
                    connection.inputStream.use { input ->
                        java.io.FileOutputStream(partial, append).use { output ->
                            val buffer = ByteArray(64 * 1024)
                            while (true) {
                                currentCoroutineContext().ensureActive()
                                val count = input.read(buffer)
                                if (count < 0) break
                                output.write(buffer, 0, count)
                                done += count
                                report()
                            }
                            output.fd.sync()
                        }
                    }
                    currentCoroutineContext().ensureActive()
                    if (done == 0L || (total > 0 && done != total)) throw IOException("Incomplete download")
                    val complete = File(base.path + ".apk")
                    check(partial.renameTo(complete)) { "Cannot save update" }
                    validatorFile.delete()
                    onProgress(100)
                    complete
                } finally {
                    connection.disconnect()
                }
            }
        }
}
