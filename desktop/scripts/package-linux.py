#!/usr/bin/env python3
"""Stage a Linux Qt Quick build without launching any Velune process."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, default=ROOT/'build-qml')
parser.add_argument('--output', type=Path, default=ROOT/'dist/linux-qml')
parser.add_argument('--qml', type=Path, default=Path(os.environ.get('VELUNE_QML_DIR','/usr/lib/x86_64-linux-gnu/qt6/qml')))
parser.add_argument('--audio-runtime', type=Path, default=Path.home()/'.local/lib/velune/resources/native/lib')
parser.add_argument('--tools', type=Path, default=Path.home()/'.local/lib/velune/resources/tools')
parser.add_argument('--ffmpeg', type=Path, default=Path(shutil.which('ffmpeg') or '/usr/bin/ffmpeg'))
args = parser.parse_args()
if not (args.qml/'QtQuick/qmldir').exists():
    parser.error('Install the Qt 6.4 QML runtime modules or pass --qml with their extracted directory.')
if not (args.audio_runtime/'libmpv.so.2').exists():
    parser.error('Pass --audio-runtime with the audited libmpv runtime directory.')
if not all((args.tools/name).is_file() for name in ('yt-dlp','deno','versions.json')):
    parser.error('Pass --tools with the verified playback tools and versions.json.')
if not args.ffmpeg.is_file():
    parser.error('Install ffmpeg or pass --ffmpeg with an audited binary for MP3 downloads.')
# Never silently reuse stale package content.
if args.output.exists():
    args.output.rename(args.output.with_name(args.output.name+'-previous-'+str(time.time_ns())))
args.output.mkdir(parents=True)
out=args.output.resolve()
def copy(src, dst):
    dst.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(src.resolve(),dst)
for name in ('velune','velune-login'):
    copy(args.build/name,out/name)
copy(args.build/'native/velune-audio',out/'native/velune-audio')
shutil.copytree(args.audio_runtime,out/'native/lib')
shutil.copytree(args.tools,out/'tools')
copy(args.ffmpeg,out/'tools/ffmpeg')
shutil.copytree(args.qml,out/'qml',symlinks=False)
plugin_root=Path('/usr/lib/x86_64-linux-gnu/qt6/plugins')
for name in ('platforms','platformthemes','xcbglintegrations','egldeviceintegrations','imageformats','tls','networkinformation','generic','platforminputcontexts','wayland-shell-integration','wayland-decoration-client','wayland-graphics-integration-client'):
    if (plugin_root/name).is_dir():shutil.copytree(plugin_root/name,out/'plugins'/name)
# Main UI never uses WebEngine. These files are consumed only by velune-login.
copy(Path('/usr/lib/qt6/libexec/QtWebEngineProcess'),out/'libexec/QtWebEngineProcess')
shutil.copytree('/usr/share/qt6/resources',out/'resources')
shutil.copytree('/usr/share/qt6/translations/qtwebengine_locales',out/'translations/qtwebengine_locales')
(out/'qt.conf').write_text('[Paths]\nPrefix=.\nLibraries=lib\nPlugins=plugins\nQml2Imports=qml\nLibraryExecutables=libexec\nData=.\nTranslations=translations\n')
copy(ROOT/'assets/velune.svg',out/'assets/velune.svg')
fonts=Path.home()/'.local/share/fonts/velune-inter'
if fonts.is_dir():shutil.copytree(fonts,out/'fonts')
# Stage the dependency closure; libc/loader and GPU drivers remain system-owned.
lib=out/'lib';lib.mkdir()
# Keep an explicitly supplied FFmpeg runtime directory available while auditing
# and staging its dependency closure (useful for portable/audited binaries).
extra_runtime = os.environ.get('VELUNE_FFMPEG_LIBRARY_PATH', '')
runtime_path = os.pathsep.join(filter(None, [str(lib), extra_runtime, os.environ.get('LD_LIBRARY_PATH', '')]))
env={**os.environ,'LD_LIBRARY_PATH':runtime_path}
base_re=re.compile(r'^(?:linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so)')
qt_forbidden=re.compile(r'Qt6(?:Widgets|Multimedia|QuickWidgets)|libnode|electron',re.I)
def dependencies(binary, environment=env):
    result=subprocess.run(['ldd',str(binary)],env=environment,capture_output=True,text=True,check=True)
    if 'not found' in result.stdout:raise SystemExit('Missing runtime dependency: '+str(binary)+'\n'+result.stdout)
    deps=[]
    for line in result.stdout.splitlines():
        match=re.match(r'\s*(\S+)\s+=>\s+(/\S+)',line)
        if match:deps.append((match[1],Path(match[2])))
    return deps
binaries=[out/'velune',out/'velune-login',out/'libexec/QtWebEngineProcess',out/'tools/ffmpeg']
binaries+=list((out/'qml').rglob('*.so'))+list((out/'plugins').rglob('*.so'))
for binary in binaries:
    for name,source in dependencies(binary):
        if qt_forbidden.search(name):raise SystemExit('Unexpected dependency in '+str(binary)+': '+name)
        if not base_re.match(name) and not (lib/name).exists():copy(source,lib/name)
# Verify all staged ELF dependency closures with their own environment, without starting the app.
for binary in binaries:
    dependencies(binary)
main_deps=[name for name,_ in dependencies(out/'velune')]
if any('WebEngine' in name or qt_forbidden.search(name) for name in main_deps):
    raise SystemExit('Browser/widget dependency leaked into the main executable.')
audio_env={**os.environ,'LD_LIBRARY_PATH':str(out/'native/lib')}
for binary in [out/'native/velune-audio',*(out/'native/lib').glob('*.so*')]:
    if any('Qt' in name for name,_ in dependencies(binary,audio_env)):
        raise SystemExit('Qt dependency leaked into native audio.')
notices=out/'notices';notices.mkdir()
previous=Path.home()/'.local/lib/velune-qt-backup/notices'
if previous.exists():shutil.copytree(previous,notices,dirs_exist_ok=True)
for directory in [Path('/usr/share/doc'),Path('/tmp/velune-qml-deps/root/usr/share/doc')]:
    for doc in directory.glob('*qt*'):
        if (doc/'copyright').is_file():copy(doc/'copyright',notices/(doc.name+'.copyright'))
copy(ROOT.parent/'LICENSE',notices/'Velune-GPL-3.0.txt')
copy(ROOT/'THIRD_PARTY.md',notices/'THIRD_PARTY.md')
if Path('/usr/share/doc/ffmpeg/copyright').is_file():
    copy(Path('/usr/share/doc/ffmpeg/copyright'),notices/'ffmpeg.copyright')
manifest={str(f.relative_to(out)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(out.rglob('*')) if f.is_file()}
(out/'runtime-manifest.json').write_text(json.dumps({'version':'0.3.0','sha256':manifest,'mainDependencies':main_deps},indent=2)+'\n')
print('Packaged Qt Quick Velune:',out)
print('Dependency checks passed. No application or audio process was started.')
