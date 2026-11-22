# Playback dependencies and distribution

Velune is distributed under the repository's GNU GPL version 3. Phase 2 uses
dynamic Qt/OpenSSL linking and runtime dynamic loading of libmpv. No libmpv,
FFmpeg, Qt, or Windows runtime binary is committed or automatically downloaded
by the application. Static linking of libmpv, Qt, or FFmpeg is not configured or validated.

## Artifacts inspected for Linux validation

| Component | Exact artifact inspected | License evidence and obligations |
| --- | --- | --- |
| libmpv | Ubuntu `libmpv2`, `libmpv-dev` **0.37.0-1ubuntu4** | The package's `/usr/share/doc/libmpv2/copyright` declares its binaries **GPL-3+**. The C API header is **ISC**. Preserve both notices and provide corresponding source/build information for distributed binaries. |
| FFmpeg | Ubuntu **7:6.1.1-3ubuntu5**, libavcodec60/libavformat60 and related libraries | Both libraries' runtime license functions report **GPL version 2 or later**, with `--enable-gpl` in their configuration. Package copyright also identifies GPL-3+ dependency combinations for some codec/filter configurations. Distribute exact corresponding sources, patches and build configuration, and preserve component notices. |
| Qt Base | Ubuntu **6.4.2+dfsg-21.1build5** | LGPL-3.0/GPL alternatives and individual third-party licenses in the package copyright. Supply notices, applicable license texts, exact corresponding Qt sources and modifications; preserve users' ability to replace/relink LGPL libraries. |
| Qt Multimedia | Ubuntu **6.4.2-11build3** | Qt licensing plus the actual FFmpeg/GStreamer/plugin dependencies. Audit deployed plugins as well as Qt DLLs/shared objects. |
| Qt WebEngine | Ubuntu **6.4.2-final+dfsg-12ubuntu9** | Qt LGPL/GPL alternatives plus Chromium's many third-party licenses. Include Chromium attribution and the actual module's source/notices. Do not describe the whole WebEngine bundle as a single LGPL component. |
| QtKeychain | Ubuntu **0.14.2-1build5** | BSD-2-Clause code; BSD-3-Clause CMake portions. Retain copyright, conditions and disclaimers. |
| OpenSSL | Ubuntu **3.0.13-0ubuntu3.16** | Apache-2.0. Preserve license and applicable notices; document modifications. |

These version statements describe the validation environment, not every future
release. The installed distribution supplies further shared dependencies; those
must be inventoried if copied into a standalone package. Ubuntu package notices
were inspected locally, rather than inferring licenses from library names.

Exact Ubuntu source packages are available from Launchpad:
[mpv](https://launchpad.net/ubuntu/+source/mpv/0.37.0-1ubuntu4),
[FFmpeg](https://launchpad.net/ubuntu/+source/ffmpeg/7:6.1.1-3ubuntu5),
[Qt Base](https://launchpad.net/ubuntu/+source/qt6-base/6.4.2+dfsg-21.1build5),
[Qt Multimedia](https://launchpad.net/ubuntu/+source/qt6-multimedia/6.4.2-11build3),
[Qt WebEngine](https://launchpad.net/ubuntu/+source/qt6-webengine/6.4.2-final+dfsg-12ubuntu9),
[QtKeychain](https://launchpad.net/ubuntu/+source/qtkeychain/0.14.2-1build5), and
[OpenSSL](https://launchpad.net/ubuntu/+source/openssl/3.0.13-0ubuntu3.16).
For a release, retain the source archives and patches alongside the binaries or
use an applicable compliant source-offer mechanism; a moving upstream homepage
alone does not identify corresponding source.

## libmpv and FFmpeg build variations

The [mpv 0.37 copyright declaration](https://github.com/mpv-player/mpv/blob/v0.37.0/Copyright)
distinguishes default GPL builds from LGPL builds configured without GPL-only
code. Linked dependencies can change the resulting obligations. The inspected
Ubuntu package explicitly declares GPL-3+ binaries; its packaging comment
mentions Samba, although that library is not listed in this artifact's direct
dependencies. Do not use that discrepancy to assume a less restrictive license.

[FFmpeg's licensing documentation](https://ffmpeg.org/legal.html) explains how
optional GPL components change licensing. A binary configured with nonfree
components may be unredistributable. Audit its actual configuration and linked
libraries before packaging. A Windows DLL named `mpv-2.dll` does not establish
its source, license, FFmpeg configuration, or redistributability.

## Resolver helpers and benchmark tools

The existing helper installer pins `yt-dlp_linux` **2026.10.26** and Deno
**v2.9.8**, with release hashes in `scripts/install-playback-tools.py` and its
generated `tools/versions.json`. Phase 2 does not replace or update them.
yt-dlp source uses the Unlicense, but its standalone executables incorporate
other code and are distributed under GPLv3+ combinations; retain the release's
third-party license information and sources. See the upstream
[licensing section](https://github.com/yt-dlp/yt-dlp#license).
Deno uses MIT licensing plus bundled dependency notices; retain the exact
release's [license](https://github.com/denoland/deno/blob/main/LICENSE.md) and
its third-party attribution before distribution. These helper releases were not used for live
service benchmarking in Phase 2 and their full binary dependency trees have
not been certified for redistribution here.

FFmpeg fixture generation, PulseAudio's null sink/capture tools, and any scripting
language used to analyze benchmark JSON are development/test tools only. They
are not introduced into Velune's playback engine runtime.

## Packaging requirements

1. Record each shipped file's version, origin, SHA-256, build configuration,
   license, matching source archive/patches, and notice files. Include transitive
   libraries, Qt plugins, Chromium resources, and resolver helper components.
2. Include Velune's GPLv3 text, its corresponding source and build instructions,
   and all applicable third-party notices/license texts. Preserve LGPL library
   replacement/relinking rights and required installation information.
3. Ship exact dependency sources/build modifications using a compliant mechanism.
   Dynamic loading is not a way to avoid GPL/LGPL distribution obligations.
4. Use an MSVC-compatible Qt/WebEngine toolchain on Windows. Runtime-loading the
   official mpv C API avoids an import-library requirement, but DLL architecture,
   exported ABI and its transitive runtime dependencies still must match.
5. `deploy-windows.ps1` requires hashes/notices/source references for supplied
   extra runtimes and downloads nothing. This is an artifact inventory check,
   not certification of a binary's licensing. No Windows libmpv binary has been
   selected or bundled in this change.
6. The Linux installer copies package copyright files from a supplied runtime
   prefix. Supplying the corresponding source and any additional attribution
   remains part of preparing a distributable release.

The Windows CI dependency versions are separately pinned in its workflow. A
successful CI build does not make those artifacts a complete release package.
