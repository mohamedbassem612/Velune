# Desktop handover review — 2026-11-25

## Result and scope

Reviewed Desktop against Android's shared phone/TV protocol-4 implementation, especially `HandoverController.kt`, `HandoverProtocol.kt`, and the approved disconnection policy in `docs/VELUNE_CONNECT.md`. Phone-to-TV success was reported by the user; it was not independently reproduced in this task.

Desktop's normal transfer, return transfer, paused transfer, source-release fencing, failure/retry and complete peer-audio transfer passed local integration tests after the fixes below. This is **not a certification of complete phone/TV/Desktop parity**. The integration tests connect two actual Desktop protocol stacks over loopback TLS with isolated identities/libraries and fake audio engines; they do not run Android, mDNS discovery, or physical audio output.

## Changes

- `desktop/src/services/MusicServiceConnect.cpp`: accept Android's `order` command, retaining `shuffleOrder` as an alias. Previously Android's command reached the unsupported-command branch.
- Same file: reuse readable local audio or a validated completed Connect cache before requesting peer audio. Previously the existence of a connected media provider bypassed these local copies.
- `desktop/tests/ConnectHandoverTests.cpp`: six integration scenarios using the production TLS transport, coordinator, client, session handling and MusicService. Authentication restoration is excluded from the isolated fixture.
- `desktop/CMakeLists.txt`: register the `connect-handover` test under the existing `VELUNE_DOWNLOAD_TESTS` option.

The pre-existing uncommitted changes were preserved. Android sources, installed builds and personal application data were not changed.

## Verified cases

1. Transfer and return transfer preserve queue/index, position, speed, repeat, shuffle order and end-of-song timer. The test sends Android's exact `order` command and checks the subsequent Next selection.
2. Destination stays paused until the current audio generation confirms release; an acknowledgement from an obsolete generation cannot activate it.
3. Audio absent at the destination is transferred through the real peer-media protocol before activation, and remains available after the controller disconnects.
4. Release timeout leaves both engines paused; a late acknowledgement cannot commit the aborted transfer. Explicit resume and retry succeed.
5. A paused source transfers paused at its saved position. After explicit resume, disconnecting its controller does not stop the independently rendering Desktop coordinator.
6. Destination preparation failure does not release the source; correcting the failure and retrying succeeds.

## Remaining differences found by source review

These were not covered by the successful normal-transfer cases and remain open:

- When Desktop is a client and its coordinator disappears, `ConnectClient::chooseThisComputer()` calls `detach()`. `MusicService::updateRoute()` then restores the pre-connection `localCheckpoint`, rather than the most recent remote queue/checkpoint. Android's explicit local fallback retains the remote checkpoint. Desktop's remote mirror also does not explicitly receive a paused state from `ConnectClient::fail()`.
- Desktop queue editing removes `order` and rebuilds natural order while retaining the shuffle flag; Android edits its Media3 queue and shuffle order. Preservation of a custom shuffle permutation during insert/remove/move is not established.
- Android coordinator identity frames do not advertise `mediaVersion`/`assetVersion`, while Desktop's client reads these capabilities from identity frames. Desktop-to-TV playback can provide media to the TV via the client's connect frame, but TV-to-Desktop cached-media provisioning cannot be assumed from protocol version alone.

Thus normal-transfer checks passed, but full equivalence, especially coordinator loss/recovery and all queue-edit combinations, remains unproven and has identified gaps.

## Validation

- `bash desktop/scripts/build-linux.sh`: passed; no install performed.
- `ctest --test-dir desktop/build-qml -R connect-handover --output-on-failure --repeat until-fail:3`: passed all three runs (six scenarios per run).
- Other five CTest suites: downloads, playback-resume, storage, crossfade and settings-ui all passed with the existing packaged runtime paths below. Crossfade includes a native libmpv decoder test with null audio output; this is not a native end-to-end handover test.
- `git diff --check`: passed.
- `adb devices -l`: no attached devices.

The first broad CTest invocation failed because FFmpeg and QML runtime modules were not on the test environment's paths. They were available in the existing package; no dependencies were installed. The protocol test also initially sent commands before the client received the ownership commit; its synchronization now waits for that authoritative state.

Run from the repository root:

```sh
cmake -S desktop -B desktop/build-qml -DVELUNE_DOWNLOAD_TESTS=ON
bash desktop/scripts/build-linux.sh
ctest --test-dir desktop/build-qml -R connect-handover --output-on-failure

env PATH="$PWD/desktop/dist/linux-qml/tools:$PATH" \
    LD_LIBRARY_PATH="$PWD/desktop/build-qml/native/lib" \
    QML2_IMPORT_PATH="$PWD/desktop/dist/linux-qml/qml" \
    ctest --test-dir desktop/build-qml -E connect-handover --output-on-failure
```

Next work: cover coordinator loss/explicit local fallback with a regression test and align it with Android's retained remote checkpoint. Then validate phone ↔ Desktop (phone initiates) and Desktop ↔ TV (Desktop initiates) on the same LAN with actual discovery, speakers, downloaded and network-only tracks, interrupted Wi-Fi and retry. Windows remains outside the verified Linux build.
