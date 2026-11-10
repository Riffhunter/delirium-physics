"""RoNDelirium's recordings (Mods/RoNDelirium/recordings/*.rec): one per activation, written by the mod.
Lines, in the order things happened:
  rec 1 <character> <mesh asset> <alive|dead>      header
  fit <63 floats>                                  the skeleton's joints the build was fitted to (nm_build_fit)
  parents <21 ints>                                each part's parent (-1 for the pelvis)
  agent x y z yaw height                           nm_agent_create on the fitted build
  seed n                                           nm_agent_set_seed: its random numbers
  engine own|gta                                   which Delirium ran (gta: the developer's comparison runs only)
  ref <21 x 7>                                     the skeleton's bones in its reference pose (Delirium space)
  sync <21 x 16>                                   the ragdoll as read at a resync: bone p3 q4, its body's velocity, spin, centre of mass
  world | floor x y z size | boxes n <13n> | mesh nv nt hasmat <3nv> <3nt> [<nt>]
  stiff k (the shots' body stiffness that follow, 0 the usual) | states n <13n> | step dt | shot force weapon part at3 dir3 health hasshooter [shooter3] | upd health hasshooter [shooter3]
  msg name | mb/mf/mi key value | send | push at3 impulse3       Delirium calls, exactly as made
  frame t fallen own floor <21 x 7 Delirium's parts> <21 x 7 the game's bodies>   poses (p xyz, q xyzw), 30 a second
  event t text                                     what happened (shot, resync, killed, back_to_animation, MARK, end ...)
"""
import math, os

HEAD, PELVIS, LFOOT, RFOOT = 12, 0, 3, 6


class Rec:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.marked = '_MARK' in self.name
        self.calls = []    # (op, args) in order, frames and events included
        self.frames = []   # dict(t, fallen, own, floor, nm=[[7]*21], body=[[7]*21])
        self.events = []   # (t, text)
        self.parents = [-1] + [0] * 20
        self.header = []
        for line in open(path, encoding='utf-8', errors='replace'):
            w = line.split()
            if not w: continue
            op = w[0]
            if op == 'rec': self.header = w[1:]
            elif op == 'parents': self.parents = [int(x) for x in w[1:]]
            elif op == 'frame':
                v = [float(x) for x in w[5:]]
                f = dict(t=float(w[1]), fallen=int(w[2]), own=int(w[3]), floor=float(w[4]),
                         nm=[v[7 * k:7 * k + 7] for k in range(21)], body=[v[147 + 7 * k:147 + 7 * k + 7] for k in range(21)])
                self.frames.append(f)
                self.calls.append(('frame', f))
            elif op == 'event':
                e = (float(w[1]), ' '.join(w[2:]))
                self.events.append(e)
                self.calls.append(('event', e))
            else:
                self.calls.append((op, w[1:]))
        self.character = self.header[1] if len(self.header) > 1 else '?'
        self.alive = len(self.header) > 3 and self.header[3] == 'alive'

    def end_reason(self):
        for t, e in reversed(self.events):
            if e.startswith('end '): return e[4:]
        return '?'

    def count(self, what):
        return sum(e == what or e.startswith(what + ' ') for _, e in self.events)


def stats(r):
    """What the player saw (the game's bodies) and how Delirium drove it, in numbers."""
    fr = r.frames
    out = dict(name=r.name, who=r.character, alive=r.alive, marked=r.marked, end=r.end_reason(), secs=fr[-1]['t'] if fr else 0,
               shots=r.count('shot'), resyncs=r.count('resync'), limb_resyncs=r.count('limb_resync'),
               fell_at=next((f['t'] for f in fr if f['fallen']), None))
    if len(fr) < 5:
        out.update(jerk=0, hops=0, foot_lifts=0, gap=0, max_gap=0, max_head=0, max_head_nm=0, flags=['short'])
        return out
    dt = (fr[-1]['t'] - fr[0]['t']) / max(len(fr) - 1, 1) or 1 / 30  # the recording's own rate (record_fps: 30 by default, 60 or every frame)
    jerk = 0.0; n = 0
    for k in (HEAD, PELVIS):
        x = [f['body'][k][:3] for f in fr]
        for i in range(3, len(x)):
            d = [x[i][j] - 3 * x[i - 1][j] + 3 * x[i - 2][j] - x[i - 3][j] for j in range(3)]
            jerk += math.sqrt(sum(c * c for c in d)) / dt ** 3; n += 1
    jerk /= max(n, 1)
    # Hops: the pelvis rising fast then falling again while not getting up (its height peaks with upward speed > 0.5 m/s).
    pz = [f['body'][PELVIS][2] for f in fr]
    vz = [(pz[i + 1] - pz[i]) / dt for i in range(len(pz) - 1)]
    hops = 0; rising = False
    for i, v in enumerate(vz):
        if v > 0.5 and not rising: rising = True
        elif v < 0 and rising:
            rising = False
            if fr[i]['t'] > 0.1: hops += 1
    # Feet off the floor (12 cm up) and back down: steps, or hops if both go together.
    lifts = 0; up = [False, False]
    for f in fr:
        for i, k in enumerate((LFOOT, RFOOT)):
            u = f['body'][k][2] - f['floor'] > 0.12
            if u and not up[i]: lifts += 1
            up[i] = u
    gaps = [math.dist(f['nm'][PELVIS][:3], f['body'][PELVIS][:3]) for f in fr]
    part_gap = max(max(math.dist(f['nm'][k][:3], f['body'][k][:3]) for k in range(21)) for f in fr)
    head = max(f['body'][HEAD][2] - f['floor'] for f in fr)
    head_nm = max(f['nm'][HEAD][2] - f['floor'] for f in fr)
    flags = []
    if hops >= 2: flags.append(f'{hops} hops')
    if out['resyncs'] >= 3: flags.append(f"{out['resyncs']} resyncs")
    if part_gap > 0.6: flags.append(f'gap {part_gap:.2f} m')
    if head_nm > 2.2: flags.append(f'Delirium flung ({head_nm:.1f} m)')
    if head > 2.1: flags.append(f'body up {head:.1f} m')
    if jerk > 600: flags.append('jerky')
    out.update(jerk=round(jerk), hops=hops, foot_lifts=lifts, gap=round(sum(gaps) / len(gaps), 3), max_gap=round(part_gap, 2),
               max_head=round(head, 2), max_head_nm=round(head_nm, 2), flags=flags)
    return out


def find(paths):
    out = []
    for p in paths:
        if os.path.isdir(p): out += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith('.rec'))
        else: out.append(p)
    return out
