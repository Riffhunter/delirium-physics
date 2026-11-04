"""Where the gap to the reference is. A cost says how far off a character is; this says what kind of wrong it is.
  diagnose.py corr <dll> <ref.json> [K=V,...]   does each measure follow the situation the way the reference's does?
      Correlation across cases: ours against the reference, the reference against itself (other seeds: the ceiling) and
      ours against itself. Means can agree while this is near 0. Then per body part hit, for the measures that lag.
  diagnose.py body <dll> <ref.json> [K=V,...]   the first moments in the body's own frame, with and without the bullet's push
      Chest rotation and head against chest, in a frame made from the shoulders and the trunk, so that characters whose
      parts have different axes can be compared. What's there without a push is the body's own reaction, not the bullet.
Both take a while: every case is replayed on each engine. CALREC_PROCS sets how many processes (default 15)."""
import collections, json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import calrec
from nmcal import conj, qmul
from rec import Rec

MEASURES = ('kick_chest', 'kick_hands', 'kick_head', 'whip', 'along', 'steps', 'toward', 'reach', 'neck', 'elbow', 'travel', 'fell')
PARTS = ['pelvis', 'thighL', 'shinL', 'footL', 'thighR', 'shinR', 'footR', 'spine0', 'spine1', 'spine2', 'chest', 'neck', 'head',
         'clavL', 'upperarmL', 'forearmL', 'handL', 'clavR', 'upperarmR', 'forearmR', 'handR']
PELVIS, CHEST, HEAD, SHOULDER_L, SHOULDER_R = 0, 10, 12, 14, 18


def corr(a, b):
    n = len(a); ma, mb = sum(a) / n, sum(b) / n
    c = sum((x - ma) * (y - mb) for x, y in zip(a, b)); va = sum((x - ma) ** 2 for x in a); vb = sum((y - mb) ** 2 for y in b)
    return c / math.sqrt(va * vb) if va and vb else 0.0


def pairs(a, b, names, k):
    return [(a[n][k], b[n][k]) for n in names if a[n].get(k) is not None and b[n].get(k) is not None
            and not math.isnan(a[n][k]) and not math.isnan(b[n][k])]


def table(a, b, names):
    out = {}
    for k in MEASURES:
        p = pairs(a, b, names, k)
        out[k] = corr(*zip(*p)) if len(p) > 2 else 0.0
    p = [(a[n]['arms'][j][i], b[n]['arms'][j][i]) for n in names if a[n].get('arms') and b[n].get('arms') for j in calrec.ARMS for i in range(3)]
    out['arms'] = corr(*zip(*p)) if p else 0.0
    return out


def by_name(res): return {os.path.basename(c['path']): c for c in res if c.get('info')}


def clean_env():
    for k in ('NM_KV_TRY', 'NM_CHARFILE', 'REPLAY_FORCE', 'REPLAY_DYING_STIFF', 'REPLAY_GTA_PUSH'): os.environ.pop(k, None)


def cmd_corr(dll, ref, paths, tries):
    names = list(ref)
    clean_env()
    gta = by_name(calrec.run_cases(dll, paths, 'gta', calrec.K_OURS, seed0=900))
    os.environ.update(calrec.env_for(tries))
    own = by_name(calrec.run_cases(dll, paths, 'own', calrec.K_OURS))
    own2 = by_name(calrec.run_cases(dll, paths, 'own', calrec.K_OURS, seed0=900))
    names = [n for n in names if n in gta and n in own and n in own2]
    print('cost: ours', round(calrec.cost(ref, own, names)[0], 3), ' the reference against itself', round(calrec.cost(ref, gta, names)[0], 3), f' ({len(names)} cases)')
    rows = dict(ours_vs_ref=table(own, ref, names), ref_vs_ref=table(gta, ref, names), ours_vs_ours=table(own, own2, names))
    print(f"{'':12}" + ''.join(f'{r:>14}' for r in rows))
    for k in rows['ours_vs_ref']: print(f'{k:12}' + ''.join(f'{rows[r][k]:14.2f}' for r in rows))
    lag = sorted(MEASURES, key=lambda k: rows['ours_vs_ref'][k] - rows['ref_vs_ref'][k])[:2]
    for k in lag:
        print(f'--- {k} by part hit: cases, reference mean, our mean, r')
        g = collections.defaultdict(list)
        for n in names: g[ref[n]['info']['part']].append(n)
        for part, v in sorted(g.items()):
            p = pairs(ref, own, v, k)
            if len(p) < 3: continue
            a, b = zip(*p)
            print(f'{PARTS[part]:10} {len(p):4} {sum(a) / len(a):9.3f} {sum(b) / len(b):9.3f} {corr(a, b):+6.2f}')


# ---- the body's own frame

def sub(a, b): return [a[i] - b[i] for i in range(3)]
def dot(a, b): return sum(a[i] * b[i] for i in range(3))
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def norm(a): n = math.sqrt(dot(a, a)) or 1.0; return [x / n for x in a]


def body_frame(f):
    n = f['nm']
    up = norm(sub(n[CHEST][:3], n[PELVIS][:3])); right = norm(sub(n[SHOULDER_R][:3], n[SHOULDER_L][:3]))
    forward = norm(cross(up, right))
    return norm(cross(forward, up)), forward, up


def rotation_vector(q0, q1):
    q = qmul(tuple(q1), conj(tuple(q0)))
    if q[3] < 0: q = tuple(-x for x in q)
    n = math.hypot(q[0], q[1], q[2])
    return [q[i] / n * math.degrees(2 * math.atan2(n, q[3])) for i in range(3)] if n > 1e-9 else [0.0] * 3


def _body_case(job):
    dll, path, engine, env = job
    os.environ.update(env)
    if not calrec.case_info(path): return None
    out = collections.defaultdict(lambda: [0.0, 0.0, 0.0]); k = 0
    for fr in calrec.case_runs(dll, path, engine, 2):
        if len(fr) < 15: continue
        f0 = fr[0]; k += 1; axes = body_frame(f0); rel0 = sub(f0['nm'][HEAD][:3], f0['nm'][CHEST][:3])
        for t in (0.1, 0.2, 0.4):
            f = calrec.at(fr, t)
            rel = sub(sub(f['nm'][HEAD][:3], f['nm'][CHEST][:3]), rel0); rv = rotation_vector(f0['nm'][CHEST][3:7], f['nm'][CHEST][3:7])
            for i, ax in enumerate(axes):
                out[f'head {t}'][i] += dot(rel, ax); out[f'chest {t}'][i] += dot(rv, ax)
    return dict(alive=Rec(path).alive, v={m: [x / k for x in a] for m, a in out.items()}) if k else None


def cmd_body(dll, paths, tries):
    import concurrent.futures as cf
    clean_env()
    env = calrec.env_for(tries)
    own_env = {k: env[k] for k in ('NM_KV_TRY', 'NM_CHARFILE', 'REPLAY_DYING_STIFF') if k in env}
    force = float(tries.get('force', 1.0))
    runs = (('reference, no push', 'gta', dict(REPLAY_GTA_PUSH='0.0001')), ('ours, no push', 'own', dict(own_env, REPLAY_FORCE='0.0001')),
            ('reference', 'gta', dict(REPLAY_GTA_PUSH='1')), ('ours', 'own', dict(own_env, REPLAY_FORCE=str(force))))
    print('Means over cases. chest: rotation in degrees about right, forward, up (about right: - leans forward, + back). head: head against chest in metres: right, forward, up.')
    for name, engine, e in runs:
        with cf.ProcessPoolExecutor(int(os.environ.get('CALREC_PROCS', '15'))) as ex:
            rows = [r for r in ex.map(_body_case, [(dll, p, engine, e) for p in paths]) if r]
        for label, sel in (('on their feet', [r for r in rows if r['alive']]), ('dying', [r for r in rows if not r['alive']])):
            if not sel: continue
            print(f'{name}, {label} ({len(sel)})')
            for m in sel[0]['v']:
                print(f'   {m:10}' + ' '.join(f"{sum(r['v'][m][i] for r in sel) / len(sel):+9.3f}" for i in range(3)))


if __name__ == '__main__':
    cmd, dll, ref_path = sys.argv[1:4]
    ref = json.load(open(ref_path)); paths = [ref[n]['path'] for n in ref]
    tries = calrec.parse(sys.argv[4] if len(sys.argv) > 4 else '')
    if cmd == 'corr': cmd_corr(dll, ref, paths, tries)
    elif cmd == 'body': cmd_body(dll, paths, tries)
    else: sys.exit(__doc__)
