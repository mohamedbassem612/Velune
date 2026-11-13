package com.nikhil.yt.together

import com.nikhil.yt.localsync.LibrarySyncStatus
import com.nikhil.yt.localsync.LocalLibrarySync
import io.ktor.client.HttpClient
import io.ktor.client.engine.okhttp.OkHttp
import io.ktor.client.request.header
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.client.statement.bodyAsText
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.contentType
import java.util.concurrent.TimeUnit

/** Legacy mobile Together HTTP adapter. The library engine has no Together dependency. */
object TogetherLibraryExchange {
    suspend fun exchange(library: LocalLibrarySync, info: TogetherJoinInfo, participant: String) {
        library.status.value = LibrarySyncStatus.Syncing
        val body = library.encode(library.snapshot())
        require(body.toByteArray().size <= LocalLibrarySync.MAX_BODY_BYTES) { "Library is too large for this preview" }
        HttpClient(OkHttp) {
            engine { config { connectTimeout(10, TimeUnit.SECONDS); readTimeout(30, TimeUnit.SECONDS); writeTimeout(30, TimeUnit.SECONDS) } }
        }.use { client ->
            val response = client.post("http://${info.host}:${info.port}/library") {
                header(HttpHeaders.Authorization, "Bearer ${info.sessionKey}")
                header("X-Velune-Participant", participant)
                contentType(ContentType.Application.Json)
                setBody(body)
            }
            check(response.status.value == 200) { "Library sync unavailable (${response.status.value})" }
            val text = response.bodyAsText()
            require(text.toByteArray().size <= LocalLibrarySync.MAX_BODY_BYTES) { "Library response is too large" }
            library.accept(library.decode(text))
        }
    }
}
