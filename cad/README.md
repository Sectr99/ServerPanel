# Desk enclosure

A desktop arc for four panels: five wedge modules (screen, screen, blank centre, screen, screen) that follow an 800R monitor curve, with each screen tilted back to 65 degrees from the desk. Everything comes from one Fusion script, [PanelEnclosure/PanelEnclosure.py](PanelEnclosure/PanelEnclosure.py). Its constants at the top are the knobs, and its asserts stop the build when a change breaks a fit: tabs hitting the floor, blocked USB ports, a USB side gap under 8 mm, vents hitting bosses.

**Run it:** in Fusion, Utilities > Add-Ins > Scripts and Add-Ins, then PanelEnclosure. Copy the folder to `%APPDATA%\Autodesk\Autodesk Fusion 360\API\Scripts\` first. It builds the arc in a new design and writes print-ready STLs to `PanelEnclosure/stl/`, already oriented for printing.

## Parts

Each module is three printed parts:
- **Bezel:** the face, carrying the glass opening, the shadow line, the top round and the joint chamfers. The centre module's bezel has a 0.5 mm pocket in place of the opening, to drill for knobs and ports.
- **Hood:** open at the bottom and back. It has a frame behind the bezel (with four locator holes that match blind holes in the bezel's back), the panel's four mounting tabs, and four corner bosses for inserts.
- **Back:** an L of back panel and floor. It slides in from behind on two locator tabs and carries the vents, the cable slot, the USB-C cutout and the feet.

| STL | Print | Orientation | Settings |
|---|---|---|---|
| `screen_bezel` | 4 | face-up | 0.12 mm layers, ironing on top surface, seam painted to a back corner |
| `centre_bezel` | 1 | face-up | same |
| `screen_hood` | 4 | roof-down | draft (0.28 mm), elephant-foot compensation 0.1-0.15 mm |
| `centre_hood` | 1 | roof-down | draft |
| `back_panel` | 5 | on its floor | draft |
| `coupon_*` | 1 set | as exported | the settings of the part each one comes from |

On a 256 mm bed, two hoods fit on one plate, and so do one bezel and one back panel (129 + 91 mm deep). No part needs supports. The few overhangs are the top end of each mounting tab (5 x 20 mm) and short bridges.

## Hardware

| Item | Qty | For |
|---|---|---|
| M3 heat-set insert (4.0 mm hole, about 5.7 mm long) | 20 | hood corner bosses |
| M3 x 8 screw | 20 | back panels |
| M2.5 x 8 screw | 16 | panels to the hood tabs, into the board's mounting posts |
| Rubber foot, 0.8 in square, 0.3 in thick | 20 | floor pockets (4 mm deep, so they stand 3.6 mm proud) |
| Panel-mount USB-C extension, SparkFun CAB-15455 style (M3 holes 18 mm apart) | 4 | USB flashing without opening the module |
| 1.75 mm filament, cut into 4.5 mm pins | 20 | bezel locators (4 per module: 2 mm into the bezel, 2.5 mm into the frame) |
| CA glue or PLA-safe epoxy | | bezels to hoods, modules to each other |

For removable bezels, use 40 disc magnets, 6 x 2 mm (4 in each bezel, 4 in each frame), in place of the pins and glue. Set `PIN_D, PIN_BEZEL, PIN_FRAME = 6.2, 2.1, 2.1` and rerun. Mind the polarity: mark one face of each pair before pressing them in.

## Before printing everything

Print the four `coupon_*` pieces first. They're cut from the right (USB) side of a screen module:
- **Front coupons:** the glass opening corner in the bezel and the frame, and a mounting tab.
- **Back coupons:** a corner boss, a locator tab, a foot pocket and the USB-C cutout.

Check each fit and change the constant if needed:

| Check | Constant |
|---|---|
| Glass in the opening (0.3 mm per side) | `CLEAR` |
| Mounting tab depth: the board's posts end 15 mm behind the glass | `POST_DEPTH` |
| Tab holes over the board's holes (board assumed centred on the glass) | `HOLE_INSET`, `PCB_W`, `PCB_H` |
| Locator tab against the side wall (0.15 mm) | `LOC_CLEAR` |
| Filament pin in the bezel and frame holes (bottom-right pin, in the front coupons) | `PIN_D` |
| Insert in its hole | `INSERT_D` |
| Foot in its pocket | `FOOT` |
| USB-C cable's hole pitch and socket | `USBC_PITCH`, `USBC_CUT` |

Before gluing anything, also swipe in from each edge on a test bezel: the firmware uses edge gestures, and the glass sits 0.5 mm below the face (`GLASS_RECESS`).

## Assembly

1. Press the heat-set inserts into the hood's four corner bosses.
2. Fit the panel into the hood from the front, glass forward. With the back off, screw it through the four tabs into its posts.
3. Push four filament pins into the frame's locator holes, then glue the bezel onto the frame over them. The pins line the opening up with the panel. Check that the bezel's sides sit flush with the hood.
4. Stick the feet into the floor's pockets. Screw the USB-C extension into the back panel and plug it into the board's USB-UART port. Slide the back in and fit the four M3 screws.
5. Glue the modules side to side on a flat surface, starting from the centre and working outward. The side walls are cut so neighbours mate flush on the 800R arc.

The four screen modules are copies of one component in Fusion, so editing one edits all of them. To change a single module, right-click it in the browser and choose Make Independent.
