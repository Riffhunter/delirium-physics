# RoNDelirium: Delirium Physics for Ready or Not

This is a fork of [Delirium Physics](https://github.com/PVCPC/delirium-physics) by PVCPC (GPL-3.0), modified for
**RoNDelirium**, a UE4SS mod that drives Ready or Not's suspects' and civilians' ragdolls with Delirium. Changed from
November 2026 on. It's released under the same licence (GPL-3.0-only).

Unlike upstream Delirium, the DLL this fork builds needs **no other game**: it has its own engine values and its own
character, and none of either is taken from any game.

## What's changed

- **Own engine values** (`crates/delirium-data/src/kv_own.txt`): a value for each slot the SDK's paths read, written
  for this fork from what each slot does in the code, and calibrated on behaviour; `nm_unlock_own` opens the engine
  with them (`crates/delirium-data/src/kv.rs`).
- **Own character** (`crates/delirium-sdk/src/character_ron.json`): made from Ready or Not's suspect skeleton
  (`rondelirium/tools/ron_joints.json`) by `crates/delirium-sdk/examples/make_ron_character.rs`, a port of
  [Skyrim Delirium](https://github.com/YoucefNabil/skyrim-delirium)'s `make_character.rs` by YoucefNabil (GPL-3.0).
  Loaded by `nm_init_own_character` and `nm_build_own` (`crates/delirium-sdk/src/own_char.rs`).
- **SDK entry points for the mod** (`crates/delirium-sdk/src/lib.rs`): the shot task as a game runs it
  (`nm_agent_shot`, `nm_agent_shot_update`), fitting a build to a game's skeleton (`nm_build_fit`,
  `nm_build_with_pose_of`, `nm_build_with_joints_of`), per-agent random numbers (`nm_agent_set_seed`), world meshes and
  boxes, and diagnosis (`nm_debug_*`).
- **The mod** (`rondelirium/`): the UE4SS C++ mod (`rondelirium/mod/src`), its settings (`rondelirium/mod/delirium.ini`)
  and build files.

Each changed source file marks its changes with "RoNDelirium".

## Building

**delirium_physics.dll** (Rust, from [rustup.rs](https://rustup.rs)):

    cargo build --release -p delirium-sdk

→ `target/release/delirium_physics.dll`

**main.dll** (the mod): needs Visual Studio Build Tools (C++), CMake and Ninja, and
[RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS)'s source at commit 83330635 ("experimental-latest", UE4SS 3.0.1 beta)
checked out as `rondelirium/RE-UE4SS`. Then run `rondelirium/build.bat` (edit the vcvars64.bat path in it for your
Visual Studio) → `rondelirium/build/mod/RoNDelirium.dll`, installed as
`ReadyOrNot/Binaries/Win64/ue4ss/Mods/RoNDelirium/dlls/main.dll` next to `delirium_physics.dll`.
`cmake -DRONDELIRIUM_DEV=ON` adds the developer's tools; releases leave them out.

**The character** is made with:

    cargo run --release --example make_ron_character -- rondelirium/tools/ron_joints.json crates/delirium-sdk/src/character_ron.json

The `character_ron.json` in this repository has since been calibrated on behaviour (joint axes and ranges of the neck,
spine, arms and legs, part frames and centres, masses), so regenerating it gives the uncalibrated character.

## Credits

- [Delirium Physics](https://github.com/PVCPC/delirium-physics) by PVCPC, GPL-3.0.
- [Skyrim Delirium](https://github.com/YoucefNabil/skyrim-delirium) by YoucefNabil, GPL-3.0: the method of the character.
- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS), MIT.
