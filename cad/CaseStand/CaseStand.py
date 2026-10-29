"""Fusion script: p4_case.stl leaned back to TILT on a wedge stand, modules side by side on an 800R arc.

First pass: the case STL is imported unchanged as a mesh body. Each module's stand is one shelled
part: a frame around the case, flush with its face, that the case slides into from the front
(POCKET_CLEAR per side), on a wedge open at the bottom and back. The frame's sides are bays BAY wide; the
right one is open toward the case's USB ports and into the wedge, so plugs and cables are covered
and run out the back, and the left one matches it so the screens are evenly spaced. The stand's
sides are flared TURN/2 so neighbours mate flush, and the pocket's floor has a window over the
case's vent grids and centre slot, and holes in line with the case's four back plate holes so the
two can be screwed together from behind. The centre module holds a blank plate the size of the case
(square corners, shelled open at the back) in place of the STL.
A separate bezel covers the frame's face and the case's rim, keeping the case in. Four bosses on its
back hold M3 heat-set inserts; they pass through holes in the frame's face into the bays and land
on tabs there, and M3 x 8 screws go up through the tabs from inside the stand (driver through the
open back). The right bay's mounts sit beyond the USB ports' span.
A removable L cover closes the stand's bottom and back, between its sides (CLEAR per side). It
carries a cable slot at the foot of the back and vent slots high on it, and is held by four M3 x 8
countersunk screws into heat-set inserts in blocks on the stand's side walls.
Neighbours bolt together through their side walls. The bezel's outer front edges and the frame's
top side edges are chamfered so neighbours meet in a V groove, and the opening is bevelled.
Run from Utilities > Add-Ins > Scripts and Add-Ins, with p4_case.stl next to this script.
Units below are mm.
"""
import adsk.core, adsk.fusion, math, os, struct, traceback
from types import SimpleNamespace

STL = os.path.join(os.path.dirname(os.path.realpath(__file__)), 'p4_case.stl')
TILT = 65        # face angle from the desk
RADIUS = 800     # arc through the modules' front edges: the monitor's 800R
LAYOUT = ['screen', 'screen', 'blank', 'screen', 'screen']  # modules on the arc, left to right
DEPTH = 100      # stand footprint, module front edge to the stand's back edge on the desk
WALL = 2.5
BAY = 20         # USB bay beside the case's right side: right-angle plugs (straight ones need ~40)
CLEAR = 0.3      # sliding fits between printed parts, per side (PLA)
POCKET_CLEAR = 0.6  # case to pocket, per side: both are printed, and shrinkage plus undersized FDM pockets add up
# Window in the pocket's floor, in the STL's coordinates (back plate at its min z):
# leaves glue bands above and below the vent grids and 3 mm down each side.
WIN_X, WIN_Y = (-80, 80), (-38, 41)
SCREEN_HOLES = [(-79, -42), (-79, 47), (77, -41), (77, 48)]  # the case's back plate holes (3 mm, the board's posts), STL x, y
SCREEN_HOLE_D = 3.4  # matching holes through the pocket's floor: room for the case's play in the pocket
OPEN_X, OPEN_Y = (-84, 81), (-47, 53)  # bezel opening: the case's inner walls (the glass), STL coordinates
BEZEL_T = 3      # bezel plate over the frame's face
BOSS_D, BOSS_H = 8, 5  # insert bosses on the bezel's back: diameter, length behind the bezel
INSERT_D, INSERT_L = 4.0, 6.7  # M3 heat-set insert hole: 5.7 mm insert plus 1 mm for the plastic it pushes ahead
INSERT_LEAD = 4.4, 0.6  # tapered mouth on insert holes: diameter at the face, depth to INSERT_D (starts the insert straight)
CSK_D = 6.3      # cover screws: 90 deg countersink for M3 flat heads (ISO 10642), flush with the cover's back
# Holes across the print direction get a teardrop top (45 deg roof, no sagging bridge). The stand and
# cover are taken as printed the way they're modelled, standing on the desk side; the bezel prints
# face-up, so its holes are vertical and stay round.
PRINT_UP = (0, 0, 1)
SCREW_CLEAR = 3.4  # M3 through the tabs
TAB_T, TAB_L = 3, 10  # bay tabs under the bosses: thickness, length along the bay
MOUNT_Y = (-50.5, 50.5)  # boss centres up the face, STL y: the right bay's USB ports span y 17-45
COVER_U = (15, 22)  # cover insert blocks' centres from the back's bottom and top ends, along it
BLOCK, BLOCK_D = 10, 10  # insert blocks on the side walls: size along the wall and the back, depth in
SLOT_W, SLOT_H, SLOT_U = 40, 12, 4  # cable slot at the foot of the back (also the air intake): width, height, from the bottom
VENT_N, VENT_W, VENT_PITCH = 13, 3, 8  # exhaust slots high on the back
VENT_U = (0.5, 0.85)  # vent slots' span along the back, as fractions of its length from the bottom
# Neighbours bolt together through their side walls (M3 x 10 and a nut, from inside with the cover
# off): pulls the seam closed and lines the faces up. Hole centres as weights of the wedge's
# corners (frame bottom, frame top, back foot), clear of the cover blocks and with room for the nut.
JOIN_HOLES = [(0.6, 0.15, 0.25), (0.3, 0.45, 0.25)]
JOIN_D = 3.2
EDGE_CH = 1.0   # chamfer on the bezel's outer front edges and the frame's top side edges: neighbours meet in a V
OPEN_CH = 1.5    # 45 deg bevel round the bezel's opening, toward the glass


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


def case_frame(path):
    """Read the STL. W(y, z) maps STL (y, z) to the module's (y, z): rotated TILT about X, the
    frame's bottom cap on the desk (z = 0) and the bezel's front edge at y = 0."""
    d = open(path, 'rb').read()
    n = struct.unpack('<I', d[80:84])[0]
    v = [struct.unpack('<3f', d[84 + 50 * i + 12 + 12 * k:84 + 50 * i + 24 + 12 * k])
         for i in range(n) for k in range(3)]
    c, s = math.cos(math.radians(TILT)), math.sin(math.radians(TILT))
    x, y, z = [(min(p[k] for p in v), max(p[k] for p in v)) for k in range(3)]
    fy = (y[0] - POCKET_CLEAR - WALL, y[1] + POCKET_CLEAR + WALL)  # frame, with its top and bottom caps
    ty, tz = -(fy[0] * c - (z[1] + BEZEL_T) * s), -(fy[0] * s + z[0] * c)  # bezel's bottom edge at the front
    return SimpleNamespace(W=lambda y, z: (y * c - z * s + ty, y * s + z * c + tz),
                           ty=ty, tz=tz, c=c, s=s, x=x, y=y, z=z, fy=fy)


def add_body(comp, tb):
    base = comp.features.baseFeatures.add()
    base.startEdit()
    comp.bRepBodies.add(tb, base)
    base.finishEdit()
    return base.bodies.item(0)


def frame_box(comp, to_yz, v, x, u, t):
    """Solid box over ranges x, u, t of a frame whose x is the module's, u runs along v (a unit
    (y, z) direction) and t along v turned toward the front; to_yz(u, t) gives the module's (y, z)."""
    obb = adsk.core.OrientedBoundingBox3D.create(
        P(sum(x) / 2, *to_yz(sum(u) / 2, sum(t) / 2)),
        adsk.core.Vector3D.create(1, 0, 0), adsk.core.Vector3D.create(0, *v),
        mm(x[1] - x[0]), mm(u[1] - u[0]), mm(t[1] - t[0]))
    return add_body(comp, adsk.fusion.TemporaryBRepManager.get().createBox(obb))


def box(comp, f, x, y, z):
    """Solid box over STL-coordinate ranges x, y, z, as a body in the module (base feature)."""
    return frame_box(comp, f.W, (f.c, f.s), x, y, z)


def cylinder(comp, p0, p1, d):
    tm = adsk.fusion.TemporaryBRepManager.get()
    return add_body(comp, tm.createCylinderOrCone(P(*p0), mm(d / 2), P(*p1), mm(d / 2)))


def cyl(comp, f, x, y, z, d):
    """Cylinder along the STL's z (normal to the face) over z range z, as a body in the module."""
    return cylinder(comp, (x, *f.W(y, z[0])), (x, *f.W(y, z[1])), d)


def hole(comp, p, a, t, d, teardrop=False, mouth=None):
    """Tool bodies for a hole along unit axis a from point p (mm), over t range t along a, diameter d.
    teardrop: add a 45 deg roof toward PRINT_UP. mouth = (diameter, depth): taper from that diameter
    at p down to d at that depth (an insert lead-in, or a countersink)."""
    at = lambda k: tuple(p[i] + k * a[i] for i in range(3))
    tm = adsk.fusion.TemporaryBRepManager.get()
    tools = [add_body(comp, tm.createCylinderOrCone(P(*at(t[0])), mm(d / 2), P(*at(t[1])), mm(d / 2)))]
    if mouth:
        dm, depth = mouth
        rate = (dm - d) / depth  # diameter lost per mm in
        tools.append(add_body(comp, tm.createCylinderOrCone(P(*at(-1)), mm((dm + rate) / 2), P(*at(depth)), mm(d / 2))))
    if teardrop:
        k = sum(PRINT_UP[i] * a[i] for i in range(3))
        up = [PRINT_UP[i] - k * a[i] for i in range(3)]
        n = math.sqrt(sum(v * v for v in up))
        up = [v / n for v in up]
        side = [a[1] * up[2] - a[2] * up[1], a[2] * up[0] - a[0] * up[2], a[0] * up[1] - a[1] * up[0]]
        r = d / 2
        # A square of side r turned 45 deg, bottom corner on the axis: its upper sides are tangent to the hole.
        c = [(at(t[0])[i] + at(t[1])[i]) / 2 + up[i] * r / math.sqrt(2) for i in range(3)]
        obb = adsk.core.OrientedBoundingBox3D.create(
            P(*c), adsk.core.Vector3D.create(*a),
            adsk.core.Vector3D.create(*[(up[i] + side[i]) / math.sqrt(2) for i in range(3)]),
            mm(t[1] - t[0]), mm(r), mm(r))
        tools.append(add_body(comp, tm.createBox(obb)))
    return tools


def cut(comp, name, tools):
    for tool in tools:
        combine(comp, comp.bRepBodies.itemByName(name), tool, adsk.fusion.FeatureOperations.CutFeatureOperation)


def meet(p, d, q, e):
    """Intersection of the 2D lines p + s d and q + r e."""
    k = ((q[0] - p[0]) * e[1] - (q[1] - p[1]) * e[0]) / (d[0] * e[1] - d[1] * e[0])
    return (p[0] + k * d[0], p[1] + k * d[1])


def combine(comp, target, tool, op, keep=False):
    tools = adsk.core.ObjectCollection.create()
    tools.add(tool)
    ci = comp.features.combineFeatures.createInput(target, tools)
    ci.operation = op
    ci.isKeepToolBodies = keep
    comp.features.combineFeatures.add(ci)


def normal(face):
    return face.evaluator.getNormalAtPoint(face.pointOnFace)[1]


def shell(comp, body, test):
    faces = adsk.core.ObjectCollection.create()
    for face in body.faces:
        if test(normal(face)):
            faces.add(face)
    si = comp.features.shellFeatures.createInput(faces, False)
    si.insideThickness = adsk.core.ValueInput.createByReal(mm(WALL))
    comp.features.shellFeatures.add(si)


def chamfer(comp, edges, d):
    coll = adsk.core.ObjectCollection.create()
    for e in edges:
        coll.add(e)
    ci = comp.features.chamferFeatures.createInput2()
    ci.chamferEdgeSets.addEqualDistanceChamferEdgeSet(coll, adsk.core.ValueInput.createByReal(mm(d)), False)
    comp.features.chamferFeatures.add(ci)


def outer_face(body, n):
    """The body's face facing unit direction n (x, y, z) that lies furthest along it."""
    dot = lambda v: v.x * n[0] + v.y * n[1] + v.z * n[2]
    return max((face for face in body.faces if dot(normal(face)) > 0.99),
               key=lambda face: dot(face.pointOnFace.asVector()))


def build_module(comp, f, screen):
    W, zb, zt = f.W, f.z[0], f.z[1]
    fz = (0, -f.s, f.c)  # the STL's +z (toward the face) in the module
    at_z = lambda x, y, z: (x, *W(y, z))
    px = (f.x[0] - POCKET_CLEAR, f.x[1] + POCKET_CLEAR)    # pocket
    py = (f.y[0] - POCKET_CLEAR, f.y[1] + POCKET_CLEAR)
    xl, xr = px[0] - BAY, px[1] + BAY        # module width along its front edge
    hw = (xr - xl) / 2
    turn = 2 * math.asin(hw / RADIUS)
    flare = DEPTH * math.tan(turn / 2)
    ext = comp.features.extrudeFeatures
    NEW = adsk.fusion.FeatureOperations

    if screen:  # the STL as is, in a subcomponent tilted into the pocket
        m = adsk.core.Matrix3D.create()
        m.setToRotation(math.radians(TILT), adsk.core.Vector3D.create(1, 0, 0), adsk.core.Point3D.create(0, 0, 0))
        m.translation = adsk.core.Vector3D.create(0, mm(f.ty), mm(f.tz))
        case = comp.occurrences.addNewComponent(m).component
        case.name = 'Case'
        base = case.features.baseFeatures.add()
        base.startEdit()
        case.meshBodies.add(STL, adsk.fusion.MeshUnits.MillimeterMeshUnit, base)
        base.finishEdit()

    # Wedge side profile (YZ): frame's bottom back corner on the desk -> up its back -> back edge on the desk.
    (y1, z1), (y2, z2) = W(f.fy[0], zb), W(f.fy[1], zb)
    assert DEPTH > y2 + 10, 'DEPTH must reach behind the top of the case'
    side = polygon(comp, comp.yZConstructionPlane, [P(0, y1, z1), P(0, y2, z2), P(0, DEPTH, 0)])
    inp = ext.createInput(side, NEW.NewBodyFeatureOperation)
    inp.setSymmetricExtent(adsk.core.ValueInput.createByReal(mm(4 * hw)), True)
    stand = ext.add(inp).bodies.item(0)
    stand.name = 'Stand'

    # Frame: bays and caps around the case, back plane to face. Drawn past the flared sides so the
    # plan cut trims the bays' outer walls flush with the wedge's sides.
    combine(comp, stand, box(comp, f, (xl - flare, xr + flare), f.fy, f.z), NEW.JoinFeatureOperation)
    def trim(body, inset=0):  # to the plan: sides flared TURN/2 so neighbours mate flush, nothing below the desk
        i = inset / math.cos(turn / 2)
        plan = polygon(comp, comp.xYConstructionPlane,
                       [P(xl + i, 0, 0), P(xr - i, 0, 0), P(xr + flare - i, DEPTH, 0), P(xl - flare + i, DEPTH, 0)])
        inp = ext.createInput(plan, NEW.IntersectFeatureOperation)
        inp.setDistanceExtent(False, adsk.core.ValueInput.createByReal(mm(W(f.fy[1], zt + BEZEL_T)[1] + 10)))
        inp.participantBodies = [body]
        ext.add(inp)

    trim(stand)

    # Pocket, open at the face, before the shell so its floor and sides become walls.
    stand = comp.bRepBodies.itemByName('Stand')
    combine(comp, stand, box(comp, f, px, py, (zb, zt + 1)), NEW.CutFeatureOperation)
    shell(comp, comp.bRepBodies.itemByName('Stand'), lambda n: n.z < -0.99 or n.y > 0.5)  # open bottom and back

    # Right bay: open its wall against the case (the ports' side), keeping the face ring.
    stand = comp.bRepBodies.itemByName('Stand')
    combine(comp, stand, box(comp, f, (px[1] - 1, px[1] + WALL + 1), py, (zb, zt - WALL)), NEW.CutFeatureOperation)

    # Window in the pocket's floor (the largest face toward the face side).
    stand = comp.bRepBodies.itemByName('Stand')
    floor = max((face for face in stand.faces if normal(face).y < -0.8 and normal(face).z > 0.3), key=lambda face: face.area)
    pi = comp.constructionPlanes.createInput()
    pi.setByOffset(floor, adsk.core.ValueInput.createByReal(0))
    plane = comp.constructionPlanes.add(pi)
    win = polygon(comp, plane, [P(x, *W(y, zb)) for x, y in
                                [(WIN_X[0], WIN_Y[0]), (WIN_X[1], WIN_Y[0]), (WIN_X[1], WIN_Y[1]), (WIN_X[0], WIN_Y[1])]])
    inp = ext.createInput(win, NEW.CutFeatureOperation)
    inp.setSymmetricExtent(adsk.core.ValueInput.createByReal(mm(4 * WALL)), True)
    inp.participantBodies = [stand]
    ext.add(inp)

    # Holes through the floor in line with the case's back plate holes, to screw the two together.
    for hx, hy in SCREEN_HOLES:
        cut(comp, 'Stand', hole(comp, at_z(hx, hy, zb), fz, (-WALL - 1, 1), SCREEN_HOLE_D, teardrop=True))

    # Mounts: per boss, a hole through the frame's face and a tab across the bay under it (drawn past
    # the flared outer wall, trimmed flush below) with the screw's clearance hole.
    mounts = [(sx * (px[1] + BAY / 2), my) for sx in (-1, 1) for my in MOUNT_Y]
    for mx, my in mounts:
        cut(comp, 'Stand', hole(comp, at_z(mx, my, zt), fz, (-WALL - 1, 1), BOSS_D + 2 * CLEAR, teardrop=True))
        tx = (px[1], xr + flare) if mx > 0 else (xl - flare, px[0])
        tab = zt - BOSS_H
        stand = comp.bRepBodies.itemByName('Stand')
        combine(comp, stand, box(comp, f, tx, (my - TAB_L / 2, my + TAB_L / 2), (tab - TAB_T, tab)), NEW.JoinFeatureOperation)
        cut(comp, 'Stand', hole(comp, at_z(mx, my, tab), fz, (-TAB_T - 1, 1), SCREW_CLEAR, teardrop=True))

    # Back: a frame along the stand's sloped back, u up it from its foot on the desk, t inward.
    L = math.hypot(y2 - DEPTH, z2)
    a = ((y2 - DEPTH) / L, z2 / L)
    w = (-a[1], a[0])
    Q = lambda u, t: (DEPTH + u * a[0] + t * w[0], u * a[1] + t * w[1])
    side_x = lambda y: hw + y * math.tan(turn / 2) - WALL / math.cos(turn / 2)  # side wall's inner face

    # Cover insert blocks on the side walls (drawn past them, trimmed flush), inserts from the back.
    covers = []
    for sx in (-1, 1):
        for u0 in (COVER_U[0], L - COVER_U[1]):
            xi = side_x(Q(u0, WALL + BLOCK_D / 2)[0])
            bx = (xi - BLOCK, xi + WALL + 2) if sx > 0 else (-xi - WALL - 2, -xi + BLOCK)
            stand = comp.bRepBodies.itemByName('Stand')
            combine(comp, stand, frame_box(comp, Q, a, bx, (u0 - BLOCK / 2, u0 + BLOCK / 2), (WALL, WALL + BLOCK_D)),
                    NEW.JoinFeatureOperation)
            hx = sx * (xi - BLOCK / 2)
            cut(comp, 'Stand', hole(comp, (hx, *Q(u0, WALL)), (0, *w), (-1, INSERT_L), INSERT_D, mouth=INSERT_LEAD))
            covers.append((hx, u0))
    trim(comp.bRepBodies.itemByName('Stand'))

    # Join holes through the side walls, square to them; a neighbour's mirror the same ones.
    for wb, wt, wd in JOIN_HOLES:
        jy, jz = wb * y1 + wt * y2 + wd * DEPTH, wb * z1 + wt * z2
        for sx in (-1, 1):
            p = (sx * (hw + jy * math.tan(turn / 2)), jy, jz)
            cut(comp, 'Stand', hole(comp, p, (-sx * math.cos(turn / 2), math.sin(turn / 2), 0), (-1, WALL + 1), JOIN_D,
                                    teardrop=True))

    # V where neighbours meet over the frame's top, continuing the bezel's.
    top = outer_face(comp.bRepBodies.itemByName('Stand'), (0, f.c, f.s))
    chamfer(comp, [e for e in top.edges if any(abs(normal(g).x) > 0.9 for g in e.faces)], EDGE_CH)

    # Cover: an L of floor and back, WALL thick, between the floor's plane (the pocket's back) and
    # the desk and back, inset CLEAR from the sides; whatever the stand already fills is left to it.
    B, D, T = (y1, z1), (DEPTH, 0), (y2, z2)
    bt = (T[0] - B[0], T[1] - B[1])
    corner = [meet((0, WALL), (1, 0), B, bt), meet((0, WALL), (1, 0), Q(0, WALL), a), meet(Q(0, WALL), a, B, bt)]
    prof = polygon(comp, comp.yZConstructionPlane, [P(0, *p) for p in [B, D, T, corner[2], corner[1], corner[0]]])
    inp = ext.createInput(prof, NEW.NewBodyFeatureOperation)
    inp.setSymmetricExtent(adsk.core.ValueInput.createByReal(mm(4 * hw)), True)
    ext.add(inp).bodies.item(0).name = 'Cover'
    trim(comp.bRepBodies.itemByName('Cover'), WALL + CLEAR)
    combine(comp, comp.bRepBodies.itemByName('Cover'), comp.bRepBodies.itemByName('Stand'), NEW.CutFeatureOperation, keep=True)
    cuts = [frame_box(comp, Q, a, (-SLOT_W / 2, SLOT_W / 2), (SLOT_U, SLOT_U + SLOT_H), (-1, WALL + 1))]
    for i in range(VENT_N):
        vx = (i - (VENT_N - 1) / 2) * VENT_PITCH
        cuts.append(frame_box(comp, Q, a, (vx - VENT_W / 2, vx + VENT_W / 2), (VENT_U[0] * L, VENT_U[1] * L), (-1, WALL + 1)))
    for hx, u0 in covers:
        cuts += hole(comp, (hx, *Q(u0, 0)), (0, *w), (-1, WALL + 1), SCREW_CLEAR, teardrop=True,
                     mouth=(CSK_D, (CSK_D - SCREW_CLEAR) / 2))
    cut(comp, 'Cover', cuts)

    # Bezel: over the frame's face, flared like the stand, opening over the glass, bosses with insert holes.
    bezel = box(comp, f, (xl - flare, xr + flare), f.fy, (zt, zt + BEZEL_T))
    bezel.name = 'Bezel'
    trim(bezel)
    bezel = comp.bRepBodies.itemByName('Bezel')
    combine(comp, bezel, box(comp, f, OPEN_X, OPEN_Y, (zt - 1, zt + BEZEL_T + 1)), NEW.CutFeatureOperation)
    for mx, my in mounts:
        bezel = comp.bRepBodies.itemByName('Bezel')
        combine(comp, bezel, cyl(comp, f, mx, my, (zt - BOSS_H, zt + 0.01), BOSS_D), NEW.JoinFeatureOperation)
        cut(comp, 'Bezel', hole(comp, at_z(mx, my, zt - BOSS_H), fz, (-1, INSERT_L), INSERT_D, mouth=INSERT_LEAD))
    for outer, d in ((True, EDGE_CH), (False, OPEN_CH)):  # face found again each time: a chamfer remakes it
        front = outer_face(comp.bRepBodies.itemByName('Bezel'), fz)
        chamfer(comp, [e for loop in front.loops if loop.isOuter == outer for e in loop.edges], d)

    if not screen:  # blank plate: the case's box, shelled open at the back
        blank = box(comp, f, f.x, f.y, f.z)
        blank.name = 'Blank'
        shell(comp, blank, lambda n: n.y > 0.85 and n.z < -0.3)
    return turn, (xl + xr) / 2, hw


def build(new_doc=True):
    app = adsk.core.Application.get()
    if new_doc:
        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    design = adsk.fusion.Design.cast(app.activeProduct)
    design.designType = adsk.fusion.DesignTypes.ParametricDesignType
    design.designIntent = adsk.fusion.DesignIntentTypes.HybridDesignIntentType  # new designs are Parts: one component
    root = design.rootComponent

    # Build each kind once at the origin, place every copy on creation (moving an occurrence
    # afterwards doesn't stick in a parametric design), then drop the build copies.
    f = case_frame(STL)
    built, scratch = {}, []
    for kind in set(LAYOUT):
        occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
        occ.component.name = 'Screen module' if kind == 'screen' else 'Centre module'
        turn, cx, hw = build_module(occ.component, f, kind == 'screen')
        built[kind] = occ.component
        scratch.append(occ)

    # Modelled Z-up, facing -Y; stand up in a Y-up workspace (front toward +Z).
    up = adsk.core.Matrix3D.create()
    if app.preferences.generalPreferences.defaultModelingOrientation == \
            adsk.core.DefaultModelingOrientations.YUpModelingOrientation:
        up.setToRotation(-math.pi / 2, adsk.core.Vector3D.create(1, 0, 0), adsk.core.Point3D.create(0, 0, 0))
    centre = adsk.core.Point3D.create(mm(cx), -mm(math.sqrt(RADIUS ** 2 - hw ** 2)), 0)
    for i, kind in enumerate(LAYOUT):
        m = adsk.core.Matrix3D.create()
        m.setToRotation(((len(LAYOUT) - 1) / 2 - i) * turn, adsk.core.Vector3D.create(0, 0, 1), centre)
        m.transformBy(up)
        root.occurrences.addExistingComponent(built[kind], m)
    for occ in scratch:
        occ.deleteMe()
    app.activeViewport.fit()


def run(context):
    try:
        build()
    except Exception:
        adsk.core.Application.get().userInterface.messageBox(traceback.format_exc())
