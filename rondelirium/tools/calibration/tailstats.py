"""What the game's bodies did after Delirium let go: the .tail files beside RoNDelirium's recordings (mod 1.5-test and later,
record_tail in delirium.ini). One line per recording: the jump at the hand-over (the .rec's last pose against the tail's
first), T-poses (the arms out straight at shoulder height), snaps (a joint turning a quarter turn in a sixteenth of a
second), twitches (a joint's spin reversing, fast both ways), whole-body jumps, and the game's ragdoll and simulation flags.
usage: tailstats.py [recordings dir or .tail files ...]   (default: the folder in RON_RECORDINGS)"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rec import Rec

GAME = os.environ.get('RON_RECORDINGS', '')
PAR = [-1, 0, 1, 2, 0, 4, 5, 0, 7, 8, 9, 10, 11, 10, 13, 14, 15, 10, 17, 18, 19]
LIMBS = [2, 3, 5, 6, 11, 12, 15, 16, 19, 20, 14, 18, 1, 4]
NAMES = ['pelvis', 'thighL', 'shinL', 'footL', 'thighR', 'shinR', 'footR', 'spine0', 'spine1', 'spine2', 'chest', 'neck', 'head',
         'clavL', 'uparmL', 'foreL', 'handL', 'clavR', 'uparmR', 'foreR', 'handR']


def qmul(a, b):
    x1, y1, z1, w1 = a; x2, y2, z2, w2 = b
    return (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)


def angle(q0, q1):
    q = qmul((-q0[0], -q0[1], -q0[2], q0[3]), q1)
    return 2 * math.atan2(math.sqrt(q[0] ** 2 + q[1] ** 2 + q[2] ** 2), abs(q[3]))


def rel(P, k):
    a = P[PAR[k]][3:7]
    return qmul((-a[0], -a[1], -a[2], a[3]), P[k][3:7])


ANIM = {}  # path: [(t, flags)] from the tails' 'anim' lines


def read_tail(path):
    head, frames = None, []
    for line in open(path, encoding='utf-8', errors='replace'):
        w = line.split()
        if not w: continue
        if w[0] == 'tail': head = w[2:]  # (after the format's version)
        elif w[0] == 'anim': ANIM.setdefault(path, []).append((float(w[1]), w[2]))  # the animation blueprint's flags (1.5-test3)
        elif w[0] == 'frame':
            v = [float(x) for x in w[5:]]
            frames.append(dict(t=float(w[1]), ragdoll=int(w[2]), sim=int(w[3]), dead=int(w[4]), body=[v[7 * k:7 * k + 7] for k in range(21)]))
    return head, frames


def tpose(P):
    """Arms out straight at shoulder height: hands far apart, each near its shoulder's height, elbows straight."""
    span = math.dist(P[16][:3], P[20][:3])
    level = max(abs(P[16][2] - P[14][2]), abs(P[20][2] - P[18][2]))
    def straight(s, e, h):
        u = [P[e][i] - P[s][i] for i in range(3)]; v = [P[h][i] - P[e][i] for i in range(3)]
        d = sum(a * b for a, b in zip(u, v)) / (math.sqrt(sum(a * a for a in u)) * math.sqrt(sum(a * a for a in v)) + 1e-9)
        return d > 0.9
    # (upright: a body lying with its arms spread is not a T-pose, the arms are at shoulder height there too)
    upright = P[12][2] - P[0][2] > 0.4
    return upright and span > 1.3 and level < 0.2 and straight(14, 15, 16) and straight(18, 19, 20)


def refpose(P, floor=None):
    """The mesh's reference pose, as Ready or Not's T-pose looks: upright, both arms dead straight hanging some 0.45 m
    below the shoulders (an animation keeps the elbows bent; a standing body this straight-armed is the empty pose)."""
    def straight(s, e, h):
        u = [P[e][i] - P[s][i] for i in range(3)]; v = [P[h][i] - P[e][i] for i in range(3)]
        return sum(a * b for a, b in zip(u, v)) / (math.sqrt(sum(a * a for a in u)) * math.sqrt(sum(a * a for a in v)) + 1e-9)
    upright = P[12][2] - P[0][2] > 0.6 and (floor is None or P[0][2] - floor > 0.7)  # standing (a corpse slumped sitting hangs its arms too)
    return upright and straight(14, 15, 16) > 0.95 and straight(18, 19, 20) > 0.95 and all(-0.55 < P[h][2] - P[s][2] < -0.3 for s, h in ((14, 16), (18, 20)))


def stats(path):
    head, fr = read_tail(path)
    rec_path = path[:-5] + '.rec'
    out = dict(name=os.path.basename(path), why=head[0] if head else '?', state=head[1] if head and len(head) > 1 else '?', secs=fr[-1]['t'] if fr else 0)
    if len(fr) < 3: return out
    # The hand-over: the recording's last pose of the game's bodies against the tail's first.
    jump = None
    if os.path.exists(rec_path):
        r = Rec(rec_path)
        if r.frames: jump = max(math.dist(r.frames[-1]['body'][k][:3], fr[0]['body'][k][:3]) for k in range(21))
    snaps, twitch, teleports, tposed, flips = [], 0, [], None, []
    ref_frames = []
    _r = Rec(rec_path) if os.path.exists(rec_path) else None
    floor = _r.frames[0]['floor'] if _r and _r.frames else None
    rec_ref = sum(refpose(f['body'], floor) for f in _r.frames[-10:]) if _r else 0
    w_prev = None
    for i in range(1, len(fr)):
        dt = fr[i]['t'] - fr[i - 1]['t'] or 1 / 60
        a, b = fr[i - 1]['body'], fr[i]['body']
        w = {k: angle(rel(a, k), rel(b, k)) / dt for k in LIMBS}
        for k, s in w.items():
            if s > 25: snaps.append((round(fr[i]['t'], 2), NAMES[k]))
        if w_prev:
            # (a reversal by sign needs the axis: approximated by the spin dropping and rising fast within a frame)
            for k in LIMBS:
                if w_prev[k] > 6 and w[k] > 6 and angle(rel(fr[i - 2]['body'] if i >= 2 else a, k), rel(b, k)) < 0.3 * (w_prev[k] + w[k]) * dt:
                    twitch += 1
        w_prev = w
        mean = [sum(b[k][j] - a[k][j] for k in range(21)) / 21 / dt for j in range(3)]
        if math.sqrt(sum(c * c for c in mean)) > 9: teleports.append(round(fr[i]['t'], 2))
        if tposed is None and tpose(b): tposed = round(fr[i]['t'], 2)
        if refpose(b, floor): ref_frames.append(round(fr[i]['t'], 2))
        for key in ('ragdoll', 'sim', 'dead'):
            if fr[i][key] != fr[i - 1][key]: flips.append(f"{key}{fr[i - 1][key]}>{fr[i][key]}@{fr[i]['t']:.2f}")
    # The hand-over into the animation as the player saw it: the biggest one-frame step of any part in the tail's first
    # 0.35 s (a body handed straight to the animation jumped 0.1-0.6 m in one frame; a blended hand-over moves in steps).
    step = max((max(math.dist(fr[i - 1]['body'][k][:3], fr[i]['body'][k][:3]) for k in range(21))
                for i in range(1, len(fr)) if fr[i]['t'] <= 0.35), default=0)
    out.update(step=round(step, 2),
               jump=round(jump, 2) if jump is not None else None, snaps=len(snaps), first_snaps=snaps[:3], twitch=twitch, teleports=teleports[:3],
               tpose=tposed, refpose=(len(ref_frames), ref_frames[:1], rec_ref), flags=f"ragdoll {fr[0]['ragdoll']} sim {fr[0]['sim']} dead {fr[0]['dead']}", flips=flips[:4])
    return out


if __name__ == '__main__':
    paths = []
    for p in sys.argv[1:] or [GAME]:
        if os.path.isdir(p): paths += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith('.tail'))
        else: paths.append(p)
    rows = [stats(p) for p in paths]
    rows.sort(key=lambda r: -((r.get('tpose') is not None) * 100 + len(r.get('teleports', [])) * 10 + r.get('snaps', 0) + (r.get('jump') or 0) * 10))
    for r in rows:
        print(f"{r['name'][:56]:57}{r['state']:6}{r['why'][:22]:23} jump {r.get('jump')} step {r.get('step')} snaps {r.get('snaps')} twitch {r.get('twitch')} "
              f"tpose {r.get('tpose')} refpose(tail frames, first, rec last10) {r.get('refpose')} teleports {r.get('teleports')} {r.get('flags')} {' '.join(r.get('flips', []))}")
        p = os.path.join(sys.argv[1] if len(sys.argv) > 1 and os.path.isdir(sys.argv[1]) else GAME, r['name'])
        if ANIM.get(p): print('      anim:', ' '.join(f'{t:.2f}:{f}' for t, f in ANIM[p][:6]))
    if rows:
        print(f"\n{len(rows)} tails: T-pose {sum(r.get('tpose') is not None for r in rows)}, whole-body jumps {sum(bool(r.get('teleports')) for r in rows)}, "
              f"hand-over jump > 0.1 m {sum((r.get('jump') or 0) > 0.1 for r in rows)}")
