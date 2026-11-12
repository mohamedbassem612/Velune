# Velune Connect architecture and implementation record

## Scope and audit (2026-11-11)

Work remains on `tv` at base `e6103ea`. Existing uncommitted mobile-only Together
changes and the session report are preserved. A pre-Connect patch and copies of
previously untracked files are in `/workspace/scratch/velune-connect-baseline`.
No commits, publishing, installation, cloud backend or changes to main are authorized.

Audit: Handover already has queue replication, a TV coordinator, renderer epochs,
queue revisions and a ForwardingPlayer facade. Its protocol 2 uses plaintext TCP,
an NSD bearer token, name-based reconnect, immediate ownership transfer and no
readiness acknowledgement. It does not persist coordinator authority. LocalLibrarySync
has versioned likes and playlist tombstones, ordered playlist contents and deduplicated
history, but TogetherJoinInfo/HTTP is its only exchange transport. Google sign-in
uses WebView CookieManager/InnerTubeCookieKey. No supported delegated TV authorization
was found; exporting these cookies or the advanced login token is excluded.

## Component boundaries and ownership

- `ConnectIdentity`: non-exportable Android Keystore signing identity; public-key
  SHA-256 identifies an installation. No name or address is an authorization key.
- `ConnectTrustStore`: Keystore AES-GCM encrypted, atomic, no-backup trust records.
  Pins and display labels only; never account cookies. Reinstall/key loss requires pairing.
- `ConnectTransport`: TLS 1.2+ with client certificates, public-key pinning for trusted
  peers. Unknown peers are allowed only during explicit pairing, have no playback or
  library access, and must compare a nonce-bound code on both screens. Approval on
  both devices precedes trust. NSD contains public identity/protocol only.
- `HandoverController`: retained LAN coordinator/client orchestration, using the
  authenticated transport; existing HandoverPlayer remains the playback facade.
- Session authority: TV owns session ID/epochs/revisions. Reconnect attaches to TV
  state, never imports an old phone queue. Explicit transfer is separate from attach.
- Transfer: prepare destination paused, await readiness, release source and await
  acknowledgement, then commit epoch and activate destination. Before release,
  failure cancels preparation; after an ambiguous release/commit, fail paused rather
  than risk duplicate renderers. No distributed protocol guarantees uninterrupted
  audio and zero duplicates during arbitrary partitions.
- Library: LocalLibrarySync owns the local Room mirror and durable logical clocks;
  Connect exchanges versioned records on its authenticated channel. Trust authorization
  grants playback and library access, not Google authentication. Each device retains
  its database when a peer disappears or is revoked.
- Together policy: mobile-only; starting/joining Together explicitly detaches the
  phone from Connect, preserving TV playback. Connect attach/commands remain blocked
  until Together becomes Idle/Error. The two modes cannot operate concurrently on
  one phone player. No Together dependency in Connect pairing/sync.

## State transitions

Discovery -> untrusted -> pairing window + TLS -> compare codes -> dual approval
-> trusted -> authenticated attach -> reconcile -> connected.
Unexpected loss -> bounded exponential retry with jitter -> authenticated reconcile.
Explicit disconnect -> disable reconnect. Revoke -> remove trust + close live channel.
Transfer: idle -> preparing -> ready -> releasing source -> committed -> idle;
pre-commit failure -> abort; uncertain post-release result -> paused reconciliation.

## Implementation phases and validation contract

1. Trust, secure transport, device management, reconnect/session validation and
   Together exclusion. Legacy token trust is deliberately not migrated.
2. Buffered transfer, rollback, durable coordinator recovery and autonomous rendering.
3. Independent bidirectional library sync and truthful account/library status UI.
4. Focused JVM tests, integration/device checks, both flavor builds and documentation.

Each phase's actual outcomes and limitations are recorded below as work proceeds.
Keystore/TLS interoperability, remote focus and uninterrupted audio need Android
execution; JVM tests alone do not establish these properties or Spotify parity.

## Migration and rollback

Protocol 3 rejects legacy protocol 2 clients: both apps must be updated and paired.
Existing music database and old handover preferences are retained. Old advertised
names/tokens never become trust records. Connect trust and authority live under
noBackupFilesDir and are not cloned by Android backup. No account login is copied.

Rollback code by reversing only the Connect delta, preserving the pre-existing
working-tree patch; never use `git reset --hard`. A final Connect-only patch will
be placed in the scratch baseline directory. Removing Connect trust/session files
is optional and loses pairing/session metadata only, not Room library data. Do not
roll back a production deployment to insecure protocol 2 without assessing LAN access.

## Phase 1 — trust and session reliability

Changed files: `connect/ConnectCredentials.kt`, `ConnectIdentity.kt`,
`ConnectTransport.kt`, `ConnectPolicy.kt`; `handover/HandoverController.kt` and
`HandoverProtocol.kt`; mobile `HandoverButton.kt`, `ConnectPanel.kt`,
`SettingsScreen.kt`; TV `TvPhoneConnection.kt`; shared English/Arabic Connect
resources; `MusicService.kt` for the Together mode lease. Earlier mobile-only
Together BuildConfig/deep-link guards are preserved.

Implemented: non-exportable Keystore RSA identity; AES-GCM protected atomic pins;
mutual TLS 1.2/1.3; commit-then-reveal nonce/key-bound 48-bit comparison code and explicit approval
on both devices; 2-minute pairing window/90-second approval limit; bounded pending
handshakes; no NSD bearer token; fingerprint-based discovery/reconnect; revocation;
frame sequence validation; command IDs, duplicate suppression and acceptance
acknowledgements; capped exponential retry with jitter; explicit disconnect;
TV-owned reconciliation; replacement connections invalidate old peer handlers.
TLS shutdown runs off the UI thread because close_notify may wait for a peer.

Together entry detaches and disables automatic reconnect before the phone player
is modified. The mode lease is released after explicit leave/transport cleanup,
or a failed startup with no Together transport. Connect components have no
Together transport dependency. The TV remains unable to host/join Together.

Tests: see final validation ledger below. JVM transport tests use actual loopback
TLS and ephemeral JVM keys; they do not test Android Keystore. The Android
instrumented test covers key persistence, encrypted trust reload and pairing,
but must be executed on Android before those paths are considered device-validated.

Risks: users must compare the code, not approve blindly. Discovery is unauthenticated
and can be disrupted. Keystore/provider compatibility on actual TV hardware remains
unverified; there is no insecure fallback. Pins do not assert a Google account or
hardware attestation. A compromised already-authorized app/device is outside the
network-peer threat model. A lost local identity requires explicit pairing again.

Rollback: later phases depend on these protocol changes, so reverse the complete
Connect-only patch, not individual trust files while leaving controllers active.
Retain the pre-existing mobile-only Together changes. Neither revocation nor code
rollback removes already mirrored Room data.

## Phase 2 — independent renderer and buffered transfer

Changed files: `connect/ConnectTransfer.kt`, `handover/HandoverController.kt`,
`HandoverPlayer.kt`, `HandoverSession.kt`; new/extended transfer and authority tests.

Implemented: prepare/resolve/buffer the destination paused; await READY; request
source release and acknowledge it; capture final source position and state; reject
readiness if queue/current index changed during preparation; commit a new epoch;
activate the new renderer. UI play/prepare is fenced during transfer, and a released
phone cannot resume through normal player controls before reconciliation. A missing
release acknowledgement is ambiguous and does not trigger automatic source resume.

Phone task-removal cleanup now detaches from a remote renderer before clearing
its local player, including when the stop-on-task-clear preference is enabled.
The regression test is in `playback/StopMusicOnTaskClearTest.kt`.

TV authority is checkpointed atomically every two seconds and at ownership commits
under noBackupFilesDir, bound to the coordinator identity. Restart retains queue,
position and settings, advances the epoch and starts paused. Existing MusicService
queue persistence remains enabled. A disconnected phone cannot replace TV authority;
loss of a non-rendering phone never pauses TV playback. Existing renderer-local
autoplay remains in MusicService; mirrors suppress autoplay. No account credentials
or cached stream URL are transferred as authority to play restricted content.

Risks: arbitrary network partitions cannot guarantee continuous sound and zero
duplicates together. Destination activation has no distributed exactly-once commit
across device power loss. Unknown outcomes require reconnect and explicit resume;
command acknowledgements confirm acceptance, not successful media decoding. TV state
checkpoints serialize/write on the service thread; large queues need device latency
profiling and may warrant a dedicated ordered persistence worker. Last checkpoint
can lag actual position by roughly two seconds. Android can restrict service restart;
there is no promise of automatic boot playback. Transfers require the source to
be reachable: the TV does not silently steal ownership from an offline phone that
might still be playing. There is no cross-network remote control or cloud failover.
Autoplay/end-of-track, focus and
real audio continuity have not been exercised on devices in this session.

Rollback: reverse the complete Connect-only code patch to the captured pre-Connect
working tree. Keep music database/queue data. If testing on a device later, back up
Connect session metadata before removing it; deleting that file loses only LAN
session ownership, not the library. Never silently resume an old renderer on rollback.

## Phase 3 — independent library mirroring and account distinction

Changed files: `connect/ConnectLibraryExchange.kt`, `ConnectLibraryDelta.kt`,
`localsync/LocalLibrarySync.kt`, `LibrarySnapshot.kt`, `db/DatabaseDao.kt`,
`together/TogetherLibraryExchange.kt` (extracted legacy mobile HTTP adapter),
`ConnectPanel.kt`, resources and `docs/LOCAL_SYNC.md`; delta/merge tests.

Implemented: authenticated bidirectional exchange independent of Together; the
library engine no longer imports Together or its HTTP adapter. The legacy mobile
transport lives under `together/TogetherLibraryExchange.kt`. Periodic
local observation on both peers; acknowledged per-connection deltas, full reconciliation
after reconnect; versioned likes/unlikes and playlist tombstones; ordered available
playlist contents; idempotent history insertion and Connect history tombstones.
The atomic journal now retains observed history so clearing it can be detected.
No database schema/table change is required: the added DAO operation deletes a
history natural key from the existing event table. Existing version-1 journals
load with default history version/removal fields. Together exchange omits history
tombstones and retains its legacy append-only history behavior.

TV shows library synchronization status, trusted/connected device labels, active
renderer status and an explicit library-mirror/account-authorization distinction.
Approval covers bidirectional library edits; credentials, downloads and local audio
are not copied. Disconnect/revoke never clears a mirrored library.

Risks: whole-playlist conflict resolution, recent-history limits, snapshot size limits
and separate Room/journal commit boundaries are documented in LOCAL_SYNC.md. Tombstones
are not purged based on peer absence. Older application code ignores the new history
fields; downgrading can discard history tombstones, so preserve the no-backup journal
before a device downgrade. No migration has been run on a user's device here.

Rollback: reverse this phase only together with its controller/UI calls, or use the
complete Connect-only patch. Preserve the Room database and no-backup journal; do
not clear them to resolve connectivity issues. There is no account-token migration.

## Validation ledger and reproducible commands

The project explicitly uses `testBuildType = "release"`; Debug unit/Android test
task names do not exist. Initial attempts using those names failed before task
execution. The initial Gradle wrapper download failed, then was recovered by using
the inherited proxy with a checksum-verified Gradle 9.4.0. User-local JDK 21 and
Android SDK 36/build tools were prepared; the JDK uses the environment's CA trust.
No network verification was disabled. An initial build raced an edit to the Room
DAO and failed against stale generated code; subsequent generation/compilation passed.

Standalone core tests (no Android SDK required):

```sh
./gradlew -p tools/connect-core-tests test
```

Application validation (JDK 21 and Android SDK 36 configured):

```sh
./gradlew :app:assembleTvUniversalDebug :app:assembleMobileUniversalDebug \
  :app:testTvUniversalReleaseUnitTest :app:testMobileUniversalReleaseUnitTest \
  :app:compileTvUniversalReleaseAndroidTestSources --max-workers=2
```

The managed environment's tool binaries and raw build logs are under
`/workspace/scratch/connect-tools/`; no machine-specific paths were added to
repository build configuration. Core tests compile the actual policy, transport,
transfer and merge sources, with no mocked Android TLS implementation.

Evidence observed before the final source verification rerun:

| Level | Observed result |
| --- | --- |
| Static | Changed resource XML parses; `git diff --check` passes |
| Standalone JVM | 26 tests, 0 failures/errors, including 3 real loopback TLS tests |
| App JVM / TV Release | 88 tests, 0 failures/errors |
| App JVM / Mobile Release | 88 tests, 0 failures/errors |
| APK build | TV Universal Debug and Mobile Universal Debug assembled |
| Instrumentation source compilation | TV Release AndroidTest Kotlin/Java compiled |
| Emulator/device execution | Not run; `adb devices` listed no attached devices |
| Installation / release / publishing | Not performed; user authorization excludes installation |

Counts overlap: the standalone tests are also included in each app test suite;
these are not 202 distinct tests. The final verification rerun result is recorded
at the end of this document once it completes. An intermediate verification also
raced the new task-cleanup test against an older compiled MusicService and failed
with an unresolved helper; this is not reported as a passing run.

### Requirement coverage and outstanding runtime checks

| Behavior | Executed coverage | Still requires Android/device execution |
| --- | --- | --- |
| First pairing/approval | TLS dual approval + rejection; no trust before approval | Keystore TLS and remote-focus consent |
| Unauthorized/revoked peers | TLS rejection and real revoked reconnect rejection; pin policy | Installed-device revocation under live commands |
| Trusted reconnect | Real pinned TLS reconnect | Persistent Keystore/pins across process/device reboot |
| Library conflicts/deletions | Merge and delta tests including stale history tombstones | Room import and bilateral edits on phone/TV |
| Transfer both directions | Transfer reducer and authority queue/settings tests | ExoPlayer buffering and actual audio transitions |
| Network interruption | Pre-release/ambiguous-release reducer tests | Wi-Fi loss at each transfer phase |
| Phone disappearance | Authority test rejects phone changes to TV renderer | TV autoplay over multiple tracks with phone absent |
| TV restart | Recovery reducer retains queue/settings, advances epoch, pauses | Atomic checkpoint plus process-kill/service restoration |
| Stale renderer | Authority epoch/session rejection and TLS frame sequencing | Concurrent reconnects on actual hardware |
| Together isolation | Mode-lease unit test and guarded service entry paths | Together host/guest while switching Connect renderer |

This implementation is not a claim of Spotify Connect feature parity or completed
production security/device validation.

## Final verification result — 2026-11-11

The final build after the legacy Together adapter extraction completed successfully
(`verified-build.log`: `BUILD SUCCESSFUL in 3m 28s`). Production source hashes were
captured before this run and verified unchanged afterward. There were no further
production edits after this successful run.

| Check | Final observed result |
| --- | --- |
| Standalone core JVM suite | 28 tests, 0 failures, 0 errors; includes 4 real loopback TLS tests |
| TV Release JVM suite | 91 tests, 0 failures, 0 errors |
| Mobile Release JVM suite | 91 tests, 0 failures, 0 errors |
| TV Universal Debug APK | Assembled successfully |
| Mobile Universal Debug APK | Assembled successfully |
| TV Release instrumentation sources | Kotlin and Java compilation succeeded; tests not executed |
| Static checks | Changed resource XML parsed; `git diff --check` passed |
| Emulator/real-device tests | Not executed; no attached device, and no APK installation authorized |

The suites overlap; these are not 210 distinct tests. Phase-specific executed
coverage includes 9 policy tests and 4 TLS integration tests for phase 1;
5 transfer reducer tests, 6 authority/recovery tests and the phone task-cleanup
regression for phase 2; 7 existing merge/link tests and 3 delta/history tests for
phase 3. Runtime Android/Room/ExoPlayer checks in the matrix above remain outstanding.

Debug build artifacts (not installed):

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `app/build/outputs/apk/tvUniversal/debug/app-tv-universal-debug.apk` | 95766317 | `52b136808317f361ad5b300860b009bf1f6a12244eef77a17e46b76a647e3172` |
| `app/build/outputs/apk/mobileUniversal/debug/app-mobile-universal-debug.apk` | 95369060 | `8c3d3bb430c8bf08ba5a62b16b5f16ad00a4aad629855358922988c0005173fc` |

No minified Release APK was built or installed by this work. Release source/test
compilation is distinct from Release packaging, signing, or device execution.

### Concrete rollback artifact

The Connect-only patch and manifest are available at:

- `/workspace/scratch/velune-connect-baseline/connect-only.patch`
- `/workspace/scratch/velune-connect-baseline/connect-changed-files.txt`

The patch is generated against the actual pre-Connect working tree, including
its then-untracked TV screen and report. The real Git index was not staged and no
commit was created. Reverse-apply validation succeeded. To inspect or later roll
back, from the repository directory:

```sh
# Read-only validation; already executed successfully during this work.
git apply --reverse --check /workspace/scratch/velune-connect-baseline/connect-only.patch

# Apply only if you intentionally want to remove the Connect implementation.
# This command was NOT executed during this work.
git apply --reverse /workspace/scratch/velune-connect-baseline/connect-only.patch
```

Recheck the patch if you make additional changes before rollback. Do not clear
user data or reset the branch as a rollback shortcut. Device journal downgrade
cautions in phase 3 still apply if these changes are deployed later.

Final branch/ref checks: `tv` remains at `e6103ea`, `main` remains at `05e6c98`.
All implementation changes are uncommitted. The original Arabic session report
was compared byte-for-byte with its pre-Connect copy and is unchanged. No commit,
push, pull request, release publication, external cloud service or APK installation
was performed.
