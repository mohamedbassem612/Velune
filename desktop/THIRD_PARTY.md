# Desktop distribution notices

Velune is GPL-3.0-only; the repository LICENSE is included with packages.

The interface uses Qt Quick/QML and Qt Quick Controls 6.4.2. C++ services use Qt Core, Gui and Network, QtKeychain and OpenSSL. Qt WebEngine Quick and its Chromium components are packaged for the separate `velune-login` helper only. Qt has module-specific LGPL/GPL/commercial licensing; WebEngine includes Chromium third-party notices. Packages copy the applicable Ubuntu package copyright/source notices. Preserve corresponding source, configuration, license and relinking obligations for the exact distributed binaries. Bundled runtime hashes are recorded in `runtime-manifest.json`; the manifest is provenance information, not license clearance.

Audio uses libmpv client API 2.2+ through a standalone C++ host. The vendored official v0.37.0 `client.h` retains its upstream license header. libmpv and codec/library builds can carry GPL and other distribution requirements. Supply corresponding source and notices for the exact audited binaries. No mpv binary is fetched at application startup.

MP3 downloads invoke a separately packaged FFmpeg executable with the libmp3lame encoder. FFmpeg licensing depends on its configured components; preserve the notices and corresponding source obligations of the exact binary and shared libraries supplied through `package-linux.py --ffmpeg`. The packager includes the system FFmpeg copyright notice when available. FFmpeg is not downloaded by the running application. Android-compatible MP3 identity fields are read and written by Velune's C++ implementation; no Android/JVM tagging library is included in the desktop runtime.

The existing verified yt-dlp and Deno helper artifacts are reused with their version/provenance manifest. yt-dlp includes third-party code; executable distributions require their upstream notices. Deno is MIT with third-party dependencies. Inter uses the SIL Open Font License. Review exact artifacts before redistribution.

Electron, Node.js and npm dependencies are absent from the new application package. Their preceding source/installation remains in separate rollback backups. The HTML/CSS visual reference is documentation only. This project does not claim a security-maintained upstream release; plan supported Qt/Chromium updates before public distribution.
