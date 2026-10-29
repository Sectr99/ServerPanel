"""Fetch the sample data the PC preview renders with, into .pio/preview/ (git-ignored: it shows your printer and place).

Usage: python tools/preview_data.py [--no-camera]
- radar/: Esri dark gray map tiles (base .jpg, labels _l.png) and a NOAA radar frame per zoom, for RADAR_LAT/LON in
  src/config.h, using the same Web Mercator maths as uiRadarOrigin/uiRadarBBox in ui.cpp.
- camera.jpg: one frame from the printer's camera (PRINTER_IP/PRINTER_CODE from config.h; the code is never printed).
- spotify.jpg: an album cover from Spotify's image CDN (640 px, like the app fetches), for the Spotify screen.
Without these files the preview still runs; the radar and camera screens show their "loading" states, and the Spotify
screen a placeholder instead of album art.
"""
import math
import re
import socket
import ssl
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / ".pio" / "preview"
W, H, ZOOMS = 1024, 536, (10, 8, 6)  # RADAR_W, RADAR_H, RADAR_ZOOMS in ui.h
UA = {"User-Agent": "ServerPanel/1.0 (preview)"}
ESRI = "https://services.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_%s/MapServer/tile/%d/%d/%d"
NOAA = ("https://opengeo.ncep.noaa.gov/geoserver/conus/conus_bref_qcd/ows?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap"
        "&LAYERS=conus_bref_qcd&STYLES=&SRS=EPSG:3857&BBOX=%.1f,%.1f,%.1f,%.1f&WIDTH=%d&HEIGHT=%d"
        "&FORMAT=image/png&TRANSPARENT=true")


def config(name):
    m = re.search(r"#define\s+%s\s+\"?([^\"\s/]+)" % name, (ROOT / "src" / "config.h").read_text(encoding="utf-8"))
    return m.group(1).rstrip("f") if m else ""


def get(url, path):
    path.write_bytes(urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=20).read())


def radar(lat, lon):
    d = OUT / "radar"
    d.mkdir(parents=True, exist_ok=True)
    for z in ZOOMS:
        n = 1 << z
        world, s = 256.0 * n, math.sin(math.radians(lat))
        left = math.floor((lon + 180) / 360 * world - W / 2)
        top = math.floor((0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * world - H / 2)
        for ty in range(math.floor(top / 256), math.ceil((top + H) / 256)):
            for tx in range(math.floor(left / 256), math.ceil((left + W) / 256)):
                get(ESRI % ("Base", z, ty, tx % n), d / f"{z}_{tx % n}_{ty}.jpg")
                get(ESRI % ("Reference", z, ty, tx % n), d / f"{z}_{tx % n}_{ty}_l.png")
        o = math.pi * 6378137.0
        res = 2 * o / world
        bbox = (left * res - o, o - (top + H) * res, (left + W) * res - o, o - top * res)
        get(NOAA % (bbox + (W, H)), d / f"radar_{z}.png")
        print(f"radar: zoom {z} done")


def camera(ip, code):
    ctx = ssl.create_default_context()
    ctx.check_hostname, ctx.verify_mode = False, ssl.CERT_NONE
    s = ctx.wrap_socket(socket.create_connection((ip, 6000), timeout=10))
    login = bytearray(80)
    login[0], login[5] = 0x40, 0x30
    login[16:20], login[48:48 + len(code)] = b"bblp", code.encode()
    s.sendall(login)

    def read(n):
        b = b""
        while len(b) < n:
            c = s.recv(n - len(b))
            if not c:
                raise EOFError("camera closed the connection")
            b += c
        return b

    hdr = read(16)
    (OUT / "camera.jpg").write_bytes(read(hdr[0] | hdr[1] << 8 | hdr[2] << 16))
    print("camera: frame saved")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    radar(float(config("RADAR_LAT")), float(config("RADAR_LON")))
    get("http://i.scdn.co/image/ab67616d0000b273e8b066f70c206551210d902b", OUT / "spotify.jpg")
    print("spotify: album art saved")
    if "--no-camera" not in sys.argv and config("PRINTER_IP"):
        camera(config("PRINTER_IP"), config("PRINTER_CODE"))
