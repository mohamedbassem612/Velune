#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
package_dir=${1:-"$script_dir/../dist/linux-qml"}
install_dir="$HOME/.local/lib/velune"
if [[ ! -x "$package_dir/velune" || ! -x "$package_dir/velune-login" || ! -x "$package_dir/native/velune-audio" || ! -f "$package_dir/runtime-manifest.json" ]]; then
 echo 'Build and package the Qt Quick application first.' >&2; exit 1
fi
mkdir -p "$HOME/.local/lib" "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/scalable/apps"
staging=$(mktemp -d "$HOME/.local/lib/velune-next.XXXXXX")
cp -a -- "$package_dir/." "$staging/"
if [[ -d "$install_dir" ]]; then
 backup="$HOME/.local/lib/velune-electron-0.2.0-backup"
 if [[ -e "$backup" ]]; then backup="$HOME/.local/lib/velune-previous-$(date +%Y%m%d-%H%M%S)"; fi
 mv -- "$install_dir" "$backup"
fi
mv -- "$staging" "$install_dir"
cat > "$HOME/.local/bin/velune" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
velune_dir="$HOME/.local/lib/velune"
export LD_LIBRARY_PATH="$velune_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$velune_dir/plugins"
export QML2_IMPORT_PATH="$velune_dir/qml"
export QTWEBENGINEPROCESS_PATH="$velune_dir/libexec/QtWebEngineProcess"
export QTWEBENGINE_RESOURCES_PATH="$velune_dir/resources"
export QTWEBENGINE_LOCALES_PATH="$velune_dir/translations/qtwebengine_locales"
exec "$velune_dir/velune" "$@"
LAUNCHER
chmod +x "$HOME/.local/bin/velune"
install -m644 "$package_dir/assets/velune.svg" "$HOME/.local/share/icons/hicolor/scalable/apps/velune.svg"
cat > "$HOME/.local/share/applications/velune.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Velune
Comment=Music player with Velune Connect
Exec="$HOME/.local/bin/velune"
Icon=velune
Terminal=false
Categories=AudioVideo;Audio;Player;
StartupWMClass=Velune
DESKTOP
cat > "$HOME/.local/share/applications/velune-preview.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Velune — Separate library
Comment=Open Velune with a separate library
Exec="$HOME/.local/bin/velune" --preview
Icon=velune
Terminal=false
Categories=AudioVideo;Audio;Player;
StartupWMClass=Velune
DESKTOP
if command -v update-desktop-database >/dev/null; then update-desktop-database "$HOME/.local/share/applications"; fi
echo "Installed Velune 0.3.0 (Qt Quick) at $install_dir. The application has not been launched."
