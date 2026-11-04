"""Writes a calibration's result into the fork: the character keys (charvariant.py) into character_ron.json, the kv
slots into kv_own.txt (each line's value, its note saying where it came from and what it was), and prints 'dying' for
the mod's dying_stiffness (delirium.ini and its default in rondelirium.hpp, by hand).
usage: bake.py <tries K=V,...> <note>"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FORK = os.path.join(HERE, '..', '..', '..', 'crates')
CHAR = os.path.join(FORK, 'delirium-sdk', 'src', 'character_ron.json')
KV = os.path.join(FORK, 'delirium-data', 'src', 'kv_own.txt')

tries = {k: float(v) for k, v in (a.split('=') for a in sys.argv[1].split(',') if a)}
note = sys.argv[2]
char = [f'{k}={v:.6g}' for k, v in tries.items() if not k.isdigit() and k not in ('dying', 'force')]
if char:
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'charvariant.py'), CHAR, CHAR, *char], check=True)
    import json
    json.dump(json.load(open(CHAR, encoding='utf-8')), open(CHAR, 'w', encoding='utf-8'), indent=2)  # as the file was laid out
    print('character_ron.json:', ' '.join(char))
lines = open(KV, encoding='utf-8').read().split('\n')
for k, v in tries.items():
    if not k.isdigit(): continue
    i = next(i for i, l in enumerate(lines) if re.match(rf'^{k}\s', l))
    m = re.match(r'^(\d+)(\s+)(\S+)(\s*)(#.*)?$', lines[i])
    old, comment = m.group(3), (m.group(5) or '# ')
    comment = re.sub(r'\s*\[calibrated[^\]]*\]', '', comment)
    lines[i] = f'{k} {v:.6g}'.ljust(27) + comment + f' [{note}, was {old}]'
    print(lines[i])
open(KV, 'w', encoding='utf-8').write('\n'.join(lines))
if 'force' in tries: print('push_scale =', f"{tries['force']:.3g}", '(delirium.ini and rondelirium.hpp by hand)')
if 'dying' in tries: print('dying_stiffness =', f"{tries['dying']:.3g}", '(delirium.ini and rondelirium.hpp by hand)')
