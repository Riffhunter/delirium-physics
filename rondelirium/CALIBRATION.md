# Calibrating a physics character against a reference

This is how RoNDelirium's reactions were brought close to GTA IV's without using its numbers or its character: record
real situations in the game, replay each one offline on the reference and on your own character, measure the
differences, and let a search change your values until they shrink. It's written for modders who want to reuse the
method, in Ready or Not or in another game.

The tools are in `rondelirium/tools/calibration` (Python 3; `cma` also needs numpy). Everything here was made with
[Claude Code](https://claude.com/claude-code), reviewed and tested in game by the mod's author.

## What it got to, honestly

The cost is a sum of 14 scaled differences. Lower is better, and 0 would be identical. The first three are on the same
150 cases:

| | Cost |
|---|---|
| Early calibration with coordinate descent (mod 1.1) | 0.79 |
| Search on the character's arms and 14 engine values (v3) | 0.72 |
| Search on neck, head and spine (v4, mod 1.3) | 0.68 |
| The reference against itself, other random seeds: the most you can expect (184 cases) | 0.076 |

Those use the older reference. On the current one, over the 184 cases and with the three direction terms added below, mod 1.3 scores
0.818 and mod 1.4 0.778. On the cases the search never saw, 1.3 scores 0.836 and 1.4 0.791 (mean of seven runs with each number
shaken by about 1%, because the system's chaotic). 1.4 is still worse on the head's kick (0.087 against 0.079). The author's in-game
tests liked its reactions.

So it's close in play, and the author's in-game tests call it almost the same, but it isn't solved. On 184 cases,
mod 1.3 follows the reference well on falls and how far they travel, and hardly at all on the head's whip:

| Correlation across cases | Ours | Reference against itself |
|---|---|---|
| When they fall | 0.94 | 0.99 |
| Distance travelled | 0.88 | 0.99 |
| Pushed along the bullet | 0.85 | 1.00 |
| Head thrown, first 0.1 s | 0.78 | 1.00 |
| Arm positions | 0.76 | 0.99 |
| Chest turn, first 0.1 s | 0.43 | 0.99 |
| Head whip against the chest | 0.00 | 0.99 |

## The rules this keeps to

- **Nothing of the reference ships.** The repository and the mod hold our own values and character, found by behaviour.
  Comparing behaviour is what the method needs; copying numbers across defeats its purpose.
- **Whoever calibrates needs their own copy of the game.** Players of the finished mod don't.
- **Keep what you export or measure to yourself:** `fred.xrt`, `build.txt`, and your `fit_ref*.json`. `.gitignore`
  covers `reference/`, `*.rec` and `fit_ref*.json`.
- **Licence:** the fork is GPL-3.0, so anything built from its code is too. Keep the credits in `RONDELIRIUM.md`.

## Why this works here, and what you need elsewhere

Both sides run on the same Delirium engine through the same calls, so one recording can be replayed on either, and the
two runs differ only in their numbers and character. That's what makes small differences measurable.

If your reference is something you can't replay, such as another game's Havok setup, you can't put it through the same
hit. You'd have to compare distributions against captures of it (same weapon and hit area, not the same case), which is
much noisier. `calrec.py`'s measures only need a list of poses per frame, so that part would carry over, but expect to
need many more cases. A reference doesn't have to be a game either: `--engine json:<character.json>` replays on any
character file, so one you already like can be the target.

## The steps, for Ready or Not

Build the DLL and, for the reference, export its character from your own game:

```
cargo build --release -p delirium-sdk
NM_EXPORT=reference cargo run --release -p delirium-room -- --headless 1 --game "<folder with GTAIV.exe>"
```

Then point the tools at it, from `rondelirium/tools/calibration` (`D` is the DLL you built):

```
export DELIRIUM_GTA_DIR="<folder with GTAIV.exe>"
export DELIRIUM_GTA_CHAR="<repository>/reference"
D=<repository>/target/release/delirium_physics.dll
```

**1. Record.** Set `record = 1` in `delirium.ini` and play: different weapons, distances and hit areas. Each reaction
writes a `.rec` file. Go for at least 100; ours has 184. Close anything heavy while playing, since uneven frame rates
end up in the recordings. `recstats.py <folder>` lists them, the suspicious ones first.

**2. Measure the reference, once.** It replays every case 8 times on the reference and saves its behaviour:

```
python calrec.py ref $D fit_ref.json <recordings folder>
```

**3. Check the ceiling and your starting point.**

```
python calrec.py self $D fit_ref.json
python calrec.py parts $D fit_ref.json force=1.45
```

`self` is the reference against itself, which is the best you can expect. `parts` is your cost, term by term.

**4. See what kind of wrong it is, before searching.**

```
python diagnose.py corr $D fit_ref.json force=1.45
python diagnose.py body $D fit_ref.json force=1.45
```

**5. See which values matter.** `sens` moves each engine value down and up alone. Character keys go as `key=down:up`:

```
python calrec.py sens $D fit_ref.json ../../../crates/delirium-data/src/kv_own.txt rn=-0.3:0.3 w_neck=0.7:1.5
```

**6. Search on half the cases.** `CALREC_SET=train` uses the even cases, and `val` the odd ones, which the search never
sees. Give it the keys that step 4 and 5 pointed at, all of a group together. This took 3 hours for 300 evaluations:

```
CALREC_SET=train CALREC_SIGMA=0.8 python calrec.py cma $D fit_ref.json ../../../crates/delirium-data/src/kv_own.txt \
  jr_neck_a,jr_neck_b,jr_neck_t,rn,rk,cs_head,w_neck_lower,w_neck_upper,force force=1.45 300 search.txt
```

The best result so far is always the last `best` line of the log.

**7. Validate on the other half.** If `val` doesn't improve along with `train`, the search learned the cases, not the
behaviour, so don't keep it.

```
CALREC_SET=val python calrec.py parts $D fit_ref.json "<the best line's values>"
```

**8. Write it in and rebuild.** `bake.py` puts character keys in `character_ron.json` and engine values in `kv_own.txt`.
It prints `force` and `dying`, which go by hand in `delirium.ini` and `rondelirium.hpp`.

```
python bake.py "<the best line's values>" "calibrated, what you searched"
```

**9. Stress it.** Hundreds of agents with random hits, deaths, stairs and frame rates. 0 bad is the bar:

```
python soak.py $D 1 100 10 soak1.json
```

**10. Play it.** The numbers can't see everything: the small hops off furniture only showed up in play, and the fix for
them (`lift_cap`) was a change to the mod, not a value to search for.

## What's measured

All of it is about what a player sees, averaged over a case's runs. `calrec.py` has the definitions:

| Term | What it compares |
|---|---|
| `arms` | Hands and elbows in the chest's frame while on their feet |
| `kick_chest`, `kick_hands`, `kick_head` | How much each moves in the first 0.1 s |
| `whip` | The head against the chest, first 0.2 s |
| `along` | The pelvis along the bullet after 1 s |
| `steps`, `toward` | Steps taken, and those towards the shooter |
| `reach` | The closest hand to the wound, first 1.5 s |
| `neck` | The hands' distance to the neck after 1.5 s |
| `elbow` | How bent the elbows are while on their feet |
| `fall`, `when`, `travel` | Whether they fall, when, and how far they go |
| `dir_chest`, `dir_pelvis`, `dir_head` | Chest and pelvis turn and the head against the chest, at 0.1, 0.2 and 0.4 s, in the body's own frame |

`CALREC_NP=<ref>` adds the same chest and head comparison with almost no push, case by case and averaged over the cases (make the
reference with `calrec.py npref`, and keep it to yourself like the other one). `old_cost` is the total without the direction terms,
to compare with older versions. `CALREC_FIT_WEIGHT=term=2` weighs a term more in a search only.

## What we learned

- **Single values stop helping early.** After the first rounds, no single neck or spine key moved the cost by more than
  0.013, yet searching 15 of them together gained 0.03. Joint axes, frames and ranges only work as a set, which is
  why `cma` and not `descend` found it.
- **Split the gap between character and numbers.** Running the reference's character on your engine values
  (`CALREC_ENGINE=gta_char python calrec.py parts $D fit_ref.json force=1.45`) shows how much of the gap each one holds.
  For us it gave 0.58 against the reference's 0.076 and our 0.69, so both still have a share.
- **Matching averages isn't matching behaviour.** Over all cases, the head whip's size is off by under 20%, yet its
  correlation with the reference is 0: it doesn't respond to where the bullet went. Check correlation across cases
  (`diagnose.py corr`).
- **Compare directions, not only sizes.** With sizes alone, a search can't see a head moving the wrong way by the right
  amount, and it was rewarding a floppy neck. The `dir_` terms fixed that. The whip's size is left out of the total a search
  minimises, since `dir_head` holds it with its direction.
- **Muscles' zero is the middle of the joint's range.** So moving a range's centre (`lim_<joint>_<axis>`) moves the posture
  the body settles into. Here that was the only strong lever on the trunk's hunch. Strength, damping, muscle stiffness and
  physical masses did nothing, and other keys moved it by 1-2 degrees. Too far and it explodes: the pose ends up outside the range.
- **Undo a search's best one key at a time.** A gate failed after a long search, and a second search weighted towards it
  overfitted and did worse on unseen cases. Putting one key back by hand (`cs_head=1`) and moving one neck range a little passed them
  all: the search had moved the head's centre for a small gain and paid in head kick.
- **Measure the noise before believing a gain.** Tiny changes move this cost by about 0.005, and a term like `travel` by 25%.
  Shake every number by about 1% several times and compare the spreads, for your candidate and for what it replaces.
- **An `unstable` flag is worth opening.** One case in ours goes non-finite in the first step after a resync, in about 2% of its
  replays, and costs 0.3 when it lands in a run. The rate is the same on the old values and the new, so it isn't the candidate's doing,
  and the mod hands such a body to the game's ragdoll. Its cause in the solver is still open. Count a rate over many seeds before
  blaming, or clearing, a candidate.
- **Measure by the route the game runs.** A diagnostic that loaded the character another way gave a different head position than the
  built-in route, enough to pass a candidate that failed. Check any new number against the final DLL before believing it.
- **Take the bullet away.** Replaying with almost no push (`diagnose.py body`) separates the body's own reaction from
  the impulse. A chest that seemed to go back too far turned out to be mostly a missing forward hunch, which no push
  value could have fixed.
- **Use a frame that doesn't depend on the character.** Part axes differ between characters. A frame from the shoulders
  and the trunk compares like with like.
- **Check the reference gets what it should.** Our recordings store the shot force with the mod's `push_scale` already
  in it, and the reference was replayed with that for a while, so it was being compared under the wrong push.
  `recorded_push` in `replay.py` is that fix, and the mod now writes `push` in each recording.
- **Fit on one half, judge on the other.** A generous search will otherwise find values that suit your cases only.
- **Keep the machine awake.** Evaluations slowed from 38 s to 147 s overnight, most likely because the PC slept. And
  don't run a search while playing: it disturbed the frame rate enough to make the test meaningless.

## Using it in another game

1. **Skeleton.** The character has 21 parts (`nm.hpp`, `rec.py`, `PARTS` in `diagnose.py`). Map your bones onto them, as
   `part_of` in `rondelirium.hpp` does, and write them as `rondelirium/tools/ron_joints.json` does.
2. **Character.** Generate it with `crates/delirium-sdk/examples/make_ron_character.rs`, the method of
   [Skyrim Delirium](https://github.com/YoucefNabil/skyrim-delirium), whose character is also a good reference to read.
3. **Engine values.** `kv_own.txt` doesn't depend on the skeleton, so it's the best thing to start from as it is.
4. **Driving the ragdoll.** `rondelirium/mod/src/rondelirium.hpp` shows one way (Delirium runs free and the game's
   ragdoll follows it). The calibration doesn't depend on that part.
5. **Recording.** The tools need the mod's calls and poses as `rec.py` describes. `nm::tape` in `nm.hpp` records every
   Delirium call, so copy it along with the writer in `rondelirium.hpp`.
6. **Calibrate.** Our `character_ron.json` is fitted to Ready or Not's skeleton, so expect to redo the search on yours.
   Start from the groups that mattered for us: arm roll and ranges, then neck, head and spine.

[`docs/modding.md`](docs/modding.md) and [`docs/how-it-works.md`](docs/how-it-works.md) cover the SDK and the engine.
