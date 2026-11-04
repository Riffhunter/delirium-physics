"""Long soak test of the Delirium fork as RoNDelirium drives it (own numbers, own character: nothing from any game).
Many agents one after another, each up to `secs` seconds: bursts of hits on random parts with every weapon's push,
limb injuries, the shot message, deaths partway (health dropping to the dying reaction's), a push now and then,
resyncs from a jittered copy of the body, walls, furniture, a step and stairs, frame rates from 30 to 240.
Reports failures (non-finite or exploding bodies, step errors), the worst speeds, step cost and unauthored slots.
usage: soak.py <dll> <seed> <agents> <secs> <out.json>"""
import ctypes as C, sys, json, random, math, time
dll, seed, agents, secs, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), sys.argv[5]
lib = C.CDLL(dll)
for f in ('nm_agent_create', 'nm_world_create', 'nm_build_own', 'nm_build_fit'): getattr(lib, f).restype = C.c_void_p
assert lib.nm_unlock_own() > 0 and lib.nm_init_own_character() >= 0
B = C.c_void_p(lib.nm_build_own())
F = lambda *v: (C.c_float * len(v))(*v)
rnd = random.Random(seed)
import os
PIVOTS = []
def _pivots():
    w = C.c_void_p(lib.nm_world_create()); a = C.c_void_p(lib.nm_agent_create(B, F(0, 0, 0), C.c_float(0), C.c_float(0.3)))
    out = []
    for k in range(21):
        par = C.c_int32(); pv = (C.c_float * 3)(); fr = (C.c_float * 4)(); lm = (C.c_float * 3)(); ar = C.c_float()
        if k == 0 or lib.nm_agent_get_joint(a, k, C.byref(par), pv, fr, lm, C.byref(ar)) <= 0:
            pv = tr(a, 0)[0]
        out.append(list(pv))
    lib.nm_agent_destroy(a); lib.nm_world_destroy(w)
    return out

def tr(a, k):
    p = (C.c_float * 3)(); q = (C.c_float * 4)(); lib.nm_agent_get_part_transform(a, k, p, q); return list(p), list(q)

PIVOTS = _pivots()

def world():
    w = C.c_void_p(lib.nm_world_create())
    lib.nm_world_add_floor(w, C.c_float(0), C.c_float(0), C.c_float(-0.02), C.c_float(60))
    verts, idx = [], []
    kind = rnd.choice(['flat', 'step', 'stairs', 'slope'])
    if kind == 'step':
        h = rnd.uniform(0.1, 0.25); verts = [0.4, -2, h, 3, -2, h, 3, 2, h, 0.4, 2, h]; idx = [0, 1, 2, 0, 2, 3]
    elif kind == 'stairs':
        for s in range(8):
            x0, z = 0.3 + s * 0.28, (s + 1) * 0.17; b = len(verts) // 3
            verts += [x0, -1.5, z, x0 + 0.28, -1.5, z, x0 + 0.28, 1.5, z, x0, 1.5, z]; idx += [b, b + 1, b + 2, b, b + 2, b + 3]
    elif kind == 'slope':
        verts = [-3, -3, -0.4, 3, -3, 0.4, 3, 3, 0.4, -3, 3, -0.4]; idx = [0, 1, 2, 0, 2, 3]
    if verts:
        lib.nm_world_add_mesh(w, F(*verts), len(verts) // 3, (C.c_uint32 * len(idx))(*idx), len(idx) // 3, None)
    boxes = []
    for _ in range(rnd.randint(0, 6)):
        boxes += [rnd.uniform(-2, 2), rnd.uniform(-2, 2), rnd.uniform(0.3, 1.2), 0, 0, math.sin(rnd.uniform(0, 3)), math.cos(rnd.uniform(0, 3)),
                  rnd.uniform(0.1, 0.6), rnd.uniform(0.1, 0.6), rnd.uniform(0.3, 1.2), 0, 0, 0]
    if boxes: lib.nm_world_set_boxes(w, F(*boxes), len(boxes) // 13)
    return w, kind

weapons_push = [28, 32, 45, 52, 10]
stats = dict(agents=0, sim_seconds=0.0, steps=0, bad=0, step_errors=0, worst_speed=0.0, worst_spin=0.0, falls=0, deaths=0, resyncs=0,
             hits=0, worst_step_ms=0.0, bad_cases=[])
t_start = time.time()
for n in range(agents):
    w, kind = world()
    fit = None
    # a body of the suspect's own size now and then (the mod fits our character to each suspect's bones)
    if rnd.random() < float(__import__('os').environ.get('SOAK_FIT', '0.5')):
        s = rnd.uniform(0.9, 1.1)
        # the joints of our character (its pivots), scaled and jittered as a suspect's bones would be
        piv = [list(p) for p in PIVOTS]
        if rnd.random() < 0.5:  # a T-pose reference (arms out), as a skeleton's reference pose may be
            for sh, el, wr in ((14, 15, 16), (18, 19, 20)):
                side = 1 if piv[sh][0] > 0 else -1
                lu = math.dist(piv[sh], piv[el]); lf = math.dist(piv[el], piv[wr])
                piv[el] = [piv[sh][0] + side * lu, piv[sh][1], piv[sh][2]]
                piv[wr] = [piv[el][0] + side * lf, piv[el][1], piv[el][2]]
        pts = []
        for p in piv: pts += [x * s + rnd.uniform(-0.01, 0.01) for x in p]
        fit = C.c_void_p(lib.nm_build_fit(B, F(*pts)))
        if os.environ.get('SOAK_POSED'):  # the trunk's joints at their zero, as the mod builds it
            lib.nm_build_with_joints_of.restype = C.c_void_p
            fit = C.c_void_p(lib.nm_build_with_joints_of(fit, B, C.c_uint32(int(os.environ['SOAK_POSED']))))
    build = fit if fit else B
    a = C.c_void_p(lib.nm_agent_create(build, F(rnd.uniform(-0.5, 0.5), rnd.uniform(-0.5, 0.5), 0), C.c_float(rnd.uniform(-3.2, 3.2)), C.c_float(0.3)))
    health = rnd.choice([200, 190, 170, 150, 120])
    die_at = rnd.uniform(0.5, secs) if rnd.random() < 0.4 else None
    shooter = [rnd.uniform(-6, 6), rnd.uniform(-8, -3), 1.6]
    hit_times = sorted(rnd.uniform(0, secs * 0.7) for _ in range(rnd.randint(1, 10)))
    dt_choice = rnd.choice([1 / 30, 1 / 60, 1 / 90, 1 / 144, 1 / 240, None])
    t = 0.0; dead = False
    while t < secs:
        dt = dt_choice or rnd.uniform(1 / 240, 1 / 25)
        if die_at is not None and not dead and t >= die_at:
            dead = True; health = rnd.choice([60, 20]); stats['deaths'] += 1
        while hit_times and t >= hit_times[0]:
            hit_times.pop(0); stats['hits'] += 1
            k = rnd.randrange(21); at = tr(a, k)[0]
            d = [at[i] - shooter[i] for i in range(3)]
            lib.nm_set_shot_force_scale(C.c_float(rnd.choice(weapons_push) / 40 * rnd.uniform(0.5, 1.2)))
            lib.nm_agent_shot(a, w, None, k, F(*at), F(*d), C.c_float(health), F(*shooter))
            if lib.nm_msg_begin(a, b"shot") == 0:
                lib.nm_msg_set_float(a, b"grabHoldTime", C.c_float(1.2)); lib.nm_msg_send(a, w)
            if k in (1, 2, 3, 4, 5, 6, 14, 15, 16, 18, 19, 20) and lib.nm_msg_begin(a, b"injury") == 0:
                lib.nm_msg_set_int(a, b"part", k); lib.nm_msg_set_float(a, b"amount", C.c_float(rnd.uniform(0.2, 0.95))); lib.nm_msg_send(a, w)
        if rnd.random() < 0.002:
            lib.nm_agent_apply_push(a, F(*tr(a, rnd.randrange(21))[0]), F(rnd.uniform(-60, 60), rnd.uniform(-60, 60), rnd.uniform(0, 30)))
        if rnd.random() < 0.004:  # a resync: the body set from a slightly different copy of itself (the game's ragdoll)
            st = []
            for k in range(21):
                p, q = tr(a, k)
                v = (C.c_float * 3)(); ww = (C.c_float * 3)(); lib.nm_agent_get_part_velocity(a, k, v, ww)
                st += [x + rnd.uniform(-0.03, 0.03) for x in p] + q + list(v) + list(ww)
            lib.nm_agent_set_part_states(a, F(*st), 21); stats['resyncs'] += 1
        lib.nm_agent_shot_update(a, w, C.c_float(health), F(*shooter))
        t0 = time.perf_counter()
        rc = lib.nm_agent_step(a, w, C.c_float(dt))
        if dead:  # the mod's lift_cap (1 m/s): a dying body's centre of mass no faster up
            cp = (C.c_float * 3)(); cv = (C.c_float * 3)(); lib.nm_agent_get_com(a, cp, cv)
            if cv[2] > 1.0: lib.nm_agent_add_velocity(a, F(0.0, 0.0, 1.0 - cv[2])); stats['lift_capped'] = stats.get('lift_capped', 0) + 1
        stats['worst_step_ms'] = max(stats['worst_step_ms'], (time.perf_counter() - t0) * 1000)
        stats['steps'] += 1
        if rc != 0:
            stats['step_errors'] += 1; break
        bad = False
        for k in (0, 3, 6, 12, 16, 20):
            v = (C.c_float * 3)(); ww = (C.c_float * 3)(); lib.nm_agent_get_part_velocity(a, k, v, ww)
            sp = math.sqrt(sum(x * x for x in v)); spin = math.sqrt(sum(x * x for x in ww))
            p = tr(a, k)[0]
            if not all(math.isfinite(x) for x in list(v) + list(ww) + p) or sp > 40 or spin > 80 or abs(p[2]) > 20:
                bad = True
            stats['worst_speed'] = max(stats['worst_speed'], sp if math.isfinite(sp) else 1e9)
            stats['worst_spin'] = max(stats['worst_spin'], spin if math.isfinite(spin) else 1e9)
        if bad:
            stats['bad'] += 1
            if len(stats['bad_cases']) < 20: stats['bad_cases'].append(dict(agent=n, t=round(t, 2), kind=kind, health=health, dt=dt))
            break
        t += dt
    stats['sim_seconds'] += t
    stats['falls'] += lib.nm_agent_has_fallen(a)
    lib.nm_agent_destroy(a); lib.nm_world_destroy(w)
    if fit: lib.nm_build_destroy(fit)
    stats['agents'] += 1
miss = (C.c_uint8 * 1001)(); lib.nm_debug_kv_missing(miss, 1001)
stats['unauthored'] = [i for i in range(1001) if miss[i]]
stats['wall_seconds'] = round(time.time() - t_start, 1)
stats['steps_per_ms'] = round(stats['steps'] / max(stats['wall_seconds'] * 1000, 1), 2)
json.dump(stats, open(out, 'w'), indent=1)
print(json.dumps({k: v for k, v in stats.items() if k != 'bad_cases'}))
