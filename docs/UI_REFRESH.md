# UI refresh plan

Status: built 2026-10-28 on branch `ui-refresh` (big numbers, tab strip), on panel D; not committed yet. Was a proposal (2026-10-27). Mockups of every screen, at 1024 x 600 and drawn only with what the panel can render: https://claude.ai/artifact/1mYG27XpTpgfYwHVF2kRsK (private). The Spotify screen is the reference: big type, few labels, one strong colour, controls in a strip along the bottom.

## Design rules
- **Type:** values at 36-50 px (96-112 px light for the launcher clock and the detail view's value). Labels 14 px bold caps, and only where a value needs one. No 14 px text that has to be read from a metre away.
- **Status bar** (56 px) replaces the header. Left: the app's icon in its accent colour, a title, one subtitle. Right: gear (on Server Stats), date, clock. The status dot and page dots go: problems show as amber or red pills or lines where they apply.
- **Navigation:** Server Stats gets a bottom tab strip (64 px; icon and label per page, accent bar on the active one) instead of five dots. Swiping still works. Every other screen uses the full height.
- **Accent per app:** Server Stats cyan, Radar green, Macro Pad pink, Stream Deck teal, Spotify the album's colour. The background is a dark gradient tinted toward the accent at the top. Amber and red stay reserved for warnings.
- **Cards:** radius 20, padding 20-24, 16 px gaps, no borders. Bars are 10-12 px rounded. Graphs are area charts with a gradient fade and a 2.5 px line, with no axes on the overview pages.
- **Icons:** Material Symbols everywhere (already in place). Each launcher tile has an icon in a soft accent circle. The weather uses condition icons, and the macro keys get an icon each.

## Per screen
| Screen | Change |
|---|---|
| Launcher | 112 px clock and date; tiles keep the 2 + 3 layout with accent icon circles. The Spotify tile shows the album art and track, the Radar tile the current temperature and conditions. |
| Desktop | Rings replaced by two hero cards (CPU, GPU): load and temperature at 50 px, a load lane and a temperature lane. Memory as a slim bar card. |
| NAS | Three hero cards (CPU, GPU, Network down/up), then Memory and Array bars (Array opens Storage). |
| Storage | Free space at 50 px with a bar segmented per disk. One tile per disk: % used, bar, free space, temperature pill or "asleep". |
| Docker | One list, 51 px rows: status dot, name, uptime, CPU and memory bars with values. "7 of 8 running" pill in the status bar. |
| Printer | Camera stays the hero (624 x 351, LIVE pill, expand icon). Progress strip: 50 px %, time left, finish time, layer. Side cards: job, nozzle, bed (36 px with target and bar), fans. State pill (Printing, Paused, Error) in the status bar. |
| Camera | Edge to edge. Back chip and clock floating at the top; progress line and thin bar along the bottom, like Spotify. |
| Detail graph | 96 px current value, min/avg/max chips, a full-width area chart with dashed gridlines and time labels. |
| Settings | The same controls, each row with an icon; 56 px rows. Status rows with icons. Restart as an amber outline button. |
| Weather Radar | Full-bleed map. Floating dark chips: place (pin icon), the weather card (condition icon, 50 px temperature, forecast rows with icons), the alert pill, zoom. Bottom strip: frame time, "latest", the loop's frames, legend. |
| Radar alert | A severity-coloured band (icon, 36 px event, severity and end time), then the text in two columns with the instructions boxed. |
| Radar location | A pin and the place at 36 px, ZIP digit boxes with the cursor in accent; keypad keys use backspace and check icons. |
| Macro Pad | Each key: a 40 px icon in pink, a 24 px label and the hint. `MACRO_KEYS` gains an icon name per key (appended field). |
| Stream Deck | A slim status bar; Companion's 160 px keys centred, with rounded corners. |
| Spotify | Unchanged (the reference). |

## Implementation, in order
1. **Foundations** (ui.cpp):
   - **Components:** `statusBar()` and `tabStrip()`, a card radius of 20, gradient backgrounds, and gradient-fade area fills in `lane()`.
   - **Fonts:** a 112 px light clock font (digits, colon, `%`, `°`) for the clock and the detail value.
   - **Icons:** add the new names to `ICONS` in mkfonts.py.
   - **Redraws:** every screen now paints its whole background, so the header-only partial clear goes. Spotify already redraws fully each frame.
2. **Server Stats:** Desktop, NAS, Storage, Docker; then Printer, Camera and Detail.
3. **Launcher and Settings.**
4. **Weather Radar:** map, alert, location. The forecast needs the NWS `icon` or `shortForecast` mapped to a condition icon.
5. **Macro Pad** (icon field in `MACRO_KEYS`, config template too) and **Stream Deck**.
6. **Checks:** the preview for every screen and scenario (`samples`), a contrast check of the palette at THEME_DAY and THEME_NIGHT, then flash panel D.

## The microSD card
The refresh fits in flash: the new fonts and icons add well under 200 KB. The card helps around it, per [SDCARD.md](SDCARD.md):
- **Radar map cache:** the map appears instantly.
- **Album art cache:** for the Spotify tile and screen.
- **Japanese text:** fonts for titles and names.
- **Printer timelapses:** played back from the camera view.
- **Optional:** a launcher wallpaper or photo-frame background from the card, behind the tiles.

## Open questions
- Rings or big numbers on Desktop and NAS? The mockups drop the rings for legibility. They could return as small accents beside the numbers.
- Should the tab strip replace the dots (it costs 64 px of height), or should both stay?

## Next: making it cohesive with the Spotify screen (paused 2026-10-28)
Mockups on the same canvas (https://claude.ai/artifact/1mYG27XpTpgfYwHVF2kRsK), on two more pages:
- **Cohesive (proposal):** the Spotify screen's design on every page: content-tinted gradient, no cards, three-level type (faded context, huge value, supporting line, faded/amber status), Spotify's 92 px icon strip with a green dot (tabs + gear), a thin full-width line above it, no status bar (small clock), Car Thing tabs in settings.
  - **User:** likes **Docker** (headline "7 of 8 running" over one faint card) and **Settings** (Car Thing tabs Display / Panel / Status, big rows on black). The others looked worse than the built ones.
- **Blend (proposal 2):** in between, built on the Docker page: today's cards and content, made faint (rgba white 5%) over a content-tinted gradient; a faded context line and small clock instead of the status bar; the Spotify strip for tabs and gear; radar controls in the strip, its loop and legend in a floating card. Desktop, NAS, Storage, Printer, Radar.
  - **Waiting on:** the user's review of Blend. Then build Blend for those pages + Cohesive Docker and Settings.
