#!/usr/bin/env python3
"""Install pinned official resolver binaries; validate SHA-256 before executing."""
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import sys
import urllib.request
import zipfile

# Official release digests, pinned for reproducible builds. Update together.
ASSETS = (
    ('yt-dlp', 'https://github.com/yt-dlp/yt-dlp/releases/download/2026.10.26/yt-dlp_linux',
     '767ba9dce7052aa196f8b12652f433d76de128ba168966fa84b6394d5e6c6795', False),
    ('deno', 'https://github.com/denoland/deno/releases/download/v2.9.8/deno-x86_64-unknown-linux-gnu.zip',
     'e890975bc2549807bbcc0b584ffafda72cc7c93349dd99c80483dd784f6c4609', True),
)

def main():
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'amd64'):
        raise SystemExit('This installer targets Linux x86_64. Package matching official yt-dlp and Deno binaries for other platforms.')
    target = Path(sys.argv[1] if len(sys.argv) > 1 else Path.home() / '.local/lib/velune/tools')
    target.mkdir(parents=True, exist_ok=True)
    versions = {}
    for name, url, digest, zipped in ASSETS:
        print(f'Installing official {name} release...', flush=True)
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read(150 * 1024 * 1024)
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f'{name}: release checksum did not match; nothing installed.')
        if zipped:
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                data = archive.read(name)
        output = target / (name + '.next')
        try:
            with output.open('wb') as stream:
                stream.write(data)
            output.chmod(0o755)
            os.replace(output, target / name)
        finally:
            output.unlink(missing_ok=True)
        versions[name] = {'source': url, 'release_sha256': digest}
    (target / 'versions.json').write_text(json.dumps(versions, indent=2) + '\n')
    print('Playback tools installed.')

if __name__ == '__main__':
    main()
