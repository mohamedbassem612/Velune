#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir=${1:-"$script_dir/../build"}
runtime_prefix=${2:-}
mpv_runtime_prefix=${3:-}
install_dir="$HOME/.local/lib/velune"
mkdir -p "$install_dir" "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/scalable/apps"
install -m755 "$build_dir/velune" "$install_dir/velune.next"
mv -f -- "$install_dir/velune.next" "$install_dir/velune"
mkdir -p "$install_dir/notices"
install -m644 "$script_dir/../../LICENSE" "$install_dir/notices/Velune-GPL-3.0.txt"
if [[ -f "$script_dir/../THIRD_PARTY.md" ]]; then
 install -m644 "$script_dir/../THIRD_PARTY.md" "$install_dir/notices/THIRD_PARTY.md"
fi
if [[ -n "$mpv_runtime_prefix" ]]; then
 mkdir -p "$install_dir/lib"
 find "$mpv_runtime_prefix/usr/lib/x86_64-linux-gnu" -maxdepth 1 -name 'lib*.so*' -exec cp -a -t "$install_dir/lib" {} +
 if [[ -d "$mpv_runtime_prefix/usr/share/doc" ]]; then
  while IFS= read -r -d '' notice; do
   package=$(basename "$(dirname "$notice")")
   install -m644 "$notice" "$install_dir/notices/$package.copyright"
  done < <(find "$mpv_runtime_prefix/usr/share/doc" -name copyright -type f -print0)
 fi
fi
if [[ -d "$build_dir/tools" ]]; then
 mkdir -p "$install_dir/tools"
 cp -a "$build_dir/tools/." "$install_dir/tools/"
fi
install -m644 "$script_dir/../assets/velune.svg" "$HOME/.local/share/icons/hicolor/scalable/apps/velune.svg"
if [[ -n "$runtime_prefix" ]]; then
 mkdir -p "$install_dir/lib" "$install_dir/plugins"
 find "$runtime_prefix/usr/lib/x86_64-linux-gnu" -maxdepth 1 -name 'lib*.so*' -exec cp -a -t "$install_dir/lib" {} +
 cp -a "$runtime_prefix/usr/lib/x86_64-linux-gnu/qt6/plugins/." "$install_dir/plugins/"
 if [[ -d "$runtime_prefix/usr/share/doc" ]]; then
  while IFS= read -r -d '' notice; do
   package=$(basename "$(dirname "$notice")")
   install -m644 "$notice" "$install_dir/notices/$package.copyright"
  done < <(find "$runtime_prefix/usr/share/doc" -name copyright -type f -print0)
 fi
 if [[ -f "$runtime_prefix/usr/lib/qt6/libexec/QtWebEngineProcess" ]]; then
  install -m755 "$runtime_prefix/usr/lib/qt6/libexec/QtWebEngineProcess" "$install_dir/QtWebEngineProcess"
  mkdir -p "$install_dir/resources" "$install_dir/translations"
  cp -a "$runtime_prefix/usr/share/qt6/resources/." "$install_dir/resources/"
  cp -a "$runtime_prefix/usr/share/qt6/translations/." "$install_dir/translations/"
 fi
 if [[ -d "$runtime_prefix/usr/share/fonts/opentype/inter" ]]; then
  mkdir -p "$HOME/.local/share/fonts/velune-inter"
  cp "$runtime_prefix/usr/share/fonts/opentype/inter/"*.otf "$HOME/.local/share/fonts/velune-inter/"
  fc-cache -f "$HOME/.local/share/fonts/velune-inter" >/dev/null
 fi
fi
cat > "$HOME/.local/bin/velune" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
velune_dir="$HOME/.local/lib/velune"
if [[ -d "$velune_dir/lib" ]]; then
 export LD_LIBRARY_PATH="$velune_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
 export QT_PLUGIN_PATH="$velune_dir/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
fi
if [[ -x "$velune_dir/QtWebEngineProcess" ]]; then
 export QTWEBENGINEPROCESS_PATH="$velune_dir/QtWebEngineProcess"
 export QTWEBENGINE_RESOURCES_PATH="$velune_dir/resources"
 export QTWEBENGINE_LOCALES_PATH="$velune_dir/translations/qtwebengine_locales"
fi
exec "$velune_dir/velune" "$@"
LAUNCHER
chmod +x "$HOME/.local/bin/velune"
cat > "$HOME/.local/share/applications/velune.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Velune
Comment=Desktop music player
Exec="$HOME/.local/bin/velune"
Icon=velune
Terminal=false
Categories=AudioVideo;Audio;Player;
StartupWMClass=Velune
DESKTOP
cat > "$HOME/.local/share/applications/velune-preview.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Velune — UI Preview
Comment=Review the desktop layout without a Google session
Exec="$HOME/.local/bin/velune" --preview --data-dir "$HOME/.local/share/VelunePreview"
Icon=velune
Terminal=false
Categories=AudioVideo;Audio;Player;
StartupWMClass=Velune
DESKTOP
if command -v update-desktop-database >/dev/null; then update-desktop-database "$HOME/.local/share/applications"; fi
printf 'Installed %s\n' "$HOME/.local/bin/velune"
