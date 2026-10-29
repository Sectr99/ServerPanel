"""Copy files onto a panel's microSD card over Wi-Fi (the /sd endpoint in main.cpp), then reboot it.

    python tools/sd_upload.py <panel ip> [folder]   # default folder: sd (from mkfonts.py sd)

The folder's contents go to the card's root (sd/fonts/body.vlw -> /fonts/body.vlw). Files already on the card at the
same size are skipped. The password is the OTA one from ota.ini. Each file goes in 1 MB pieces: the panel writes a
piece to the card while this waits for its reply, so the card is never written while Wi-Fi traffic streams in.
"""
import configparser
import pathlib
import sys

import requests

PIECE = 1 << 20  # SD_PIECE in main.cpp

root = pathlib.Path(__file__).resolve().parent.parent
ip = sys.argv[1]
src = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else root / 'sd')
ini = configparser.ConfigParser(inline_comment_prefixes=';')
ini.read(root / 'ota.ini')
web = requests.Session()
web.auth = requests.auth.HTTPDigestAuth('panel', ini['ota']['password'])
base = f'http://{ip}'


def check(r):
    if r.status_code == 401:
        sys.exit('wrong password: the panel takes the one in ota.ini it was built with')
    if not r.ok:
        sys.exit(f'{r.status_code}: {r.text.strip()}')
    return r


def listing(folder):
    r = web.get(f'{base}/sd', params={'path': folder}, timeout=10)
    if r.status_code == 404:
        return {}
    check(r)
    return {n: int(s) for n, s in (line.split('\t') for line in r.text.splitlines())}


if not src.is_dir():
    sys.exit(f'no folder {src} (python tools/mkfonts.py sd makes sd/)')
try:
    listing('/')
except requests.ConnectionError:
    sys.exit(f'no answer from {ip}: is the panel on and on Wi-Fi?')
sent = 0
for f in sorted(p for p in src.rglob('*') if p.is_file()):
    path = '/' + f.relative_to(src).as_posix()
    folder, name = path.rsplit('/', 1)
    data = f.read_bytes()
    if listing(folder or '/').get(name) == len(data):
        print(f'{path}: already there')
        continue
    print(f'{path}: {len(data) >> 10} KB ...', end=' ', flush=True)
    for offset in range(0, max(len(data), 1), PIECE):  # an empty file still needs its one (empty) piece
        r = web.post(f'{base}/sd', params={'offset': offset, 'total': len(data)},
                     files={'file': (path, data[offset:offset + PIECE])}, timeout=60)
        check(r)
    print('ok')
    sent += 1
if sent:
    print(check(web.post(f'{base}/reboot', timeout=10)).text.strip())
