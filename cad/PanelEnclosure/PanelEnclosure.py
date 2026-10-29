"""Fusion script: desktop arc enclosure for four Waveshare ESP32-P4-WIFI6-Touch-LCD-7B panels.

Five wedge modules (screen, screen, blank centre, screen, screen) whose side walls
are cut at TURN/2 so neighbours mate flush, turning TURN degrees at each joint.
Each module is three parts: a face bezel, a hood (open at the bottom and back) and a
removable L of back panel plus floor, which slides in from behind on two locator tabs and
is held by four M3 screws into heat-set inserts at the hood's back corners. The hood stands on the
floor, which takes four stick-on rubber feet in recessed pockets, and the back panel
carries a panel-mount USB-C extension for flashing. The bezel is glued to a frame on the hood, located
by its outline (flush with the hood's sides, foot on the desk). Screen modules get a
glass-sized opening: the panel goes in from the front, recessed, and is screwed from
behind through four wall tabs into the board's corner holes; the centre bezel gets a
matching pocket to drill for knobs and ports. Cables leave through a slot at the bottom of the back panel,
which also lets cool air in; warm air leaves through vent slots high on the back panel.
Run from Utilities > Add-Ins > Scripts and Add-Ins. Units below are mm.

Printing (PLA, 256 mm bed): run() also writes stl/ next to this script. Bezels print
face-up, so the visible face is a top surface (use ironing); hoods and back Ls are hidden
enough for draft settings. Hoods are exported roof-down: the walls are vertical, the
frame leans 25 deg, and the frame under the glass is chamfered to OVERHANG; only the top
end of each mounting tab overhangs (5 x 20 mm, prints with slight droop). The L prints on
its floor. Two hoods fit on one plate.
"""
import adsk.core, adsk.fusion, math, os, struct, traceback

GLASS_W, GLASS_H = 164.28, 99.17  # cover glass
# Glass edge to active area, front view as mounted: 180 degrees from Waveshare's drawing,
# like p4_case.stl (USB ports on the right). The glass is offset so the active area is centred.
BEZEL_L, BEZEL_R, BEZEL_T, BEZEL_B = 6.65, 3.06, 8.73, 4.02
PCB_W, PCB_H = 164, 97            # board behind it, assumed centred on the glass
HOLE_INSET = 4                    # 156 x 89 hole pattern
POST_DEPTH = 15      # glass front to the end of the panel's mounting posts (measured)
TAB_T = 5            # mounting tab thickness behind the posts
TAB_MARGIN = 3       # tab material around each screw hole
USB_SPAN = (9, 50)   # kept clear below the PCB top on the right: UART-C, USB-C, USB-A ports
HOLE_D = 2.9         # M2.5 clearance, printed (near-horizontal holes print undersize)
OVERHANG = 50        # steepest-down cut from flat, printed roof-down
TILT = 65            # face angle from the desk
WIDTH = 182          # module width along its front edge (sets the USB-side gap)
USB_GAP = 8          # min PCB right edge (USB ports) to inner side wall, for the plugs
RADIUS = 800         # arc of the front corners: the monitor's 800R
TURN = 2 * math.degrees(math.asin(WIDTH / 2 / RADIUS))  # turn between neighbours, ~12.8 (167.2 between faces)
FACE = 128           # face length up the slope
TOP = 45             # depth of the top behind the face (room for the board)
WALL = 2.5           # 6 lines of 0.42 mm: no gap fill
CLEAR = 0.3          # opening / floor clearance per side (PLA)
LOC_LEN, LOC_W, LOC_T = 8, 20, 2  # back panel locator tabs: reach into the hood, width, thickness
LOC_CLEAR = 0.15     # locator tab to side wall
BOSS = 10            # back corner screw bosses: width and depth
INSERT_D, SCREW_CLEAR = 4.0, 3.4  # M3 heat-set insert in the boss, M3 clearance in the panel
USBC_POS = (55, 25)  # panel-mount USB-C extension on the back panel (x, z), for USB flashing
USBC_CUT, USBC_PITCH, USBC_HOLE = (10, 4), 18, 3.2  # SparkFun CAB-15455 style: socket, M3 holes
FOOT, FOOT_T = 20.32, 7.62  # 0.8 in square rubber feet, 0.3 in thick
FOOT_RECESS, FOOT_SKIN = 4, 1.5  # pocket depth in the floor, floor left over the pocket
SLOT_W, SLOT_H = 40, 12  # cable slot at the bottom of the back panel (doubles as the air intake)
VENT_N, VENT_W, VENT_PITCH = 13, 3, 8  # exhaust slots high on the back panel
VENT_Z = (50, 95)    # slot bottom and top: clear of the locator tabs' sides and the screw boss
EDGE_R = 3           # round on the face/roof and roof/back edges
EDGE_CHAMFER = 0.5   # on the joint's front edges (hides glue) and the glass opening
GLASS_RECESS = 0.5   # glass front below the face
SHADOW_H, SHADOW_D = 4, 1  # recessed strip along the foot of the face: height, depth
BEZEL_T = 3          # face bezel (at least WALL), a separate part printed face-up (ironing)
FRAME_T = 3          # hood's frame behind the bezel: glue face, and keeps the hood stiff
# Bezel locator holes, blind in the bezel's back and the frame's front, at the same four
# points: two under the opening, one beside it on each side near the top. Default: pins cut
# from 1.75 mm filament. For removable bezels use 6 x 2 mm disc magnets: (6.2, 2.1, 2.1).
PIN_D, PIN_BEZEL, PIN_FRAME = 2.0, 2.0, 2.5  # hole diameter, depth into bezel, depth into frame
GLASS_BOTTOM = 20    # face bottom edge to an unshifted glass's bottom edge, up the slope
LAYOUT = ['screen', 'screen', 'blank', 'screen', 'screen']


def mm(v):
    return v / 10  # Fusion's API works in cm


def P(x, y, z):
    return adsk.core.Point3D.create(mm(x), mm(y), mm(z))


def polygon(comp, plane, pts):
    sk = comp.sketches.add(plane)
    q = [sk.modelToSketchSpace(p) for p in pts]
    for a, b in zip(q, q[1:] + q[:1]):
        sk.sketchCurves.sketchLines.addByTwoPoints(a, b)
    return sk.profiles.item(0)


def build_module(comp, screen):
    a = math.radians(TILT)
    run, rise = FACE * math.cos(a), FACE * math.sin(a)
    depth = run + TOP
    feats = comp.features
    ext = feats.extrudeFeatures
    NEW = adsk.fusion.FeatureOperations

    # Side profile (YZ): desk -> up the face -> top -> back wall.
    side = polygon(comp, comp.yZConstructionPlane,
                   [P(0, 0, 0), P(0, run, rise), P(0, depth, rise), P(0, depth, 0)])
    inp = ext.createInput(side, NEW.NewBodyFeatureOperation)
    inp.setSymmetricExtent(adsk.core.ValueInput.createByReal(mm(WIDTH * 2)), True)
    ext.add(inp)

    # Plan (XY): trapezoid, sides flared TURN/2 so neighbours mate flush.
    flare = depth * math.tan(math.radians(TURN / 2))
    plan = polygon(comp, comp.xYConstructionPlane,
                   [P(-WIDTH / 2, 0, 0), P(WIDTH / 2, 0, 0),
                    P(WIDTH / 2 + flare, depth, 0), P(-WIDTH / 2 - flare, depth, 0)])
    ext.addSimple(plan, adsk.core.ValueInput.createByReal(mm(rise + 10)),
                  NEW.IntersectFeatureOperation)

    body = comp.bRepBodies.item(0)
    body.name = 'Shell'

    def edges(b, test):  # edges whose end points pass test(p, q), in mm
        c = adsk.core.ObjectCollection.create()
        for ed in b.edges:
            p, q = (adsk.core.Point3D.create(g.x * 10, g.y * 10, g.z * 10)
                    for g in (ed.startVertex.geometry, ed.endVertex.geometry))
            if test(p, q) or test(q, p):
                c.add(ed)
        return c

    def near(*pairs):
        return all(abs(x - y) < 0.01 for x, y in pairs)

    def chamfer(es, dist):
        ci = feats.chamferFeatures.createInput2()
        ci.chamferEdgeSets.addEqualDistanceChamferEdgeSet(es, adsk.core.ValueInput.createByReal(mm(dist)), False)
        feats.chamferFeatures.add(ci)

    # Round the top edges (face/roof, roof/back) before the shell so the inside follows;
    # the back panel is cut from this solid, so it carries the back round. Then chamfer the
    # joint's front edges (side face meets face) so glued joints read as a clean V.
    fi = feats.filletFeatures.createInput()
    fi.edgeSetInputs.addConstantRadiusEdgeSet(
        edges(body, lambda p, q: near((p.z, rise), (q.z, rise), (p.y, q.y)) and (near((p.y, run)) or near((p.y, depth)))),
        adsk.core.ValueInput.createByReal(mm(EDGE_R)), False)
    feats.filletFeatures.add(fi)
    chamfer(edges(body, lambda p, q: near((p.y, 0), (p.z, 0), (abs(p.x), WIDTH / 2)) and q.z > 1), EDGE_CHAMFER)
    tm = adsk.fusion.TemporaryBRepManager.get()
    outer = tm.copy(body)  # trims the rails to the walls later
    faces = adsk.core.ObjectCollection.create()
    faces.add(min(body.faces, key=lambda f: f.centroid.z))  # open the bottom
    si = feats.shellFeatures.createInput(faces, False)
    si.insideThickness = adsk.core.ValueInput.createByReal(mm(WALL))
    feats.shellFeatures.add(si)

    BT = adsk.fusion.BooleanTypes

    def add(tool, op, target='Shell'):  # temporary body -> combined into a named body
        bf = feats.baseFeatures.add()
        bf.startEdit()
        comp.bRepBodies.add(tool, bf)
        bf.finishEdit()
        tools = adsk.core.ObjectCollection.create()
        for b in bf.bodies:
            tools.add(b)
        ci = feats.combineFeatures.createInput(comp.bRepBodies.itemByName(target), tools)
        ci.operation = op
        feats.combineFeatures.add(ci)

    def obox(c, l, w, L, W, H):  # box centred on c: L along l, W along w, H along l x w
        return tm.createBox(adsk.core.OrientedBoundingBox3D.create(c, l, w, mm(L), mm(W), mm(H)))

    def ycyl(x, z, y0, y1, dia):  # hole along Y
        return tm.createCylinderOrCone(P(x, y0, z), mm(dia / 2), P(x, y1, z), mm(dia / 2))

    X, Y, Z = (adsk.core.Vector3D.create(*v) for v in ((1, 0, 0), (0, 1, 0), (0, 0, 1)))

    # Removable back, an L of panel and floor. The hood loses its back wall (and the last
    # WALL of the sides and roof) and the bottom WALL of its side walls behind the frame:
    # it stands on the floor, which runs the full width, so the feet carry the whole module.
    # Two locator tabs follow the flared side walls, and four screws go into heat-set
    # inserts in bosses at the hood's back corners.
    t = math.radians(TURN / 2)
    yb = depth - WALL
    y0 = (BEZEL_T + FRAME_T) / math.sin(a) + WALL / math.tan(a) + CLEAR  # floor front, clear of the frame

    def half(y):  # inner half-width at depth y, less CLEAR
        return WIDTH / 2 + y * math.tan(t) - WALL / math.cos(t) - CLEAR

    def full(y):  # outer half-width at depth y
        return WIDTH / 2 + y * math.tan(t)

    floor = polygon(comp, comp.xYConstructionPlane,
                    [P(-full(y0), y0, 0), P(full(y0), y0, 0), P(full(depth), depth, 0), P(-full(depth), depth, 0)])
    ext.addSimple(floor, adsk.core.ValueInput.createByReal(mm(WALL)),
                  NEW.NewBodyFeatureOperation).bodies.item(0).name = 'Back panel'

    add(obox(P(0, depth, rise / 2), X, Y, 4 * WIDTH, 2 * WALL, 4 * rise), NEW.CutFeatureOperation)
    ly = depth + 10 - (y0 - CLEAR)
    add(obox(P(0, y0 - CLEAR + ly / 2, WALL / 2 - 0.5), X, Y, 4 * WIDTH, ly, WALL + 1), NEW.CutFeatureOperation)

    # Corner bosses. The top ones hang under the roof; the bottom ones sit just above the
    # feet pads (the floor slides under them) and taper into the side wall at 45 deg, so
    # they print roof-down without support.
    zb0 = FOOT_RECESS + FOOT_SKIN + 0.5  # bottom boss underside
    screws = []  # (x, z) of each corner screw
    for s in (-1, 1):
        xi = s * (WIDTH / 2 + (yb - BOSS) * math.tan(t) - WALL / math.cos(t))  # inner wall, narrowest
        xc = xi - s * BOSS / 2
        bx, bw = xi - s * (BOSS - WALL) / 2, BOSS + WALL  # boss block runs WALL into the wall
        top = obox(P(bx, yb - BOSS / 2, rise - (BOSS + WALL) / 2), X, Y, bw, BOSS, BOSS + WALL)
        bot = obox(P(bx, yb - BOSS / 2, zb0 + BOSS), X, Y, bw, BOSS, 2 * BOSS)
        r2 = math.sqrt(0.5)
        h = adsk.core.Vector3D.create(-s * r2, 0, r2)  # 45 deg, up and away from the wall
        ax, az = xi - s * BOSS, zb0 + BOSS  # the taper starts at the boss's inner top corner
        tm.booleanOperation(bot, obox(P(ax + 15 * h.x, yb - BOSS / 2, az + 15 * h.z),
                                      adsk.core.Vector3D.create(s * r2, 0, r2), Y, 60, 3 * BOSS, 30),
                            BT.DifferenceBooleanType)
        for blk in (top, bot):
            tm.booleanOperation(blk, outer, BT.IntersectionBooleanType)
            add(blk, NEW.JoinFeatureOperation)
        screws += [(xc, rise - WALL - BOSS / 2), (xc, zb0 + BOSS / 2)]
    for x, z in screws:
        add(ycyl(x, z, yb - BOSS + 1.5, yb + 1, INSERT_D), NEW.CutFeatureOperation)

    # Feet: a pad on the floor at each corner, pocketed from below, so the feet stand
    # FOOT_T - FOOT_RECESS proud of the floor.
    assert FOOT_T > FOOT_RECESS, 'feet would sit inside their pockets'
    pad = FOOT + 4
    yf = (BEZEL_T + FRAME_T + (FOOT_RECESS + FOOT_SKIN) * math.cos(a)) / math.sin(a) + CLEAR  # frame, at pad top
    for y in (yf + pad / 2, yb - pad / 2 - 1):
        for s in (-1, 1):
            x = s * (half(y - pad / 2) - pad / 2 - 1)
            add(obox(P(x, y, (WALL + FOOT_RECESS + FOOT_SKIN) / 2 - 0.25), X, Y, pad, pad,
                     FOOT_RECESS + FOOT_SKIN - WALL + 0.5), NEW.JoinFeatureOperation, 'Back panel')
            add(obox(P(x, y, (FOOT_RECESS - 1) / 2), X, Y, FOOT + 0.6, FOOT + 0.6, FOOT_RECESS + 1),
                NEW.CutFeatureOperation, 'Back panel')

    panel = obox(P(0, depth, rise / 2), X, Y, 4 * WIDTH, 2 * WALL, 4 * rise)
    tm.booleanOperation(panel, outer, BT.IntersectionBooleanType)
    for s in (-1, 1):
        along = adsk.core.Vector3D.create(s * math.sin(t), math.cos(t), 0)
        yc = yb - (LOC_LEN - 1) / 2 * math.cos(t)
        xc = s * (WIDTH / 2 + yc * math.tan(t) - (WALL + LOC_CLEAR + LOC_T / 2) / math.cos(t))
        tm.booleanOperation(panel, obox(P(xc, yc, rise / 2), along, Z, LOC_LEN + 1, LOC_W, LOC_T), BT.UnionBooleanType)
    for x, z in screws:
        tm.booleanOperation(panel, ycyl(x, z, yb - 1, depth + 1, SCREW_CLEAR), BT.DifferenceBooleanType)
    tm.booleanOperation(panel, obox(P(0, depth, 0), X, Y, SLOT_W, 4 * WALL, 2 * (WALL + SLOT_H)), BT.DifferenceBooleanType)
    assert (VENT_N - 1) * VENT_PITCH / 2 + VENT_W < half(depth) - BOSS - 5, 'vents hit the corner bosses or tabs'
    for i in range(VENT_N):
        tm.booleanOperation(panel, obox(P((i - (VENT_N - 1) / 2) * VENT_PITCH, depth, sum(VENT_Z) / 2), X, Y,
                                        VENT_W, 4 * WALL, VENT_Z[1] - VENT_Z[0]), BT.DifferenceBooleanType)
    ux, uz = USBC_POS
    tm.booleanOperation(panel, obox(P(ux, depth, uz), X, Y, USBC_CUT[0], 4 * WALL, USBC_CUT[1]), BT.DifferenceBooleanType)
    for s in (-1, 1):
        x = ux + s * USBC_PITCH / 2
        tm.booleanOperation(panel, tm.createCylinderOrCone(P(x, yb - 1, uz), mm(USBC_HOLE / 2), P(x, depth + 1, uz),
                                                           mm(USBC_HOLE / 2)), BT.DifferenceBooleanType)
    add(panel, NEW.JoinFeatureOperation, 'Back panel')

    # Face frame: u across, v up the slope, n out of the face.
    u = adsk.core.Vector3D.create(1, 0, 0)
    v = adsk.core.Vector3D.create(0, math.cos(a), math.sin(a))
    n = u.crossProduct(v)

    def at(uu, vv, d):  # face coords -> model point; d = depth behind the face
        return P(uu * u.x + vv * v.x - d * n.x, uu * u.y + vv * v.y - d * n.y, uu * u.z + vv * v.z - d * n.z)

    def box(u0, u1, v0, v1, d0, d1):
        obb = adsk.core.OrientedBoundingBox3D.create(at((u0 + u1) / 2, (v0 + v1) / 2, (d0 + d1) / 2),
                                                     u, v, mm(u1 - u0), mm(v1 - v0), mm(d1 - d0))
        return tm.createBox(obb)

    def cyl(uu, vv, d0, d1, dia):
        return tm.createCylinderOrCone(at(uu, vv, d0), mm(dia / 2), at(uu, vv, d1), mm(dia / 2))

    def union(bodies):
        acc = bodies[0]
        for b in bodies[1:]:
            tm.booleanOperation(acc, b, adsk.fusion.BooleanTypes.UnionBooleanType)
        return acc

    gu = (BEZEL_R - BEZEL_L) / 2                # glass centre, across
    gb = GLASS_BOTTOM + (BEZEL_T - BEZEL_B) / 2  # glass bottom edge
    gw, gt = GLASS_W / 2 + CLEAR, gb + GLASS_H + CLEAR  # opening half-width and top
    ev = gb - CLEAR  # opening bottom

    # The hood's frame behind the bezel. It starts at the front wall's back so the bezel,
    # split off at BEZEL_T at the end, is solid even when it's thicker than WALL.
    fb = BEZEL_T + FRAME_T  # frame back
    frame = box(-WIDTH, WIDTH, -50, FACE + 50, WALL, fb)
    tm.booleanOperation(frame, outer, BT.IntersectionBooleanType)
    add(frame, NEW.JoinFeatureOperation)

    # Printed roof-down, the frame under the opening would start as a 164 mm bridge: chamfer
    # its edge at the bezel to OVERHANG (hidden behind the bezel and glass).
    b = math.radians(OVERHANG)
    w = adsk.core.Vector3D.create(0, math.cos(b), -math.sin(b))  # back and down
    L, T = 10, 5  # chamfer box: along w, and up off the chamfer plane (into the opening)
    under = tm.createBox(adsk.core.OrientedBoundingBox3D.create(
        P(gu, ev * math.cos(a) + BEZEL_T * math.sin(a) + L / 2 * w.y + T / 2 * math.sin(b),
          ev * math.sin(a) - BEZEL_T * math.cos(a) + L / 2 * w.z + T / 2 * math.cos(b)),
        u, w, mm(2 * gw), mm(L), mm(T)))
    shadow = box(-WIDTH, WIDTH, -5, SHADOW_H, -5, SHADOW_D)  # recessed strip along the face's foot
    # The opening: through to the tabs on a screen module. The centre one gets a GLASS_RECESS
    # pocket in the bezel (to drill for knobs and ports) and the same opening in the frame.
    d = POST_DEPTH + GLASS_RECESS if screen else fb + 1
    cuts = [box(gu - gw, gu + gw, ev, gt, -5, d) if screen else box(gu - gw, gu + gw, ev, gt, -5, GLASS_RECESS),
            under, shadow]
    if not screen:
        cuts.append(box(gu - gw, gu + gw, ev, gt, BEZEL_T, d))

    # Locator holes, one cylinder each across the bezel/frame split. Side holes sit midway
    # between the opening and the outside of the side wall (which flares wider up the face).
    assert PIN_BEZEL <= BEZEL_T - 0.8 and PIN_FRAME <= FRAME_T, 'locator holes go through the bezel or frame'
    vs = gt - 15
    ys = vs * math.cos(a) + BEZEL_T * math.sin(a)
    pins = [(-60, ev / 2), (60, ev / 2)] + [((gu + s * gw + s * (WIDTH / 2 + ys * math.tan(t))) / 2, vs) for s in (-1, 1)]
    for pu, pv in pins:
        assert pv - PIN_D / 2 > SHADOW_H + 1, 'locator hole runs into the shadow line'
        cuts.append(cyl(pu, pv, BEZEL_T - PIN_BEZEL, BEZEL_T + PIN_FRAME, PIN_D))

    if screen:
        pb = gb + (GLASS_H - PCB_H) / 2  # PCB bottom edge
        # Tightest point is the PCB's bottom corner at the face; the walls flare outward behind it.
        gap = WIDTH / 2 + pb * math.cos(a) * math.tan(t) - WALL / math.cos(t) - (gu + PCB_W / 2)
        assert gap >= USB_GAP, 'USB side gap %.1f mm < %s: widen WIDTH' % (gap, USB_GAP)
        holes = [(gu + su * (PCB_W / 2 - HOLE_INSET), pb + HOLE_INSET + sv * (PCB_H - 2 * HOLE_INSET))
                 for su in (-1, 1) for sv in (0, 1)]
        e = d + TAB_T  # tab back; d is the tab front, where the posts land
        pt = pb + PCB_H  # PCB top edge

        # One tab per post, from the hole out into the side wall.
        m = HOLE_D / 2 + TAB_MARGIN
        lo, hi = holes[0][1], holes[1][1]  # bottom and top hole rows
        assert (lo - m) * math.sin(a) - e * math.cos(a) > WALL, 'tabs reach the floor: raise GLASS_BOTTOM'
        assert hi - m >= pt - USB_SPAN[0] and lo + m <= pt - USB_SPAN[1], 'tabs block the USB ports'
        tabs = []
        for hu, hv in holes:
            s = 1 if hu > 0 else -1
            r = box(min(hu - s * m, s * WIDTH), max(hu - s * m, s * WIDTH), hv - m, hv + m, d, e)
            tm.booleanOperation(r, outer, BT.IntersectionBooleanType)  # into the side wall
            tabs.append(r)
        add(union(tabs), NEW.JoinFeatureOperation)
        cuts += [cyl(hu, hv, d - 1, e + 1, HOLE_D) for hu, hv in holes]
    add(union(cuts), NEW.CutFeatureOperation)

    # Chamfer the opening's front edges: the four edges on the face plane around the glass.
    def on_opening(p, q):
        return all(abs(r.y * math.sin(a) - r.z * math.cos(a)) < 0.01 and abs(r.x - gu) < gw + 0.01 and
                   ev - 0.01 < r.y * math.cos(a) + r.z * math.sin(a) < gt + 0.01 for r in (p, q))
    ring = edges(comp.bRepBodies.itemByName('Shell'), on_opening)
    assert ring.count == 4, 'found %d opening edges, expected 4' % ring.count
    chamfer(ring, EDGE_CHAMFER)

    # Split the bezel off: everything within BEZEL_T of the face plane.
    front = box(-WIDTH, WIDTH, -50, FACE + 50, -50, BEZEL_T)
    bezel = tm.copy(comp.bRepBodies.itemByName('Shell'))
    tm.booleanOperation(bezel, front, BT.IntersectionBooleanType)
    add(front, NEW.CutFeatureOperation)
    bf = feats.baseFeatures.add()
    bf.startEdit()
    comp.bRepBodies.add(bezel, bf)
    bf.finishEdit()
    bf.bodies.item(0).name = 'Bezel'


def placements():
    """(x, y, heading deg) of each module's front-edge centre, chained corner to corner from the middle."""
    mid = len(LAYOUT) // 2
    out = {mid: (0.0, 0.0, 0.0)}
    for step in (1, -1):
        x, y, h, i = 0.0, 0.0, 0.0, mid
        while 0 <= i + step < len(LAYOUT):
            r = math.radians(h)
            cx, cy = x + step * WIDTH / 2 * math.cos(r), y + step * WIDTH / 2 * math.sin(r)
            h -= step * TURN
            r = math.radians(h)
            x, y = cx + step * WIDTH / 2 * math.cos(r), cy + step * WIDTH / 2 * math.sin(r)
            i += step
            out[i] = (x, y, h)
    return [out[i] for i in range(len(LAYOUT))]


def build():
    app = adsk.core.Application.get()
    app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    design = adsk.fusion.Design.cast(app.activeProduct)
    design.designType = adsk.fusion.DesignTypes.ParametricDesignType
    root = design.rootComponent

    # Build each kind once at the origin, place every instance on creation (moving an
    # occurrence afterwards doesn't stick in a parametric design), then drop the build copy.
    built, scratch = {}, []
    for kind in set(LAYOUT):
        occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
        occ.component.name = 'Screen module' if kind == 'screen' else 'Centre module'
        build_module(occ.component, kind == 'screen')
        built[kind] = occ.component
        scratch.append(occ)
    # Modules are modelled Z-up, facing -Y; stand them up in a Y-up workspace (front toward +Z).
    up = adsk.core.Matrix3D.create()
    if app.preferences.generalPreferences.defaultModelingOrientation == \
            adsk.core.DefaultModelingOrientations.YUpModelingOrientation:
        up.setToRotation(-math.pi / 2, adsk.core.Vector3D.create(1, 0, 0), adsk.core.Point3D.create(0, 0, 0))
    for kind, (x, y, h) in zip(LAYOUT, placements()):
        m = adsk.core.Matrix3D.create()
        m.setToRotation(math.radians(h), adsk.core.Vector3D.create(0, 0, 1), adsk.core.Point3D.create(0, 0, 0))
        m.translation = adsk.core.Vector3D.create(mm(x), mm(y), 0)
        m.transformBy(up)
        root.occurrences.addExistingComponent(built[kind], m)
    for occ in scratch:
        occ.deleteMe()
    app.activeViewport.fit()


def write_stl(body, path, role):
    """Binary STL in print orientation: hoods ('Shell') roof-down, bezels face-up, the back L as modelled."""
    ct, st = math.cos(math.radians(TILT)), math.sin(math.radians(TILT))
    turn = {'Shell': lambda x, y, z: (x, -y, -z),                      # 180 deg about X
            'Bezel': lambda x, y, z: (x, y * ct + z * st, z * ct - y * st)}  # face normal to +Z
    calc = body.meshManager.createMeshCalculator()
    calc.surfaceTolerance = mm(0.01)  # round screw holes, not hexagons
    mesh = calc.calculate()
    c = mesh.nodeCoordinatesAsDouble
    pts = [(c[i] * 10, c[i + 1] * 10, c[i + 2] * 10) for i in range(0, len(c), 3)]
    if role in turn:
        pts = [turn[role](*p) for p in pts]
    z0 = min(p[2] for p in pts)
    pts = [(x, y, z - z0) for x, y, z in pts]
    idx = mesh.nodeIndices
    with open(path, 'wb') as f:
        f.write(b'\0' * 80 + struct.pack('<I', len(idx) // 3))
        for i in range(0, len(idx), 3):  # zero normals: slicers take them from the winding
            f.write(struct.pack('<12fH', 0, 0, 0, *pts[idx[i]], *pts[idx[i + 1]], *pts[idx[i + 2]], 0))


def export_stls(folder):
    """Every part, plus fit-test coupons cut from a screen module (print these first)."""
    os.makedirs(folder, exist_ok=True)
    design = adsk.fusion.Design.cast(adsk.core.Application.get().activeProduct)
    written = []
    for comp in design.allComponents:
        for body in comp.bRepBodies:
            if body.name not in ('Shell', 'Back panel', 'Bezel'):
                raise RuntimeError('unexpected body %s in %s' % (body.name, comp.name))
            kind = comp.name.split()[0].lower()  # screen / centre
            name = {'Shell': kind + '_hood', 'Bezel': kind + '_bezel'}.get(body.name, 'back_panel')
            if name in written:  # every module's back panel is the same
                continue
            write_stl(body, os.path.join(folder, name + '.stl'), body.name)
            written.append(name)

    # Coupons, from the right (USB) side of a screen module, cut by axis-aligned boxes so each
    # piece keeps a flat face on the bed. Front: the glass opening corner in bezel and frame,
    # and a mounting tab with its hole. Back: a corner boss with its insert hole, a locator
    # tab, a foot pocket and the USB-C cutout, in both the hood and the back panel.
    a = math.radians(TILT)
    depth = FACE * math.cos(a) + TOP
    regions = {'front': (40, 2 * WIDTH, -5, 40, -1, 45, ('Bezel', 'Shell')),
               'back': (40, 2 * WIDTH, depth - WALL - 30, depth + 1, -1, 70, ('Shell', 'Back panel'))}
    tm = adsk.fusion.TemporaryBRepManager.get()
    comp = next(c for c in design.allComponents if c.name.startswith('Screen'))
    for where, (x0, x1, y0, y1, z0, z1, roles) in regions.items():
        for role in roles:
            piece = tm.copy(comp.bRepBodies.itemByName(role))
            tm.booleanOperation(piece, tm.createBox(adsk.core.OrientedBoundingBox3D.create(
                P((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), adsk.core.Vector3D.create(1, 0, 0),
                adsk.core.Vector3D.create(0, 1, 0), mm(x1 - x0), mm(y1 - y0), mm(z1 - z0))),
                adsk.fusion.BooleanTypes.IntersectionBooleanType)
            name = 'coupon_%s_%s' % (where, {'Shell': 'hood', 'Bezel': 'bezel', 'Back panel': 'back_panel'}[role])
            write_stl(piece, os.path.join(folder, name + '.stl'), role)
            written.append(name)
    return written


def run(context):
    try:
        build()
        export_stls(os.path.join(os.path.dirname(os.path.realpath(__file__)), 'stl'))
    except Exception:
        adsk.core.Application.get().userInterface.messageBox(traceback.format_exc())
