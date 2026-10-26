# Experimental bandwidth diagnostics (v1.3.5)

Open **Settings → Experimental settings → Options**, enable **Record bandwidth logs**,
then use the app normally. Return here to inspect **Logs** or **Functions**, filter
by source/function/reason, and **Export logs** to a local TSV file. The file opens
in a text editor or spreadsheet. The Arabic interface has the same controls.

Recording is off by default. The opt-in survives app restarts; the actual logs do
not. Export before the app process is stopped. At most 2,000 rows are kept in
memory; session totals continue to include evicted rows, whereas the function
summary covers retained rows. All HTTP clients, including clients already created
before enabling, check the opt-in for every new request. Requests already in flight
when enabling/clearing are not retrospectively instrumented. Disabling stops
counting in-flight requests and marks their rows as stopped.

## What a row means

Each row contains timestamp, named client/source, available calling function/stack,
purpose, redacted endpoint, method, bytes received/sent, status and elapsed time.
The stable client/source remains available when coroutine scheduling or optimizer
inlining makes the originating stack unavailable. Application class/member names
are preserved in release builds so available stacks remain readable.

- HTTP rows count body bytes read/written at the **network interceptor**, before
  transparent decompression. Redirects and network retries are separate exchanges;
  cache hits and local media reads never become upstream HTTP rows. Streaming
  bodies are counted as read, without cloning, buffering whole bodies, or making
  additional network requests. Buffer read-ahead and transport draining mean this
  is a diagnostic body-byte measurement, not a mobile carrier billing counter.
- `CALL` failures are zero-byte diagnostics for failed calls (including DNS/TLS
  failures before an HTTP exchange). A call failure can accompany a failed HTTP
  row; it does not add bytes a second time.
- WebSocket rows count received or successfully queued application frame payloads,
  including application heartbeats. Enqueue does not guarantee delivery. Automatic
  protocol ping/pong, framing, handshakes, compression and connection overhead are
  excluded. Listen Together host/client and Discord gateway are instrumented.
- `OBSERVED` rows identify WebView requests and translator calls. Their byte fields
  are blank in TSV and shown as **Unavailable** in the app. They may use internal
  caches; these rows are observations, not proof of a network transfer. Function
  summaries only add measured payload bytes.
- **Android app total** uses `TrafficStats` for the app UID from activation, excluding
  paused recording periods. It includes protocol overhead and other traffic
  attributed to that UID; device support and isolated WebView network processes
  can limit coverage. It is intentionally reported separately from payload totals.

No request/response bodies, headers, cookies, passwords, access tokens, query
strings or URL fragments are retained. Only fixed allowlisted route segments
survive endpoint redaction; room IDs, video IDs and asset filenames become `:id`.
Error messages are reduced to exception class names. The local caller-attribution
header used between Ktor and OkHttp is removed by the application interceptor
before any request is transmitted. Export uses Android's document picker and no
upload service. No diagnostic files are automatically written to disk.

## Network function inventory

These are the concrete request producers and network entry points in this revision.
Higher-level viewmodels, synchronizers and repositories can call the same producers;
their frames appear in the available calling stack. A function can return cached
data without consuming bandwidth, so runtime rows are the evidence of actual
upstream body transfers.

| Source | Functions / entry points | Purpose |
| --- | --- | --- |
| `InnerTube` | `search`, `getSearchSuggestions` | Search and autocomplete |
| `InnerTube` | `player`, `registerPlayback` | Resolve streams; playback tracking requested by the app |
| `InnerTube` | `browse`, `next`, `getQueue`, `getTranscript`, `getSwJsData`, `accountMenu` | Home/library/artist/album/playlist data, queue, transcript, session metadata and account |
| `InnerTube` | `likeVideo`, `unlikeVideo`, `subscribeChannel`, `unsubscribeChannel`, `likePlaylist`, `unlikePlaylist` | Account actions |
| `InnerTube` | `addToPlaylist`, `addPlaylistToPlaylist`, `removeFromPlaylist`, `moveSongPlaylist`, `createPlaylist`, `renamePlaylist`, `deletePlaylist` | Playlist editing |
| `InnerTube` | `returnYouTubeDislike`, `getMediaInfo` | External media/dislike metadata |
| `NewPipeDownloaderImpl` / `NewPipeUtils` | `execute`; callers `warmUp`, `getSignatureTimestamp`, extractor/player helpers | YouTube player JavaScript, signatures and fallback extraction |
| `YTPlayerUtils` | `validateStatus`, reached from `playerResponseForPlayback` / fallback resolution | Stream URL validation / range probes |
| `MusicService` | `createDataSourceFactory`, `createCacheDataSource` upstream | Audio playback on network cache miss |
| `DownloadUtil` | resolving data source / `downloadManager` upstream | Background audio download on network cache miss |
| `Mp3Converter` | `downloadCover` | MP3 artwork; audio conversion from existing cache is local |
| `App` / Coil | `newImageLoader`; all `AsyncImage` and shared image-loader requests, including `Lyrics` and `ComposeToImage` | Image thumbnails, artwork and share-card image loads on cache miss |
| `VeluneWidget` | `updateVeluneWidgetState` | Widget artwork after a track change |
| `Thumbnail` | remembered OkHttp / Media3 video data source | Animated artwork / canvas video |
| `VeluneCanvas` | `getBySongArtist`, `getByAlbumId`, `getByAlbumUrl`, `isHealthy` | Animated-artwork lookup and service health |
| `LrcLib` | `queryLyrics`; callers `getLyrics`, `getAllLyrics`, `lyrics` | Lyric search |
| `BetterLyrics` | `fetchTTML`; callers `getLyrics`, `getAllLyrics` | Synchronized lyric lookup |
| `SimpMusicLyrics` | `getLyricsByVideoId`; callers `getLyrics`, `getAllLyrics` | Lyric lookup by video ID |
| `KuGou` | `searchSongs`, `searchLyricsByKeyword`, `searchLyricsByHash`, `downloadLyrics`; callers `getLyrics`, `getAllPossibleLyricsOptions`, `getLyricsCandidate` | Lyric search and download |
| `LastFM` | `getToken`, `getSession`, `getMobileSession`, `updateNowPlaying`, `scrobble` | Authentication and listening activity |
| `ListenBrainzManager` | `submitPlayingNow`, `submitFinished` | Listening activity submission |
| `KizzyRPC` / Discord `ApiService` | `getUserInfo`, `getImage` | Discord account and presence artwork |
| `DiscordWebSocket` | `connect`, `send` (opcode included), incoming collection | Presence, reconnects and heartbeats |
| `TogetherOnlineApi` | `createSession`, `resolveCode` | Online room creation / joining |
| `TogetherClient` | connection hellos, `requestControl`, `requestAddTrack`, `sendHeartbeat`, `runLoop` | Room membership, playback requests and synchronization |
| `TogetherOnlineHost` | connection hellos, sends and `runLoop` | Host state, controls, broadcasts and heartbeat replies |
| `TogetherServer` | `handleClient`, all sends to client sessions and incoming loop | Local room server traffic |
| `Updater` | `fetchReleasesNetwork`, `getCommitHistory`; public release/check functions use the same client | Update checks, changelog, release list and commit history |
| `ApkInstaller` | `download` | Update APK download |
| Release notes UI | `fetchReleaseNotesText` | Release notes HTML |
| Translator library | `LyricsMenu.translateBatch`, `LyricsMenu.translateLine`, `DiscordRPC.translate` | Observed translation calls; internal byte counts unavailable |
| WebViews | `LoginScreen.shouldInterceptRequest`, `DiscordLoginScreen.shouldInterceptRequest`, `PoTokenExtractionActivity.shouldInterceptRequest` | Observed login/token page resource requests; internal byte counts unavailable |

Ktor clients use the existing OkHttp engine dependency consistently, including the
two former CIO lyric clients, so all native HTTP clients share the same measurement
boundary. Timeouts, request/response plugins, proxy selection and caching policies
are retained. Widget art, APK downloads and release-notes HTML also use the measured
OkHttp clients. The two private Coil loaders now use the application singleton so
they participate in the same network instrumentation and image cache.

## Rollback

**Rollback experiment** immediately disables recording, erases in-memory logs and
counters, invalidates observers of in-flight transfers, and removes the persisted
opt-in. It leaves playback, downloads, accounts and user preferences intact.
Previously exported files are owned by the user and remain where they were saved.
This is a feature reset; Android does not silently downgrade an installed app.

To remove the implementation from source, v1.3.5 contains the feature in one commit.
Create a regular revert commit with `git revert v1.3.5`, review/build it, then push
the revert to `main` and publish a **higher** version tag. Do not force-push or
reuse a release tag. The last pre-feature release is `v1.3.4`, at
`10520af05a608e9b8ce4f1cc20da3366fdbcb259`.

## Validation

Run `./gradlew :diagnostics:test :innertube:test :app:assembleUniversalDebug`.
The diagnostics integration tests use a local HTTP server to check opt-in toggles,
uploads/downloads, compressed responses, redirects, cache hits, HTTP errors,
Ktor caller propagation and header stripping, endpoint redaction, WebSocket UTF-8
payloads, concurrent row eviction, and rollback during an in-flight transfer.
The release-tag workflow also runs the diagnostics tests before building and
publishing the signed universal APK.
