"""A table of RoNDelirium's recordings: what each looked like in numbers (the game's bodies, what the player saw), the
suspicious ones first, the marked ones (F9) flagged.
usage: recstats.py [recordings dir or .rec files ...] [--json out.json]
(default: the folder in RON_RECORDINGS)"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rec import Rec, stats, find

GAME = os.environ.get('RON_RECORDINGS', '')
args = sys.argv[1:]
out = None
if '--json' in args:
    i = args.index('--json'); out = args[i + 1]; del args[i:i + 2]
assert args or GAME, 'give the recordings, or set RON_RECORDINGS to their folder'
rows = [stats(Rec(p)) for p in find(args or [GAME])]
rows.sort(key=lambda s: (not s['marked'], -len(s['flags']), -s['jerk']))
print(f"{'':2}{'recording':58}{'alive':>6}{'secs':>6}{'shots':>6}{'fell':>6}{'jerk':>6}{'hops':>5}{'lifts':>6}{'gap':>6}  end / flags")
for s in rows:
    fell = f"{s['fell_at']:.1f}" if s['fell_at'] is not None else '-'
    print(f"{'M ' if s['marked'] else '  '}{s['name'][:57]:58}{'yes' if s['alive'] else 'no':>6}{s['secs']:6.1f}{s['shots']:6}{fell:>6}{s['jerk']:6}"
          f"{s['hops']:5}{s['foot_lifts']:6}{s['max_gap']:6.2f}  {s['end']}{'  << ' + ', '.join(s['flags']) if s['flags'] else ''}")
if rows:
    n = len(rows)
    print(f"\n{n} recordings, {sum(s['marked'] for s in rows)} marked, {sum(bool(s['flags']) for s in rows)} flagged; "
          f"mean jerk {sum(s['jerk'] for s in rows) / n:.0f}, hops {sum(s['hops'] for s in rows) / n:.2f}, "
          f"fell {sum(s['fell_at'] is not None for s in rows) / n:.0%}")
if out: json.dump(rows, open(out, 'w'), indent=1)
