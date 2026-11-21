#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 1 ]]; then
 printf 'Usage: %s /path/to/Google-Desktop-client.json\n' "$0" >&2
 exit 2
fi
python3 - "$1" <<'PY'
import json, os, pathlib, re, sys, tempfile
source = pathlib.Path(sys.argv[1])
try:
    document = json.loads(source.read_text())
    client = document['installed']
    if not re.fullmatch(r'[A-Za-z0-9_-]+\.apps\.googleusercontent\.com', client['client_id']):
        raise ValueError()
except (OSError, ValueError, KeyError, TypeError):
    sys.exit('Please choose the JSON downloaded for a Google OAuth Desktop app client.')
directory = pathlib.Path(os.environ.get('XDG_CONFIG_HOME', str(pathlib.Path.home()/'.config'))) / 'Velune' / 'Velune'
directory.mkdir(parents=True, exist_ok=True)
fd, name = tempfile.mkstemp(prefix='.google-oauth-', dir=directory)
try:
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, 'w') as file:
        json.dump({'installed': {'client_id': client['client_id'], 'client_secret': client.get('client_secret', '')}}, file)
    os.replace(name, directory/'google-oauth.json')
finally:
    if os.path.exists(name):
        os.unlink(name)
print('Google registration configured for Velune. Restart Velune, then choose Continue with Google.')
PY
