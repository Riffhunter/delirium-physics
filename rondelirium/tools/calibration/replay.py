"""Runs a RoNDelirium recording again offline: the same calls on Delirium, in the same order (the build fitted to the
same skeleton, the same world, resyncs, shots, messages and steps).
  --engine own (default): our numbers and character. With the DLL the game had, Delirium comes out as recorded (how far
      off is printed); with a changed DLL or NM_KV_TRY, how a change would have moved Delirium in that very case.
  --engine gta: the reference. GTA IV's numbers and character, read from YOUR OWN copy of the game at run time (nothing of
      it is in this repository), fitted to the same skeleton, its states rebuilt from the ragdoll as recorded (sync lines)
      through its own joints. Needs DELIRIUM_GTA_DIR (the folder with GTAIV.exe) and DELIRIUM_GTA_CHAR (see CALIBRATION.md).
  --engine gta_numbers / gta_char: diagnosis, half and half: the reference's numbers on our character, or the reverse.
  --engine json:<character.json>: likewise a character file (nm-character-1), with our numbers.
  --rebuild: rebuild the states from the sync lines even on the recording's own engine (checks nmcal against the mod).
The game's bodies are what they were (open loop): Delirium is set from them at the start and at each resync as
recorded, and runs free between, as in the game.
usage: replay.py <dll> <in.rec> [out.rec] [--engine ...] [--rebuild]
out.rec: the recording with this replay's Delirium as a third body (alt), for recviewer.html"""
import ctypes as C, math, os, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rec import Rec
from nmcal import Cal

GTA = os.environ.get('DELIRIUM_GTA_DIR', '')        # your own game's folder (with GTAIV.exe), for the reference engines
GTA_CHAR = os.environ.get('DELIRIUM_GTA_CHAR', '')  # what NM_EXPORT wrote from it (fred.xrt, build.txt): keep it to yourself
_libs = {}
import random as _random


def load(dll, engine):
    """The DLL set up for an engine: a copy of it per engine (its numbers and character are global to a loaded DLL)."""
    key = (dll, engine)
    if key in _libs: return _libs[key]
    import zlib
    # one copy per build of the DLL (its time in the name): a copy still loaded by another process is never stale
    copy = os.path.join(tempfile.gettempdir(), f"ronreplay_{zlib.crc32(dll.encode()):08x}_{zlib.crc32(engine.encode()):08x}_{int(os.path.getmtime(dll))}.dll")
    if not os.path.exists(copy) or os.path.getsize(copy) != os.path.getsize(dll):
        tmp = f'{copy}.{os.getpid()}'
        shutil.copy2(dll, tmp)
        try: os.replace(tmp, copy)
        except OSError: os.remove(tmp)  # another replay put it there first and has it loaded
    lib = C.CDLL(copy)
    for f in ('nm_agent_create', 'nm_world_create', 'nm_build_own', 'nm_build_fit', 'nm_build_load', 'nm_build_from_character'):
        getattr(lib, f).restype = C.c_void_p
    if engine.startswith('gta'):
        assert GTA and GTA_CHAR, 'set DELIRIUM_GTA_DIR and DELIRIUM_GTA_CHAR for the reference engines (see CALIBRATION.md)'
    if engine == 'gta_numbers':  # diagnosis: GTA IV's numbers, our character
        assert lib.nm_unlock(GTA.encode()) == 0 and lib.nm_init_own_character() >= 0
        base = C.c_void_p(lib.nm_build_own())
    elif engine == 'gta_char':  # diagnosis: our numbers, GTA IV's character
        assert lib.nm_unlock_own() > 0 and lib.nm_init((GTA_CHAR + '/fred.xrt').encode()) >= 0
        base = C.c_void_p(lib.nm_build_load((GTA_CHAR + '/build.txt').encode()))
    elif engine == 'gta':
        assert lib.nm_unlock(GTA.encode()) == 0 and lib.nm_init((GTA_CHAR + '/fred.xrt').encode()) >= 0
        base = C.c_void_p(lib.nm_build_load((GTA_CHAR + '/build.txt').encode()))
    elif engine.startswith('json:'):
        txt = open(engine[5:], encoding='utf-8').read().encode()
        assert lib.nm_unlock_own() > 0 and lib.nm_init_character(txt) >= 0
        base = C.c_void_p(lib.nm_build_from_character(txt))
    else:
        assert lib.nm_unlock_own() > 0 and lib.nm_init_own_character() >= 0
        base = C.c_void_p(lib.nm_build_own())
    assert base, 'no build'
    _libs[key] = (lib, base)
    return lib, base


def recorded_push(r):
    """The push_scale a recording's shot values were made with: its 'push' line (1 if it has none)."""
    line = next((x for op, x in r.calls if op == 'push'), None)
    return float(line[0]) if line else 1.0


def replay(dll, path, out=None, engine='own', rebuild=False, jitter=None, jseed=1, hook=None):
    """jitter (m, rad): noise on Delirium's states at each resync, to see a case's spread (its outcome turns on
    differences far below a millimetre); default REPLAY_JITTER, 0."""
    jitter = float(os.environ.get('REPLAY_JITTER', '0')) if jitter is None else jitter
    rnd = _random.Random(jseed)
    jit = lambda st: [v + jitter * (2 * rnd.random() - 1) for v in st] if jitter else st
    lib, base = load(dll, engine)
    Fl = lambda v: (C.c_float * len(v))(*[float(x) for x in v])
    r = Rec(path)
    rec_engine = next((x[0] for op, x in r.calls if op == 'engine'), 'own')
    # the trunk's joints at their zero (mod v0.37 and later, its 'posed' line), or NM_POSED to try: 1 the whole pose, >1 bits
    rec_posed = int(next((x[0] for op, x in r.calls if op == 'posed'), 0))
    posed = int(os.environ.get('NM_POSED') or rec_posed)
    own = engine == rec_engine and not rebuild and posed == rec_posed  # the recorded states as they were, on the body they were made for
    ref = joints = None; cal = None
    for op, x in r.calls:
        if op == 'ref': ref = [((float(x[7 * k]), float(x[7 * k + 1]), float(x[7 * k + 2])), tuple(float(v) for v in x[7 * k + 3:7 * k + 7])) for k in range(21)]
        if op == 'fit': joints = [float(v) for v in x]
    mine_cal = None  # ours, for showing another character's parts as our parts on the same bones
    if not own:
        assert ref, 'this recording has no ref/sync lines (made before v0.33): it replays on its own engine only'
        cal = Cal(lib, base, joints, ref, posed=posed)
        if engine != 'own':
            olib, obase = load(dll, 'own')
            mine_cal = Cal(olib, obase, joints, ref)
    from nmcal import compose, inverse

    def shown(k, t):
        """A part of this replay as our part on the same bone (characters put their parts' frames differently)."""
        if not mine_cal: return t
        bone = compose(((t[0], t[1], t[2]), tuple(t[3:7])), inverse(cal.off[k]))
        p, q = compose(bone, mine_cal.off[k])
        return list(p) + list(q)
    build = base; a = w = None; last_health = 200.0
    gta = engine.startswith('gta'); dying = os.environ.get('REPLAY_DYING_STIFF')
    lib.nm_set_shot_body_stiffness(C.c_float(0.0))  # global in the DLL: nothing left from the last replay
    worst = 0.0; first_off = None; errs = []; lines = []; sync_err = 0.0; last_sync = None
    pos = []  # this replay's Delirium per frame
    # lift_cap (mod 1.2): the recording's 'lift' line, or REPLAY_LIFT_CAP to try (0 = off); alive until the mod's event says not
    lift = float(os.environ.get('REPLAY_LIFT_CAP') or next((x[0] for op, x in r.calls if op == 'lift'), 0))
    alive = r.alive
    lift_log = [] if os.environ.get('REPLAY_LIFT_LOG') else None  # diagnosis: the dying body's centre of mass, up speed per step
    for op, x in r.calls:
        if op == 'fit':
            build = cal.build if cal else C.c_void_p(lib.nm_build_fit(base, Fl(x)))
            assert build, 'nm_build_fit failed'
            if not cal and posed:
                lib.nm_build_with_joints_of.restype = C.c_void_p; lib.nm_build_with_pose_of.restype = C.c_void_p
                build = C.c_void_p(lib.nm_build_with_pose_of(build, base) if posed == 1 else lib.nm_build_with_joints_of(build, base, C.c_uint32(posed)))
        elif op == 'agent':
            a = C.c_void_p(lib.nm_agent_create(build, Fl(x[:3]), C.c_float(float(x[3])), C.c_float(float(x[4]))))
        elif op == 'seed':
            lib.nm_agent_set_seed(a, C.c_uint32(int(x[0])))
        elif op == 'world':
            w = C.c_void_p(lib.nm_world_create())
        elif op == 'floor':
            lib.nm_world_add_floor(w, *[C.c_float(float(v)) for v in x])
        elif op == 'boxes':
            n = int(x[0]); lib.nm_world_set_boxes(w, Fl(x[1:1 + 13 * n]), n)
        elif op == 'mesh':
            nv, nt, hm = int(x[0]), int(x[1]), int(x[2])
            v = x[3:3 + 3 * nv]; t = x[3 + 3 * nv:3 + 3 * nv + 3 * nt]; m = x[3 + 3 * nv + 3 * nt:3 + 3 * nv + 4 * nt] if hm else None
            lib.nm_world_add_mesh(w, Fl(v), nv, (C.c_uint32 * len(t))(*[int(i) for i in t]), nt,
                                  (C.c_uint8 * len(m))(*[int(i) for i in m]) if m else None)
        elif op == 'sync':
            last_sync = [[float(v) for v in x[16 * k:16 * k + 16]] for k in range(21)]
            if not own: lib.nm_agent_set_part_states(a, Fl(jit(cal.states(last_sync))), 21)
        elif op == 'states':
            if own:
                st = [float(v) for v in x[1:]]
                if os.environ.get('REPLAY_STILL'):  # the ragdoll's velocities left out (diagnosis)
                    st = [0.0 if i % 13 >= 7 else v for i, v in enumerate(st)]
                lib.nm_agent_set_part_states(a, Fl(jit(st)), int(x[0]))
            elif rebuild and engine == rec_engine and last_sync:  # nmcal against the mod's own
                sync_err = max(sync_err, max(abs(float(p) - q) for p, q in zip(x[1:], cal.states(last_sync))))
        elif op == 'step':
            lib.nm_agent_step(a, w, C.c_float(float(x[0])))
            if lift and not alive and not gta:  # the mod's lift_cap: a dying body's centre of mass no faster up than this
                cp = (C.c_float * 3)(); cv = (C.c_float * 3)(); lib.nm_agent_get_com(a, cp, cv)
                if cv[2] > lift: lib.nm_agent_add_velocity(a, Fl([0.0, 0.0, lift - cv[2]]))
            if lift_log is not None and not alive:
                cp = (C.c_float * 3)(); cv = (C.c_float * 3)(); lib.nm_agent_get_com(a, cp, cv); lift_log.append(cv[2])
        elif op == 'event':
            if x[1] in ('incapacitated', 'killed'): alive = False
        elif op == 'stiff':  # the mod's dying stiffness: ours only (GTA IV's side is as GTA IV had it), unless tried
            if not gta and not dying: lib.nm_set_shot_body_stiffness(C.c_float(float(x[0])))
        elif op == 'shot':
            last_health = float(x[9])
            if dying and not gta:  # a value to try, on any recording: the dying body's stiffness
                lib.nm_set_shot_body_stiffness(C.c_float(float(dying) if last_health <= 60 else 0.0))
            # the mod's push_scale, to try: in place of the one the recording was made with (in its shot values)
            # The push this run gets: GTA IV's engine always its own (1, whatever the recording was made with), ours REPLAY_FORCE
            try_push = os.environ.get('REPLAY_FORCE')
            push = float(os.environ.get('REPLAY_GTA_PUSH', 1.0)) if gta else float(try_push) if try_push else recorded_push(r)
            lib.nm_set_shot_force_scale(C.c_float(float(x[0]) * push / recorded_push(r)))
            weapon = None if x[1] == '-' else x[1].encode()
            shooter = Fl(x[11:14]) if x[10] == '1' else None
            lib.nm_agent_shot(a, w, weapon, int(x[2]), Fl(x[3:6]), Fl(x[6:9]), C.c_float(float(x[9])), shooter)
        elif op == 'upd':
            last_health = float(x[0])
            if dying and not gta: lib.nm_set_shot_body_stiffness(C.c_float(float(dying) if last_health <= 60 else 0.0))
            lib.nm_agent_shot_update(a, w, C.c_float(float(x[0])), Fl(x[2:5]) if x[1] == '1' else None)
        elif op == 'msg':
            lib.nm_msg_begin(a, x[0].encode())
        elif op == 'mb':
            lib.nm_msg_set_bool(a, x[0].encode(), int(x[1]))
        elif op == 'mf':
            v = float(x[1])
            if x[0] == 'grabHoldTime' and os.environ.get('REPLAY_GRAB'): v = float(os.environ['REPLAY_GRAB'])  # diagnosis
            lib.nm_msg_set_float(a, x[0].encode(), C.c_float(v))
        elif op == 'mi':
            lib.nm_msg_set_int(a, x[0].encode(), int(x[1]))
        elif op == 'send':
            lib.nm_msg_send(a, w)
        elif op == 'push':
            lib.nm_agent_apply_push(a, Fl(x[:3]), Fl(x[3:6]))
        elif op == 'frame':
            mine = []
            for k in range(21):
                p = (C.c_float * 3)(); q = (C.c_float * 4)(); lib.nm_agent_get_part_transform(a, k, p, q)
                mine.append(shown(k, list(p) + list(q)))
            fallen = lib.nm_agent_has_fallen(a)
            if hook: hook(lib, a, x)
            pos.append(dict(t=x['t'], fallen=fallen, floor=x['floor'], nm=mine, body=x['body']))
            e = max(math.dist(mine[k][:3], x['nm'][k][:3]) for k in range(21))
            errs.append(e); worst = max(worst, e)
            if first_off is None and e > 0.01: first_off = x['t']
            if out:
                v = ' '.join(f'{c:.4f}' for part in x['nm'] + x['body'] + mine for c in part)
                lines.append(f"frame {x['t']:.4f} {x['fallen']} {x['own']} {x['floor']:.3f} {v}\n")
            continue
        if out:
            lines.append((f"event {x[0]:.3f} {x[1]}" if op == 'event' else ' '.join([op] + list(x))) + '\n')
    if a: lib.nm_agent_destroy(a)
    if w: lib.nm_world_destroy(w)
    if out:
        with open(out, 'w') as f:
            f.write('rec ' + ' '.join(r.header) + '\n')
            f.write(f'alt {engine} {"(same calls as recorded)" if own else "(states rebuilt for its character)"}\n')
            f.write('parents ' + ' '.join(map(str, r.parents)) + '\n')
            f.writelines(lines)
    return dict(frames=len(errs), worst=worst, mean=sum(errs) / max(len(errs), 1), first_off=first_off, sync_err=sync_err, own=own, lift_log=lift_log,
                replayed=pos, rec=r)


if __name__ == '__main__':
    args = sys.argv[1:]
    engine = 'own'; rebuild = '--rebuild' in args
    if rebuild: args.remove('--rebuild')
    if '--engine' in args:
        i = args.index('--engine'); engine = args[i + 1]; del args[i:i + 2]
    res = replay(args[0], args[1], args[2] if len(args) > 2 else None, engine, rebuild)
    what = 'as recorded' if res['own'] else f'on {engine} (states rebuilt)'
    print(f"{res['frames']} frames {what}; Delirium off the recording by {res['mean'] * 100:.2f} cm on average, {res['worst'] * 100:.2f} cm at worst"
          + (f"; apart (>1 cm) from {res['first_off']:.2f} s" if res['first_off'] is not None else ''))
    if rebuild: print(f"rebuilt states against the mod's: {res['sync_err']:.2e} at worst")
