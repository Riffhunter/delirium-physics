"""Calibration on the game's own cases: our Delirium against GTA IV's on the recordings, each case replayed several
times (jittered: one run proves little) on each. v2 (wide calibration): what's compared is what the player sees:
  arms      hands and elbows in the chest's frame while on their feet
  kick      the first tenth of a second: chest turn, hands and head thrown
  whip      the head against the chest in the first 0.2 s
  along     the pelvis along the bullet after 1 s (+ away from the shooter)
  steps     steps taken, and those towards the shooter
  reach     the closest hand to the wound in the first 1.5 s
  neck      the hands' distance to the neck after 1.5 s (hands at the neck: the dying curled up)
  elbow     the elbows' bend while on their feet
  fall      falls, when, how far they go
Both engines' states are rebuilt from the game's bodies (sync lines) for every recording, whichever engine made it.
CALIBRATION.md explains the method and the order to run these in. RON_RECORDINGS: the recordings folder, if none is given.
  calrec.py ref <dll> <out.json> [recordings...]     GTA IV's side, once (from your own copy; keep the file to yourself)
  calrec.py self <dll> <ref.json>                     GTA IV against itself with other jitter seeds: the noise floor
  calrec.py eval <dll> <ref.json> [K=V,...]           our side with the tries: prints the cost and its parts
  calrec.py sens <dll> <ref.json> <kv_own.txt> [char keys...]  each value down and up alone, the cost's change
  calrec.py parts <dll> <ref.json> [K=V,...]          the same, one line, readable
  calrec.py descend <dll> <ref.json> <kv_own.txt> <keys,...> [start K=V,...]  coordinate descent on those keys
  calrec.py cma <dll> <ref.json> <kv_own.txt> <keys,...> <start K=V,...> <evals> <log>  many keys at once (needs numpy)
Tries: a number is a kv slot (NM_KV_TRY), 'dying' the dying body's stiffness (the mod's dying_stiffness), 'force' the
mod's push_scale, any other name a character variant key (charvariant.py) on the base character. CALREC_SET=train|val|all (default all): the
cases with an even or odd index in the ref, so one half fits and the other checks."""
import concurrent.futures as cf, json, math, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nmcal import rotate, conj, qmul

GAME = os.environ.get('RON_RECORDINGS', '')
HERE = os.path.dirname(os.path.abspath(__file__))
BASE_CHAR = os.path.join(HERE, '..', '..', '..', 'crates', 'delirium-sdk', 'src', 'character_ron.json')
ARMS = dict(l_hand=16, r_hand=20, l_elbow=15, r_elbow=19)
PELVIS, LFOOT, RFOOT, CHEST, NECK, HEAD = 0, 3, 6, 10, 11, 12
K_OURS, K_GTA, JIT = 4, 8, 1e-4
DYING = 6.0  # the mod's dying_stiffness (delirium.ini) unless tried
ADDITIVE = dict(rc=0.4, ru=0.4, rf=0.4, rh=0.4, rt=0.2, rs=0.2, rp=0.2, rn=0.2, rk=0.2, sh1=0.1, sh2=0.1, sht=0.1, cl1=0.1, cl2=0.1, el=0.1)
SCALE = dict(arms=0.10, kick_chest=10.0, kick_hands=0.15, kick_head=0.10, whip=0.10, along=0.30, steps=2.0, toward=1.0,
             reach=0.15, neck=0.15, elbow=20.0, fall=0.5, when=0.5, travel=0.5)


def at(fr, t):
    return next((f for f in fr if f['t'] - fr[0]['t'] >= t), fr[-1])


def in_chest(f, k):
    c = f['nm'][CHEST]
    return rotate(conj(tuple(c[3:7])), tuple(f['nm'][k][j] - c[j] for j in range(3)))


def angle(q0, q1):
    dq = qmul(conj(tuple(q0)), tuple(q1))
    return 2 * math.degrees(math.acos(min(1.0, abs(dq[3]))))


def run_features(fr, info):
    """One run of a case in numbers (None: not measurable in this run)."""
    d, part = info['dir'], info['part']
    n2 = math.hypot(d[0], d[1]) or 1.0; u = (d[0] / n2, d[1] / n2)
    f0 = fr[0]; t0 = f0['t']
    o = {}
    arms = {k: [0.0, 0.0, 0.0] for k in ARMS}; n = 0; elbow = 0.0
    for f in fr:
        if f['fallen']: continue
        for name, k in ARMS.items():
            v = in_chest(f, k)
            for j in range(3): arms[name][j] += v[j]
        for up, fo, h in ((14, 15, 16), (18, 19, 20)):
            a = [f['nm'][fo][j] - f['nm'][up][j] for j in range(3)]; b = [f['nm'][h][j] - f['nm'][fo][j] for j in range(3)]
            na, nb = math.hypot(*a), math.hypot(*b)
            if na > 1e-6 and nb > 1e-6: elbow += math.degrees(math.acos(max(-1, min(1, sum(x * y for x, y in zip(a, b)) / na / nb)))) / 2
        n += 1
    o['arms'] = {k: [v / n for v in a] for k, a in arms.items()} if n > 3 else None
    o['elbow'] = elbow / n if n > 3 else None
    o['standing'] = n
    f1 = at(fr, 0.1)
    o['kick_chest'] = angle(f0['nm'][CHEST][3:7], f1['nm'][CHEST][3:7])
    o['kick_hands'] = (math.dist(f0['nm'][16][:3], f1['nm'][16][:3]) + math.dist(f0['nm'][20][:3], f1['nm'][20][:3])) / 2
    o['kick_head'] = math.dist(f0['nm'][HEAD][:3], f1['nm'][HEAD][:3])
    o['whip'] = max(math.dist(in_chest(f0, HEAD), in_chest(f, HEAD)) for f in fr if f['t'] - t0 <= 0.2)
    fe = at(fr, 1.0)
    o['along'] = (fe['nm'][PELVIS][0] - f0['nm'][PELVIS][0]) * u[0] + (fe['nm'][PELVIS][1] - f0['nm'][PELVIS][1]) * u[1]
    steps = toward = 0
    for k in (LFOOT, RFOOT):
        up = False; lift = None
        for f in fr:
            if f['fallen']: break
            high = f['nm'][k][2] - f['floor'] > 0.12
            if high and not up: steps += 1; lift = f['nm'][k][:2]
            if up and not high and lift:
                if (f['nm'][k][0] - lift[0]) * u[0] + (f['nm'][k][1] - lift[1]) * u[1] < -0.05: toward += 1
            up = high
    o['steps'], o['toward'] = steps, toward
    o['reach'] = min(math.dist(f['nm'][h][:3], f['nm'][part][:3]) for f in fr if f['t'] - t0 <= 1.5 for h in (16, 20)) \
        if part not in (15, 16, 19, 20) else None  # a hit hand or forearm can't be reached for
    late = [f for f in fr if f['t'] - t0 >= 1.5]
    o['neck'] = sum((math.dist(f['nm'][16][:3], f['nm'][NECK][:3]) + math.dist(f['nm'][20][:3], f['nm'][NECK][:3])) / 2 for f in late) / len(late) if len(late) > 5 else None
    o['fell'] = next((f['t'] - t0 for f in fr if f['fallen']), None)
    o['travel'] = max(math.hypot(f['nm'][PELVIS][0] - f0['nm'][PELVIS][0], f['nm'][PELVIS][1] - f0['nm'][PELVIS][1]) for f in fr)
    return o


def features(runs, info):
    """A case: its runs' features averaged (each over the runs where it's measurable)."""
    per = [run_features(fr, info) for fr in runs if len(fr) > 15]
    if not per: return None
    out = {}
    for key in per[0]:
        vals = [p[key] for p in per if p[key] is not None]
        if key == 'arms':
            out[key] = {k: [sum(v[k][j] for v in vals) / len(vals) for j in range(3)] for k in ARMS} if vals else None
        elif key == 'fell':
            out['fell_p'] = len(vals) / len(per); out[key] = sum(vals) / len(vals) if vals else None
        else:
            out[key] = sum(vals) / len(vals) if vals else None
    return out


def cost(ref, mine, names=None):
    parts = {k: 0.0 for k in SCALE}
    names = names or list(ref)
    unstable = 0
    for name in names:
        g, o = ref[name], mine[name]
        flat = [v for k, v in o.items() if isinstance(v, float)] + ([x for a in o['arms'].values() for x in a] if o.get('arms') else [])
        if any(math.isnan(v) or math.isinf(v) for v in flat):
            unstable += 1; continue  # a body Delirium lost: a penalty below instead of its parts
        if g['arms'] and o['arms']:
            parts['arms'] += sum(math.dist(o['arms'][k], g['arms'][k]) for k in ARMS) / 4 / SCALE['arms']
        for k in ('kick_chest', 'kick_hands', 'kick_head', 'whip', 'along', 'steps', 'toward', 'reach', 'neck', 'elbow', 'travel'):
            if g.get(k) is not None and o.get(k) is not None: parts[k] += abs(o[k] - g[k]) / SCALE[k]
        parts['fall'] += abs(o['fell_p'] - g['fell_p']) / SCALE['fall']
        if o['fell'] is not None and g['fell'] is not None: parts['when'] += abs(o['fell'] - g['fell']) / SCALE['when']
    n = max(len(names), 1)
    parts = {k: v / n / len(SCALE) for k, v in parts.items()}
    parts['unstable'] = 0.3 * unstable  # each case with a lost body costs more than any fit could gain
    return sum(parts.values()), parts


def recs(args):
    from rec import find
    assert args or GAME, 'give the recordings, or set RON_RECORDINGS to their folder'
    return [p for p in find(args or [GAME]) if 'ref ' in open(p, encoding='utf-8', errors='replace').read(400000)]


def case_info(path):
    from rec import Rec
    r = Rec(path)
    shot = next((x for op, x in r.calls if op == 'shot'), None)
    return dict(part=int(shot[2]), dir=[float(v) for v in shot[6:9]]) if shot else None


def case_runs(dll, path, engine, k, seed0=100):
    from replay import replay
    os.environ['NM_POSED'] = '0'  # GTA IV's trunk as it is; ours as the mod has it (trunk_at_zero off)
    return [replay(dll, path, engine=engine, rebuild=True, jitter=JIT, jseed=seed0 + i)['replayed'] for i in range(k)]


def _case(job):
    dll, path, engine, k, seed0 = job
    info = case_info(path)
    f = info and features(case_runs(dll, path, engine, k, seed0), info)
    return dict(f, path=path, info=info) if f else dict(path=path, info=None)


def run_cases(dll, paths, engine, k, seed0=100):
    jobs = [(dll, p, engine, k, seed0) for p in paths]
    with cf.ProcessPoolExecutor(int(os.environ.get('CALREC_PROCS', '15'))) as ex: return list(ex.map(_case, jobs))


def subset(ref):
    s = os.environ.get('CALREC_SET', 'all'); names = sorted(ref)
    return names if s == 'all' else names[0 if s == 'train' else 1::2]


def kv_values(table):
    vals = {}; section = None
    for line in open(table, encoding='utf-8'):
        if line.startswith('# ---'): section = line
        m = re.match(r'^(\d+)\s+(\S+)', line)
        if m and section and any(s in section for s in ('tuning', 'balancer', 'world', 'found by', 'calibrated')):
            try: vals[m.group(1)] = float(m.group(2))
            except ValueError: pass
    return vals


def env_for(tries):
    """The environment for an eval with these tries (see the module's doc)."""
    env = dict(os.environ)
    kv = [(k, v) for k, v in tries.items() if k.isdigit()]
    char = [(k, v) for k, v in tries.items() if not k.isdigit() and k not in ('dying', 'force')]
    env['NM_KV_TRY'] = ','.join(f'{k}={v:.6g}' for k, v in kv)
    env['REPLAY_DYING_STIFF'] = f"{tries.get('dying', DYING):.6g}"
    env['REPLAY_FORCE'] = f"{tries.get('force', 1.0):.6g}"
    if char:
        import hashlib
        spec = [f'{k}={v:.6g}' for k, v in sorted(char)]
        d = os.path.join(os.environ.get('TEMP', '.'), 'calrec_chars'); os.makedirs(d, exist_ok=True)
        f = os.path.join(d, hashlib.md5(' '.join(spec).encode()).hexdigest()[:12] + '.json')
        if not os.path.exists(f):
            subprocess.run([sys.executable, '-I', os.path.join(HERE, 'charvariant.py'), os.environ.get('CALREC_CHAR', BASE_CHAR), f, *spec], check=True)
        env['NM_CHARFILE'] = f
    elif os.environ.get('CALREC_CHAR'):
        env['NM_CHARFILE'] = os.environ['CALREC_CHAR']
    return env


def show(tries): return ','.join(f'{k}={v:.6g}' for k, v in tries.items())


def parse(s):
    return {k: float(v) for k, v in (a.split('=') for a in s.split(',') if a)} if s else {}


def run_eval(dll, ref_path, tries):
    p = subprocess.run([sys.executable, '-I', os.path.abspath(__file__), 'eval', dll, ref_path], env=env_for(tries), capture_output=True, text=True)
    try: return json.loads(p.stdout.strip().splitlines()[-1])['cost']
    except Exception:
        sys.stderr.write(p.stderr[-2000:]); return 99.0


if __name__ == '__main__':
    cmd, dll = sys.argv[1], sys.argv[2]
    if cmd == 'ref':
        out = sys.argv[3]
        res = run_cases(dll, recs(sys.argv[4:]), 'gta', K_GTA)
        ref = {os.path.basename(c['path']): c for c in res if c['info']}
        json.dump(ref, open(out, 'w'), indent=1)
        print(len(ref), 'cases')
    elif cmd == 'self':  # GTA IV against itself: other seeds, the same count of runs as ours
        ref = json.load(open(sys.argv[3])); names = subset(ref)
        res = run_cases(dll, [ref[n]['path'] for n in names], 'gta', K_OURS, seed0=900)
        mine = {os.path.basename(c['path']): c for c in res}
        c, parts = cost(ref, mine, names)
        print(json.dumps(dict(cost=c, parts=parts)))
    elif cmd == 'eval':  # the tries come in the environment (env_for), the ref's cases in CALREC_SET
        ref = json.load(open(sys.argv[3])); names = subset(ref)
        if len(sys.argv) > 4:  # from the command line: build the environment and run again
            print(json.dumps(dict(cost=run_eval(dll, sys.argv[3], parse(sys.argv[4]))))); sys.exit()
        res = run_cases(dll, [ref[n]['path'] for n in names], os.environ.get('CALREC_ENGINE', 'own'), K_OURS)  # another: diagnosis
        mine = {os.path.basename(c['path']): c for c in res}
        c, parts = cost(ref, mine, names)
        print(json.dumps(dict(cost=c, parts=parts, mine=mine)))
    elif cmd == 'parts':  # calrec.py parts <dll> <ref.json> [tries]: the cost's parts, ours against GTA IV's
        p = subprocess.run([sys.executable, '-I', os.path.abspath(__file__), 'eval', dll, sys.argv[3]],
                           env=env_for(parse(sys.argv[4] if len(sys.argv) > 4 else '')), capture_output=True, text=True)
        r = json.loads(p.stdout.strip().splitlines()[-1])
        print(round(r['cost'], 4), ' '.join(f'{k} {v:.3f}' for k, v in r['parts'].items()))
    elif cmd == 'sens':
        ref_path, table = sys.argv[3], sys.argv[4]
        vals = kv_values(table)
        bt = parse(os.environ.get('CALREC_BASE', ''))  # tries every one of them starts from
        base = run_eval(dll, ref_path, bt)
        jobs = [{k: v * m} for k, v in vals.items() if v != 0 for m in (0.6, 1.6)]
        for spec in sys.argv[5:]:  # a character key: key=down:up
            k, r = spec.split('='); jobs += [{k: float(v)} for v in r.split(':')]
        print('base', round(base, 4), len(jobs), 'tries', flush=True)
        res = []
        for t in jobs:
            s = run_eval(dll, ref_path, {**bt, **t}); res.append((s, t)); print(show(t), round(s - base, 4), flush=True)
        by = {}
        for s, t in res: by.setdefault(next(iter(t)), []).append((s, t))
        print('--- sorted')
        for k, lst in sorted(by.items(), key=lambda kv: min(x[0] for x in kv[1])):
            print(k, ' '.join(f'{next(iter(t.values())):.4g}:{s - base:+.4f}' for s, t in lst))
    elif cmd == 'descend':
        ref_path, table, keys = sys.argv[3], sys.argv[4], sys.argv[5].split(',')
        vals = kv_values(table)
        cur = parse(sys.argv[6] if len(sys.argv) > 6 else '')
        start = lambda k: cur.get(k, vals.get(k, DYING if k == 'dying' else 1.0 if k[:2] in ('w_', 'm_') or k[:3] in ('mb_', 'mp_', 'cs_') or k in ('arm', 'shw', 'force') else 0.0))
        best = run_eval(dll, ref_path, cur); print('start', round(best, 4), show(cur), flush=True)
        for rnd in range(4):
            improved = False
            for k in keys:
                v0 = start(k)
                if k in ADDITIVE:  # angles: steps added, not scaled
                    step = ADDITIVE[k] / 2 ** rnd
                    cands = [v0 + step * m for m in ((-2, -1, 1, 2) if rnd == 0 else (-1, 1))]
                else:
                    mult = (0.5, 0.75, 1.33, 2.0) if rnd == 0 else (0.85, 1.18) if rnd < 3 else (0.93, 1.07)
                    cands = [v0 * m for m in mult] if v0 != 0 else [x for x in (-0.2, -0.1, 0.1, 0.2)]
                for v in cands:
                    s = run_eval(dll, ref_path, {**cur, k: v})
                    if s < best - 0.002: best = s; cur[k] = v; improved = True
                print(f'round {rnd} {k}: {best:.4f}  {show(cur)}', flush=True)
            if not improved and rnd > 0: break
        print('best', round(best, 4), show(cur))
    elif cmd == 'cma':  # calrec.py cma <dll> <ref.json> <kv_own.txt> <keys,...> <start tries> <evals> <log>
        # separable CMA-ES over many keys at once: angles in steps (ADDITIVE, jr_ 0.15 rad), the rest on a log scale
        # (x 1.28 a step). Every evaluation goes to the log; the best one so far is its last 'best' line.
        import numpy as np
        ref_path, table, keys = sys.argv[3], sys.argv[4], sys.argv[5].split(',')
        cur = parse(sys.argv[6]); budget = int(sys.argv[7]); log = open(sys.argv[8], 'a', encoding='utf-8')
        vals = kv_values(table)
        v0 = {k: cur.get(k, vals.get(k, DYING if k == 'dying' else 1.0 if k[:2] in ('w_', 'm_') or k[:3] in ('mb_', 'mp_', 'cs_') or k in ('arm', 'shw', 'force') else 0.0)) for k in keys}
        lin = {k: ADDITIVE.get(k, 0.15) for k in keys if k in ADDITIVE or k.startswith('jr_')}
        def decode(x):
            t = dict(cur)
            for i, k in enumerate(keys): t[k] = v0[k] + x[i] * lin[k] if k in lin else v0[k] * math.exp(0.25 * x[i])
            return t
        n = len(keys); lam = 4 + int(3 * math.log(n)); mu = lam // 2
        w = np.log(mu + 0.5) - np.log(np.arange(1, mu + 1)); w /= w.sum(); mueff = 1 / (w ** 2).sum()
        cs = (mueff + 2) / (n + mueff + 5); ds = 1 + 2 * max(0, math.sqrt((mueff - 1) / (n + 1)) - 1) + cs
        cc = (4 + mueff / n) / (n + 4 + 2 * mueff / n)
        c1 = 2 / ((n + 1.3) ** 2 + mueff) * (n + 2) / 3; cmu = min(1 - c1, 2 * (mueff - 2 + 1 / mueff) / ((n + 2) ** 2 + mueff) * (n + 2) / 3)
        chi = math.sqrt(n) * (1 - 1 / (4 * n) + 1 / (21 * n * n))
        rng = np.random.default_rng(int(os.environ.get('CALREC_SEED', '1')))
        m = np.zeros(n); sigma = float(os.environ.get('CALREC_SIGMA', '1.0')); C = np.ones(n); ps = np.zeros(n); pc = np.zeros(n)
        best = run_eval(dll, ref_path, decode(m)); best_x = m.copy(); used = 1
        log.write(f'start {best:.4f} {show(decode(m))}\n'); log.flush()
        gen = 0
        while used + lam <= budget:
            z = rng.standard_normal((lam, n)); y = z * np.sqrt(C); xs = m + sigma * y
            fs = []
            for x in xs:
                f = run_eval(dll, ref_path, decode(x)); fs.append(f); used += 1
                log.write(f'eval {gen} {f:.4f}\n')
                if f < best: best, best_x = f, x.copy(); log.write(f'best {best:.4f} {show(decode(x))}\n')
                log.flush()
            order = np.argsort(fs); ysel = y[order[:mu]]; yw = w @ ysel
            m = m + sigma * yw
            ps = (1 - cs) * ps + math.sqrt(cs * (2 - cs) * mueff) * (yw / np.sqrt(C))
            hs = np.linalg.norm(ps) / math.sqrt(1 - (1 - cs) ** (2 * (gen + 1))) < (1.4 + 2 / (n + 1)) * chi
            pc = (1 - cc) * pc + hs * math.sqrt(cc * (2 - cc) * mueff) * yw
            C = (1 - c1 - cmu) * C + c1 * (pc ** 2 + (1 - hs) * cc * (2 - cc) * C) + cmu * (w @ (ysel ** 2))
            sigma *= math.exp((cs / ds) * (np.linalg.norm(ps) / chi - 1))
            log.write(f'gen {gen} median {float(np.median(fs)):.4f} gen_best {min(fs):.4f} best {best:.4f} sigma {sigma:.3f}\n'); log.flush()
            gen += 1
        print('best', round(best, 4), show(decode(best_x)))
