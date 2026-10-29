"""Convert TrueType fonts into anti-aliased VLW fonts (LovyanGFX "smooth fonts"), one per UI role.

Usage:  python tools/mkfonts.py [family ...]      (default: all families below, then the icons)
        python tools/mkfonts.py icons              (just the icons)
        python tools/mkfonts.py sd                 (Japanese and accents for the microSD card, in sd/fonts/)
Writes src/fonts/<family>.h, a header of byte arrays vlw_<role>[] that ui.cpp loads when config.h has
#define FONT_HEADER "fonts/<family>.h". Source fonts live in tools/fonts/ (all SIL Open Font License, from Google Fonts).
Icons: src/fonts/icons.h, one small VLW font per icon size, from Google's Material Symbols Rounded (Apache 2.0),
downloaded into tools/fonts/ on first use (15 MB, so git-ignored), plus an ICON_<NAME> UTF-8 string per icon.

VLW layout (big-endian int32): header {glyph count, version 11, size, 0, ascent, descent}, then per glyph
{code point, height, width, x advance, top offset (baseline to top), left offset, 0}, then 8-bit alpha bitmaps
in the same order. Glyphs must be sorted by code point (LovyanGFX binary-searches them).
"""
import os
import struct
import sys
import urllib.request

import freetype

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, "fonts")
CHARS = list(range(0x20, 0x7F)) + [0xB0]  # printable ASCII + degree sign

# Roles mirror the bitmap fonts they replace (ui.cpp): size = em height in pixels.
ROLES = {                 # (style, px[, characters: default all of CHARS])
    "small": ("regular", 14),   # was DejaVu12: axis and table labels
    "body": ("regular", 18),    # was FreeSans9pt
    "bodyb": ("bold", 18),      # was FreeSansBold9pt: section titles
    "stat": ("regular", 24),    # was FreeSans12pt: stat values
    "title": ("bold", 25),      # was FreeSansBold12pt: page titles, buttons
    "big": ("bold", 36),        # was FreeSansBold18pt: big values
    "huge": ("bold", 50),       # was FreeSansBold24pt: gauge numbers
    "clock": ("light", 32),     # was Orbitron Light 24
    "xl": ("light", 112, "0123456789:%.-° "),  # launcher clock, detail view values: digits only
}

# family -> style -> (file, variable-font weight or None)
FAMILIES = {
    "inter": {"regular": ("Inter-VF.ttf", 400), "bold": ("Inter-VF.ttf", 650), "light": ("Inter-VF.ttf", 300)},
    "montserrat": {"regular": ("Montserrat-VF.ttf", 400), "bold": ("Montserrat-VF.ttf", 650), "light": ("Montserrat-VF.ttf", 300)},
    "barlow": {"regular": ("Barlow-Regular.ttf", None), "bold": ("Barlow-SemiBold.ttf", None), "light": ("Barlow-Light.ttf", None)},
    "rajdhani": {"regular": ("Rajdhani-Medium.ttf", None), "bold": ("Rajdhani-Bold.ttf", None), "light": ("Rajdhani-Regular.ttf", None)},
    "exo2": {"regular": ("Exo2-VF.ttf", 400), "bold": ("Exo2-VF.ttf", 650), "light": ("Exo2-VF.ttf", 300)},
}


def load_face(fname, weight, px, fill=None):
    face = freetype.Face(os.path.join(FONTS, fname))
    if weight is not None:  # variable font: set the weight axis (and optical size, if present, to match px)
        info = face.get_variation_info()
        coords = []
        for axis in info.axes:
            tag = axis.tag
            if tag == "wght":
                coords.append(weight)
            elif tag == "opsz":
                coords.append(min(max(px, axis.minimum), axis.maximum))
            elif tag == "FILL" and fill is not None:
                coords.append(fill)
            else:
                coords.append(axis.default)
        face.set_var_design_coords(coords)
    face.set_pixel_sizes(0, px)
    return face


def vlw(fname, weight, px, glyphs=None):
    """glyphs: [(code point stored, code point in the font, FILL axis value)]; default: CHARS as they are."""
    glyphs = sorted(glyphs or [(c, c, None) for c in CHARS])
    faces = {}
    metrics, bitmaps = [], []
    for code, src, fill in glyphs:
        face = faces.get(fill) or faces.setdefault(fill, load_face(fname, weight, px, fill))
        face.load_char(chr(src), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
        g = face.glyph
        bm = g.bitmap
        w, h, pitch = bm.width, bm.rows, bm.pitch
        raw = bytes(bm.buffer)
        bitmaps.append(b"".join(raw[r * pitch:r * pitch + w] for r in range(h)))
        adv = int(round(g.advance.x / 64))
        if w > 255 or adv > 255:
            sys.exit(f"{fname} {px}px: glyph {code:#x} too wide for VLW (8-bit widths)")
        metrics.append(struct.pack(">7i", code, h, w, adv, g.bitmap_top, g.bitmap_left, 0))
    ascent = int(round(face.size.ascender / 64))
    descent = int(round(-face.size.descender / 64))
    header = struct.pack(">6i", len(glyphs), 11, px, 0, ascent, descent)
    return header + b"".join(metrics) + b"".join(bitmaps)


def write_header(family):
    styles = FAMILIES[family]
    out_dir = os.path.join(os.path.dirname(HERE), "src", "fonts")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, family + ".h")
    total = 0
    with open(path, "w") as f:
        f.write(f"// Generated by tools/mkfonts.py: {family}, anti-aliased VLW fonts. Do not edit.\n#pragma once\n#include <stdint.h>\n")
        for role, (style, px, *chars) in ROLES.items():
            fname, weight = styles[style]
            data = vlw(fname, weight, px, [(ord(c), ord(c), None) for c in chars[0]] if chars else None)
            total += len(data)
            f.write(f"\n// {role}: {fname} {'wght ' + str(weight) if weight else ''} {px}px, {len(data)} bytes\n")
            f.write(f"static const uint8_t vlw_{role}[] = {{\n")
            for i in range(0, len(data), 24):
                f.write("  " + ",".join(str(b) for b in data[i:i + 24]) + ",\n")
            f.write("};\n")
    print(f"{path}: {total // 1024} KB")


# Icons: {role: (px, [(icon name, filled)])}. Names and code points: tools/fonts/MaterialSymbolsRounded.codepoints.
ICON_FONT = "MaterialSymbolsRounded"
ICON_URL = ("https://github.com/google/material-design-icons/raw/master/variablefont/"
            "MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D")
ICON_WEIGHT = 500
ICONS = {
    "icon_xs": (18, [("bedtime", True)]),  # pills
    # status bar, tab strip, card headers, settings rows, chips, forecast rows
    "icon_s": (28, [("settings", True), ("arrow_back_ios_new", False), ("add", False), ("remove", False),
                    ("expand_more", False), ("expand_less", False), ("chevron_right", False), ("close", False),
                    ("desktop_windows", True), ("dns", True), ("hard_drive", True), ("deployed_code", True), ("print", True),
                    ("radar", False), ("keyboard", True), ("grid_view", True), ("music_note", False),
                    ("developer_board", True), ("videogame_asset", True), ("swap_vert", True), ("memory_alt", True),
                    ("mode_fan", True), ("whatshot", True), ("heat", True), ("open_in_full", False),
                    ("light_mode", True), ("bedtime", True), ("autorenew", True), ("home", True), ("show_chart", True),
                    ("schedule", True), ("dark_mode", True), ("nights_stay", True), ("brightness_low", True),
                    ("widgets", True), ("wifi", False), ("lan", False), ("timer", False), ("memory", False),
                    ("restart_alt", False), ("location_on", True), ("warning", True), ("shield", True),
                    ("sunny", True), ("clear_night", True), ("partly_cloudy_day", True), ("partly_cloudy_night", True),
                    ("cloud", True), ("rainy", True), ("thunderstorm", True), ("weather_snowy", True), ("foggy", True),
                    ("computer", True), ("smartphone", True), ("speaker", True), ("tv", True), ("devices", True),
                    ("library_music", True), ("speaker_group", True)]),
    # Spotify controls, macro keys, launcher tiles, weather now, keypad
    "icon_m": (40, [("favorite", False), ("favorite", True), ("shuffle", False), ("repeat", False),
                    ("repeat_one", False), ("skip_previous", True), ("skip_next", True), ("play_arrow", True),
                    ("pause", True), ("play_pause", True), ("volume_up", True), ("volume_down", True),
                    ("volume_off", True), ("close", False), ("dns", True), ("radar", False), ("keyboard", True),
                    ("grid_view", True), ("music_note", False), ("mic_off", True), ("screenshot_region", False),
                    ("monitoring", False), ("desktop_windows", True), ("lock", True), ("keyboard_command_key", False),
                    ("backspace", True), ("check", False), ("library_music", True), ("speaker_group", True), ("computer", True), ("smartphone", True),
                    ("speaker", True), ("tv", True), ("devices", True),
                    ("sunny", True), ("clear_night", True), ("partly_cloudy_day", True), ("partly_cloudy_night", True),
                    ("cloud", True), ("rainy", True), ("thunderstorm", True), ("weather_snowy", True), ("foggy", True)]),
    # placeholders (album art, camera) and the alert sheet
    "icon_l": (80, [("music_note", False), ("videocam", True), ("warning", True)]),
}


def write_icons():
    for ext in (".ttf", ".codepoints"):
        path = os.path.join(FONTS, ICON_FONT + ext)
        if not os.path.exists(path):
            print(f"downloading {ICON_FONT}{ext} ...")
            urllib.request.urlretrieve(ICON_URL + ext, path)
    codes = dict(line.split() for line in open(os.path.join(FONTS, ICON_FONT + ".codepoints")))
    path = os.path.join(os.path.dirname(HERE), "src", "fonts", "icons.h")
    total = 0
    with open(path, "w") as f:
        f.write("// Generated by tools/mkfonts.py: Material Symbols Rounded icons (Apache 2.0), anti-aliased VLW fonts, one\n"
                "// per size. Draw an icon as text: ICON_<NAME> in its size's font. Do not edit.\n#pragma once\n#include <stdint.h>\n")
        spare, filled_code, defined = 0xF8F0, {}, set()  # a filled variant gets a spare code point, the same in every size
        for role, (px, icons) in ICONS.items():
            glyphs = []
            for name, filled in icons:
                code = int(codes[name], 16)
                if filled:
                    if name not in filled_code:
                        filled_code[name], spare = spare, spare + 1
                    code = filled_code[name]
                glyphs.append((code, int(codes[name], 16), 1 if filled else 0))
                macro = "ICON_" + name.upper() + ("_FILL" if filled else "")
                if macro not in defined:
                    defined.add(macro)
                    utf8 = "".join(f"\\x{b:02x}" for b in chr(code).encode())
                    f.write(f'#define {macro} "{utf8}"\n')
            if len({g[0] for g in glyphs}) != len(glyphs):
                sys.exit(f"{role}: two icons share a code point")
            data = vlw(ICON_FONT + ".ttf", ICON_WEIGHT, px, glyphs)
            total += len(data)
            f.write(f"\n// {role}: {px}px, {len(data)} bytes\nstatic const uint8_t vlw_{role}[] = {{\n")
            for i in range(0, len(data), 24):
                f.write("  " + ",".join(str(b) for b in data[i:i + 24]) + ",\n")
            f.write("};\n")
    print(f"{path}: {total // 1024} KB")


# ---------- fonts for the microSD card: what Inter lacks ----------
# One VLW file per text role in sd/fonts/<role>.vlw: accented Latin and typographic punctuation from Inter, then
# Japanese from Noto Sans JP (SIL OFL, Google Fonts; downloaded on first use, git-ignored): JIS X 0208 symbols, kana
# and all 6,355 level 1 and 2 kanji, plus half-width katakana. ui.cpp draws a role's text from Inter in flash and falls
# back to these for anything else (uiLoadExtraFonts). Copy sd/ to the card's root.
JP_FONT = "NotoSansJP.ttf"
JP_URL = "https://github.com/google/fonts/raw/main/ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf"
SD_ROLES = ["small", "body", "bodyb", "stat", "title", "big", "huge"]


def jis_chars():
    out = []
    for row in list(range(1, 9)) + list(range(16, 85)):  # symbols, alphanumerics, kana; then the kanji
        for cell in range(1, 95):
            try:
                ch = bytes([0xA0 + row, 0xA0 + cell]).decode("euc_jp")
            except UnicodeDecodeError:
                continue
            if ord(ch) > 0x7F:
                out.append(ord(ch))
    return out + list(range(0xFF61, 0xFFA0))  # half-width katakana


def vlw_multi(px, sources):
    """sources: [(file, weight, [code points])], earlier ones winning a code point. Ascent and descent: the first's."""
    faces, glyphs = [], {}
    for fname, weight, codes in sources:
        face = load_face(fname, weight, px)
        faces.append(face)
        for c in codes:
            if c not in glyphs and face.get_char_index(c):
                glyphs[c] = face
    metrics, bitmaps = [], []
    for code in sorted(glyphs):
        face = glyphs[code]
        face.load_char(chr(code), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
        g = face.glyph
        bm = g.bitmap
        w, h, pitch = bm.width, bm.rows, bm.pitch
        raw = bytes(bm.buffer)
        bitmaps.append(b"".join(raw[r * pitch:r * pitch + w] for r in range(h)))
        adv = int(round(g.advance.x / 64))
        if w > 255 or adv > 255:
            sys.exit(f"{px}px: glyph {code:#x} too wide for VLW (8-bit widths)")
        metrics.append(struct.pack(">7i", code, h, w, adv, g.bitmap_top, g.bitmap_left, 0))
    first = faces[0]
    ascent, descent = int(round(first.size.ascender / 64)), int(round(-first.size.descender / 64))
    header = struct.pack(">6i", len(glyphs), 11, px, 0, ascent, descent)
    return header + b"".join(metrics) + b"".join(bitmaps), len(glyphs)


def write_sd():
    path = os.path.join(FONTS, JP_FONT)
    if not os.path.exists(path):
        print(f"downloading {JP_FONT} ...")
        urllib.request.urlretrieve(JP_URL, path)
    latin = list(range(0xA1, 0x180)) + list(range(0x2010, 0x2028)) + list(range(0x2030, 0x203B)) + [0x20AC, 0x2122]
    jp = jis_chars()
    out_dir = os.path.join(os.path.dirname(HERE), "sd", "fonts")
    os.makedirs(out_dir, exist_ok=True)
    total = 0
    for role in SD_ROLES:
        style, px = ROLES[role][:2]
        inter, weight = FAMILIES["inter"][style]
        data, n = vlw_multi(px, [(inter, weight, latin), (JP_FONT, weight, jp)])
        with open(os.path.join(out_dir, role + ".vlw"), "wb") as f:
            f.write(data)
        total += len(data)
        print(f"sd/fonts/{role}.vlw: {px}px, {n} glyphs, {len(data) // 1024} KB")
    print(f"sd/fonts: {total // (1024 * 1024)} MB")


if __name__ == "__main__":
    for fam in sys.argv[1:] or [*FAMILIES, "icons"]:
        write_icons() if fam == "icons" else write_sd() if fam == "sd" else write_header(fam)
