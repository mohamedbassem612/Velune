# Windows deployment

The current 0.3.0 package scripts target Linux x86_64. No Windows package has been built or tested.

A Windows port must build `velune` and `velune-login` with matching Qt Quick / WebEngine Quick modules, OpenSSL and QtKeychain, then stage their QML/plugin dependencies with the matching Qt deployment tools. Keep WebEngine out of the main executable linkage. Stage QtWebEngineProcess/resources/locales only for sign-in, and retain the Chromium sandbox.

Build the existing native `velune-audio` helper and provide an audited `mpv-2.dll` runtime, codecs and verified Windows yt-dlp/Deno helpers. Adapt `ProcessPlaybackEngine`'s platform library environment and runtime path before claiming Windows playback support: the current packaged library directory is Linux-specific. Preserve licenses and corresponding source. Windows authentication, keyring, audio, Connect, installation and updates require separate validation.
