"""One-time Spotify login for the panel's Spotify app: gets a refresh token and saves it, with your app's client ID
and secret, to SPOTIFY_* in src/config.h. Python 3, no packages needed.

1. developer.spotify.com/dashboard > Create app: any name and description, Redirect URI
   http://127.0.0.1:8888/callback, and tick Web API. (Its owner needs Spotify Premium.)
2. From the app's settings, copy the Client ID and Client secret, then run:
       python tools/spotify_auth.py
3. Log in and agree in the browser tab that opens. Then build and flash.
"""
import base64
import getpass
import http.server
import json
import pathlib
import re
import secrets
import urllib.parse
import urllib.request
import webbrowser

REDIRECT = "http://127.0.0.1:8888/callback"
SCOPES = ("user-read-playback-state user-modify-playback-state user-library-read user-library-modify "
          "user-read-recently-played playlist-read-private playlist-read-collaborative")
CONFIG = pathlib.Path(__file__).resolve().parent.parent / "src" / "config.h"


def main():
    print(__doc__)
    cid = input("Client ID: ").strip()
    secret = getpass.getpass("Client secret (not shown): ").strip()
    state = secrets.token_urlsafe(16)
    url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode(
        {"client_id": cid, "response_type": "code", "redirect_uri": REDIRECT, "scope": SCOPES, "state": state})
    result = {}

    class Callback(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            if q.get("state") == [state]:  # anything else (a favicon request) isn't Spotify's answer
                result.update({k: v[0] for k, v in q.items()})
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Done: close this tab." if "code" in result else b"Login failed: see the terminal.")

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(("127.0.0.1", 8888), Callback)
    print("Opening the Spotify login. If no browser opens, visit:\n" + url)
    webbrowser.open(url)
    while not result:
        server.handle_request()
    if "code" not in result:
        raise SystemExit("Spotify said: " + result.get("error", "no code"))

    req = urllib.request.Request(
        "https://accounts.spotify.com/api/token",
        data=urllib.parse.urlencode(
            {"grant_type": "authorization_code", "code": result["code"], "redirect_uri": REDIRECT}).encode(),
        headers={"Authorization": "Basic " + base64.b64encode(f"{cid}:{secret}".encode()).decode()})
    refresh = json.load(urllib.request.urlopen(req))["refresh_token"]

    text = CONFIG.read_text(encoding="utf-8")
    for name, value in (("SPOTIFY_CLIENT_ID", cid), ("SPOTIFY_CLIENT_SECRET", secret), ("SPOTIFY_REFRESH", refresh)):
        text, n = re.subn(rf'(#define {name}\s+)"[^"]*"', lambda m: f'{m.group(1)}"{value}"', text)
        if n != 1:
            raise SystemExit(f"{name} not found in {CONFIG}: copy the Spotify block from src/config.example.h first")
    CONFIG.write_text(text, encoding="utf-8", newline="\n")
    print(f"Saved to {CONFIG}. Now build and flash.")


if __name__ == "__main__":
    main()
