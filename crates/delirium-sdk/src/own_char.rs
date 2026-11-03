//! RoNDelirium: our own character, no game's file. character_own.txt (tools/make_character.py) describes 21 parts:
//! Ready or Not's proportions standing straight, anatomical segment masses, Ready or Not's shapes, anatomical joint
//! ranges and our own muscles. From it, the NM asset (joints, part frames, masses, self-collision) and the ragdoll
//! build come out the way the engine builds them from a game's skeleton and ragdoll (Build::new).
use crate::*;
use delirium_data::nm_asset;

static OWN_TABLE: &str = include_str!("character_own.txt");

struct Line {
    name: String,
    parent: Option<usize>,
    mass: f32,
    boxed: Option<[f32; 3]>,
    radius: f32,
    half: f32,
    centre: Vec3,
    frame: Mat3,
    pivot: Vec3,
    bone_z: Vec3,
    bone_x: Vec3,
    kind: String,
    ranges: [(f32, f32); 3],
    muscle: f32,
    torque: f32,
}

fn lines() -> Vec<Line> {
    OWN_TABLE.lines().filter(|l| !l.trim().is_empty() && !l.starts_with('#')).map(|l| {
        let w: Vec<&str> = l.split_whitespace().collect();
        let f = |i: usize| w[i].parse::<f32>().unwrap();
        let v = |i: usize| Vec3::new(f(i), f(i + 1), f(i + 2));
        let (boxed, radius, half, at) = if w[3] == "b" { (Some([f(4), f(5), f(6)]), 0.0, 0.0, 7) } else { (None, f(4), f(5), 6) };
        let frame = Mat3::from_cols(v(at + 3), v(at + 6), v(at + 9));
        let k = at + 21;
        let ranges = [(f(k + 1), f(k + 2)), (f(k + 3), f(k + 4)), (f(k + 5), f(k + 6))];
        Line { name: w[0].to_string(), parent: w[1].parse::<i32>().ok().filter(|&p| p >= 0).map(|p| p as usize), mass: f(2), boxed, radius, half,
               centre: v(at), frame, pivot: v(at + 12), bone_z: v(at + 15), bone_x: v(at + 18), kind: w[k].to_string(), ranges,
               muscle: f(k + 7), torque: f(k + 8) }
    }).collect()
}

/// Which way each joint group's axes point (the engine's behaviours set angles on them by its convention): the twist
/// axis' sign, the lean axis' sign, and whether the right side mirrors the left. NM_CHAR_SIGNS="hip=+-m,..." tries others.
/// The muscles' damping ratio (1 critical) and stiffness factor; NM_DAMP / NM_MS try others.
const DAMPING_RATIO: f32 = 1.0;
const LIMIT_SCALE: f32 = 1.0;
const TORQUE_SCALE: f32 = 1.0;
const MUSCLE_STIFFNESS: f32 = 1.0;

fn env_f(k: &str, d: f32) -> f32 {
    std::env::var(k).ok().and_then(|v| v.parse().ok()).unwrap_or(d)
}

/// Each joint's muscles over its group's (by joint: hip, knee, ankle, spine, neck, head, clav, shoulder, elbow, wrist);
/// NM_JOINT="knee=1.2,..." tries others.
const JOINT_SCALE: &str = "";

fn joint_scale(name: &str) -> f32 {
    joint_value(name, "NM_JOINT", JOINT_SCALE)
}

/// Each joint's muscle range width over character_own.txt's (NM_LIMJ="ankle=1.5,..." tries others).
const LIMIT_JOINT: &str = "";

fn joint_value(name: &str, var: &str, baked: &str) -> f32 {
    let group = [("Thigh", "hip"), ("Calf", "knee"), ("Foot", "ankle"), ("Spine", "spine"), ("Neck", "neck"), ("Head", "head"),
                 ("Clavicle", "clav"), ("UpperArm", "shoulder"), ("Forearm", "elbow"), ("Hand", "wrist")]
        .iter().find(|(k, _)| name.contains(k)).map_or("", |(_, g)| *g);
    let env = std::env::var(var).unwrap_or_default();
    baked.split(',').chain(env.split(',')).filter_map(|e| e.split_once('=')).filter(|(g, _)| *g == group)
        .filter_map(|(_, v)| v.parse::<f32>().ok()).last().unwrap_or(1.0)
}

/// The muscles' strength by group (none, legs, trunk, arms) over character_own.txt's.
const MUSCLE_SCALE: [f32; 4] = [1.0, 0.4608, 0.2867, 0.6];  // calibrated on behaviour against the original engine

const SIGNS: &str = "hip=++x,knee=-+,ankle=+-x,spine=++x,neck=++,head=++,clav=+-x2,shoulder=++x23,elbow=-+M,wrist=++x";

fn axis_signs(name: &str) -> (f32, f32, bool, [bool; 3]) {
    let group = [("Thigh", "hip"), ("Calf", "knee"), ("Foot", "ankle"), ("Spine", "spine"), ("Neck", "neck"), ("Head", "head"),
                 ("Clavicle", "clav"), ("UpperArm", "shoulder"), ("Forearm", "elbow"), ("Hand", "wrist")]
        .iter().find(|(k, _)| name.contains(k)).map_or("", |(_, g)| *g);
    let env = std::env::var("NM_CHAR_SIGNS").unwrap_or_default();
    let spec = SIGNS.split(',').chain(env.split(',')).filter_map(|e| e.split_once('=')).filter(|(g, _)| *g == group).map(|(_, v)| v).last().unwrap_or("++");
    let b = spec.as_bytes();
    let sign = |c: Option<&u8>| if c == Some(&b'-') { -1.0 } else { 1.0 };
    let (st, mut sl) = (sign(b.first()), sign(b.get(1)));
    let right = name.contains("_R_");
    if right && b.get(2) == Some(&b'm') { sl = -sl; }
    let st = if right && (b.get(2) == Some(&b'M') || b.get(3) == Some(&b'M')) { -st } else { st };
    if right && b.get(3) == Some(&b'm') { sl = -sl; }
    // 'x': the leans swapped (the lean axis the other way across the bone)
    // '1' '2' '3': the right side's angle about that axis read the other way (the asset's flip, for mirrored limbs)
    let flip = [right && spec.contains('1'), right && spec.contains('2'), right && spec.contains('3')];
    (st, sl, spec.contains('x'), flip)
}

/// The engine's asset space from Delirium's body space (nmrs::ours, its own inverse).
fn asset(v: Vec3) -> [f32; 3] {
    [-v.x, -v.y, v.z]
}

/// The NM asset: joints (one a part after the pelvis), the parts' frames, masses and self-collision pairs.
pub fn asset_of() -> (Vec<nm_asset::Joint>, Vec<[[f32; 3]; 4]>, Vec<f32>, Vec<(usize, usize)>) {
    let ls = lines();
    let joints = ls.iter().enumerate().skip(1).map(|(c, l)| {
        let hinge = l.kind == "hinge";
        // a hinge turns about its twist axis (the bone frame's x, across the limb); a ball twists along the bone
        let (twist, lean) = if hinge { (l.bone_x, l.bone_z) } else { (l.bone_z, l.bone_x) };
        let (st, sl, swap, flip) = axis_signs(&l.name);
        let lean = if swap && !hinge { twist.cross(lean) } else { lean };
        let ranges = if swap && !hinge { [l.ranges[1], l.ranges[0], l.ranges[2]] } else { l.ranges };
        let (twist, lean) = (twist * st, lean * sl);
        // the same anatomical ranges about the turned axes: an axis turned round mirrors its range
        let m = |(lo, hi): (f32, f32), s: f32| if s < 0.0 { (-hi, -lo) } else { (lo, hi) };
        let limits = if hinge { [m(ranges[0], st), ranges[1], ranges[2]] } else { [m(ranges[0], sl), m(ranges[1], st * sl), m(ranges[2], st)] };
        // The muscles' ranges centred on the standing rest: an effector's zero is its range's middle, and the behaviours
        // aim at angles from it. An anatomical range (the shoulder's, mostly forward and out) put that middle with the
        // arms raised, and the arms flew up into a cross. The physics keeps the anatomical limits (the bones').
        let widen = env_f("NM_LIMIT", LIMIT_SCALE) * joint_value(&l.name, "NM_LIMJ", LIMIT_JOINT);
        let limits = limits.map(|(lo, hi)| { let h = (hi - lo) * 0.5 * widen; (-h, h) });
        // NM_MUSCLE="scale" or "scale,legs,trunk,arms": development sweeps of the muscles' strength
        let tune: Vec<f32> = std::env::var("NM_MUSCLE").unwrap_or_default().split(',').filter_map(|x| x.parse().ok()).collect();
        let group = if l.name.contains("Thigh") || l.name.contains("Calf") || l.name.contains("Foot") { 1 }
                    else if l.name.contains("Spine") || l.name.contains("Neck") || l.name.contains("Head") { 2 } else { 3 };
        let s = l.muscle * MUSCLE_SCALE[group] * tune.first().copied().unwrap_or(1.0) * tune.get(group).copied().unwrap_or(1.0)
              * joint_scale(&l.name);
        nm_asset::Joint { name: format!("own_{}", l.name.trim_start_matches("Own_").to_lowercase()), index: (c - 1) as u32, three: !hinge,
                          pos: asset(l.pivot), twist_axis: asset(twist), lean_axis: asset(lean), limits, flip,
                          muscle_stiffness: env_f("NM_MS", MUSCLE_STIFFNESS), damping: 2.0 * s * env_f("NM_DAMP", DAMPING_RATIO), strength: s * s, max_torque: [l.torque * env_f("NM_TORQUE", TORQUE_SCALE); 2] }
    }).collect();
    // each part's frame as asset_frame reads it back: rows -row0(R), -row1(R), row2(R), then the centre
    let parts = ls.iter().map(|l| {
        let r = l.frame;
        [(-r.row(0)).to_array(), (-r.row(1)).to_array(), r.row(2).to_array(), asset(l.centre)]
    }).collect();
    let masses = ls.iter().map(|l| l.mass).collect();
    // legs against each other, arms against the body, legs and head
    let mut pairs = vec![];
    for a in [1, 2, 3] { for b in [4, 5, 6] { pairs.push((a, b)); } }
    // the upper arms hang against the chest at rest: only the forearms and hands meet the body (as Ready or Not's own)
    for a in [15, 16, 19, 20] { for b in [0, 1, 2, 4, 5, 7, 8, 9, 10, 12] { pairs.push((a, b)); } }
    for (a, b) in [(15, 19), (16, 20), (16, 19), (15, 20)] { pairs.push((a, b)); }
    (joints, parts, masses, pairs)
}

/// The ragdoll build, as Build::new makes one from a skeleton and its ragdoll parts.
pub fn build_of() -> (Build, Vec<Transform>) {
    let ls = lines();
    let pi = std::f32::consts::PI;
    let parts: Vec<delirium_data::ragdoll::Part> = ls.iter().enumerate().map(|(i, l)| {
        let (x, y, z) = (l.frame.x_axis, l.frame.y_axis, l.frame.z_axis);
        delirium_data::ragdoll::Part { bone: i, parent: l.parent, mass: l.mass, radius: l.radius, half: l.half,
                                       frame: [x.to_array(), y.to_array(), z.to_array(), l.centre.to_array()],
                                       kind: if l.boxed.is_some() { delirium_data::kv!(0 int) } else { 1 },
                                       extent: l.boxed.unwrap_or([0.0; 3]), inertia: [0.0; 3], length: 2.0 * l.half }
    }).collect();
    let bind: Vec<Transform> = ls.iter().map(|l| {
        let z = l.bone_z.normalize();
        let x = (l.bone_x - z * l.bone_x.dot(z)).normalize();
        Transform { translation: l.pivot, rotation: Quat::from_mat3(&Mat3::from_cols(x, z.cross(x), z)).normalize(), scale: Vec3::ONE }
    }).collect();
    let bones: Vec<delirium_data::skeleton::Bone> = ls.iter().enumerate().map(|(i, l)| {
        // free axes (x lateral, y forward, z twist) and their ranges, as a game skeleton's bone flags give them
        let free: &[usize] = match l.kind.as_str() { "ball" => &[0, 1, 2], "hinge" => &[0], _ => &[] };
        let mut flags = 0u32;
        let (mut lo, mut hi) = ([0.0f32; 3], [0.0f32; 3]);
        for &k in free {
            flags |= (16 << k) | (2 << k);
            let r = if l.kind == "hinge" { l.ranges[0] } else { l.ranges[k] };
            (lo[k], hi[k]) = (r.0.max(-pi), r.1.min(pi));
        }
        delirium_data::skeleton::Bone { name: l.name.clone(), parent: l.parent, id: i as u16, translation: bind[i].translation.to_array(),
                                        rotation: bind[i].rotation.to_array(), flags, rot_min: lo, rot_max: hi, block: [0.0; 4] }
    }).collect();
    let mut build = Build::new(&parts, &bind, &bones, delirium::nmrs::HOMER);
    let mut frames: Vec<Transform> = ls.iter().map(|l| Transform { translation: l.centre, rotation: Quat::from_mat3(&l.frame).normalize(), scale: Vec3::ONE }).collect();
    for (p, l) in build.parts.iter_mut().zip(&ls) {
        p.length = 2.0 * l.half;
    }
    delirium::nmrs::asset_layout(&mut build, &mut frames);
    (build, frames)
}

/// The character (make_ron_character: Skyrim Delirium's method, GPL-3, on Ready or Not's skeleton).
static RON_CHARACTER: &str = include_str!("character_ron.json");
/// Skyrim Delirium's own character (YoucefNabil/skyrim-delirium, plugin/data/skyrim_character.json, GPL-3.0-only: made
/// from the Skyrim skeleton and public anthropometry, no GTA IV data). The mod fits it to each suspect's bones.
#[cfg(feature = "skyrim-char")]
static SKYRIM_CHARACTER: &str = include_str!("character_skyrim.json");

fn ron_character() -> crate::character::Character {
    // NM_CHARFILE=<character.json>: a variant to try (development calibration), instead of the built-in one
    if let Ok(path) = std::env::var("NM_CHARFILE") {
        let text = std::fs::read_to_string(&path).expect("NM_CHARFILE");
        return crate::character::Character::parse(&text).expect("NM_CHARFILE json");
    }
    // Our own character (make_ron_character, Skyrim Delirium's method on Ready or Not's skeleton) by default: Skyrim
    // Delirium's, fitted to this skeleton, had its spine 3-8 times past its joints' limits at rest. NM_SKYRIMCHAR=1 for it.
    #[cfg(feature = "skyrim-char")]
    if std::env::var("NM_SKYRIMCHAR").is_ok() {
        return crate::character::Character::parse(SKYRIM_CHARACTER).expect("character json");
    }
    let text = RON_CHARACTER;
    crate::character::Character::parse(text).expect("character json")
}

/// NM_OLDCHAR=1: the first own character (character_own.txt), for comparisons.
fn old_character() -> bool {
    std::env::var("NM_OLDCHAR").is_ok()
}

/// RoNDelirium: our own character into the engine (after nm_unlock_own); 0 the first time, 1 if one was already in.
#[unsafe(no_mangle)]
pub extern "C" fn nm_init_own_character() -> i32 {
    if !delirium_data::kv::open() {
        return fail(NM_E_STATE, "nm_init_own_character: call nm_unlock_own first");
    }
    let fresh = delirium::nmrs::ASSET[delirium::nmrs::HOMER].get().is_none();
    let (j, p, m, s) = if old_character() { asset_of() } else { ron_character().asset() };
    delirium::nmrs::set(delirium::nmrs::HOMER, j, p, m, s);
    if fresh { 0 } else { 1 }
}

/// RoNDelirium: our own character's build (after nm_init_own_character); free with nm_build_destroy.
#[unsafe(no_mangle)]
pub extern "C" fn nm_build_own() -> *mut NmBuild {
    let (build, frames) = if old_character() { build_of() } else { ron_character().build().expect("character_ron.json has its build") };
    Box::into_raw(Box::new(NmBuild { build, frames }))
}
