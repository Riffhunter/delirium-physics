"""RoNDelirium's own character for the Delirium fork (no game's character file): writes character_own.txt.

The proportions come from Ready or Not's own skeleton (a suspect's bones, logged in game: ron_bones.txt), the
segment masses from de Leva's (1996) adjustments of Zatsiorsky's tables for an 80 kg man, the shapes from Ready or
Not's physics asset, the joint ranges from anatomy, the muscles our own. Standing straight, arms hanging, facing -Y,
left +X, up +Z (Delirium's body space), metres, pelvis at the origin.

usage: make_character.py <ron_bones.txt> <out character_own.txt>"""
import math, sys

bones = {}
for line in open(sys.argv[1]):
    w = line.split()
    if len(w) >= 4 and w[1] == 'pos':
        bones[w[0]] = (float(w[2]) / 100, -float(w[3]) / 100, float(w[4]) / 100)  # UE cm, left-handed -> m, right-handed

def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def add(a, b): return tuple(x + y for x, y in zip(a, b))
def mul(a, s): return tuple(x * s for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a): l = math.sqrt(dot(a, a)); return mul(a, 1 / l)
def length(a): return math.sqrt(dot(a, a))

# the suspect's own frame: left across the hips, up, and back = up x left
up = (0, 0, 1)
left = sub(bones['thigh_LE'], bones['thigh_RI']); left = norm(sub(left, mul(up, dot(left, up))))
back = cross(up, left)
o = bones['pelvis']
def body(name):
    p = sub(bones[name], o)
    p = (dot(p, left), dot(p, back), dot(p, up))
    # left and right as one: the logged pose holds a gun, so its two sides differ
    pair = name[:-2] + ('RI' if name.endswith('LE') else 'LE') if name[-2:] in ('LE', 'RI') else None
    if pair:
        q = sub(bones[pair], o); q = (dot(q, left), dot(q, back), dot(q, up))
        p = ((abs(p[0]) + abs(q[0])) / 2 * (1 if p[0] > 0 else -1), (p[1] + q[1]) / 2, (p[2] + q[2]) / 2)
    return p
def seg(a, b):
    other = lambda n: n[:-2] + ('RI' if n.endswith('LE') else 'LE')
    return (length(sub(bones[a], bones[b])) + length(sub(bones[other(a)], bones[other(b)]))) / 2

# standing straight: legs under the hips, arms hanging at the sides (lengths from the suspect's own bones)
P = {}
P['pelvis'] = (0.0, 0.0, 0.0)
for s, side in (('LE', 1), ('RI', -1)):
    hip = body('thigh_' + s)
    hip = (side * abs(hip[0]), 0.0, hip[2])
    # the legs' rest: the thigh forward by HIP_FLEX, the knee bent by KNEE_BEND degrees (standing easy, not locked straight:
    # the rest is the muscles' zero, and locked straight the legs stepped stiff and hopped)
    import os
    hf, kb = (math.radians(float(os.environ.get(k, d))) for k, d in (('HIP_FLEX', 3), ('KNEE_BEND', 6)))
    # and apart: each leg out by LEG_ABD degrees (a stance as wide as the shoulders, not the legs hanging together)
    la = math.radians(float(os.environ.get('LEG_ABD', 0)))
    leg_dir = lambda a: (side * math.sin(la) * math.cos(a), -math.sin(a) * math.cos(la), -math.cos(a) * math.cos(la))
    knee = add(hip, mul(leg_dir(hf), seg('thigh_' + s, 'calf_' + s)))
    ankle = add(knee, mul(leg_dir(hf - kb), seg('calf_' + s, 'foot_' + s)))
    P['hip_' + s], P['knee_' + s], P['ankle_' + s] = hip, knee, ankle
    clav = body('clavicle_' + s); clav = (side * abs(clav[0]), 0.0, clav[2])
    shoulder = body('upperarm_' + s); shoulder = (side * abs(shoulder[0]), 0.01, shoulder[2])
    # the arms' rest: down, raised forward by ARM_FLEX and out by ARM_ABD degrees, the elbow bent forward by ELBOW_BEND
    import os
    fl, ab, eb = (math.radians(float(os.environ.get(k, d))) for k, d in (('ARM_FLEX', 10), ('ARM_ABD', 8), ('ELBOW_BEND', 25)))
    def arm_dir(flex, abd):
        return (side * math.sin(abd) * math.cos(flex), -math.sin(flex), -math.cos(abd) * math.cos(flex))
    elbow = add(shoulder, mul(arm_dir(fl, ab), seg('upperarm_' + s, 'lowerarm_' + s)))
    wrist = add(elbow, mul(arm_dir(fl + eb, ab), seg('lowerarm_' + s, 'hand_' + s)))
    P['clav_' + s], P['shoulder_' + s], P['elbow_' + s], P['wrist_' + s] = clav, shoulder, elbow, wrist
    P['handend_' + s] = add(wrist, mul(arm_dir(fl + eb, ab), 0.18))
for b in ('spine_1', 'spine_2', 'spine_3', 'neck_1', 'head'):
    p = body(b); P[b] = (0.0, p[1], p[2])
P['spine_1b'] = mul(add(P['spine_1'], P['spine_2']), 0.5)
P['head_top'] = add(P['head'], (0, -0.02, 0.21))

X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)
nX, nY, nZ = (-1, 0, 0), (0, -1, 0), (0, 0, -1)
# name, parent, mass kg, shape, pivot, end, the part frame's x and y (y along the bone, towards the parent),
# joint kind (physics / NM), the joint's ranges (rad) about the bone frame's x (lateral), y (forward x bone) and its twist
parts = [
    # name        parent mass shape                pivot        end          frame x, y          joint  ranges (x, y, twist)       muscle s, max torque
    ('Pelvis',     -1, 11.2, ('c', 0.098, 0.082),  'pelvis',    None,        (Z, X),             'root', None,                       0, 0),
    ('L_Thigh',     0, 11.3, ('c', 0.080, None),   'hip_LE',    'knee_LE',   (nX, None),         'ball', ((-1.9, 0.4), (-0.3, 0.8), (-0.5, 0.5)), 10.5, 300),
    ('L_Calf',      1, 3.5,  ('c', 0.057, None),   'knee_LE',   'ankle_LE',  (nX, None),         'hinge', ((0.0, 2.4),),             10.5, 250),
    ('L_Foot',      2, 1.1,  ('b', (0.045, 0.045, 0.12)), 'ankle_LE', None,  (nX, Z),            'lock', ((-0.6, 0.5), (-0.3, 0.3), (-0.2, 0.2)), 11, 150),
    ('R_Thigh',     0, 11.3, ('c', 0.080, None),   'hip_RI',    'knee_RI',   (nX, None),         'ball', ((-1.9, 0.4), (-0.8, 0.3), (-0.5, 0.5)), 10.5, 300),
    ('R_Calf',      4, 3.5,  ('c', 0.057, None),   'knee_RI',   'ankle_RI',  (nX, None),         'hinge', ((0.0, 2.4),),             10.5, 250),
    ('R_Foot',      5, 1.1,  ('b', (0.045, 0.045, 0.12)), 'ankle_RI', None,  (nX, Z),            'lock', ((-0.6, 0.5), (-0.3, 0.3), (-0.2, 0.2)), 11, 150),
    ('Spine0',      0, 4.4,  ('c', 0.075, 0.075),  'spine_1',   'spine_1b',  (Z, X),             'ball', ((-0.3, 0.4), (-0.25, 0.25), (-0.3, 0.3)), 11, 250),
    ('Spine1',      7, 4.4,  ('c', 0.085, 0.050),  'spine_1b',  'spine_2',   (Z, X),             'ball', ((-0.3, 0.4), (-0.25, 0.25), (-0.3, 0.3)), 11, 250),
    ('Spine2',      8, 4.6,  ('c', 0.095, 0.045),  'spine_2',   'spine_3',   (Z, X),             'lock', ((-0.2, 0.3), (-0.2, 0.2), (-0.2, 0.2)), 11, 250),
    ('Spine3',      9, 9.0,  ('c', 0.100, 0.040),  'spine_3',   'neck_1',    (Z, X),             'ball', ((-0.3, 0.4), (-0.25, 0.25), (-0.4, 0.4)), 11, 250),
    ('Neck',       10, 0.9,  ('c', 0.055, None),   'neck_1',    'head',      (nX, None),         'lock', ((-0.4, 0.5), (-0.3, 0.3), (-0.6, 0.6)), 9, 60),
    ('Head',       11, 5.0,  ('c', 0.095, None),   'head',      'head_top',  (nX, None),         'ball', ((-0.6, 0.7), (-0.4, 0.4), (-1.0, 1.0)), 9, 60),
    ('L_Clavicle', 10, 1.0,  ('c', 0.045, None),   'clav_LE',   'shoulder_LE', (Z, None),        'lock', ((-0.3, 0.3), (-0.3, 0.3), (-0.2, 0.2)), 10, 100),
    ('L_UpperArm', 13, 2.2,  ('c', 0.055, None),   'shoulder_LE', 'elbow_LE', (Y, None),         'ball', ((-1.7, 1.7), (-1.65, 1.65), (-1.5, 1.5)), 10, 120),
    ('L_Forearm',  14, 1.3,  ('c', 0.040, None),   'elbow_LE',  'wrist_LE',  (Y, None),          'hinge', ((-2.5, 0.0),),            9, 100),
    ('L_Hand',     15, 0.5,  ('b', (0.026, 0.09, 0.05)), 'wrist_LE', 'handend_LE', (nX, None),            'ball', ((-1.0, 1.0), (-0.4, 0.4), (-0.8, 0.8)), 7, 40),
    ('R_Clavicle', 10, 1.0,  ('c', 0.045, None),   'clav_RI',   'shoulder_RI', (Z, None),        'lock', ((-0.3, 0.3), (-0.3, 0.3), (-0.2, 0.2)), 10, 100),
    ('R_UpperArm', 17, 2.2,  ('c', 0.055, None),   'shoulder_RI', 'elbow_RI', (nY, None),        'ball', ((-1.7, 1.7), (-1.65, 1.65), (-1.5, 1.5)), 10, 120),
    ('R_Forearm',  18, 1.3,  ('c', 0.040, None),   'elbow_RI',  'wrist_RI',  (nY, None),         'hinge', ((-2.5, 0.0),),            9, 100),
    ('R_Hand',     19, 0.5,  ('b', (0.05, 0.09, 0.026)), 'wrist_RI', 'handend_RI', (nY, None),            'ball', ((-1.0, 1.0), (-0.4, 0.4), (-0.8, 0.8)), 7, 40),
]

def frame(x_ref, y_dir):
    y = norm(y_dir)
    x = norm(sub(x_ref, mul(y, dot(x_ref, y))))
    return x, y, cross(x, y)

out = ['# RoNDelirium own character (tools/make_character.py): no game file. One part a line:',
       '# name parent mass  kind(c radius half | b ex ey ez)  centre(3)  frame x(3) y(3) z(3)  pivot(3)  bone z(3) bone x(3)',
       '#   joint(root|ball|hinge|lock)  ranges x lo hi  y lo hi  twist lo hi  muscle s  max torque']
for name, parent, mass, shape, piv, end, (fx, fy), kind, ranges, s, torque in parts:
    pivot = P[piv]
    if name in ('L_Foot', 'R_Foot'):
        centre = add(pivot, (0, -0.07, -0.045))
    elif end is None:
        centre = pivot
    else:
        centre = mul(add(pivot, P[end]), 0.5)
    # the frame: y along the bone towards the parent (limbs, neck, head), or as given (pelvis, spine: across the body)
    if fy is None:
        x, y, zz = frame(fx, sub(pivot, P[end]))
    else:
        x, y, zz = frame(fx, fy)
    if shape[0] == 'c':
        r, half = shape[1], shape[2]
        if half is None:
            half = max(length(sub(P[end], pivot)) / 2 - r * 0.5, 0.01)
        sh = f'c {r} {half:.4f}'
    else:
        sh = 'b ' + ' '.join(str(e) for e in shape[1])
    # the bone frame for the joint: z away from the parent along the bone (up for the spine), x lateral
    bz = (0, 0, 1) if name.startswith('Spine') or name in ('Pelvis', 'Head', 'Neck') else norm(sub(P[end], pivot)) if end else (0, 0, -1)
    if name in ('L_Clavicle', 'R_Clavicle'):
        bz = norm(sub(P[end], pivot))
    bx = norm(sub((1, 0, 0), mul(bz, dot((1, 0, 0), bz)))) if abs(bz[0]) < 0.9 else norm(sub((0, -1, 0), mul(bz, dot((0, -1, 0), bz))))
    rg = ' '.join(f'{lo} {hi}' for lo, hi in (ranges or ())) + ' 0 0' * (3 - len(ranges or ()))
    f3 = lambda v: ' '.join(f'{c:.5f}' for c in v)
    out.append(f'Own_{name} {parent} {mass} {sh}  {f3(centre)}  {f3(x)} {f3(y)} {f3(zz)}  {f3(pivot)}  {f3(bz)} {f3(bx)}  {kind} {rg}  {s} {torque}')
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
# the joints (each NM part's pivot with its parent) for make_ron_character (the Skyrim Delirium method, GPL-3)
NM = ["Buttocks", "Thigh_Left", "Shin_Left", "Foot_Left", "Thigh_Right", "Shin_Right", "Foot_Right", "Spine0", "Spine1", "Spine2",
      "Spine3", "Neck", "Head", "Clavicle_Left", "Upper_Arm_Left", "Lower_Arm_Left", "Hand_Left", "Clavicle_Right", "Upper_Arm_Right",
      "Lower_Arm_Right", "Hand_Right"]
PIV = ['pelvis', 'hip_LE', 'knee_LE', 'ankle_LE', 'hip_RI', 'knee_RI', 'ankle_RI', 'spine_1', 'spine_1b', 'spine_2', 'spine_3', 'neck_1', 'head',
       'clav_LE', 'shoulder_LE', 'elbow_LE', 'wrist_LE', 'clav_RI', 'shoulder_RI', 'elbow_RI', 'wrist_RI']
if len(sys.argv) > 3:
    import json
    json.dump({'joints': {n: list(P[k]) for n, k in zip(NM, PIV)}}, open(sys.argv[3], 'w'), indent=1)
print('\n'.join(out[3:]))
print('total mass', sum(p[2] for p in parts), 'kg; head top', P['head_top'][2], 'feet', P['ankle_LE'][2])
