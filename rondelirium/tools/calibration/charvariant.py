"""A variant of the character (nm-character-1 json) for calibrating its arms: each joint's effector zero is the middle of
its limits, so shifting a range moves where the arm rests and is driven from. Symmetric: the right side's flipped axes
(flip [_, true, true]) take the opposite shift.
usage: charvariant.py <in.json> <out.json> [key=value ...]
  sh1, sh2, sht   shoulder lean1 / lean2 / twist range shifted (rad)
  cl1, cl2        clavicle lean1 / lean2 shifted (rad)
  el              elbow range shifted (rad)
  arm             arm muscle strength scale (clavicle, shoulder, elbow, wrist; damping with its square root)
  shw             shoulder range widened by this factor about its middle
  nocross, nochest  self-collision pairs dropped (see below)
  rc, ru, rf, rh  clavicle / upper arm / forearm / hand roll about its bone (rad, the right side mirrored)
  rt, rs, rp      thigh / shin / foot roll; rn, rk neck / head roll
  w_<joint>       those joints' ranges widened about their middle; m_, mb_, mp_<group> masses; jr_<group>_<a|b|t> joint axes
                  turned; cs_<group> part centres moved along the bone; lim_<joint>_<axis> ranges moved (see the comments below)"""
import json, sys

src, dst = sys.argv[1], sys.argv[2]
p = {k: float(v) for k, v in (a.split('=') for a in sys.argv[3:])}
c = json.load(open(src, encoding='utf-8'))
for j in c['joints']:
    name = j['name']; right = name.endswith('_right')
    sign = lambda axis: -1.0 if (right and j['flip'][axis]) else 1.0
    def shift(axis, d):
        lo, hi = j['limits'][axis]; d *= sign(axis); j['limits'][axis] = [lo + d, hi + d]
    if name.startswith('shoulder'):
        shift(0, p.get('sh1', 0)); shift(1, p.get('sh2', 0)); shift(2, p.get('sht', 0))
        w = p.get('shw', 1.0)
        if w != 1.0:
            for axis in range(3):
                lo, hi = j['limits'][axis]; m = (lo + hi) / 2; h = (hi - lo) / 2 * w; j['limits'][axis] = [m - h, m + h]
    for axis, key in ((0, 'fl1'), (1, 'fl2'), (2, 'flt')):  # an axis's sense reversed: a target "forward" goes the other way
        if p.get(key, 0) and (name.startswith('shoulder') or (p.get('flcl', 0) and name.startswith('clavicle'))):
            lo, hi = j['limits'][axis]; j['limits'][axis] = [-hi, -lo]; j['flip'][axis] = not j['flip'][axis]
    if name.startswith('clavicle'):
        shift(0, p.get('cl1', 0)); shift(1, p.get('cl2', 0))
    if name.startswith('elbow'):
        shift(0, p.get('el', 0))
    if any(name.startswith(s) for s in ('clavicle', 'shoulder', 'elbow', 'wrist')) and 'arm' in p:
        j['strength'] *= p['arm']; j['damping'] *= p['arm'] ** 0.5
# Self-collision: nocross=1 drops the arm-against-arm pairs (both hands on a gun touch), nochest=1 the forearms' and
# hands' against the chest (Spine2, Spine3): either set apart the pose a held gun leaves them in, at once.
names = [q['name'] for q in c['parts']]
def nm(i): return names[i] if isinstance(i, int) else i
def keep(pr):
    a, b = (nm(pr[0]), nm(pr[1])) if isinstance(pr, list) else (nm(pr['a']), nm(pr['b']))
    arm = lambda x: any(t in x for t in ('Lower_Arm', 'Hand'))
    if p.get('nocross') and arm(a) and arm(b): return False
    if p.get('nochest') and ((arm(a) and b in ('Spine2', 'Spine3')) or (arm(b) and a in ('Spine2', 'Spine3'))): return False
    return True
c['self_collision'] = [pr for pr in c['self_collision'] if keep(pr)]
# Roll: a build part turned about its own bone (the axis its anchor lies on) by this angle (rad), the right side the
# other way. The fit lines each part up with its bone but keeps its roll, and the joints' axes go with the part: a
# limb's roll sets the plane its joints bend in and where their ranges sit.
import math
ROLL = dict(rc=(13, 17), ru=(14, 18), rf=(15, 19), rh=(16, 20), rt=(1, 4), rs=(2, 5), rp=(3, 6), rn=(11, None), rk=(12, None))
def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]
for key, sides in ROLL.items():
    th = p.get(key, 0.0)
    if not th: continue
    for side, k in zip((1.0, -1.0), sides):
        if k is None: continue
        b = c['build'][k]; a = b['anchor']; n = math.sqrt(sum(x * x for x in a)) or 1.0
        h = side * th / 2; r = [a[0] / n * math.sin(h), a[1] / n * math.sin(h), a[2] / n * math.sin(h), math.cos(h)]
        b['frame']['rot'] = qmul(b['frame']['rot'], r); b['bind'] = list(b['frame']['rot'])
# w_<joint name prefix>: those joints' ranges widened by this factor about their middle (spine, neck, hip, knee,
# ankle, elbow, wrist, clavicle). m_<group>: that group's masses scaled (arms, head, trunk, legs, pelvis).
MASS = dict(arms=range(13, 21), head=(11, 12), trunk=range(7, 11), legs=range(1, 7), pelvis=(0,))
for key, f in p.items():
    if key.startswith('w_') and f != 1.0:
        for j in c['joints']:
            if j['name'].startswith(key[2:]):
                j['limits'] = [[(lo + hi) / 2 - (hi - lo) / 2 * f, (lo + hi) / 2 + (hi - lo) / 2 * f] if hi > lo else [lo, hi] for lo, hi in j['limits']]
    if key.startswith('m_') and f != 1.0:
        for k in MASS[key[2:]]:
            c['parts'][k]['mass'] *= f; c['build'][k]['mass'] *= f
    # mb_<group>: only the physical bodies' masses (what's simulated); mp_<group>: only the NM model's (what the
    # behaviours plan with). 'all' for every part.
    for pre, where in (('mb_', 'build'), ('mp_', 'parts')):
        if key.startswith(pre) and f != 1.0:
            for k in (range(21) if key[3:] == 'all' else MASS[key[3:]]):
                c[where][k]['mass'] *= f
# jr_<group>_<a|b|t>: each of that group's joint frames (twist and lean axes together, its ranges with them) turned
# by this angle (rad) about its own lean axis (a), its other one (b) or its twist (t); the right side mirrored.
# Groups: shoulder, elbow, clavicle, wrist, hip, knee, ankle, spine, neck.
def rot(v, u, th):
    cu = math.cos(th); su = math.sin(th); d = sum(x * y for x, y in zip(u, v))
    cr = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    return [v[i] * cu + cr[i] * su + u[i] * d * (1 - cu) for i in range(3)]
def unit(v): n = math.sqrt(sum(x * x for x in v)) or 1.0; return [x / n for x in v]
def axes(j):
    t = unit(j['twist_axis']); l = j['lean_axis']; l = unit([l[i] - t[i] * sum(a * b for a, b in zip(l, t)) for i in range(3)])
    return dict(t=t, a=l, b=[t[1] * l[2] - t[2] * l[1], t[2] * l[0] - t[0] * l[2], t[0] * l[1] - t[1] * l[0]])
for key, th in p.items():
    if not key.startswith('jr_') or not th: continue
    group, ax = key[3:].rsplit('_', 1)
    left = next((j for j in c['joints'] if j['name'].startswith(group) and not j['name'].endswith('_right')), None)
    uL = axes(left)[ax] if left else None
    for j in c['joints']:
        if not j['name'].startswith(group): continue
        u = axes(j)[ax]; sgn = 1.0
        if j['name'].endswith('_right') and uL:
            sgn = 1.0 if sum(x * y for x, y in zip(u, [uL[0], -uL[1], -uL[2]])) >= 0 else -1.0
        tw = rot(j['twist_axis'], u, sgn * th); le = rot(j['lean_axis'], u, sgn * th)
        j['twist_axis'], j['lean_axis'] = tw, le
# s_<joint prefix>, d_<joint prefix>, ms_<joint prefix>: those joints' strength, damping and muscle stiffness scaled.
for key, f in p.items():
    for pre, field in (('s_', 'strength'), ('d_', 'damping'), ('ms_', 'muscle_stiffness')):
        if key.startswith(pre) and f != 1.0:
            for j in c['joints']:
                if j['name'].startswith(key[len(pre):]): j[field] *= f
# lim_<joint prefix>_<axis>: those joints' range moved along that axis (rad), the right side's flipped axes the other way.
for key, d in p.items():
    if key.startswith('lim_') and d:
        group, axis = key[4:].rsplit('_', 1); axis = int(axis)
        for j in c['joints']:
            if j['name'].startswith(group):
                sg = -1.0 if (j['name'].endswith('_right') and j['flip'][axis]) else 1.0
                j['limits'][axis] = [j['limits'][axis][0] + d * sg, j['limits'][axis][1] + d * sg]
# cs_<group>: that group's parts' centres (where their mass sits, and their bodies) moved along the line from the
# part's own joint: their distance from it times this. Groups: spine (spine0-2), chest (spine3), neck, head, legs
# (thighs, shins), feet, arms (upper arms, forearms), hands.
CENTRE = dict(spine=(7, 8, 9), chest=(10,), neck=(11,), head=(12,), legs=(1, 2, 4, 5), feet=(3, 6), arms=(14, 15, 18, 19), hands=(16, 20))
for key, f in p.items():
    if key.startswith('cs_') and f != 1.0:
        for k in CENTRE[key[3:]]:
            piv = c['joints'][k - 1]['pos']; cen = c['parts'][k]['frame'][3]
            c['parts'][k]['frame'][3] = [piv[i] + (cen[i] - piv[i]) * f for i in range(3)]
json.dump(c, open(dst, 'w', encoding='utf-8'))
