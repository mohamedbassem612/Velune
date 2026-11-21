#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir=${1:-"$script_dir/../build"}
runtime_prefix=${2:-}
install_dir="$HOME/.local/lib/velune"
mkdir -p "$install_dir" "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/scalable/apps"
install -m755 "$build_dir/velune" "$install_dir/velune.next"
mv -f -- "$install_dir/velune.next" "$install_dir/velune"
install -m644 "$script_dir/../assets/velune.svg" "$HOME/.local/share/icons/hicolor/scalable/apps/velune.svg"
if [[ -n "$runtime_prefix" ]]; then
 mkdir -p "$install_dir/lib" "$install_dir/plugins"
 find "$runtime_prefix/usr/lib/x86_64-linux-gnu" -maxdepth 1 -name 'lib*.so*' -exec cp -a -t "$install_dir/lib" {} +
 cp -a "$runtime_prefix/usr/lib/x86_64-linux-gnu/qt6/plugins/." "$install_dir/plugins/"
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
 export QT_MEDIA_BACKEND=ffmpeg
fi
exec "$velune_dir/velune" "$@"
LAUNCHER
chmod +x "$HOME/.local/bin/velune"
if [[ -f "$script_dir/../config/google-oauth.json" ]]; then
 "$script_dir/configure-google-login.sh" "$script_dir/../config/google-oauth.json"
fi
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
