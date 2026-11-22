# Windows x64 build and deployment

Use Visual Studio 2022's x64 C++ toolchain, CMake 3.21+, Ninja, and a matching
MSVC Qt installation with Widgets, Network, Test, Multimedia, WebEngineWidgets,
WebChannel and Positioning. Qt WebEngine is the reason to use the supported
MSVC Qt toolchain rather than mix MinGW and MSVC Qt libraries.
Build/install QtKeychain against the same Qt; provide matching x64 OpenSSL.

```powershell
cmake -S desktop -B desktop/build-win -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64;C:/velune-deps" `
  -DOPENSSL_ROOT_DIR=C:/velune-deps `
  -DMPV_ROOT=C:/verified-mpv-sdk `
  -DVELUNE_BUILD_BENCHMARKS=ON
cmake --build desktop/build-win --parallel 2
ctest --test-dir desktop/build-win --output-on-failure --timeout 90
```

`MPV_ROOT/include/mpv/client.h` must expose client API 2.2 or newer. libmpv is
runtime-loaded, so an MSVC `.lib` import library is not needed. Place the audited
x64 `mpv-2.dll` or `libmpv-2.dll` beside the executable, along with its required
runtime dependencies. Velune checks the C API major/minor and required exports.
Use `dumpbin /headers`, `dumpbin /dependents`, and a clean-machine smoke test to
check architecture and dependent DLLs. Do not substitute an arbitrary internet
binary whose license/configuration/source is unknown.

Default playback remains Qt. In PowerShell:

```powershell
$env:VELUNE_PLAYBACK_BACKEND = 'mpv' # or qt
desktop/build-win/velune.exe
```

Missing mpv, incompatible exports/API, or initialization failure produces a
diagnostic and falls back to available Qt. A media failure does not silently
start a second backend. `-DVELUNE_ENABLE_MPV=OFF` builds the Qt-only variant;
missing development headers also disables the optional backend.

## Stage a package

Supply an audited runtime manifest for extras that `windeployqt` does not cover:
OpenSSL, QtKeychain, optional libmpv and its dependencies, and the existing
official yt-dlp/Deno executables. Keep the helpers under `bin/tools/yt-dlp.exe`
and `bin/tools/deno.exe`. There is no helper download at application startup.

Example manifest structure, with real hashes/paths/source references substituted:

```json
{
  "files": [
    {
      "file": "verified/mpv-2.dll",
      "target": "bin/mpv-2.dll",
      "sha256": "REPLACE_WITH_ACTUAL_64_HEX_SHA256",
      "license": "ACTUAL_BINARY_LICENSE",
      "source": "EXACT_CORRESPONDING_SOURCE_AND_BUILD_CONFIGURATION",
      "notice": "verified/mpv-COPYRIGHT.txt"
    }
  ]
}
```

```powershell
desktop/scripts/deploy-windows.ps1 `
  -BuildDirectory desktop/build-win `
  -Destination desktop/package-win `
  -QtBin C:/Qt/6.8.3/msvc2022_64/bin `
  -RuntimeManifest C:/verified-deps/runtime-manifest.json
```

The script runs `cmake --install` and `windeployqt`, verifies supplied hashes,
copies notices, and checks WebEngineProcess, resource packs and locales. Also
retain ICU/snapshot files when required by that Qt build, Qt TLS/multimedia/
platform/image plugins, and the supported Visual C++ redistributable. Qt's
[deployment guide](https://doc.qt.io/qt-6.8/windows-deployment.html) describes
`windeployqt`; it does not replace deployment of non-Qt dependencies.

Test outside the build environment/PATH: launch, open embedded sign-in without
changing authentication behavior, verify HTTPS/Connect TLS, test both playback
backends and each audio format, and confirm missing-mpv fallback. Do not use
real account credentials in CI. Consult [THIRD_PARTY.md](THIRD_PARTY.md) before
redistributing any staged binary.

## Validation status

`.github/workflows/desktop.yml` adds Windows 2022/MSVC x64 compilation, Qt
deployment checks and deterministic tests with optional mpv headers enabled
and disabled. Its mpv-header job compiles the real adapter and exercises the
fake C API, while native mpv decoding is explicitly skipped without a verified
runtime. The workflow has not been executed from this local working tree.
No Windows compilation, native runtime playback, or clean-machine packaging
test has been performed in the available Linux environment. Local Linux validation passed actionlint 1.7.7, YAML parsing, and PowerShell
7.4.13 parsing of the deployment script and all workflow PowerShell steps.
PowerShell manifest rejection checks also passed for a hash mismatch, a parent
path destination, and a missing notice before staging. The benchmark now includes
Windows headers before PSAPI declarations. These checks do not compile MSVC code
or exercise Windows DLL loading, Qt deployment, or clean-machine execution.
The PowerShell packaging script still requires Windows execution for full validation.
