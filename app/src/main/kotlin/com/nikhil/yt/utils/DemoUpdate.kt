package com.nikhil.yt.utils

import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext

/** Local-only update rehearsal. The temporary payload is never installed. */
object DemoUpdate {
    const val SIZE_BYTES = 1024 * 1024

    suspend fun run(cacheDir: File, onProgress: suspend (Int) -> Unit) {
        withContext(Dispatchers.IO) {
            val directory = File(cacheDir, "update-demo")
            check(directory.isDirectory || directory.mkdirs())
            val payload = File.createTempFile("demo-", ".bin", directory)
            try {
                payload.outputStream().use { output ->
                    val chunk = ByteArray(16 * 1024)
                    onProgress(0)
                    repeat(SIZE_BYTES / chunk.size) { index ->
                        delay(80) // Keep progress visible and allow cancellation.
                        currentCoroutineContext().ensureActive()
                        output.write(chunk)
                        onProgress((index + 1) * 100 / (SIZE_BYTES / chunk.size))
                    }
                }
                check(payload.length() == SIZE_BYTES.toLong())
            } finally {
                payload.delete()
            }
        }
    }
}
