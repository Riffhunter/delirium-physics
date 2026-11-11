// Delirium driving Ready or Not's ragdolls: Delirium runs free with its own body and the game's Chaos
// ragdoll follows it as a puppet (see tick). (Delirium's torques alone, applied to Chaos's bodies, shook them: its
// joints carry an armature, a large added inertia Chaos's don't.)
//
// Spaces: the game is centimetres, left-handed, Z up; Delirium metres, right-handed, Z up. Mirroring Y converts:
// points and velocities (x, -y, z) / 100, rotations (x, y, z, w) -> (-x, y, -z, w), angular velocities and torques
// (-x, y, -z).
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include "nm.hpp"
#include "ufn.hpp"

namespace rondelirium
{
    using namespace RC;
    using namespace RC::Unreal;
    using ufn::DQuat;
    using ufn::DTransform;
    using ufn::DVec;

    // A weak pointer, safely on any UE4SS. An object with no serial number yet gets one through a soft reference, and until
    // UE4SS's 1b4af88 (so on the build SWAT Unleashed requires) UE4SS copied that reference with the wrong layout on UE 5.3:
    // it read garbage and now and then crashed. Asked by reflection, the engine gives it its number and nothing's copied.
    inline FWeakObjectPtr weak(UObject* object)
    {
        FWeakObjectPtr out;
        FUObjectItem* item = object ? FUObjectArray::IndexToObject(object->GetInternalIndex()) : nullptr;
        if (!item) return out;
        if (!item->GetSerialNumber())
        {
            static UObject* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
            static UFunction* soft = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:Conv_ObjectToSoftObjectReference"));
            if (library && soft) ufn::Call(soft).set<UObject*>(STR("Object"), object).invoke(library);
        }
        if (item->GetSerialNumber()) out = object;  // (with no number, empty: as if gone, rather than UE4SS's way)
        return out;
    }

    // ---- settings (delirium.ini beside the mod's dlls folder) ----

    struct Settings
    {
        bool enabled = true;
        float duration = 3.0f;    // seconds Delirium drives a dead body before handing it back
        float health = 60.0f;     // the dying reaction's health: 50-70 staggers a moment and goes down; 100 and up never falls
        float dead_max = 8.0f;    // seconds at most a dead body stays Delirium's while bullets keep coming
        float fade = 1.5f;        // the last seconds of a dead body's drive: Delirium's hold eases off, the ragdoll goes limp gently
        bool headshot_limp = true;     // killed by a bullet in the head or neck: no dying reaction, the body drops lifeless
        float headshot_duration = 0.0f;  // seconds Delirium still holds such a body, easing off over all of them (0 = let go at once)
        float track_pos = 8.0f;     // 1/s: how fast a body is pulled onto its Delirium part's place
        float track_rot = 12.0f;    // 1/s: likewise its turn
        float push_scale = 1.556f;  // the bullet's push: the mod's own for the kind of gun (weapon_push) times this (1.556: the value the character was fitted with)
        float resync = 0.4f;        // m: Delirium's pelvis this far from the ragdoll's, and Delirium is set from the ragdoll again
        float max_kick = 2.0f;      // m/s a body's velocity may change in one frame: a guard against jolts
        float max_spin_kick = 10.0f;  // rad/s likewise for its spin
        float follow = 1.0f;        // how much of Delirium's velocity each body takes per frame (0-1): lower is softer
        bool stagger = true;        // a live suspect or civilian who's shot goes into the game's own short ragdoll, Delirium's shot reaction driving it
        float stagger_time = 2.5f;  // seconds of the stagger before the animation takes back over
        float blend_out = 0.8f;     // seconds blending from the staggered body back into the animation
        float stagger_health = 200.0f;  // the shot task's health for an unhurt character: fights to stay up
        float weak_health = 70.0f;      // and for one nearly out of the game's health: goes down (GTA IV's falls below ~100)
        // Only one hurt enough staggers: with more of the game's health left than this (0-1), a bullet gets the game's
        // own flinch, and they fight on. A leg always staggers them (it gives under them).
        float react_below = 1.01f;
        // The chance (0-1) a staggering suspect fires back after a bullet, blind, where the gun in their hand points
        // (as GTA IV's do): a burst of fire_burst seconds, a moment after the hit.
        float fire_back = 0.6f;
        float fire_burst = 1.0f;
        bool leg_always = true;
        // The game's health left can also scale the stagger, from unhurt (healthy_*) to nearly gone (the full values).
        // Off by default (the values below change nothing): a short stagger, or one half the animation's, twitched.
        float healthy_stagger = 2.5f;  // seconds an unhurt one staggers (stagger_time when nearly gone)
        float healthy_blend = 1.0f;    // how much of an unhurt one's body the physics takes (the rest stays the animation's)
        float healthy_push = 1.0f;     // the bullet's push on an unhurt one, times push_scale
        float weak_push = 1.0f;        // and on one nearly gone
        float recover_time = 0.0f;     // seconds after a stagger an unhurt one shrugs off the next bullet (none when nearly gone)
        float leg_injury = 0.9f;   // how much a rifle bullet in a leg weakens it (0-1): ~0.5 and up it gives, they kneel or go down
        float arm_injury = 0.4f;    // likewise an arm: it hangs
        float grab_time = 1.2f;     // seconds a hand holds the wound (GTA IV's 3)
        float settle_after = 1.6f;  // a stagger still on its feet and settled this long after the hit goes back to the animation
        bool walls = true;         // the walls around found by traces for Delirium (the level's big meshes have no box of their own)
        bool corpses = true;       // the bodies lying near as boxes for Delirium (one falling on another lay on it, not through it)
        float rebuild_world = 1.0f;  // metres the body goes from where Delirium's world was made before it's made again there (0 = never)
        bool log = true;
        bool record = true;        // each activation written to recordings/ (tools/recstats.py, replay.py, recviewer.html)
        int record_keep = 300;     // the newest this many kept (marked ones always)
        // The shot reaction's body stiffness for a dying body (0: the same as alive). The usual one, calibrated on the
        // living, curled the dying into a ball with the arms pulled in at full strength: the hands ended at the neck.
        float dying_stiffness = 6.0f;
        float lift_cap = 1.0f;       // a dying body's centre of mass rises no faster than this (m/s; 0 = off): Delirium (GTA IV too) could make one push off the floor and fly
        bool trunk_at_zero = false;  // after the fit, the trunk's joints at their zero (for Skyrim Delirium's character, NM_SKYRIMCHAR)
        // The taser: Delirium's electrocute on the body while the game has them tased. Tuned by eye: GTA IV gave nothing to measure against.
        bool taser = true;
        bool taser_stiff = false;     // rigid as they shake (slower down, and the hands come to the face), or loose
        float taser_strength = 2.0f;  // each jolt's size
        float taser_interval = 0.07f; // seconds between jolts
        float taser_large = 2.0f;     // the bigger jolt every second or two, times this
        float taser_min = 1.0f;       // seconds at least, however short the game's stun
        float taser_max = 8.0f;       // and at most, or when the game's state can't be read
        float taser_health = 40.0f;   // the shot task's health while shocked: as stagger_health, lower gives way sooner (an unhurt one's 200 kept them walking)
        float taser_stiffness = 2.0f; // the shot task's body stiffness while shocked, as dying_stiffness: the living's pulled their hands to the face on the floor
        float capsule_speed = 4.0f;   // m/s at most the character's capsule moves following the staggering body
        // A killing bullet: Delirium takes the body at once, on the mesh's own physics, instead of waiting for the game's
        // death ragdoll (0.1-0.6 s later, its death animation playing until then: the canned fold over a shotgun blast).
        bool death_hit = true;
        bool keep_momentum = true;    // a stagger begins at the character's own speed (walking, running), not stopped dead
        bool pose_bridge = true;      // back to the animation through the pose they had when hit, driven, not into the T-pose
        float handover = 0.25f;       // seconds the bridge's last pose, held still, is blended into the live animation (0 = at once)
        bool handover_hand = false;   // the animation's left hand on the gun (its IK) eased in with the hand-over, not at once
        int dead_capsule = 2;         // a dead one's capsule: 0 left where they died, 1 brought to the body, 2 that and no collision
        float record_fps = 30.0f;     // poses recorded a second (0: every frame, to see a shimmer faster than 30 a second)
        float record_tail = 3.0f;     // seconds the game's bodies are still recorded after Delirium lets go (a .tail beside the .rec; 0 = none)
    };
    inline Settings cfg;

    inline void load_settings(const std::filesystem::path& file)
    {
        std::ifstream f(file);
        std::string line;
        while (std::getline(f, line))
        {
            auto eq = line.find('=');
            if (eq == std::string::npos || line[0] == ';' || line[0] == '#') continue;
            auto key = line.substr(0, eq);
            auto val = line.substr(eq + 1);
            key.erase(key.find_last_not_of(" \t") + 1);
            float v = std::strtof(val.c_str(), nullptr);
            if (key == "enabled") cfg.enabled = v != 0;
            else if (key == "duration") cfg.duration = v;
            else if (key == "health") cfg.health = v;
            else if (key == "fade") cfg.fade = v;
            else if (key == "headshot_limp") cfg.headshot_limp = v != 0;
            else if (key == "headshot_duration") cfg.headshot_duration = std::max(v, 0.0f);
            else if (key == "dead_max") cfg.dead_max = v;
            else if (key == "max_kick") cfg.max_kick = v;
            else if (key == "track_pos") cfg.track_pos = v;
            else if (key == "track_rot") cfg.track_rot = v;
            else if (key == "resync") cfg.resync = v;
            else if (key == "push_scale") cfg.push_scale = std::max(v, 0.0f);
            else if (key == "max_spin_kick") cfg.max_spin_kick = v;
            else if (key == "stagger") cfg.stagger = v != 0;
            else if (key == "stagger_time") cfg.stagger_time = v;
            else if (key == "blend_out") cfg.blend_out = std::max(v, 0.05f);
            else if (key == "handover") cfg.handover = std::clamp(v, 0.0f, 1.0f);
            else if (key == "handover_hand") cfg.handover_hand = v != 0;
            else if (key == "dead_capsule") cfg.dead_capsule = std::clamp(int(v), 0, 2);
            else if (key == "stagger_health") cfg.stagger_health = v;
            else if (key == "weak_health") cfg.weak_health = v;
            else if (key == "follow") cfg.follow = std::clamp(v, 0.0f, 1.0f);
            else if (key == "leg_injury") cfg.leg_injury = std::clamp(v, 0.0f, 0.95f);
            else if (key == "arm_injury") cfg.arm_injury = std::clamp(v, 0.0f, 0.95f);
            else if (key == "walls") cfg.walls = v != 0;
            else if (key == "corpses") cfg.corpses = v != 0;
            else if (key == "rebuild_world") cfg.rebuild_world = std::max(v, 0.0f);
            else if (key == "fire_back") cfg.fire_back = std::clamp(v, 0.0f, 1.0f);
            else if (key == "fire_burst") cfg.fire_burst = std::max(v, 0.0f);
            else if (key == "react_below") cfg.react_below = std::clamp(v, 0.0f, 1.01f);
            else if (key == "leg_always") cfg.leg_always = v != 0;
            else if (key == "healthy_stagger") cfg.healthy_stagger = std::max(v, 0.2f);
            else if (key == "healthy_blend") cfg.healthy_blend = std::clamp(v, 0.1f, 1.0f);
            else if (key == "healthy_push") cfg.healthy_push = std::max(v, 0.0f);
            else if (key == "weak_push") cfg.weak_push = std::max(v, 0.0f);
            else if (key == "recover_time") cfg.recover_time = std::max(v, 0.0f);
            else if (key == "grab_time") cfg.grab_time = std::max(v, 0.0f);
            else if (key == "settle_after") cfg.settle_after = std::max(v, 0.3f);
            else if (key == "log") cfg.log = v != 0;
            else if (key == "record") cfg.record = v != 0;
            else if (key == "record_keep") cfg.record_keep = std::max(int(v), 10);
            else if (key == "dying_stiffness") cfg.dying_stiffness = std::max(v, 0.0f);
            else if (key == "lift_cap") cfg.lift_cap = std::max(v, 0.0f);
            else if (key == "trunk_at_zero") cfg.trunk_at_zero = v != 0;
            else if (key == "taser_reaction") cfg.taser = v != 0;
            else if (key == "taser_stiff") cfg.taser_stiff = v != 0;
            else if (key == "taser_health") cfg.taser_health = std::max(v, 0.0f);
            else if (key == "taser_stiffness") cfg.taser_stiffness = std::max(v, 0.0f);
            else if (key == "taser_strength") cfg.taser_strength = std::max(v, 0.0f);
            else if (key == "taser_interval") cfg.taser_interval = std::max(v, 0.02f);
            else if (key == "taser_large") cfg.taser_large = std::max(v, 1.0f);
            else if (key == "taser_min") cfg.taser_min = std::max(v, 0.0f);
            else if (key == "taser_max") cfg.taser_max = std::max(v, 0.5f);
            else if (key == "capsule_speed") cfg.capsule_speed = std::max(v, 0.5f);
            else if (key == "death_hit") cfg.death_hit = v != 0;
            else if (key == "keep_momentum") cfg.keep_momentum = v != 0;
            else if (key == "pose_bridge") cfg.pose_bridge = v != 0;
            else if (key == "record_fps") cfg.record_fps = std::clamp(v, 0.0f, 240.0f);
            else if (key == "record_tail") cfg.record_tail = std::clamp(v, 0.0f, 10.0f);
        }
    }

    // ---- maths ----

    struct T { DVec p; DQuat q; };

    inline DVec operator+(DVec a, DVec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    inline DVec operator-(DVec a, DVec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    inline DVec operator*(DVec a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    inline DVec cross(DVec a, DVec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    inline double len(DVec a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
    inline DVec clamp_len(DVec a, double m) { double l = len(a); return l > m && l > 0 ? a * (m / l) : a; }
    inline DQuat mul(DQuat a, DQuat b)
    {
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }
    inline DQuat conj(DQuat a) { return {-a.x, -a.y, -a.z, a.w}; }
    inline DVec rotate(DQuat q, DVec v)
    {
        DVec u{q.x, q.y, q.z};
        DVec t = cross(u, v) * 2.0;
        return v + t * q.w + cross(u, t);
    }
    inline T compose(const T& a, const T& b) { return {a.p + rotate(a.q, b.p), mul(a.q, b.q)}; }
    inline T inverse(const T& a) { auto qi = conj(a.q); return {rotate(qi, a.p) * -1.0, qi}; }

    // game <-> Delirium
    inline DVec to_nm_point(DVec v) { return {v.x / 100, -v.y / 100, v.z / 100}; }
    inline DVec to_ue_point(DVec v) { return {v.x * 100, -v.y * 100, v.z * 100}; }
    inline DQuat to_nm_rot(DQuat q) { return {-q.x, q.y, -q.z, q.w}; }
    inline DVec to_nm_axial(DVec v) { return {-v.x, v.y, -v.z}; }
    inline DVec to_ue_force(DVec f) { return {f.x * 100, -f.y * 100, f.z * 100}; }
    inline DVec to_ue_torque(DVec t) { return {-t.x * 1e4, t.y * 1e4, -t.z * 1e4}; }
    inline T to_nm(const DTransform& t) { return {to_nm_point(t.pos), to_nm_rot(t.rot)}; }

    // ---- the skeleton (PHYS_RoN_Player_Master_Shared_V2: suspects and civilians) ----

    // Each Delirium part's bone (its frame follows it) and the ragdoll body that carries it (the clavicles have none).
    inline const wchar_t* BONE[nm::PARTS] = {L"pelvis", L"thigh_LE", L"calf_LE", L"foot_LE", L"thigh_RI", L"calf_RI", L"foot_RI",
                                             L"spine_1", L"spine_1", L"spine_2", L"spine_3", L"neck_1", L"head",
                                             L"clavicle_LE", L"upperarm_LE", L"lowerarm_LE", L"hand_LE",
                                             L"clavicle_RI", L"upperarm_RI", L"lowerarm_RI", L"hand_RI"};
    inline const wchar_t* BODY[nm::PARTS] = {L"pelvis", L"thigh_LE", L"calf_LE", L"foot_LE", L"thigh_RI", L"calf_RI", L"foot_RI",
                                             L"spine_1", L"spine_1", L"spine_2", L"spine_3", L"neck_1", L"head",
                                             L"spine_3", L"upperarm_LE", L"lowerarm_LE", L"hand_LE",
                                             L"spine_3", L"upperarm_RI", L"lowerarm_RI", L"hand_RI"};

    // The part a hit bone belongs to (helper and twist bones included); the chest when nothing matches.
    inline int part_of(StringType bone)
    {
        std::transform(bone.begin(), bone.end(), bone.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        auto has = [&](const wchar_t* s) { return bone.find(s) != StringType::npos; };
        bool left = has(L"_le");
        if (has(L"head") || has(L"jaw") || has(L"eye")) return 12;
        if (has(L"neck")) return 11;
        if (has(L"hand") || has(L"finger") || has(L"thumb")) return left ? 16 : 20;
        if (has(L"lowerarm") || has(L"forearm")) return left ? 15 : 19;
        if (has(L"upperarm")) return left ? 14 : 18;
        if (has(L"clavicle")) return left ? 13 : 17;
        if (has(L"foot") || has(L"toe") || has(L"heel")) return left ? 3 : 6;
        if (has(L"calf")) return left ? 2 : 5;
        if (has(L"thigh")) return left ? 1 : 4;
        if (has(L"spine_3")) return 10;
        if (has(L"spine_2")) return 9;
        if (has(L"spine_1")) return 7;
        if (has(L"pelvis") || has(L"root")) return 0;
        return 10;
    }

    template <typename... Args>
    void log(const CharType* fmt, Args&&... args)
    {
        if (cfg.log) Output::send(fmt, std::forward<Args>(args)...);
    }

    // ---- calling the game ----

    inline FName name(const wchar_t* s) { return FName(s, FNAME_Add); }

    inline DTransform bone_world(UObject* mesh, const wchar_t* bone)
    {
        ufn::Call c(ufn::find(mesh, STR("GetSocketTransform")));
        c.set(STR("InSocketName"), name(bone)).set<uint8>(STR("TransformSpace"), 0).invoke(mesh);
        return c.get<DTransform>(STR("ReturnValue"));
    }

    inline DVec body_vec(UObject* mesh, const TCHAR* fn, const wchar_t* bone)
    {
        ufn::Call c(ufn::find(mesh, fn));
        c.set(STR("BoneName"), name(bone)).invoke(mesh);
        return c.get<DVec>(STR("ReturnValue"));
    }

    // ---- calibration: Delirium's body fitted to the skeleton's reference pose ----

    struct Calib
    {
        void* build = nullptr;
        std::array<T, nm::PARTS> off{};      // part frame in its bone's frame (Delirium space)
        std::array<float, nm::PARTS> mass{}; // the fitted parts' masses (kg)
        std::array<int32_t, nm::PARTS> parent{};
        std::array<DVec, nm::PARTS> anchor_parent{};  // each part's joint pivot in its parent's frame
        std::array<DVec, nm::PARTS> anchor{};         // and in its own
        T clavicle_l{}, clavicle_r{};  // the clavicle bones in spine_3's frame (reference pose): they have no ragdoll body
        std::array<float, nm::PARTS * 3> joints{};  // the skeleton's joints the build was fitted to (for the recordings)
        std::array<T, nm::PARTS> ref{};               // and its bones' reference pose (a replay fits another character with them)
        std::string asset;
        uint32_t posed = 0;  // the parts whose joints were set at their zero after the fit (recorded: a replay builds the same)
    };

    inline void* base_build = nullptr;
    inline const char* engine = "own";  // "gta": the developer's comparison runs (dev_gta.txt), never a release
    inline std::map<UObject*, Calib> calibs;  // by skeletal mesh asset

    // A bone's reference transform in component space (Delirium units), walking its parents.
    inline bool ref_pose(UObject* mesh, const wchar_t* bone, std::map<StringType, T>& memo, T& out)
    {
        if (auto it = memo.find(bone); it != memo.end()) { out = it->second; return true; }
        ufn::Call idx(ufn::find(mesh, STR("GetBoneIndex")));
        idx.set(STR("BoneName"), name(bone)).invoke(mesh);
        int32 i = idx.get<int32>(STR("ReturnValue"));
        ufn::Call ref(ufn::find(mesh, STR("GetRefPoseTransform")));
        if (i < 0 || !ref) return false;
        ref.set<int32>(STR("BoneIndex"), i).invoke(mesh);
        auto local = ref.get<DTransform>(STR("ReturnValue"));
        T t{local.pos, local.rot};  // still game units: converted once composed
        ufn::Call par(ufn::find(mesh, STR("GetParentBone")));
        par.set(STR("BoneName"), name(bone)).invoke(mesh);
        auto parent = par.get<FName>(STR("ReturnValue")).ToString();
        if (parent != STR("None") && !parent.empty())
        {
            T up;
            if (!ref_pose(mesh, parent.c_str(), memo, up)) return false;
            t = compose(up, t);
        }
        memo[bone] = t;
        out = t;
        return true;
    }

    inline Calib* calibrate(UObject* mesh)
    {
        ufn::Call asset_call(ufn::find(mesh, STR("GetSkeletalMeshAsset")));
        asset_call.invoke(mesh);
        UObject* asset = asset_call.get<UObject*>(STR("ReturnValue"));
        if (auto it = calibs.find(asset); it != calibs.end()) return it->second.build ? &it->second : nullptr;
        Calib& cal = calibs[asset];

        std::map<StringType, T> memo;
        std::array<T, nm::PARTS> bone{};
        for (int k = 0; k < nm::PARTS; ++k)
        {
            T t;
            if (!ref_pose(mesh, BONE[k], memo, t))
            {
                Output::send<LogLevel::Warning>(STR("[RoNDelirium] no reference pose for bone {}: this skeleton isn't supported\n"), BONE[k]);
                return nullptr;
            }
            bone[k] = {to_nm_point(t.p), to_nm_rot(t.q)};
        }
        T spine2;
        ref_pose(mesh, L"spine_2", memo, spine2);
        // Each part's joint with its parent; part 8 (Delirium's second spine part) has no bone: halfway up spine_1.
        float joints[nm::PARTS * 3];
        for (int k = 0; k < nm::PARTS; ++k)
        {
            DVec j = k == 8 ? (bone[7].p + to_nm_point(spine2.p)) * 0.5 : bone[k].p;
            joints[3 * k] = static_cast<float>(j.x);
            joints[3 * k + 1] = static_cast<float>(j.y);
            joints[3 * k + 2] = static_cast<float>(j.z);
        }
        std::copy(joints, joints + nm::PARTS * 3, cal.joints.begin());
        cal.ref = bone;
        if (asset) for (wchar_t ch : asset->GetName()) cal.asset += char(ch < 128 ? ch : '?');
        void* fitted = nm::api.build_fit(base_build, joints);
        if (!fitted)
        {
            Output::send<LogLevel::Error>(STR("[RoNDelirium] nm_build_fit: {}\n"), to_wstring(nm::api.error()));
            return nullptr;
        }
        // The trunk (spine, neck, head) standing as the character's own joints have it at their zero, the limbs as
        // fitted: turned to this skeleton's spine, the joints were 3-8 times past their limits before any bullet, and
        // the first frame of every reaction snapped them back, the whole upper body turning as one block (stiff, the
        // arms flung back). With GTA IV's own character (dev_gta.txt) its build is left as fitted.
        constexpr uint32_t TRUNK = (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12);
        if (std::string_view(engine) == "own" && cfg.trunk_at_zero)
        {
            if (void* posed = nm::api.build_with_joints_of(fitted, base_build, TRUNK)) fitted = posed, cal.posed = TRUNK;
        }
        float total = 0;
        for (int k = 0; k < nm::PARTS; ++k)
        {
            float p[3], q[4], radius, half;
            int32_t parent;
            nm::api.build_part_frame(fitted, k, p, q);
            nm::api.build_part_info(fitted, k, &cal.mass[k], &radius, &half, &parent);
            total += cal.mass[k];
            T part{{p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}};
            cal.off[k] = compose(inverse(bone[k]), part);
        }
        // The joints' pivots in their parts' frames, from an agent standing in the fitted pose: the state handed to
        // Delirium is put together through them, so its joints never start apart (its solver would snap them back).
        float origin[3]{};
        void* probe = nm::api.agent_create(fitted, origin, 0.0f, 0.3f);
        std::array<T, nm::PARTS> stand{};
        for (int k = 0; k < nm::PARTS; ++k)
        {
            float p[3], q[4];
            nm::api.agent_get_part_transform(probe, k, p, q);
            stand[k] = {{p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}};
        }
        for (int k = 1; k < nm::PARTS; ++k)
        {
            int32_t parent;
            float pivot[3], frame[4], limits[3], arm[3];
            nm::api.agent_get_joint(probe, k, &parent, pivot, frame, limits, arm);
            DVec at{pivot[0], pivot[1], pivot[2]};
            cal.parent[k] = parent;
            cal.anchor_parent[k] = rotate(conj(stand[parent].q), at - stand[parent].p);
            cal.anchor[k] = rotate(conj(stand[k].q), at - stand[k].p);
        }
        nm::api.agent_destroy(probe);
        cal.clavicle_l = compose(inverse(bone[10]), bone[13]);
        cal.clavicle_r = compose(inverse(bone[10]), bone[17]);
        cal.build = fitted;
        log(STR("[RoNDelirium] calibrated {}: fitted body {:.1f} kg, head {:.2f} m up\n"), asset ? asset->GetName() : StringType(STR("?")), total,
            bone[12].p.z - bone[3].p.z);
        return &cal;
    }

    // ---- agents ----

    // One pellet of a shotgun blast: where it landed and which way it went (Delirium space).
    struct Pellet
    {
        StringType bone;
        DVec at{};
        DVec dir{};
    };

    // A bullet's hit, from Multicast_SpawnBloodEffects' HitResult (Delirium space).
    struct Hit
    {
        StringType bone;
        DVec at{};
        DVec dir{};
        DVec from{};                    // where the bullet came from: the shooter's eyes (looked at)
        const char* weapon = nullptr;   // the kind of gun (pistol, smg, rifle, battle rifle, shotgun): the bullet's push
        int count = 1;                  // bullets that landed together, in the same frame (a shotgun's pellets): their pushes add up
        std::vector<Pellet> pellets;    // those bullets, each pushing the part it hit (the ones in this hit's part go with the shot)
    };

    // The kind of the shooter's gun: Ready or Not's weapon subclass (as RoNGore reads it).
    inline const char* weapon_of(UObject* instigator)
    {
        if (!instigator) return nullptr;
        UObject* holders[2] = {instigator, nullptr};
        if (auto pawn = instigator->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn")); pawn && *pawn) holders[1] = *pawn;
        for (UObject* h : holders)
        {
            auto inv = h ? h->GetValuePtrByPropertyNameInChain<UObject*>(STR("InventoryComp")) : nullptr;
            auto gun = inv && *inv ? (*inv)->GetValuePtrByPropertyNameInChain<UObject*>(STR("LastEquippedWeapon")) : nullptr;
            auto sub = gun && *gun ? (*gun)->GetValuePtrByPropertyNameInChain<uint8>(STR("Subclass")) : nullptr;
            if (!sub) continue;
            switch (*sub)
            {
            case 9: return "pistol";
            case 4:
            case 5: return "smg";
            case 1:
            case 3: return "rifle";
            case 2: return "battle rifle";
            case 6: return "shotgun";
            default: return "rifle";
            }
        }
        return nullptr;
    }

    // A random number in [lo, hi).
    inline double random(double lo, double hi)
    {
        static std::mt19937 gen{std::random_device{}()};
        return std::uniform_real_distribution<double>(lo, hi)(gen);
    }

    // How hard the weapon hits a limb, a rifle's 1.
    inline float weapon_power(const char* weapon)
    {
        if (!weapon) return 0.8f;
        std::string w = weapon;
        if (w == "pistol") return 0.65f;
        if (w == "smg") return 0.75f;
        return 1.0f;  // rifles, shotguns
    }

    // The bullet's push (N s) by the kind of gun: ours, tuned in play (before push_scale).
    inline float weapon_push(const char* weapon)
    {
        if (!weapon) return 40.0f;
        std::string w = weapon;
        if (w == "pistol") return 28.0f;
        if (w == "smg") return 32.0f;
        if (w == "battle rifle") return 52.0f;
        if (w == "shotgun") return 10.0f;  // a pellet
        return 45.0f;                      // a rifle
    }
    constexpr float ENGINE_SHOT_FORCE = 40.0f;  // what the engine pushes with for a weapon it has no table for

    struct Agent
    {
        FWeakObjectPtr character;
        FWeakObjectPtr mesh;
        Calib* cal = nullptr;
        void* agent = nullptr;
        void* world = nullptr;
        double time = 0;
        bool started = false;
        bool alive = false;  // a stagger on a live character
        bool limp = false;   // killed by a bullet in the head: drops lifeless (headshot_duration, not the dying reaction's)
        bool new_hit = true; // a bullet to hand to the shot task this frame
        bool own = false;    // the stagger runs on the mesh's own physics (not the game's ragdoll)
        double blend_left = -1;  // blending back into the animation: seconds left
        FWeakObjectPtr old_asset;  // the mesh's physics asset and collision before the stagger
        FName old_profile;
        // What the hands held when the stagger began (the weapon): moved from its socket (which follows the animation's
        // hand) onto the physical hand's bone for the stagger, and put back on the socket, where it was, afterwards.
        struct Held
        {
            FWeakObjectPtr root;
            const wchar_t* hand;
            T offset;
            DVec scale;
            FWeakObjectPtr parent;
            FName socket;
            DVec rel_loc, rel_rot, rel_scale;  // its relative location, rotation (pitch, yaw, roll) and scale on the socket
            // Its parts (the gun's mesh, its lights) and where they sat on it: they carry the gun's place on the hand (see grab_held).
            struct Part { FWeakObjectPtr comp; FName socket; DVec loc, rot, scale; T on_hand; };
            std::vector<Part> parts;
        };
        std::vector<Held> held;
        double next_gun_diag = 0;
        uint8 capsule_response = 255;  // the capsule's response to physics bodies before the stagger (255: untouched)
        Hit hit;
        double next_diag = 0;
        int resyncs = 0;
        DVec start_at{};  // the pelvis where the reaction began, for how far Delirium and the body went
        std::array<float, 4> hurt{};  // each limb's injury so far (left arm, right arm, left leg, right leg)
        double actor_yaw = 0, hips_yaw = 0;  // the actor's yaw and the hips' heading when the stagger began
        double floor = 0, head_start = 0;  // the floor under them and the head's height over it when the reaction began
        double fire_from = -1, fire_until = -1, next_fire = 0;  // firing back: the burst's window (stagger time) and next shot
        float left = 1;    // the game's health left (0-1) at the last hit
        double total = 0;  // seconds of this stagger, all its hits together (time restarts with each)
        bool tase_first = false;  // the stagger was begun by a taser, no bullet: no push, no hand to a wound, no firing back
        bool shock_pending = false, shock = false;  // the electrocute message to send, and running
        double shock_since = 0, shock_for = 0;  // when it began (now_s), and the stun's length if the game told us (0: not told)
        double still = 0;  // seconds Delirium's pelvis has been settled, on their feet, since the last hit
        std::array<double, nm::PARTS> stuck{};  // seconds each body has stayed far from its Delirium part
        bool limb_resync = false;  // a limb that can't follow: Delirium set from the ragdoll again next frame
        int limb_resyncs = 0;
        int propped = 0;
        bool loose = false;  // Delirium lost the body (it kept drifting off): the body falls as a plain ragdoll until the stagger ends  // limb resyncs of a dead body held up above where Delirium lies
        std::array<DQuat, 2> wrist{};  // each hand's turn on its forearm as the animation held the gun (left, right)
        double next_sim_check = 0;  // when to look again whether the bodies still simulate (own physics)
        DVec last_pelvis{};         // the body's pelvis last frame, for one carried off by something else
        DVec last_actor{};
        double last_pelvis_t = -1;
        bool dragged_told = false;
        bool death_hit = false;     // taken at the killing bullet, before the game's death ragdoll (death_hit)
        bool handing_over = false;  // ended into a handover (the bodies held still, blended into the animation)
        bool corpse_capsule = false;  // dead: its capsule handed to corpse_capsules (brought to the body)
        uint32 anim = 0xFFFFFFFEu;  // the animation blueprint's ragdoll flags last frame (anim_flags)
        std::array<T, nm::PARTS> start_rel{};  // each body as the animation had it when the stagger began, on the actor (pose_bridge)
        bool has_start = false;
        bool bridging = false;      // this blend back goes through the pose when hit (pose_bridge), on the actor held as it began
        T bridge_frame{};           // the actor as the blend began (Delirium space), the pose's frame
        DVec bridge_at{}, bridge_rot{};  // and as the game has it (location, rotator), held there until the animation takes over
        std::shared_ptr<std::string> tape;  // the recording: Delirium's calls, the poses, what happened
        std::string tape_name;
        double next_frame = 0;
        uint32_t seed = 0;  // its random numbers' seed in Delirium (recorded: a replay sets it again)
        double clock = 0;  // seconds since Delirium took them, all hits together (the recording's time)
        DVec world_at{};   // where Delirium's world (ground, furniture, walls) was made around the body
        double world_t = 0;
        int worlds = 0;    // made again since
        bool marked = false;
    };

    // How much of the body the stagger's physics takes: part for an unhurt one, who keeps their feet and their aim
    // through it, all of it for one nearly gone.
    inline float peak_blend(const Agent& a)
    {
        return cfg.healthy_blend + (1.0f - cfg.healthy_blend) * (1.0f - a.left);
    }

    inline std::vector<Agent> agents;
    inline double now_s = 0;  // seconds since the mod started
    inline std::map<UObject*, double> recovered_at;  // a character back from a stagger shrugs off bullets until then
    inline std::map<UObject*, Hit> last_hits;  // each character's last bullet hit
    // This frame's hits on each character. A shotgun's pellets land together, and only the last one was taken: a blast pushed
    // a quarter of a rifle bullet (10 against 45), less than a pistol. Now one hit for them all, its push their sum, at the
    // pellet nearest the middle of the spread.
    inline std::map<UObject*, std::vector<Hit>> frame_hits;
    inline std::map<UObject*, double> last_head;  // when each character last took a bullet in the head or neck
    inline std::vector<FWeakObjectPtr> pending;  // ragdolls that started since the last tick
    inline std::vector<FWeakObjectPtr> shot;     // characters hit since the last tick
    inline std::vector<FWeakObjectPtr> tracked;  // every character hit this mission: watched for their ragdoll
    inline std::vector<FWeakObjectPtr> people;   // every suspect and civilian that exists: watched for the taser only
    inline std::vector<FWeakObjectPtr> censused; // the dead whose capsule went in the log (capsule_census)
    struct Born { UObject* object; int32 index; };
    inline std::vector<Born> born;               // suspects and civilians just made, for people (they may be made off the game thread)
    inline std::mutex born_lock;
    inline std::vector<FWeakObjectPtr> released; // let go of: not taken again until they're shot again

    inline std::vector<FWeakObjectPtr> laid_to_rest; // dead bodies Delirium let go of: their ragdoll is the game's from then on

    inline bool was_laid_to_rest(UObject* c)
    {
        return std::any_of(laid_to_rest.begin(), laid_to_rest.end(), [&](const FWeakObjectPtr& r) { return r.Get() == c; });
    }

    inline bool was_released(UObject* c)
    {
        return std::any_of(released.begin(), released.end(), [&](const FWeakObjectPtr& r) { return r.Get() == c; });
    }

    inline bool flag(UObject* character, const TCHAR* fn)
    {
        ufn::Call c(ufn::find(character, fn));
        c.invoke(character);
        return c.get<bool>(STR("ReturnValue"));
    }

    // Killed outright (an incapacitated one is dead() too, but the game still has them writhing on the floor).
    inline bool killed(UObject* character)
    {
        auto by = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("KilledBy"));
        return by && *by;
    }

    inline bool dead(UObject* character)
    {
        for (auto p : {STR("KilledBy"), STR("IncapacitatedBy")})
        {
            auto by = character->GetValuePtrByPropertyNameInChain<UObject*>(p);
            if (by && *by) return true;
        }
        return false;
    }

    // The game's health left, 0-1 (1 if it can't be read).
    inline float health_left(UObject* character)
    {
        auto comp = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("CharacterHealth"));
        if (!comp || !*comp) return 1.0f;
        auto now = (*comp)->GetValuePtrByPropertyNameInChain<float>(STR("Resource"));
        auto most = (*comp)->GetValuePtrByPropertyNameInChain<float>(STR("MaxResource"));
        return now && most && *most > 0 ? std::clamp(*now / *most, 0.0f, 1.0f) : 1.0f;
    }

    // A function of the character answering yes or no: 1, 0, or -1 when this class has no such function.
    inline int asked(UObject* character, const TCHAR* fn)
    {
        UFunction* f = ufn::find(character, fn);
        if (!f) return -1;
        ufn::Call c(f);
        c.invoke(character);
        return c.get<bool>(STR("ReturnValue")) ? 1 : 0;
    }

    // Is the game holding them tased? 1, 0, or -1 when it can't be told.
    inline int game_tased(UObject* character)
    {
        return asked(character, STR("IsCurrentlyTased"));
    }

    // In the game's hands now, not ours: the arrest's animation must get the body.
    inline bool being_arrested(UObject* character)
    {
        return asked(character, STR("IsBeingArrested")) == 1 || asked(character, STR("IsArrested")) == 1;
    }

    inline bool driving(UObject* character)
    {
        return std::any_of(agents.begin(), agents.end(), [&](const Agent& a) { return a.character.Get() == character; });
    }

    inline void release_held(Agent& a);

    // The game's physical animation (it blends its own animations, a wounded or incapacitated pose, into the ragdoll):
    // off while Delirium drives the body, or the two pull the same bones; back on when Delirium lets go.
    inline void game_physical_animation(UObject* character, float strength)
    {
        auto comp = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("PhysicalAnimationComp"));
        if (comp && *comp) ufn::Call(ufn::find(*comp, STR("SetStrengthMultiplyer"))).set(STR("InStrengthMultiplyer"), strength).invoke(*comp);
    }

    struct Ramp { FWeakObjectPtr character; double t; };
    inline std::vector<Ramp> physical_animation_ramps;  // the game's physical animation easing back in, 0 to 1 over 1.5 s

    // ---- the recordings (Mods/RoNDelirium/recordings): one file per activation, F9 marks the latest ----
    // A recording holds every call the mod made on Delirium for it (tools/replay.py runs them again offline), the poses of
    // Delirium's body and the game's, 30 a second (tools/recviewer.html shows them), and what happened, in order.

    inline std::filesystem::path rec_dir;  // set by the mod at load
    inline std::atomic<bool> mark_asked{false};  // F9, from the key's thread: done on the next tick
    struct Written { std::filesystem::path file; double at; };
    inline std::vector<Written> written;  // recordings written lately, which F9 can still mark

    inline std::string narrow(const StringType& w)
    {
        std::string s;
        for (auto ch : w) s += ch > 32 && ch < 128 ? char(ch) : '_';
        return s;
    }

    // A line of what happened, on the agent's recording.
    inline void note(Agent& a, const std::string& what)
    {
        if (a.tape) std::format_to(std::back_inserter(*a.tape), "event {:.3f} {}\n", a.clock, what);
    }

    inline void open_tape(Agent& a, UObject* character)
    {
        if (!cfg.record || rec_dir.empty()) return;
        a.tape = std::make_shared<std::string>();
        a.tape->reserve(1 << 18);
        SYSTEMTIME t;
        GetLocalTime(&t);
        a.tape_name = std::format("{:04}{:02}{:02}_{:02}{:02}{:02}_{}", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                                  narrow(character->GetName()));
        auto& s = *a.tape;
        std::format_to(std::back_inserter(s), "rec 1 {} {} {}\n", narrow(character->GetName()), a.cal->asset.empty() ? "?" : a.cal->asset,
                       a.alive ? "alive" : "dead");
        // push: the push_scale the shot values below include (replay.py tries others in its place)
        // lift: the lift_cap replay.py applies the same way (it isn't in the calls)
        std::format_to(std::back_inserter(s), "engine {}\npush {}\nlift {}\nfit", engine, cfg.push_scale, cfg.lift_cap);
        nm::tape::put(s, a.cal->joints.data(), nm::PARTS * 3);
        if (a.cal->posed) std::format_to(std::back_inserter(s), "\nposed {}", a.cal->posed);
        s += "\nref";
        for (auto& t : a.cal->ref)
        {
            float v[7] = {float(t.p.x), float(t.p.y), float(t.p.z), float(t.q.x), float(t.q.y), float(t.q.z), float(t.q.w)};
            nm::tape::put(s, v, 7);
        }
        s += "\nparents";
        for (int k = 0; k < nm::PARTS; ++k) std::format_to(std::back_inserter(s), " {}", k ? a.cal->parent[k] : -1);
        std::format_to(std::back_inserter(s), "\nagent 0 0 0 0 0.3\nseed {}\n", a.seed);
        nm::tape::on[a.agent] = a.tape.get();
    }

    inline void close_tape(Agent& a, const CharType* why)
    {
        if (!a.tape) return;
        note(a, "end " + narrow(why));
        nm::tape::on.erase(a.agent);
        nm::tape::on.erase(a.world);
        nm::tape::stiff_written.erase(a.tape.get());
        std::error_code ec;
        std::filesystem::create_directories(rec_dir, ec);
        auto file = rec_dir / (a.tape_name + (a.marked ? "_MARK" : "") + ".rec");
        std::ofstream(file, std::ios::binary).write(a.tape->data(), std::streamsize(a.tape->size()));
        written.push_back({file, now_s});
        a.tape.reset();
        // The oldest unmarked ones go past record_keep.
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> old;
        for (auto& e : std::filesystem::directory_iterator(rec_dir, ec))
        {
            auto stem = e.path().stem().string();
            if (e.path().extension() == ".rec" && !stem.ends_with("_MARK")) old.emplace_back(e.last_write_time(ec), e.path());
        }
        if (int(old.size()) > cfg.record_keep)
        {
            std::sort(old.begin(), old.end());
            for (size_t i = 0; i < old.size() - cfg.record_keep; ++i) std::filesystem::remove(old[i].second, ec);
        }
    }

    // F9: the recordings of the last seconds and those going on now marked (_MARK), so they're the ones looked at.
    inline void mark_recent()
    {
        int n = 0;
        for (auto& a : agents)
        {
            if (a.tape && !a.marked) a.marked = true, note(a, "MARK"), ++n;
        }
        for (auto& w : written)
        {
            auto stem = w.file.stem().string();
            if (stem.ends_with("_MARK")) continue;
            auto to = w.file;
            to.replace_filename(stem + "_MARK.rec");
            std::error_code ec;
            std::filesystem::rename(w.file, to, ec);
            if (!ec) w.file = to, ++n;
        }
        Output::send(STR("[RoNDelirium] F9: {} recording(s) marked\n"), n);
        MessageBeep(n ? MB_OK : MB_ICONHAND);
    }

    // Diagnosis: the animation blueprint's own ragdoll state (RoNAnimInstance_HumanBase_V2's flags). Going back to the
    // animation, the body ended in the mesh's reference pose (the T-pose players saw): its animation side gave no pose.
    // Which of these it holds while Delirium drives, and after, goes in the recordings ("anim ..." when they change).
    inline const wchar_t* ANIM_FLAGS[] = {L"bInRagdoll", L"bRecoveringFromRagdoll", L"bIsGetUpPlaying", L"bIsDead", L"bIsPlayingDeathAnim",
                                          L"bIncapacitated", L"bSurrendered", L"bFullBodyMontagePlaying", L"bAnyMontageIsActive"};
    constexpr uint32 ANIM_UNKNOWN = 0xFFFFFFFFu;

    inline uint32 anim_flags(UObject* mesh)
    {
        if (!mesh) return ANIM_UNKNOWN;
        ufn::Call get(ufn::find(mesh, STR("GetAnimInstance")));
        if (!get) return ANIM_UNKNOWN;
        get.invoke(mesh);
        UObject* anim = get.get<UObject*>(STR("ReturnValue"));
        if (!anim) return ANIM_UNKNOWN;
        static std::map<UClass*, std::vector<FBoolProperty*>> props;
        UClass* cls = anim->GetClassPrivate();
        auto it = props.find(cls);
        if (it == props.end())
        {
            std::vector<FBoolProperty*> found(std::size(ANIM_FLAGS), nullptr);
            for (FProperty* p : cls->ForEachPropertyInChain())
            {
                for (size_t i = 0; i < std::size(ANIM_FLAGS); ++i)
                {
                    if (p->GetName() == ANIM_FLAGS[i]) found[i] = CastField<FBoolProperty>(p);
                }
            }
            it = props.emplace(cls, std::move(found)).first;
        }
        uint32 bits = 0;
        for (size_t i = 0; i < it->second.size(); ++i)
        {
            if (it->second[i] && it->second[i]->GetPropertyValueInContainer(anim)) bits |= 1u << i;
        }
        return bits;
    }

    // The animation blueprint's left hand on the gun (RoNAnimInstance: LeftHandIKAlpha, its item-based one, the grip):
    // going back to the animation the left hand jumped 0.2-0.5 m in the first frame while the rest of the body blended.
    inline const wchar_t* HAND_IK[] = {L"LeftHandIKAlpha", L"LeftHandIKAlpha_ItemBased", L"LeftHandGripAlpha"};

    inline UObject* anim_instance(UObject* mesh)
    {
        if (!mesh) return nullptr;
        ufn::Call get(ufn::find(mesh, STR("GetAnimInstance")));
        if (!get) return nullptr;
        get.invoke(mesh);
        return get.get<UObject*>(STR("ReturnValue"));
    }

    // The three as float slots on the animation instance (null where it has none, or not as a float).
    inline std::array<float*, 3> hand_ik(UObject* anim)
    {
        std::array<float*, 3> out{};
        if (!anim) return out;
        static std::map<UClass*, std::array<FFloatProperty*, 3>> props;
        UClass* cls = anim->GetClassPrivate();
        auto it = props.find(cls);
        if (it == props.end())
        {
            std::array<FFloatProperty*, 3> found{};
            for (FProperty* p : cls->ForEachPropertyInChain())
                for (size_t i = 0; i < 3; ++i)
                    if (p->GetName() == HAND_IK[i]) found[i] = CastField<FFloatProperty>(p);
            it = props.emplace(cls, found).first;
            log(STR("[RoNDelirium] {}: left hand IK {}{}{}\n"), cls->GetName(), found[0] ? STR("alpha ") : STR(""), found[1] ? STR("item-alpha ") : STR(""),
                found[2] ? STR("grip") : STR("") );
        }
        for (size_t i = 0; i < 3; ++i)
            if (it->second[i]) out[i] = it->second[i]->ContainerPtrToValuePtr<float>(anim);
        return out;
    }

    inline std::string anim_text(uint32 bits)
    {
        if (bits == ANIM_UNKNOWN) return "unknown";
        std::string s;
        for (size_t i = 0; i < std::size(ANIM_FLAGS); ++i)
        {
            if (bits & (1u << i)) s += (s.empty() ? "" : ",") + narrow(ANIM_FLAGS[i]);
        }
        return s.empty() ? "none" : s;
    }

    // After Delirium lets go: the game's bodies still recorded a few seconds (record_tail), in a .tail beside the .rec, to
    // see what the game's ragdoll, its get-up and its death do with the body (twitches, a T-pose) once it's no longer ours.
    // Lines: tail 1 <why> <alive|dead>, then frame t inragdoll simulating dead <21 x 7 the bodies, as the .rec's>.
    struct Tail { FWeakObjectPtr character, mesh; Calib* cal; std::string buf; double t, until, next; std::filesystem::path file; uint32 anim = 0xFFFFFFFEu;
                  std::array<float, 3> ik{-1, -1, -1}; };
    inline std::vector<Tail> tails;

    inline bool simulating(UObject* mesh, const wchar_t* bone)
    {
        UFunction* fn = ufn::find(mesh, STR("IsSimulatingPhysics"));
        if (!fn) return false;
        ufn::Call c(fn);
        c.set(STR("BoneName"), name(bone)).invoke(mesh);
        return c.get<bool>(STR("ReturnValue"));
    }

    inline void tail_tick(float dt)
    {
        std::erase_if(tails, [dt](Tail& t) {
            UObject* c = t.character.Get();
            UObject* mesh = t.mesh.Get();
            bool done = !c || !mesh || t.t >= t.until;
            if (!done)
            {
                // (the animation blueprint's ragdoll flags, each frame: a line when they change)
                if (uint32 f = anim_flags(mesh); f != t.anim)
                {
                    t.anim = f;
                    std::format_to(std::back_inserter(t.buf), "anim {:.4f} {}\n", t.t, anim_text(f));
                }
                // (the left hand's IK as the animation has it: a line when it changes)
                auto ik = hand_ik(anim_instance(mesh));
                std::array<float, 3> v{};
                for (size_t i = 0; i < 3; ++i) v[i] = ik[i] ? *ik[i] : -1.0f;
                if (std::abs(v[0] - t.ik[0]) > 0.02f || std::abs(v[1] - t.ik[1]) > 0.02f || std::abs(v[2] - t.ik[2]) > 0.02f)
                {
                    t.ik = v;
                    std::format_to(std::back_inserter(t.buf), "ik {:.4f} {:.3f} {:.3f} {:.3f}\n", t.t, v[0], v[1], v[2]);
                }
            }
            if (!done && t.t >= t.next)
            {
                t.next = cfg.record_fps > 0 ? t.next + 1.0 / cfg.record_fps : t.t;
                std::format_to(std::back_inserter(t.buf), "frame {:.4f} {} {} {}", t.t, flag(c, STR("IsInRagdoll")) ? 1 : 0, simulating(mesh, L"pelvis") ? 1 : 0, dead(c) ? 1 : 0);
                T spine3 = to_nm(bone_world(mesh, BONE[10]));
                for (int k = 0; k < nm::PARTS; ++k)
                {
                    T bone = k == 13 ? compose(spine3, t.cal->clavicle_l) : k == 17 ? compose(spine3, t.cal->clavicle_r) : to_nm(bone_world(mesh, BONE[k]));
                    T part = compose(bone, t.cal->off[k]);
                    std::format_to(std::back_inserter(t.buf), " {:.4f} {:.4f} {:.4f} {:.4f} {:.4f} {:.4f} {:.4f}", part.p.x, part.p.y, part.p.z, part.q.x, part.q.y, part.q.z, part.q.w);
                }
                t.buf += '\n';
            }
            t.t += dt;
            if (!done) return false;
            std::error_code ec;
            std::filesystem::create_directories(t.file.parent_path(), ec);
            std::ofstream(t.file, std::ios::binary).write(t.buf.data(), std::streamsize(t.buf.size()));
            return true;
        });
    }

    inline void end(Agent& a, const CharType* why)
    {
        if (a.tape && cfg.record_tail > 0 && a.cal)
        {
            Tail t{a.character, a.mesh, a.cal, std::format("tail 1 {} {}\n", narrow(why), a.alive ? "alive" : "dead"), 0.0, double(cfg.record_tail), 0.0,
                   rec_dir / (a.tape_name + (a.marked ? "_MARK" : "") + ".tail")};
            tails.push_back(std::move(t));
        }
        close_tape(a, why);
        release_held(a);  // nothing left off its socket, however the agent ends
        // The game's physical animation back: at once on one standing in the animation (no bodies simulate), eased in on
        // one the game's ragdoll has (snapped back, it jolted the body), never on the dead (it pulled corpses about).
        if (auto c = a.character.Get())
        {
            // (Not on a dead one whose bodies are still ours, the game's ragdoll not come yet: it pulled them into its death animation.)
            if (!flag(c, STR("IsInRagdoll"))) { if (!(a.death_hit && dead(c)) && !a.handing_over) game_physical_animation(c, 1.0f); }
            else if (!killed(c)) physical_animation_ramps.push_back({weak(c), 0.0});
        }
        if (a.agent) nm::api.agent_destroy(a.agent);
        if (a.world) nm::api.world_destroy(a.world);
        a.agent = a.world = nullptr;
        if (auto c = a.character.Get())
        {
            released.push_back(weak(c));
            if (dead(c) && !was_laid_to_rest(c)) laid_to_rest.push_back(weak(c));
            log(STR("[RoNDelirium] {} released ({}, {:.1f} s)\n"), c->GetName(), why, a.time);
        }
    }

    // Did a bullet in the head or neck just kill them? (Neck too: RoNGore's decapitations count it as the head. Within half
    // a second, not the last hit only: a shotgun's other pellets land in the same moment, and the game's death ragdoll
    // comes a little after the bullet.)
    inline void note_head_hit(UObject* c, const StringType& bone)
    {
        if (int part = part_of(bone); part == 11 || part == 12) last_head[c] = now_s;
    }

    inline bool head_kill(UObject* c)
    {
        auto it = last_head.find(c);
        return cfg.headshot_limp && it != last_head.end() && now_s - it->second < 0.5;
    }

    // ---- a stagger on the mesh's own physics ----
    // The game's ragdoll (EnableRagdoll) drops the weapon and ends in its get-up-off-the-floor animation, standing or
    // not. A stagger instead turns on the mesh's bodies below the pelvis itself, with the ragdoll's collision and
    // physics asset: the game still thinks the character is on their feet. It ends blending back into the animation
    // if Delirium kept them up, or in the game's ragdoll (and its get-up) if they went down.

    inline FName fname_call(UObject* obj, const TCHAR* fn)
    {
        ufn::Call c(ufn::find(obj, fn));
        c.invoke(obj);
        return c.get<FName>(STR("ReturnValue"));
    }

    inline void simulate(UObject* mesh, bool on)
    {
        ufn::Call(ufn::find(mesh, STR("SetAllBodiesBelowSimulatePhysics")))
            .set(STR("InBoneName"), name(L"pelvis")).set<bool>(STR("bNewSimulate"), on).set<bool>(STR("bIncludeSelf"), true).invoke(mesh);
    }

    inline void blend(UObject* mesh, float weight)
    {
        ufn::Call(ufn::find(mesh, STR("SetAllBodiesBelowPhysicsBlendWeight")))
            .set(STR("InBoneName"), name(L"pelvis")).set(STR("PhysicsBlendWeight"), weight).set<bool>(STR("bSkipCustomPhysicsType"), false)
            .set<bool>(STR("bIncludeSelf"), true).invoke(mesh);
    }

    inline void set_asset(UObject* mesh, UObject* asset)
    {
        ufn::Call(ufn::find(mesh, STR("SetPhysicsAsset"))).set(STR("NewPhysicsAsset"), asset).set<bool>(STR("bForceReInit"), true).invoke(mesh);
    }

    inline void set_profile(UObject* mesh, FName profile)
    {
        ufn::Call(ufn::find(mesh, STR("SetCollisionProfileName"))).set(STR("InCollisionProfileName"), profile).set<bool>(STR("bUpdateOverlaps"), false).invoke(mesh);
    }

    inline void grab_held(Agent& a, UObject* character, UObject* mesh);
    inline void release_held(Agent& a);
    inline void capsule_ignore(Agent& a, UObject* character);
    inline void capsule_restore(Agent& a, UObject* character);

    // Which way the hips face (degrees, the game's yaw): across them, from the right thigh to the left.
    inline double hips_heading(UObject* mesh)
    {
        DVec across = bone_world(mesh, L"thigh_LE").pos - bone_world(mesh, L"thigh_RI").pos;
        return std::atan2(across.y, across.x) * 180.0 / 3.14159265358979;
    }

    inline DVec actor_rotation(UObject* character)
    {
        ufn::Call c(ufn::find(character, STR("K2_GetActorRotation")));
        c.invoke(character);
        return c.get<DVec>(STR("ReturnValue"));  // pitch, yaw, roll
    }

    // The actor as a frame in Delirium space: its place, and its heading only (the animation stands upright on it). The
    // game's yaw turns the other way here (Y mirrored).
    inline T actor_frame(UObject* character)
    {
        ufn::Call c(ufn::find(character, STR("K2_GetActorLocation")));
        c.invoke(character);
        const double yaw = -actor_rotation(character).y * 3.14159265358979 / 180.0;
        return T{to_nm_point(c.get<DVec>(STR("ReturnValue"))), DQuat{0, 0, std::sin(yaw / 2), std::cos(yaw / 2)}};
    }

    inline DQuat slerp(DQuat a, DQuat b, double t)
    {
        double d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        if (d < 0) b = {-b.x, -b.y, -b.z, -b.w}, d = -d;
        DQuat r;
        if (d > 0.9995) r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        else
        {
            const double th = std::acos(d), s = std::sin(th), wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
            r = {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
        }
        const double n = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        return {r.x / n, r.y / n, r.z / n, r.w / n};
    }

    // The actor turned as the staggering body turned, so the animation it blends back into faces where the body does
    // (it snapped back to the old facing).
    inline void face_as_body(Agent& a, UObject* character, UObject* mesh)
    {
        double turn = std::remainder(hips_heading(mesh) - a.hips_yaw, 360.0);
        if (std::abs(turn) < 5.0) return;
        DVec r = actor_rotation(character);
        r.y = a.actor_yaw + turn;
        ufn::Call(ufn::find(character, STR("K2_SetActorRotation"))).set(STR("NewRotation"), r).set<bool>(STR("bTeleportPhysics"), false).invoke(character);
        log(STR("[RoNDelirium]   turned {:.0f} degrees with the body\n"), turn);
    }

    // The ragdoll asset's joint limits as the game made them (its defaults): read once per asset through reflection.
    struct AssetLimit { StringType bone, parent; float s1, s2, tw; uint8 m1, m2, mt; };
    inline const std::vector<AssetLimit>& asset_limits(UObject* asset)
    {
        static std::map<UObject*, std::vector<AssetLimit>> cache;
        static const std::vector<AssetLimit> none;
        if (!asset) return none;
        if (auto it = cache.find(asset); it != cache.end()) return it->second;
        auto& out = cache[asset];
        auto field = [](UStruct* s, uint8* base, const wchar_t* name, uint8** at) -> FProperty* {
            if (!s || !base) return nullptr;
            for (FProperty* p : s->ForEachPropertyInChain())
            {
                if (p->GetName() == name) { *at = base + p->GetOffset_Internal(); return p; }
            }
            return nullptr;
        };
        auto sub = [](FProperty* p) -> UStruct* { auto sp = p ? CastField<FStructProperty>(p) : nullptr; return sp ? sp->GetStruct() : nullptr; };
        struct RawArray { UObject** data; int32 num; int32 max; };
        uint8* at = nullptr;
        if (!field(asset->GetClassPrivate(), reinterpret_cast<uint8*>(asset), STR("ConstraintSetup"), &at)) return out;
        RawArray arr = *reinterpret_cast<RawArray*>(at);
        for (int32 i = 0; arr.data && i < arr.num && i < 64; ++i)
        {
            UObject* tmpl = arr.data[i];
            if (!tmpl) continue;
            uint8* inst = nullptr;
            UStruct* is = sub(field(tmpl->GetClassPrivate(), reinterpret_cast<uint8*>(tmpl), STR("DefaultInstance"), &inst));
            if (!is) continue;
            uint8* x = nullptr;
            AssetLimit l{STR("?"), STR("?"), -1, -1, -1, 255, 255, 255};
            if (field(is, inst, STR("ConstraintBone1"), &x)) l.bone = reinterpret_cast<FName*>(x)->ToString();
            if (field(is, inst, STR("ConstraintBone2"), &x)) l.parent = reinterpret_cast<FName*>(x)->ToString();
            uint8 *prof = nullptr, *cone = nullptr, *twist = nullptr;
            UStruct* ps = sub(field(is, inst, STR("ProfileInstance"), &prof));
            UStruct* cs = sub(field(ps, prof, STR("ConeLimit"), &cone));
            UStruct* ts = sub(field(ps, prof, STR("TwistLimit"), &twist));
            if (field(cs, cone, STR("Swing1LimitDegrees"), &x)) l.s1 = *reinterpret_cast<float*>(x);
            if (field(cs, cone, STR("Swing2LimitDegrees"), &x)) l.s2 = *reinterpret_cast<float*>(x);
            if (field(cs, cone, STR("Swing1Motion"), &x)) l.m1 = *x;
            if (field(cs, cone, STR("Swing2Motion"), &x)) l.m2 = *x;
            if (field(ts, twist, STR("TwistLimitDegrees"), &x)) l.tw = *reinterpret_cast<float*>(x);
            if (field(ts, twist, STR("TwistMotion"), &x)) l.mt = *x;
            out.push_back(l);
        }
        return out;
    }

    // Ready or Not's ragdoll holds the shoulders within 45 and 30 degrees of its reference pose with no twist, the spine
    // nearly rigid, the wrists bending one way, the head unable to tilt sideways and the thighs to turn: the character
    // reached for a wound and only the forearm moved, and a dead one's head followed Delirium's whip only two thirds of the
    // way. Wider while Delirium drives the body (and after: the game's ragdoll is no worse for it). Only ever wider: the
    // asset's own limits are read first (SetAngularLimits sets them outright, and another asset, PHYS_Human_Shared_V2,
    // had its spine and shoulders looser than these values, which narrowed them).
    inline void loosen_joints(UObject* mesh)
    {
        UFunction* fn = ufn::find(mesh, STR("SetAngularLimits"));
        static bool told = false;
        if (!told) told = true, log(STR("[RoNDelirium]   joint limits {}\n"), fn ? STR("widened (shoulders, wrists, spine, neck, head, thighs)") : STR("can't be changed (no SetAngularLimits)"));
        if (!fn) return;
        ufn::Call get_asset(ufn::find(mesh, STR("GetPhysicsAsset")));
        get_asset.invoke(mesh);
        const auto& have = asset_limits(get_asset.get<UObject*>(STR("ReturnValue")));
        struct L { const wchar_t* bone; float swing1, twist, swing2; };
        static const L limits[] = {{L"upperarm_LE", 85, 45, 70}, {L"upperarm_RI", 85, 45, 70}, {L"hand_LE", 60, 30, 35}, {L"hand_RI", 60, 30, 35},
                                   {L"spine_1", 12, 12, 18}, {L"spine_2", 10, 12, 15}, {L"spine_3", 8, 10, 12}, {L"neck_1", 15, 25, 15},
                                   {L"head", 35, 45, 25}, {L"thigh_LE", 60, 20, 25}, {L"thigh_RI", 60, 20, 25}};
        auto same = [](const StringType& a, const wchar_t* b) {
            return a.size() == std::wcslen(b) && std::equal(a.begin(), a.end(), b, [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
        };
        // An axis as the asset has it: locked 0, free all the way, limited its angle.
        auto open = [](float deg, uint8 motion) { return motion == 2 ? 0.0f : motion == 0 ? 180.0f : std::max(deg, 0.0f); };
        for (auto& l : limits)
        {
            float s1 = l.swing1, tw = l.twist, s2 = l.swing2;
            auto it = std::find_if(have.begin(), have.end(), [&](const AssetLimit& a) { return same(a.bone, l.bone); });
            if (it != have.end())
            {
                const float h1 = open(it->s1, it->m1), h2 = open(it->s2, it->m2), ht = open(it->tw, it->mt);
                if (h1 >= s1 && h2 >= s2 && ht >= tw) continue;  // already as loose: left as the game has it
                s1 = std::max(s1, h1); s2 = std::max(s2, h2); tw = std::max(tw, ht);
            }
            ufn::Call(fn).set(STR("InBoneName"), name(l.bone)).set(STR("Swing1LimitAngle"), s1).set(STR("TwistLimitAngle"), tw)
                .set(STR("Swing2LimitAngle"), s2).invoke(mesh);
        }
    }

    // The mesh's bodies on, as the ragdoll has them; false (nothing changed) if the character has no ragdoll setup.
    inline bool own_physics_on(Agent& a, UObject* character, UObject* mesh)
    {
        a.actor_yaw = actor_rotation(character).y;
        a.hips_yaw = hips_heading(mesh);
        auto active = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("Rep_ActiveRagdollPhysAsset"));
        auto comp = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("RagdollComponent"));
        if (!active || !*active || !comp || !*comp) return false;
        auto profile = (*comp)->GetValuePtrByPropertyNameInChain<FName>(STR("RagdollCollisionProfileName"));
        ufn::Call get_asset(ufn::find(mesh, STR("GetPhysicsAsset")));
        get_asset.invoke(mesh);
        a.old_asset = weak(get_asset.get<UObject*>(STR("ReturnValue")));
        a.old_profile = fname_call(mesh, STR("GetCollisionProfileName"));
        if (a.old_asset.Get() != *active) set_asset(mesh, *active);
        if (profile) set_profile(mesh, *profile);
        grab_held(a, character, mesh);  // where the weapon sits in the hand, while the hand is still the animation's
        capsule_ignore(a, character);
        // The bodies start still when they start simulating: one walking or running when the bullet came stopped dead and
        // only then reacted (70 of 72 recorded staggers began at 0 m/s; the game's death ragdoll keeps the motion). They
        // carry on at the character's own speed (along the ground, 2.5 m/s at most: replayed at 3.5, Delirium's solver lost
        // a body now and then), and Delirium is set from that.
        DVec carry{};
        if (UFunction* gv = cfg.keep_momentum ? ufn::find(character, STR("GetVelocity")) : nullptr)
        {
            ufn::Call c(gv);
            c.invoke(character);
            carry = c.get<DVec>(STR("ReturnValue"));
            carry.z = 0;
            carry = clamp_len(carry, 250.0);
        }
        simulate(mesh, true);
        blend(mesh, peak_blend(a));
        if (len(carry) > 10.0)  // cm/s
        {
            ufn::Call(ufn::find(mesh, STR("SetAllPhysicsLinearVelocity"))).set(STR("NewVel"), carry).set<bool>(STR("bAddToCurrent"), false).invoke(mesh);
            log(STR("[RoNDelirium]   carries on at {:.1f} m/s\n"), len(carry) / 100.0);
        }
        if (auto move = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("CharacterMovement")); move && *move)
        {
            ufn::Call(ufn::find(*move, STR("StopMovementImmediately"))).invoke(*move);
        }
        log(STR("[RoNDelirium]   own physics on: asset {} -> {}, collision {} -> {}\n"), a.old_asset.Get() ? a.old_asset.Get()->GetName() : StringType(STR("?")),
            (*active)->GetName(), a.old_profile.ToString(), profile ? profile->ToString() : StringType(STR("(unchanged)")));
        a.own = true;
        return true;
    }

    inline DTransform world_of(UObject* component)
    {
        ufn::Call c(ufn::find(component, STR("K2_GetComponentToWorld")));
        c.invoke(component);
        return c.get<DTransform>(STR("ReturnValue"));
    }

    // The actors in the character's hands (within 40 cm of one), and where each sits in it.
    inline void grab_held(Agent& a, UObject* character, UObject* mesh)
    {
        struct RawArray { UObject** data; int32 num; int32 max; };
        ufn::Call c(ufn::find(character, STR("GetAttachedActors")));
        c.set<bool>(STR("bResetArray"), true).set<bool>(STR("bRecursivelyIncludeAttachedActors"), false).invoke(character);
        auto actors = c.get<RawArray>(STR("OutActors"));
        const wchar_t* hands[] = {L"hand_LE", L"hand_RI"};
        DTransform hand_t[2] = {bone_world(mesh, hands[0]), bone_world(mesh, hands[1])};
        static UClass* controller = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.Controller"));
        for (int32 i = 0; actors.data && i < actors.num; ++i)
        {
            UObject* actor = actors.data[i];
            // Only things on a socket (a civilian's AI controller rides along on the character, on none).
            if (!actor || (controller && actor->IsA(controller))) continue;
            auto root = actor->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootComponent"));
            if (!root || !*root) continue;
            DTransform w = world_of(*root);
            auto socket = fname_call(*root, STR("GetAttachSocketName")).ToString();
            const double from_hand = std::min(len(w.pos - hand_t[0].pos), len(w.pos - hand_t[1].pos));
            // Only what's on this character's own body: the player's taser shows up here once its probes are in them, on
            // the socket of the player's hand, and must stay there.
            ufn::Call on(ufn::find(*root, STR("GetAttachParent")));
            on.invoke(*root);
            UObject* on_comp = on.get<UObject*>(STR("ReturnValue"));
            UObject* on_owner = nullptr;
            if (on_comp)
            {
                ufn::Call owner(ufn::find(on_comp, STR("GetOwner")));
                owner.invoke(on_comp);
                on_owner = owner.get<UObject*>(STR("ReturnValue"));
            }
            const bool mine = on_owner == character;
            // All they carry, in the log, for bug reports.
            log(STR("[RoNDelirium]   attached {}: socket {}, {:.0f} cm from a hand{}\n"), actor->GetName(), socket, from_hand, mine ? STR("") : STR(", on someone else"));
            if (!mine || socket == STR("None") || from_hand > 40.0) continue;
            // The hand its socket names (a two-handed gun sits nearer the left), else the nearer one.
            int best = socket.starts_with(STR("Right")) ? 1 : socket.starts_with(STR("Left")) ? 0
                     : len(w.pos - hand_t[0].pos) < len(w.pos - hand_t[1].pos) ? 0 : 1;
            T hand{hand_t[best].pos, hand_t[best].rot};
            Agent::Held h{weak(*root), hands[best], compose(inverse(hand), T{w.pos, w.rot}), w.scale};
            ufn::Call parent(ufn::find(*root, STR("GetAttachParent")));
            parent.invoke(*root);
            h.parent = weak(parent.get<UObject*>(STR("ReturnValue")));
            h.socket = fname_call(*root, STR("GetAttachSocketName"));
            auto prop = [&](const TCHAR* n) { auto v = (*root)->GetValuePtrByPropertyNameInChain<DVec>(n); return v ? *v : DVec{}; };
            h.rel_loc = prop(STR("RelativeLocation"));
            h.rel_rot = prop(STR("RelativeRotation"));
            h.rel_scale = prop(STR("RelativeScale3D"));
            // On the physical hand's bone: the engine carries it with the simulated hand, in step (set by hand after the frame,
            // it lagged a frame and shook). But the game zeroes a held item's root on what it's attached to, every frame: a
            // root kept where it was on the bone ended at the bone's origin, turned 69 degrees, the gun across the hand. So the
            // root goes where the game wants it, at zero on the bone, and its parts stay where they are, on it, off that zero.
            ufn::Call count(ufn::find(*root, STR("GetNumChildrenComponents")));
            count.invoke(*root);
            for (int32 k = 0, n = count.get<int32>(STR("ReturnValue")); k < n; ++k)
            {
                ufn::Call child(ufn::find(*root, STR("GetChildComponent")));
                child.set<int32>(STR("ChildIndex"), k).invoke(*root);
                UObject* c = child.get<UObject*>(STR("ReturnValue"));
                if (!c) continue;
                auto of = [&](const TCHAR* n2) { auto v = c->GetValuePtrByPropertyNameInChain<DVec>(n2); return v ? *v : DVec{}; };
                h.parts.push_back({weak(c), fname_call(c, STR("GetAttachSocketName")), of(STR("RelativeLocation")), of(STR("RelativeRotation")), of(STR("RelativeScale3D")), T{}});
            }
            auto parts_off = [](Agent::Held& held) {
                for (auto& p : held.parts)
                {
                    if (UObject* c = p.comp.Get())
                    {
                        ufn::Call(ufn::find(c, STR("K2_DetachFromComponent"))).set<uint8>(STR("LocationRule"), 1).set<uint8>(STR("RotationRule"), 1)
                            .set<uint8>(STR("ScaleRule"), 1).set<bool>(STR("bCallModify"), false).invoke(c);
                    }
                }
            };
            parts_off(h);
            ufn::Call(ufn::find(*root, STR("K2_AttachToComponent"))).set(STR("Parent"), mesh).set(STR("SocketName"), name(hands[best]))
                .set<uint8>(STR("LocationRule"), 2).set<uint8>(STR("RotationRule"), 2).set<uint8>(STR("ScaleRule"), 1)
                .set<bool>(STR("bWeldSimulatedBodies"), false).invoke(*root);
            for (auto& p : h.parts)
            {
                UObject* c = p.comp.Get();
                if (!c) continue;
                ufn::Call(ufn::find(c, STR("K2_AttachToComponent"))).set(STR("Parent"), *root).set(STR("SocketName"), p.socket)
                    .set<uint8>(STR("LocationRule"), 1).set<uint8>(STR("RotationRule"), 1).set<uint8>(STR("ScaleRule"), 1)
                    .set<bool>(STR("bWeldSimulatedBodies"), false).invoke(c);
                DTransform t = world_of(c);
                p.on_hand = compose(inverse(hand), T{t.pos, t.rot});
            }
            a.held.push_back(h);
            log(STR("[RoNDelirium]   carries {} in {} (from socket {})\n"), actor->GetName(), hands[best], h.socket.ToString());
        }
        // The gun the game says they have, to compare with the above in a bug report.
        auto inv = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("InventoryComp"));
        auto gun = inv && *inv ? (*inv)->GetValuePtrByPropertyNameInChain<UObject*>(STR("LastEquippedWeapon")) : nullptr;
        log(STR("[RoNDelirium]   last equipped: {}\n"), gun && *gun ? (*gun)->GetName() : StringType(STR("none")));
    }

    // Each held item back on its socket, where it sat before the stagger.
    inline void release_held(Agent& a)
    {
        for (auto& h : a.held)
        {
            UObject* root = h.root.Get();
            UObject* parent = h.parent.Get();
            if (!root || !parent) continue;
            ufn::Call(ufn::find(root, STR("K2_AttachToComponent"))).set(STR("Parent"), parent).set(STR("SocketName"), h.socket)
                .set<uint8>(STR("LocationRule"), 0).set<uint8>(STR("RotationRule"), 0).set<uint8>(STR("ScaleRule"), 0)
                .set<bool>(STR("bWeldSimulatedBodies"), false).invoke(root);
            ufn::Call(ufn::find(root, STR("K2_SetRelativeLocationAndRotation"))).set(STR("NewLocation"), h.rel_loc).set(STR("NewRotation"), h.rel_rot)
                .set<bool>(STR("bSweep"), false).set<bool>(STR("bTeleport"), true).invoke(root);
            ufn::Call(ufn::find(root, STR("SetRelativeScale3D"))).set(STR("NewScale3D"), h.rel_scale).invoke(root);
            for (auto& p : h.parts)  // the parts back as they were on it
            {
                UObject* c = p.comp.Get();
                if (!c) continue;
                ufn::Call(ufn::find(c, STR("K2_SetRelativeLocationAndRotation"))).set(STR("NewLocation"), p.loc).set(STR("NewRotation"), p.rot)
                    .set<bool>(STR("bSweep"), false).set<bool>(STR("bTeleport"), true).invoke(c);
                ufn::Call(ufn::find(c, STR("SetRelativeScale3D"))).set(STR("NewScale3D"), p.scale).invoke(c);
            }
        }
        a.held.clear();
    }

    // The character's capsule ignoring the physics bodies for the stagger (the staggering body leant on it and hung in
    // the air), and back as it was.
    inline UObject* capsule_of(UObject* character)
    {
        auto c = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("CapsuleComponent"));
        return c && *c ? *c : nullptr;
    }

    constexpr uint8 PHYSICS_BODY = 5;  // ECC_PhysicsBody

    inline void capsule_ignore(Agent& a, UObject* character)
    {
        UObject* capsule = capsule_of(character);
        if (!capsule) return;
        ufn::Call get(ufn::find(capsule, STR("GetCollisionResponseToChannel")));
        get.set<uint8>(STR("Channel"), PHYSICS_BODY).invoke(capsule);
        a.capsule_response = get.get<uint8>(STR("ReturnValue"));
        ufn::Call(ufn::find(capsule, STR("SetCollisionResponseToChannel"))).set<uint8>(STR("Channel"), PHYSICS_BODY).set<uint8>(STR("NewResponse"), 0).invoke(capsule);
    }

    inline void capsule_restore(Agent& a, UObject* character)
    {
        UObject* capsule = capsule_of(character);
        if (!capsule || a.capsule_response > 2) return;
        ufn::Call(ufn::find(capsule, STR("SetCollisionResponseToChannel"))).set<uint8>(STR("Channel"), PHYSICS_BODY)
            .set<uint8>(STR("NewResponse"), a.capsule_response).invoke(capsule);
        a.capsule_response = 255;
    }

    // Handing the body to the game's ragdoll: the capsule stands where they did, and blocking the bodies again at once it
    // bounced the lying body up. Its response comes back once they're up (never if they died).
    struct CapsuleLater { FWeakObjectPtr character; uint8 response; double not_before; };
    inline std::vector<CapsuleLater> capsules_later;

    inline void capsule_later(Agent& a, UObject* character)
    {
        if (a.capsule_response <= 2) capsules_later.push_back({weak(character), a.capsule_response, now_s + 1.0});
        a.capsule_response = 255;
    }

    // Back to the animation: the bodies off, the mesh's own asset and collision again.
    inline void own_physics_off(Agent& a, UObject* character, UObject* mesh)
    {
        blend(mesh, 0.0f);
        simulate(mesh, false);
        if (auto old = a.old_asset.Get()) set_asset(mesh, old);
        set_profile(mesh, a.old_profile);
        release_held(a);
        capsule_restore(a, character);
        a.own = false;
    }

    // The hand-over to the animation at the bridge's end. Turning the physics off let the animation in at once, a jump of
    // 0.1-0.6 m (head, hands) in one frame. Now the bodies stop simulating but stay where they are (KinematicBonesUpdateType
    // SkipAllBones: the animation doesn't move them) and the mesh blends them in with bBlendPhysics on, their weight going
    // from 1 to 0 over handover seconds. (1.5-test4 did this without bBlendPhysics, and the bodies' weight did nothing
    // once they no longer simulated: the animation came in at once.) The physics asset and collision come back at the end.
    struct Handover
    {
        FWeakObjectPtr character, mesh, asset;
        FName profile;
        uint8 kinematic = 0;
        double left = 0, total = 0;
        std::array<float, 3> ik_game{-1, -1, -1};     // the animation's own left-hand IK values (what it writes each frame)
        std::array<float, 3> ik_written{-9, -9, -9};  // what we wrote over them last frame
    };
    inline std::vector<Handover> handovers;

    // The animation's left-hand IK scaled by how far the hand-over is (0 at its start, the animation's own at its end).
    // The animation writes its values each frame: one that differs from what we wrote is its own, taken as the new one.
    inline void handover_hand_ik(Handover& h, UObject* mesh, double done)
    {
        if (!cfg.handover_hand) return;
        auto ik = hand_ik(anim_instance(mesh));
        for (size_t i = 0; i < 3; ++i)
        {
            if (!ik[i]) continue;
            if (std::abs(*ik[i] - h.ik_written[i]) > 1e-4f) h.ik_game[i] = *ik[i];
            if (h.ik_game[i] < 0) continue;
            h.ik_written[i] = done >= 1.0 ? h.ik_game[i] : float(h.ik_game[i] * std::clamp(done, 0.0, 1.0));
            *ik[i] = h.ik_written[i];
        }
    }

    inline FBoolProperty* blend_physics_prop(UObject* mesh)
    {
        static std::map<UClass*, FBoolProperty*> found;
        UClass* cls = mesh->GetClassPrivate();
        if (auto it = found.find(cls); it != found.end()) return it->second;
        FBoolProperty* prop = nullptr;
        for (FProperty* p : cls->ForEachPropertyInChain())
            if (p->GetName() == STR("bBlendPhysics")) prop = CastField<FBoolProperty>(p);
        return found[cls] = prop;
    }

    inline uint8* kinematic_bones(UObject* mesh)
    {
        return mesh ? mesh->GetValuePtrByPropertyNameInChain<uint8>(STR("KinematicBonesUpdateType")) : nullptr;
    }

    inline bool handover_begin(Agent& a, UObject* character, UObject* mesh)
    {
        FBoolProperty* bp = blend_physics_prop(mesh);
        uint8* kin = kinematic_bones(mesh);
        if (!bp || !kin) return false;
        Handover h{a.character, a.mesh, a.old_asset, a.old_profile, *kin, cfg.handover, cfg.handover};
        release_held(a);
        capsule_restore(a, character);
        *kin = 1;  // EKinematicBonesUpdateToPhysics::SkipAllBones
        bp->SetPropertyValueInContainer(mesh, true);
        blend(mesh, 1.0f);
        simulate(mesh, false);
        a.own = false;
        a.handing_over = true;
        handover_hand_ik(h, mesh, 0.0);
        handovers.push_back(h);
        note(a, "handover");
        return true;
    }

    inline void handover_finish(Handover& h)
    {
        UObject* mesh = h.mesh.Get();
        UObject* c = h.character.Get();
        if (!mesh) return;
        blend(mesh, 0.0f);
        handover_hand_ik(h, mesh, 1.0);
        if (FBoolProperty* bp = blend_physics_prop(mesh)) bp->SetPropertyValueInContainer(mesh, false);
        if (uint8* kin = kinematic_bones(mesh)) *kin = h.kinematic;
        if (auto old = h.asset.Get()) set_asset(mesh, old);
        set_profile(mesh, h.profile);
        if (c && !dead(c) && !flag(c, STR("IsInRagdoll"))) game_physical_animation(c, 1.0f);
    }

    // A character handed over and shot (or ragdolled) again: the hand-over done at once, before anything else takes the mesh.
    inline void handover_cut(UObject* character)
    {
        std::erase_if(handovers, [&](Handover& h) {
            if (h.character.Get() != character) return false;
            handover_finish(h);
            return true;
        });
    }

    inline void handover_tick(double dt)
    {
        std::erase_if(handovers, [dt](Handover& h) {
            UObject* mesh = h.mesh.Get();
            UObject* c = h.character.Get();
            if (!mesh || !c) return true;
            h.left -= dt;
            if (h.left <= 0 || dead(c) || flag(c, STR("IsInRagdoll")))
            {
                handover_finish(h);
                return true;
            }
            double x = std::clamp(h.left / h.total, 0.0, 1.0);
            double w = x * x * (3 - 2 * x);
            blend(mesh, float(w));
            handover_hand_ik(h, mesh, 1.0 - w);
            return false;
        });
    }

    inline bool has_authority(UObject* character)
    {
        UFunction* auth = ufn::find(character, STR("HasAuthority"));
        if (!auth) return true;
        ufn::Call c(auth);
        c.invoke(character);
        return c.get<bool>(STR("ReturnValue"));
    }

    inline DVec actor_location(UObject* character)
    {
        ufn::Call get(ufn::find(character, STR("K2_GetActorLocation")));
        get.invoke(character);
        return get.get<DVec>(STR("ReturnValue"));
    }

    // A dead one's capsule. Staggering, it went under Delirium's pelvis (capsule_follow); killed, it stayed where they died
    // while the body fell on, 0.8 m off in the median and up to 4 m (39 deaths mid-stagger): a capsule standing over an
    // empty spot, which raised the player's gun walking past it (Nexus reports). Brought to the body's pelvis while the
    // game's ragdoll simulates, at its height (moving the actor while the bodies are kinematic, frozen by the game, would
    // carry the corpse along: never then). dead_capsule 2 also turns the capsule's collision off.
    struct CorpseCapsule
    {
        FWeakObjectPtr character, mesh;
        double since = 0, idle = 0, settled = 0, moved = 0, first_off = -1;
    };
    inline std::vector<CorpseCapsule> corpse_capsules;

    inline void corpse_capsule_add(UObject* character, UObject* mesh)
    {
        if (!cfg.dead_capsule || !character || !mesh) return;
        if (std::any_of(corpse_capsules.begin(), corpse_capsules.end(), [&](const CorpseCapsule& k) { return k.character.Get() == character; })) return;
        corpse_capsules.push_back({weak(character), weak(mesh), now_s});
        if (cfg.dead_capsule >= 2)
            if (UObject* capsule = capsule_of(character))
                ufn::Call(ufn::find(capsule, STR("SetCollisionEnabled"))).set<uint8>(STR("NewType"), 0).invoke(capsule);  // NoCollision
    }

    inline void corpse_capsule_tick(double dt)
    {
        std::erase_if(corpse_capsules, [dt](CorpseCapsule& k) {
            UObject* c = k.character.Get();
            UObject* mesh = k.mesh.Get();
            if (!c || !mesh || !dead(c)) return true;
            auto done = [&](const CharType* why) {
                log(STR("[RoNDelirium] {} capsule brought to the body ({}): {:.2f} m off at first, moved {:.2f} m in {:.1f} s\n"), c->GetName(), why,
                    std::max(k.first_off, 0.0), k.moved, now_s - k.since);
                return true;
            };
            if (!has_authority(c)) return true;  // online, the actor's place is the server's
            DVec at = actor_location(c);
            DVec pelvis = bone_world(mesh, L"pelvis").pos;
            DVec to{pelvis.x, pelvis.y, at.z};
            double off = len(to - at) / 100.0;
            if (k.first_off < 0) k.first_off = off;
            if (!simulating(mesh, L"pelvis"))
            {
                k.idle += dt;
                return k.idle > 1.0 ? done(STR("the body no longer simulates")) : false;
            }
            k.idle = 0;
            if (now_s - k.since > 20.0) return done(STR("20 s"));
            if (off < 0.03)
            {
                k.settled += dt;
                return k.settled > 2.0 ? done(STR("settled")) : false;
            }
            k.settled = 0;
            const double most = 8.0 * std::max(dt, 1.0 / 240.0);  // m this frame
            if (off > most) to = at + (to - at) * (most / off);
            ufn::Call(ufn::find(c, STR("K2_SetActorLocation"))).set(STR("NewLocation"), to).set<bool>(STR("bSweep"), false)
                .set<bool>(STR("bTeleport"), false).invoke(c);
            k.moved += std::min(off, most);
            return false;
        });
    }

    // Diagnosis (log = 1): every dead one's capsule a second and four seconds after they died, whoever took the body (the
    // mod off with F8 too: how the game leaves a capsule on its own). Collision enabled (0 none, 1 query, 2 physics, 3 both),
    // profile, responses to channels 0-17 (I ignore, O overlap, B block), how far from the body's pelvis.
    inline bool v2(UObject* character);
    struct DeathSeen { FWeakObjectPtr character; double t; int stage; bool mod; };
    inline std::vector<DeathSeen> deaths_seen;

    inline void capsule_census(UObject* c, const DeathSeen& d, const CharType* when)
    {
        UObject* capsule = capsule_of(c);
        auto slot = c->GetValuePtrByPropertyNameInChain<UObject*>(STR("Mesh"));
        if (!capsule || !slot || !*slot) return;
        ufn::Call en(ufn::find(capsule, STR("GetCollisionEnabled")));
        en.invoke(capsule);
        StringType resp;
        for (uint8 ch = 0; ch < 18; ++ch)
        {
            ufn::Call r(ufn::find(capsule, STR("GetCollisionResponseToChannel")));
            r.set<uint8>(STR("Channel"), ch).invoke(capsule);
            uint8 v = r.get<uint8>(STR("ReturnValue"));
            resp += v == 0 ? STR('I') : v == 1 ? STR('O') : v == 2 ? STR('B') : STR('?');
        }
        DVec at = actor_location(c);
        DVec pelvis = bone_world(*slot, L"pelvis").pos;
        log(STR("[RoNDelirium] capsule of the dead {} at {}: collision {}, profile {}, channels {}, {:.2f} m from the body's pelvis "
                "(up {:.2f}), body {}, {}\n"),
            c->GetName(), when, en.get<uint8>(STR("ReturnValue")), fname_call(capsule, STR("GetCollisionProfileName")).ToString(), resp,
            std::hypot(at.x - pelvis.x, at.y - pelvis.y) / 100.0, (at.z - pelvis.z) / 100.0,
            simulating(*slot, L"pelvis") ? STR("simulating") : STR("not simulating"), d.mod ? STR("Delirium took it") : STR("the game's alone (mod off)"));
    }

    inline void census_tick()
    {
        if (!cfg.log) return;
        std::erase_if(deaths_seen, [](const DeathSeen& d) { return d.character.Get() == nullptr || d.stage >= 2; });
        for (auto& t : tracked)
        {
            UObject* c = t.Get();
            if (!c || !dead(c)) continue;
            auto it = std::find_if(deaths_seen.begin(), deaths_seen.end(), [&](const DeathSeen& d) { return d.character.Get() == c; });
            if (it == deaths_seen.end())
            {
                if (std::any_of(censused.begin(), censused.end(), [&](const FWeakObjectPtr& x) { return x.Get() == c; })) continue;
                censused.push_back(weak(c));
                deaths_seen.push_back({weak(c), now_s, 0, cfg.enabled && v2(c)});
                continue;
            }
            double age = now_s - it->t;
            if (it->stage == 0 && age > 1.0) capsule_census(c, *it, STR("1 s")), it->stage = 1;
            else if (it->stage == 1 && age > 4.0) capsule_census(c, *it, STR("4 s")), it->stage = 2;
        }
        std::erase_if(censused, [](const FWeakObjectPtr& x) { return x.Get() == nullptr; });
    }

    // The character's capsule kept under the staggering pelvis, so the animation picks up where the body ended.
    // Delirium's pelvis, not the mesh's pelvis bone: the bone is the physics blended with the animation, and the animation
    // goes where the actor goes, so following the bone fed back. Blending back into the animation it ran off in the last
    // tenth of a second (the body slid a third of a metre at 3 m/s), and a civilian whose bodies the game took back from
    // the physics was carried off at 20-150 m/s, across the room and into the air. No faster than capsule_speed either.
    inline void capsule_follow(UObject* character, DVec pelvis_nm, double dt)
    {
        // Online, the actor's place is the server's (it replicates it): a client moving it fought the replication, and the
        // host's moves reached everyone. Only where the game has authority (always, playing alone).
        if (UFunction* auth = ufn::find(character, STR("HasAuthority")))
        {
            ufn::Call c(auth);
            c.invoke(character);
            if (!c.get<bool>(STR("ReturnValue")))
            {
                static bool told = false;
                if (!told) told = true, log(STR("[RoNDelirium]   a network client: the capsule's place left to the server\n"));
                return;
            }
        }
        ufn::Call get(ufn::find(character, STR("K2_GetActorLocation")));
        get.invoke(character);
        DVec at = get.get<DVec>(STR("ReturnValue"));
        DVec to = to_ue_point(pelvis_nm);
        to.z = at.z;
        const double most = cfg.capsule_speed * 100.0 * std::max(dt, 1.0 / 240.0);  // cm this frame
        if (DVec d = to - at; len(d) > most) to = at + d * (most / len(d));
        ufn::Call(ufn::find(character, STR("K2_SetActorLocation"))).set(STR("NewLocation"), to).set<bool>(STR("bSweep"), true)
            .set<bool>(STR("bTeleport"), false).invoke(character);
        if (auto move = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("CharacterMovement")); move && *move)
        {
            ufn::Call(ufn::find(*move, STR("StopMovementImmediately"))).invoke(*move);
        }
    }

    // Diagnosis (log = 1): the ragdoll asset's joint limits as the game made them (its defaults, before loosen_joints), once
    // per asset: which of them the body is held at while Delirium's limbs go further (the forearms, the head).
    inline void log_constraints(UObject* asset)
    {
        static std::vector<UObject*> seen;
        if (!cfg.log || !asset || std::find(seen.begin(), seen.end(), asset) != seen.end()) return;
        seen.push_back(asset);
        const auto& all = asset_limits(asset);
        if (all.empty()) log(STR("[RoNDelirium]   {}: no joint limits to read\n"), asset->GetName());
        const CharType* MOTION[] = {STR("free"), STR("limited"), STR("locked")};
        auto motion = [&](uint8 m) { return m < 3 ? MOTION[m] : STR("?"); };
        for (const AssetLimit& l : all)
        {
            log(STR("[RoNDelirium]   {} joint {} - {}: swing1 {:.0f} {}, swing2 {:.0f} {}, twist {:.0f} {}\n"), asset->GetName(), l.bone, l.parent, l.s1, motion(l.m1),
                l.s2, motion(l.m2), l.tw, motion(l.mt));
        }
    }

    inline Agent* start(UObject* character, bool alive = false)
    {
        if (!cfg.enabled || !character || driving(character)) return nullptr;
        handover_cut(character);
        auto mesh_slot = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("Mesh"));
        if (!mesh_slot || !*mesh_slot) return nullptr;
        UObject* mesh = *mesh_slot;
        // Only the V2 ragdoll (suspects, civilians): the SWAT's has no feet or neck.
        auto active = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("Rep_ActiveRagdollPhysAsset"));
        if (!active || !*active || !(*active)->GetName().ends_with(STR("_V2"))) return nullptr;
        log_constraints(*active);
        Calib* cal = calibrate(mesh);
        if (!cal) return nullptr;

        Agent a;
        a.character = weak(character);
        a.mesh = weak(mesh);
        a.cal = cal;
        a.alive = alive;
        a.limp = !alive && head_kill(character);
        float at[3]{};
        a.agent = nm::api.agent_create(cal->build, at, 0.0f, 0.3f);
        if (!a.agent)
        {
            Output::send<LogLevel::Error>(STR("[RoNDelirium] agent: {}\n"), to_wstring(nm::api.error()));
            if (a.agent) nm::api.agent_destroy(a.agent);
            return nullptr;
        }
        if (auto it = last_hits.find(character); it != last_hits.end()) a.hit = it->second;
        a.seed = uint32_t(random(0, 4294967295.0));
        nm::api.agent_set_seed(a.agent, a.seed);
        open_tape(a, character);
        note(a, std::format("{} hit {}", alive ? "staggers" : "ragdolled", a.hit.bone.empty() ? "?" : narrow(a.hit.bone)));
        game_physical_animation(character, 0.0f);
        log(STR("[RoNDelirium] {} {}: Delirium drives it (hit {})\n"), character->GetName(), alive ? STR("staggers") : STR("ragdolled"),
            a.hit.bone.empty() ? STR("?") : a.hit.bone);
        agents.push_back(std::move(a));
        return &agents.back();
    }

    // The floor under a point (Delirium space): a line trace down through the level (WorldStatic and WorldDynamic only),
    // the character itself ignored. Not people: by the visibility channel, the traces of the ground grid hit the
    // characters standing or falling around (and a dead one's own gear) at 1.2-1.8 m, and Delirium, its legs inside
    // that "floor", flew up and lost the body.
    inline double floor_below(UObject* character, DVec from, double fallback)
    {
        static UObject* lib = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
        static UFunction* trace = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingleForObjects"));
        if (!lib || !trace) return fallback;
        struct RawArray { void* data; int32 num; int32 max; };
        static uint8 types[2] = {0, 1};  // ObjectTypeQuery1, 2: WorldStatic, WorldDynamic
        ufn::Call c(trace);
        c.set(STR("WorldContextObject"), character).set(STR("Start"), to_ue_point(from)).set(STR("End"), to_ue_point(from - DVec{0, 0, 3.0}))
            .set(STR("ObjectTypes"), RawArray{types, 2, 2}).set<bool>(STR("bTraceComplex"), false).set<uint8>(STR("DrawDebugType"), 0)
            .set<bool>(STR("bIgnoreSelf"), true).invoke(lib);
        if (!c.get<bool>(STR("ReturnValue"))) return fallback;
        return to_nm_point(c.field<DVec>(STR("OutHit"), STR("ImpactPoint"))).z;
    }

    // The level around the character for Delirium, which otherwise knows only its floor: each static mesh within a few
    // metres as a box (its local bounds, turned and placed as the mesh is), 13 floats each as nm_world_set_boxes takes
    // them. Left out: floors (Delirium has its own), blocks the pelvis is inside (a room's whole shell), the character's
    // own things (its weapon).
    inline std::vector<float> level_boxes(UObject* character, DVec pelvis, double floor, const std::array<T, nm::PARTS>& parts)
    {
        static UObject* lib = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
        static UFunction* overlap = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:BoxOverlapComponents"));
        static UClass* mesh_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.StaticMeshComponent"));
        std::vector<float> out;
        if (!lib || !overlap || !mesh_class) return out;
        struct RawArray { void* data; int32 num; int32 max; };
        static uint8 types[2] = {0, 1};  // ObjectTypeQuery1, 2: WorldStatic, WorldDynamic
        ufn::Call c(overlap);
        c.set(STR("WorldContextObject"), character).set(STR("BoxPos"), to_ue_point(pelvis)).set(STR("Extent"), DVec{250, 250, 150})
            .set(STR("ObjectTypes"), RawArray{types, 2, 2}).set(STR("ComponentClassFilter"), mesh_class).invoke(lib);
        auto found = c.get<RawArray>(STR("OutComponents"));
        struct Box { double dist; std::array<float, 13> f; };
        std::vector<Box> boxes;
        for (int32 i = 0; found.data && i < found.num; ++i)
        {
            UObject* comp = static_cast<UObject**>(found.data)[i];
            if (!comp) continue;
            ufn::Call owner(ufn::find(comp, STR("GetOwner")));
            owner.invoke(comp);
            UObject* actor = owner.get<UObject*>(STR("ReturnValue"));
            if (actor == character) continue;
            if (actor)
            {
                ufn::Call up(ufn::find(actor, STR("GetAttachParentActor")));
                up.invoke(actor);
                if (up.get<UObject*>(STR("ReturnValue")) == character) continue;
            }
            ufn::Call bounds(ufn::find(comp, STR("GetLocalBounds")));
            if (!bounds) continue;
            bounds.invoke(comp);
            DVec lo = bounds.get<DVec>(STR("Min")), hi = bounds.get<DVec>(STR("Max"));
            DTransform t = world_of(comp);
            DVec s{std::abs(t.scale.x), std::abs(t.scale.y), std::abs(t.scale.z)};
            DVec mid{(lo.x + hi.x) / 2 * t.scale.x, (lo.y + hi.y) / 2 * t.scale.y, (lo.z + hi.z) / 2 * t.scale.z};
            DVec half{(hi.x - lo.x) / 2 * s.x / 100, (hi.y - lo.y) / 2 * s.y / 100, (hi.z - lo.z) / 2 * s.z / 100};
            if (half.x < 0.01 || half.y < 0.01 || half.z < 0.01) continue;
            if (half.x > 2.0 && half.y > 2.0 && half.z > 2.0) continue;  // a room-sized block
            T box{to_nm_point(t.pos + rotate(t.rot, mid)), to_nm_rot(t.rot)};
            // A box the body already touches (a hand on a counter, a back to a wall) would be shoved out of at the first step:
            // left out, with some room around the parts.
            auto inside = [&](DVec p, double room) {
                DVec rel = rotate(conj(box.q), p - box.p);
                return std::abs(rel.x) < half.x + room && std::abs(rel.y) < half.y + room && std::abs(rel.z) < half.z + room;
            };
            if (std::any_of(parts.begin(), parts.end(), [&](const T& part) { return inside(part.p, 0.15); })) continue;
            // Its top: the highest corner.
            DVec ax = rotate(box.q, DVec{half.x, 0, 0}), ay = rotate(box.q, DVec{0, half.y, 0}), az = rotate(box.q, DVec{0, 0, half.z});
            double top = box.p.z + std::abs(ax.z) + std::abs(ay.z) + std::abs(az.z);
            if (top < floor + 0.12) continue;  // a floor or a rug: Delirium has its floor
            boxes.push_back({len(box.p - pelvis), {float(box.p.x), float(box.p.y), float(box.p.z), float(box.q.x), float(box.q.y), float(box.q.z),
                                                    float(box.q.w), float(half.x), float(half.y), float(half.z), 0, 0, 0}});
        }
        std::sort(boxes.begin(), boxes.end(), [](const Box& x, const Box& y) { return x.dist < y.dist; });
        for (size_t i = 0; i < boxes.size() && i < 48; ++i) out.insert(out.end(), boxes[i].f.begin(), boxes[i].f.end());
        return out;
    }

    // The walls around the character, by traces: the level's walls are mostly parts of big meshes (a room's whole shell,
    // left out of level_boxes), and Delirium, not knowing them, pulled bodies half into them. Each trace that hits a
    // steep surface gives a slab behind it, its face on the wall: 16 directions at the knees, the hips and the chest.
    inline std::vector<float> wall_slabs(UObject* character, DVec pelvis, double floor, const std::array<T, nm::PARTS>& parts)
    {
        static UObject* lib = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
        static UFunction* trace = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.KismetSystemLibrary:LineTraceSingleForObjects"));
        std::vector<float> out;
        if (!lib || !trace) return out;
        struct RawArray { UObject** data; int32 num; int32 max; };
        struct TypeArray { void* data; int32 num; int32 max; };
        static uint8 types[2] = {0, 1};  // the level only (WorldStatic, WorldDynamic): a person beside them isn't a wall
        // The character's own things (its weapon) aren't walls.
        ufn::Call attached(ufn::find(character, STR("GetAttachedActors")));
        attached.set<bool>(STR("bResetArray"), true).set<bool>(STR("bRecursivelyIncludeAttachedActors"), true).invoke(character);
        auto own = attached.get<RawArray>(STR("OutActors"));
        std::vector<UObject*> ignore(own.data, own.data + std::max(own.num, 0));
        constexpr double REACH = 2.0, HALF_DEPTH = 0.2, HALF_SIDE = 0.35;
        std::vector<DVec> found;
        for (double height : {0.35, 0.9, 1.4})
        {
            for (int i = 0; i < 16; ++i)
            {
                double yaw = i * 3.14159265358979 / 8;
                DVec from{pelvis.x, pelvis.y, floor + height};
                DVec to = from + DVec{std::cos(yaw), std::sin(yaw), 0} * REACH;
                ufn::Call c(trace);
                c.set(STR("WorldContextObject"), character).set(STR("Start"), to_ue_point(from)).set(STR("End"), to_ue_point(to))
                    .set(STR("ObjectTypes"), TypeArray{types, 2, 2}).set<bool>(STR("bTraceComplex"), false)
                    .set(STR("ActorsToIgnore"), RawArray{ignore.data(), int32(ignore.size()), int32(ignore.size())})
                    .set<uint8>(STR("DrawDebugType"), 0).set<bool>(STR("bIgnoreSelf"), true).invoke(lib);
                if (!c.get<bool>(STR("ReturnValue"))) continue;
                DVec at = to_nm_point(c.field<DVec>(STR("OutHit"), STR("ImpactPoint")));
                if (std::hypot(at.x - from.x, at.y - from.y) < 0.05) continue;  // started inside something
                DVec n = to_nm_point(c.field<DVec>(STR("OutHit"), STR("ImpactNormal")));
                if (len(n) < 1e-6) continue;
                n = n * (1.0 / len(n));
                if (std::abs(n.z) > 0.6) continue;  // a slope or a step, the ground mesh has it
                // One slab per stretch of wall.
                if (std::any_of(found.begin(), found.end(), [&](DVec f) { return len(f - at) < 0.3; })) continue;
                found.push_back(at);
                DVec flat = DVec{n.x, n.y, 0} * (1.0 / std::hypot(n.x, n.y));
                double angle = std::atan2(flat.y, flat.x);
                T slab{at - flat * HALF_DEPTH, {0, 0, std::sin(angle / 2), std::cos(angle / 2)}};  // its x out of the wall
                // A body already through the wall there: no slab (it'd be shoved out at the first step).
                bool through = std::any_of(parts.begin(), parts.end(), [&](const T& part) {
                    DVec rel = rotate(conj(slab.q), part.p - slab.p);
                    return std::abs(rel.x) < HALF_DEPTH && std::abs(rel.y) < HALF_SIDE && std::abs(rel.z) < HALF_SIDE;
                });
                if (through) continue;
                out.insert(out.end(), {float(slab.p.x), float(slab.p.y), float(slab.p.z), float(slab.q.x), float(slab.q.y), float(slab.q.z),
                                       float(slab.q.w), float(HALF_DEPTH), float(HALF_SIDE), float(HALF_SIDE), 0, 0, 0});
            }
        }
        return out;
    }

    // The dead lying near (still, nobody driving them) as boxes for Delirium, one per trunk, head and limb bone: a body
    // falling on another lay on it in the game while Delirium, not knowing it, lay on the floor below and the body hung
    // half a metre over it the whole time (a tased one on a corpse, 10 s). The floor's traces no longer hit people, so a
    // corpse isn't floor either. Boxes a part of this body already touches are left out (shoved out at the first step).
    inline std::vector<float> corpse_boxes(UObject* character, DVec pelvis, const std::array<T, nm::PARTS>& parts, int* bodies)
    {
        std::vector<float> out;
        *bodies = 0;
        std::vector<std::pair<double, UObject*>> close_by;
        std::vector<UObject*> seen;
        // (log = 1: each one within 4 m and why it was or wasn't taken; in 1.5-test8 none ever was, even a corpse 1.7 m off)
        auto consider = [&](UObject* c) {
            if (!c || c == character || std::find(seen.begin(), seen.end(), c) != seen.end()) return;
            seen.push_back(c);
            auto slot = c->GetValuePtrByPropertyNameInChain<UObject*>(STR("Mesh"));
            if (!slot || !*slot) return;
            UObject* mesh = *slot;
            DVec at = to_nm_point(bone_world(mesh, L"pelvis").pos);
            double d = len(at - pelvis);
            if (d > 4.0) return;
            double v = len(to_nm_point(body_vec(mesh, STR("GetPhysicsLinearVelocity"), L"pelvis")));
            const CharType* why = !dead(c) ? STR("alive") : driving(c) ? STR("Delirium drives it") : d > 2.5 ? STR("over 2.5 m")
                                : v > 0.6 ? STR("still moving") : nullptr;
            log(STR("[RoNDelirium]   {} {:.2f} m off (pelvis {:.2f} m up, {:.2f} m/s, {}): {}\n"), c->GetName(), d, at.z - pelvis.z, v,
                simulating(mesh, L"pelvis") ? STR("simulating") : STR("not simulating"), why ? why : STR("a corpse to lie on"));
            if (!why) close_by.push_back({d, c});
        };
        for (auto& t : people) consider(t.Get());
        for (auto& t : tracked) consider(t.Get());
        log(STR("[RoNDelirium]   looked for the dead near among {} people and {} hit\n"), people.size(), tracked.size());
        std::sort(close_by.begin(), close_by.end());
        if (close_by.size() > 3) close_by.resize(3);
        struct Seg { const wchar_t* from; const wchar_t* to; double r; };
        static const Seg SEGS[] = {{L"pelvis", L"spine_3", 0.14}, {L"thigh_LE", L"calf_LE", 0.08}, {L"calf_LE", L"foot_LE", 0.06},
                                   {L"thigh_RI", L"calf_RI", 0.08}, {L"calf_RI", L"foot_RI", 0.06}, {L"upperarm_LE", L"lowerarm_LE", 0.055},
                                   {L"lowerarm_LE", L"hand_LE", 0.045}, {L"upperarm_RI", L"lowerarm_RI", 0.055}, {L"lowerarm_RI", L"hand_RI", 0.045},
                                   {L"neck_1", L"head", 0.11}};
        for (auto& [d, c] : close_by)
        {
            UObject* mesh = *c->GetValuePtrByPropertyNameInChain<UObject*>(STR("Mesh"));
            bool any = false;
            for (const Seg& sg : SEGS)
            {
                DVec a = to_nm_point(bone_world(mesh, sg.from).pos), b = to_nm_point(bone_world(mesh, sg.to).pos);
                if (&sg == &SEGS[9]) b = b + (b - a) * (0.09 / std::max(len(b - a), 1e-3));  // the skull, above the head bone
                DVec u = b - a;
                double l = len(u);
                if (l < 1e-3 || l > 1.2) continue;
                u = u * (1.0 / l);
                // The box's x along the bone: the turn from x to u.
                DVec x{1, 0, 0};
                DVec ax = cross(x, u);
                double w = 1.0 + u.x;
                DQuat q = w < 1e-6 ? DQuat{0, 0, 1, 0} : DQuat{ax.x, ax.y, ax.z, w};
                double qn = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
                q = {q.x / qn, q.y / qn, q.z / qn, q.w / qn};
                DVec mid = (a + b) * 0.5;
                DVec half{l / 2 + sg.r * 0.5, sg.r, sg.r};
                bool touching = std::any_of(parts.begin(), parts.end(), [&](const T& part) {
                    DVec rel = rotate(conj(q), part.p - mid);
                    return std::abs(rel.x) < half.x + 0.05 && std::abs(rel.y) < half.y + 0.05 && std::abs(rel.z) < half.z + 0.05;
                });
                if (touching)
                {
                    log(STR("[RoNDelirium]   {}: {}-{} left out (this body touches it)\n"), c->GetName(), sg.from, sg.to);
                    continue;
                }
                out.insert(out.end(), {float(mid.x), float(mid.y), float(mid.z), float(q.x), float(q.y), float(q.z), float(q.w),
                                       float(half.x), float(half.y), float(half.z), 0, 0, 0});
                any = true;
            }
            *bodies += any;
        }
        return out;
    }

    // Continuous collision on the ragdoll's bodies: driven fast at a thin wall, they went through it between frames.
    inline void use_ccd(UObject* mesh)
    {
        UFunction* fn = ufn::find(mesh, STR("SetUseCCD"));
        if (!fn) return;
        std::vector<const wchar_t*> done;
        for (auto body : BODY)
        {
            if (std::find(done.begin(), done.end(), body) != done.end()) continue;
            done.push_back(body);
            ufn::Call(fn).set<bool>(STR("InUseCCD"), true).set(STR("BoneName"), name(body)).invoke(mesh);
        }
    }

    // The ground around as Delirium's floor: a grid of traces down (3 m square, every 25 cm) made triangles, so steps,
    // kerbs and slopes are under the body, not one flat floor at the pelvis's height (a body on stairs lay on the top
    // step's height and hung in the air). Returns the lowest point.
    inline double ground_mesh(void* world, UObject* character, DVec pelvis, double fallback)
    {
        constexpr int N = 13;
        constexpr double STEP = 0.25;
        std::vector<float> at;
        at.reserve(N * N * 3);
        double lowest = 1e9;
        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                DVec p{pelvis.x + (i - N / 2) * STEP, pelvis.y + (j - N / 2) * STEP, pelvis.z + 1.0};
                double z = floor_below(character, p, fallback);
                lowest = std::min(lowest, z);
                at.insert(at.end(), {float(p.x), float(p.y), float(z)});
            }
        }
        std::vector<uint32_t> tris;
        for (int i = 0; i + 1 < N; ++i)
        {
            for (int j = 0; j + 1 < N; ++j)
            {
                uint32_t a = i * N + j, b = (i + 1) * N + j, c = (i + 1) * N + j + 1, d = i * N + j + 1;
                tris.insert(tris.end(), {a, b, c, a, c, d});
            }
        }
        nm::api.world_add_mesh(world, at.data(), N * N, tris.data(), int32_t(tris.size() / 3), nullptr);
        return lowest;
    }

    // A rotation as an axis times its angle (rad), the short way round.
    inline DVec rotvec(DQuat q)
    {
        if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
        double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
        if (s < 1e-9) return DVec{q.x, q.y, q.z} * 2.0;
        double angle = 2.0 * std::atan2(s, q.w);
        return DVec{q.x, q.y, q.z} * (angle / s);
    }

    // The shot task's health for a live character, from the game's own: unhurt is stagger_health, nearly gone weak_health,
    // so each bullet leaves them less able to stay up, as in GTA IV.
    inline float live_health(UObject* character)
    {
        return cfg.weak_health + (cfg.stagger_health - cfg.weak_health) * health_left(character);
    }

    // For bug reports, twice a second: how far each part of what they hold has left its place on the hand. It should stay 0
    // (the hand carries it rigidly); if the game moves it too, this shows.
    inline void watch_held(Agent& a, UObject* mesh)
    {
        if (!cfg.log || a.time < a.next_gun_diag) return;
        a.next_gun_diag = a.time + 0.5;
        for (auto& h : a.held)
        {
            UObject* root = h.root.Get();
            if (!root) continue;
            DTransform hand = bone_world(mesh, h.hand);
            for (auto& p : h.parts)
            {
                UObject* c = p.comp.Get();
                if (!c) continue;
                DTransform w = world_of(c);
                T now = compose(inverse(T{hand.pos, hand.rot}), T{w.pos, w.rot});
                log(STR("[RoNDelirium]   {} {}: {:.0f} deg, {:.1f} cm off its place on the hand\n"), root->GetName(), c->GetName(),
                    len(rotvec(mul(now.q, conj(p.on_hand.q)))) * 57.29578, len(now.p - p.on_hand.p));
            }
        }
    }

    // One frame of one agent; false when it's done.
    //
    // Delirium runs free with its own body (its joints, their armature, its muscles, a floor at the level's), and the
    // game's ragdoll is its puppet: each body takes its part's velocity plus a pull towards the part's pose, and Chaos
    // still moves it with the level's real collisions. Delirium is set from the ragdoll only at the start, and again
    // if the two drift apart (a wall Delirium doesn't know about): copying the ragdoll into it every frame made it
    // fight a body that never quite fits its joints, and the bodies spun and flew.
    inline bool tick(Agent& a, float dt)
    {
        UObject* character = a.character.Get();
        UObject* mesh = a.mesh.Get();
        if (!character || !mesh) return end(a, STR("gone")), false;
        if (!a.corpse_capsule && dead(character)) a.corpse_capsule = true, corpse_capsule_add(character, mesh);
        // The game's ragdoll may take a frame or two to start after EnableRagdoll.
        // (Taken at the killing bullet: the game's death ragdoll comes up to 0.6 s after it; never, and the body's ours till the end.)
        if (!a.own && !a.death_hit && a.time > 0.3 && !flag(character, STR("IsInRagdoll"))) return end(a, STR("ragdoll off")), false;
        // A wounded leg gives them longer: going down on it takes a second or two.
        // The healthier, the shorter: an unhurt one shrugs it off, one nearly gone reels the full stagger_time.
        double stagger_time = cfg.healthy_stagger + (cfg.stagger_time - cfg.healthy_stagger) * (1.0 - a.left)
                            + (std::max(a.hurt[2], a.hurt[3]) >= 0.4f ? 1.5 : 0.0);
        // Tased: the stagger's clocks wait while the game holds them, then it ends as any does (down, or back to the animation).
        if (a.shock)
        {
            double held = now_s - a.shock_since;
            int state = game_tased(character);
            bool arrested = being_arrested(character);
            bool over = held >= cfg.taser_max || !a.alive || dead(character) || arrested;
            if (held >= cfg.taser_min)
            {
                if (state == 0) over = true;
                else if (state < 0 && held >= (a.shock_for > 0 ? a.shock_for : 4.0)) over = true;
            }
            if (over)
            {
                if (nm::api.msg_begin(a.agent, "electrocute") == 0)
                {
                    nm::api.msg_set_bool(a.agent, "start", 0);
                    nm::api.msg_send(a.agent, a.world);
                }
                a.shock = false;
                note(a, std::format("shock_over {:.1f}", held));
                log(STR("[RoNDelirium]   the shock's over ({:.1f} s)\n"), held);
                if (a.alive && arrested) return end(a, STR("being arrested")), false;
                a.time = std::max(a.time, stagger_time - 0.4);
                a.total = 0;
                a.still = 0;
            }
            else
            {
                a.time = 0;
                a.total = 0;
                a.still = 0;
            }
        }
        const double dead_for = a.limp ? cfg.headshot_duration : cfg.duration;
        if (a.time > (a.alive ? stagger_time + cfg.blend_out + 0.5 : dead_for)) return end(a, STR("time")), false;
        if (!a.alive && a.total > cfg.dead_max) return end(a, STR("most a corpse gets")), false;
        // The game's physical animation off while Delirium drives, every frame: the game turns it on itself (for one
        // incapacitated, its writhing on the floor), and the two pulling the same bones stood the body half up, then flung it.
        game_physical_animation(character, 0.0f);
        // A stagger that turns deadly becomes the dying reaction.
        if (a.alive && dead(character))
        {
            log(STR("[RoNDelirium]   {} ({:.1f} s{})\n"), killed(character) ? STR("killed") : STR("incapacitated"), a.time,
                a.blend_left >= 0 ? STR(", blending back") : STR(""));
            a.alive = false;
            a.limp = head_kill(character);
            note(a, killed(character) ? "killed" : "incapacitated");
            if (a.own)  // the game's death ragdoll takes these same bodies over: its weapon and capsule as it left them
            {
                release_held(a);
                capsule_later(a, character);
                // Killed while blending back into the animation: the bodies all physics again (left part animation, the
                // corpse hung in the air in the pose it had).
                blend(mesh, 1.0f);
            }
            a.own = false;
            a.blend_left = -1;
            a.time = 0;
            a.total = 0;
        }
        // The stagger's end: back into the animation if Delirium kept them up, the game's ragdoll and get-up if not.
        // Bullet after bullet restarts the stagger's clock, but not past a second more in all: they can't be kept in it.
        if (a.own && a.alive && (a.time > stagger_time || a.still > 0.6 || a.total > stagger_time + 1.0) && a.blend_left < 0)
        {
            // Down only if they went down: one who started crouched or kneeling (a civilian giving up) counts as fallen
            // to Delirium from the start, and was handed to the game's ragdoll and its get-up.
            float hp[3], hq[4];
            nm::api.agent_get_part_transform(a.agent, 12, hp, hq);
            bool went_down = nm::api.agent_has_fallen(a.agent) && hp[2] - a.floor < std::min(a.head_start - 0.35, 1.1);
            // Loose (Delirium lost it): whether the body itself went down.
            if (a.loose) went_down = to_nm(bone_world(mesh, BONE[12])).p.z - a.floor < std::min(a.head_start - 0.35, 1.1);
            // A leg that gave and left them bent over it goes down too: stood back up from that, they snapped to aiming.
            bool leg = std::max(a.hurt[2], a.hurt[3]) >= 0.4f;
            if (leg && hp[2] - a.floor < a.head_start - 0.2) went_down = true;
            if (went_down)
            {
                release_held(a);
                capsule_later(a, character);
                ufn::Call(ufn::find(character, STR("EnableRagdoll"))).set(STR("Duration"), 1.0f).set<bool>(STR("bClearBodyVelocities"), false).invoke(character);
                a.own = false;
                return end(a, STR("down: the game gets them up")), false;
            }
            a.blend_left = cfg.blend_out;
            note(a, std::format("back_to_animation head {:.2f}", hp[2] - a.floor));
            log(STR("[RoNDelirium]   back to the animation ({}), head {:.2f} m up (began {:.2f})\n"), a.time > stagger_time ? STR("time") : STR("settled"),
                hp[2] - a.floor, a.head_start);
            face_as_body(a, character, mesh);
            a.bridging = cfg.pose_bridge && a.has_start;
            if (a.bridging)
            {
                a.bridge_frame = actor_frame(character);
                ufn::Call at(ufn::find(character, STR("K2_GetActorLocation")));
                at.invoke(character);
                a.bridge_at = at.get<DVec>(STR("ReturnValue"));
                a.bridge_rot = actor_rotation(character);
            }
        }
        // Blending back: Delirium keeps driving the body (left alone it sagged while the animation came in), eased in and
        // out so neither end jumps.
        if (a.blend_left >= 0)
        {
            a.blend_left -= dt;
            if (a.blend_left <= 0)
            {
                if (!(a.bridging && cfg.handover > 0 && handover_begin(a, character, mesh))) own_physics_off(a, character, mesh);
                recovered_at[character] = now_s + cfg.recover_time * a.left;
                return end(a, STR("back to the animation")), false;
            }
            double x = a.blend_left / cfg.blend_out;
            // (the bridge: the physics keeps all of the body, the drive below takes it to the pose; the animation at the end)
            if (!a.bridging) blend(mesh, float(peak_blend(a) * x * x * (3 - 2 * x)));
        }

        // The ragdoll now: each body's motion and centre of mass, each part's bone.
        std::map<StringType, std::pair<DVec, DVec>> motion;  // body: velocity at its centre of mass, angular velocity
        std::map<StringType, DVec> com;
        for (auto body : BODY)
        {
            if (motion.contains(body)) continue;
            motion[body] = {to_nm_point(body_vec(mesh, STR("GetPhysicsLinearVelocity"), body)),
                            to_nm_axial(body_vec(mesh, STR("GetPhysicsAngularVelocityInRadians"), body))};
            com[body] = to_nm_point(body_vec(mesh, STR("GetCenterOfMass"), body));
        }
        std::array<T, nm::PARTS> bone{}, part{};
        T spine3 = to_nm(bone_world(mesh, BONE[10]));
        for (int k = 0; k < nm::PARTS; ++k)
        {
            bone[k] = k == 13 ? compose(spine3, a.cal->clavicle_l) : k == 17 ? compose(spine3, a.cal->clavicle_r) : to_nm(bone_world(mesh, BONE[k]));
            part[k] = compose(bone[k], a.cal->off[k]);
        }

        // Delirium set from the ragdoll: rotations its bodies', positions and velocities put together from the pelvis
        // out through Delirium's joints, so its body never starts pulled apart.
        auto sync = [&]() {
            std::array<DVec, nm::PARTS> centre{}, vel{}, spin{};
            for (int k = 0; k < nm::PARTS; ++k) spin[k] = motion[BODY[k]].second;
            centre[0] = part[0].p;
            vel[0] = motion[BODY[0]].first + cross(spin[0], part[0].p - com[BODY[0]]);
            for (int k = 1; k < nm::PARTS; ++k)
            {
                int p = a.cal->parent[k];
                DVec from_parent = rotate(part[p].q, a.cal->anchor_parent[k]);
                DVec to_pivot = rotate(part[k].q, a.cal->anchor[k]);
                centre[k] = centre[p] + from_parent - to_pivot;
                vel[k] = vel[p] + cross(spin[p], from_parent) - cross(spin[k], to_pivot);
            }
            // The ragdoll as read (its bones, each part's body's motion and centre of mass): a replay on another
            // character puts its own states together from these, as this does for ours.
            if (a.tape)
            {
                auto& s = *a.tape;
                s += "sync";
                for (int k = 0; k < nm::PARTS; ++k)
                {
                    auto [v, w] = motion[BODY[k]];
                    DVec c = com[BODY[k]];
                    float x[16] = {float(bone[k].p.x), float(bone[k].p.y), float(bone[k].p.z), float(bone[k].q.x), float(bone[k].q.y), float(bone[k].q.z),
                                   float(bone[k].q.w), float(v.x), float(v.y), float(v.z), float(w.x), float(w.y), float(w.z), float(c.x), float(c.y), float(c.z)};
                    nm::tape::put(s, x, 16);
                }
                s += '\n';
            }
            float states[nm::PARTS * 13];
            for (int k = 0; k < nm::PARTS; ++k)
            {
                float* s = states + 13 * k;
                s[0] = float(centre[k].x), s[1] = float(centre[k].y), s[2] = float(centre[k].z);
                s[3] = float(part[k].q.x), s[4] = float(part[k].q.y), s[5] = float(part[k].q.z), s[6] = float(part[k].q.w);
                s[7] = float(vel[k].x), s[8] = float(vel[k].y), s[9] = float(vel[k].z);
                s[10] = float(spin[k].x), s[11] = float(spin[k].y), s[12] = float(spin[k].z);
            }
            return nm::api.agent_set_part_states(a.agent, states, nm::PARTS) == 0;
        };
        auto delirium = [&](int k) {
            float p[3], q[4];
            nm::api.agent_get_part_transform(a.agent, k, p, q);
            return T{{p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}};
        };

        // Delirium's world around the body where it is now: the ground as it is (steps, kerbs, slopes) under it, a flat floor
        // below its lowest point as a backstop, walls and furniture (and the dead lying near) as boxes, so Delirium steps and
        // reaches around them instead of pulling the body through. Made at the start and again each time the body has gone
        // rebuild_world metres from where it was made: the ground grid is 3 m square, and a body carried further fell
        // against walls and furniture Delirium had never been told of (bodies that went 1.4 m or more drifted off it
        // nearly five times as often).
        auto make_world = [&](double floor) {
            void* w = nm::api.world_create();
            if (a.tape) *a.tape += "world\n", nm::tape::on[w] = a.tape.get();
            double lowest = ground_mesh(w, character, part[0].p, floor);
            nm::api.world_add_floor(w, float(part[0].p.x), float(part[0].p.y), float(lowest - 0.02), 50.0f);
            auto boxes = level_boxes(character, part[0].p, floor, part);
            size_t furniture = boxes.size() / 13;
            size_t slabs_n = 0;
            if (cfg.walls)
            {
                auto slabs = wall_slabs(character, part[0].p, floor, part);
                slabs_n = slabs.size() / 13;
                boxes.insert(boxes.end(), slabs.begin(), slabs.end());
            }
            int lying = 0;
            if (cfg.corpses)
            {
                auto dead_near = corpse_boxes(character, part[0].p, part, &lying);
                boxes.insert(boxes.end(), dead_near.begin(), dead_near.end());
            }
            if (!boxes.empty()) nm::api.world_set_boxes(w, boxes.data(), int32_t(boxes.size() / 13));
            if (a.world)
            {
                nm::tape::on.erase(a.world);
                nm::api.world_destroy(a.world);
            }
            a.world = w;
            a.world_at = part[0].p;
            a.world_t = a.clock;
            log(STR("[RoNDelirium]   {}{} furniture boxes, {} wall slabs around, {} boxes of {} bodies lying near\n"),
                a.worlds ? STR("world made again here: ") : STR(""), furniture, slabs_n, boxes.size() / 13 - furniture - slabs_n, lying);
        };
        if (!a.started)
        {
            if (!sync()) return end(a, STR("error")), false;
            a.start_at = part[0].p;
            if (a.own && a.alive)
            {
                const T on = inverse(actor_frame(character));
                for (int k = 0; k < nm::PARTS; ++k) a.start_rel[k] = compose(on, part[k]);
                a.has_start = true;
            }
            a.wrist[0] = mul(conj(part[15].q), part[16].q);
            a.wrist[1] = mul(conj(part[19].q), part[20].q);
            double feet = std::min(part[3].p.z, part[6].p.z) - 0.08;
            double floor = floor_below(character, part[0].p, feet);
            a.floor = floor;
            a.head_start = part[12].p.z - floor;
            make_world(floor);
            use_ccd(mesh);
            loosen_joints(mesh);
            log(STR("[RoNDelirium]   floor {:.2f} m below the pelvis{}\n"), part[0].p.z - floor, floor == feet ? STR(" (no trace hit: under the feet)") : STR(""));
            a.started = true;
        }
        else if (a.limb_resync)
        {
            // A limb the ragdoll can't bring where Delirium has it (Ready or Not's joint limits are narrower than the
            // character's): Delirium takes the ragdoll's pose again, rather than the body being kicked at it every frame.
            a.limb_resync = false;
            ++a.limb_resyncs;
            note(a, "limb_resync");
            // A body held up by something Delirium doesn't know (a dead one propped half a metre up on furniture): set from
            // it again and again, Delirium dropped to its floor each time and the body jerked with it, in hops. Let go.
            // Only when held up (its pelvis well above Delirium's): a limb caught under a body lying on the floor is common,
            // and cut those short at a second.
            if (!a.alive && part[0].p.z - delirium(0).p.z > 0.15 && ++a.propped >= 3)
                return end(a, STR("held up by something Delirium can't see")), false;
            if (!sync()) return end(a, STR("error")), false;
        }
        else if (!a.loose && len(delirium(0).p - part[0].p) > cfg.resync)
        {
            if (!sync()) return end(a, STR("error")), false;
            note(a, "resync");
            // Pulled apart again and again: something else is moving the body (it was flung across the room). Let go.
            if (++a.resyncs >= 3 && !a.own) return end(a, STR("keeps drifting from the ragdoll")), false;
            // On our own physics too: Delirium flung off again and again (wedged in a box at the start) dragged the body
            // about in hops, 160 times in 3 s. The body is let fall as a plain ragdoll then, and the stagger ends as any
            // does, on the floor: handed to the game's ragdoll mid-fall, its get-up began from there and stood up in a T-pose.
            if (a.resyncs >= 6 && a.own && !a.loose)
            {
                a.loose = true;
                note(a, "loose");
                log(STR("[RoNDelirium]   keeps drifting from Delirium: the body falls loose until the stagger ends\n"));
            }
        }

        // The body gone out of the world Delirium was given: made again around it (not while it hangs loose).
        if (a.started && !a.loose && cfg.rebuild_world > 0 && a.clock - a.world_t > 0.5
            && std::hypot(part[0].p.x - a.world_at.x, part[0].p.y - a.world_at.y) > cfg.rebuild_world)
        {
            double floor = floor_below(character, part[0].p, a.floor);
            ++a.worlds;
            note(a, "world_again");
            make_world(floor);
        }

        // The shot task, as GTA IV runs it: each bullet starts it or carries it on, and it runs every frame between.
        // Dying where they already lay (killed on the floor): limp, or the dying reaction braced to get up.
        // Dying: the reaction's stagger for its first moment only, then limp. Each bullet restarted it, and a corpse kept
        // on its feet by bullet after bullet jerked about standing (it danced) for seconds.
        float health = a.alive ? (a.shock || a.shock_pending ? cfg.taser_health : live_health(character)) : (a.head_start < 0.7 || a.total > 0.8) ? std::min(cfg.health, 20.0f) : cfg.health;
        float from[3] = {float(a.hit.from.x), float(a.hit.from.y), float(a.hit.from.z)};
        const float* shooter = len(a.hit.from) > 0 ? from : nullptr;
        if (a.new_hit)
        {
            int hit = a.hit.bone.empty() ? 10 : part_of(a.hit.bone);
            DVec dir = a.hit.dir;
            // No bullet seen (a death the blood and damage hooks missed): the reaction only, no push. The old stand-in,
            // pelvis to chest, pushed them straight up: Delirium flew off and lost the body in 0.2 s.
            bool aimed = len(dir) >= 1e-3;
            if (!aimed) dir = part[10].p - part[0].p;
            dir = dir * (1.0 / len(dir));
            DVec hit_at = len(a.hit.at - part[hit].p) < 0.5 ? a.hit.at : part[hit].p;
            float at[3] = {float(hit_at.x), float(hit_at.y), float(hit_at.z)};
            float d[3] = {float(dir.x), float(dir.y), float(dir.z)};
            // The push by the health left: a light shove for an unhurt one, a hard one for one nearly gone (or dying).
            float left = a.alive ? a.left : 0.0f;
            // (global in the engine: set for each agent's own calls, the dying's lower)
            nm::api.set_shot_body_stiffness(a.alive ? (a.shock || a.shock_pending ? cfg.taser_stiffness : 0.0f) : cfg.dying_stiffness);
            float health_push = cfg.healthy_push + (cfg.weak_push - cfg.healthy_push) * (1.0f - left);
            // A shotgun blast: each pellet pushes the part it hit, with its own direction (those in the hit part with the
            // shot). An arm or a leg takes a few at most (2 on a shin, foot, forearm or hand, 3 on a thigh or upper arm);
            // the rest goes on through the joint, to the pelvis or the chest. The whole blast on the part nearest the
            // spread's middle threw a shin over the head: offline, 3 pellets on it still did (the foot 0.8-1.5 m up), 2 not.
            auto pellet_cap = [](int k) { return k == 1 || k == 4 || k == 14 || k == 18 ? 3 : (k >= 2 && k <= 6) || k == 15 || k == 16 || k == 19 || k == 20 ? 2 : 99; };
            auto limb_root = [](int k) { return k >= 1 && k <= 6 ? 0 : 10; };
            int with_shot = std::max(a.hit.count, 1);
            std::vector<std::pair<int, DVec>> extra;  // (part, impulse) beyond the shot
            float each = weapon_push(a.hit.weapon) * cfg.push_scale * health_push;
            if (a.hit.pellets.size() > 1)
            {
                std::array<int, nm::PARTS> taken{};
                with_shot = 0;
                for (const Pellet& x : a.hit.pellets)
                {
                    int k = part_of(x.bone);
                    if (k < 0 || k >= int(nm::PARTS)) k = hit;
                    DVec pd = len(x.dir) >= 1e-3 ? x.dir * (1.0 / len(x.dir)) : dir;
                    if (taken[k] >= pellet_cap(k)) { extra.push_back({limb_root(k), pd * each}); continue; }
                    ++taken[k];
                    if (k == hit) ++with_shot;
                    else extra.push_back({k, pd * each});
                }
                with_shot = std::max(with_shot, 1);
            }
            else if (with_shot > pellet_cap(hit))  // a blast with no pellet list: the cap, the rest through the joint
            {
                for (int i = pellet_cap(hit); i < with_shot; ++i) extra.push_back({limb_root(hit), dir * each});
                with_shot = pellet_cap(hit);
            }
            nm::api.set_shot_force_scale(a.tase_first || !aimed ? 0.0f : weapon_push(a.hit.weapon) * float(with_shot) / ENGINE_SHOT_FORCE * cfg.push_scale
                                         * health_push);
            nm::api.agent_shot(a.agent, a.world, nullptr, hit, at, d, health, shooter);
            if (!a.tase_first && aimed)
            {
                for (auto& [k, imp] : extra)
                {
                    float pat[3] = {float(part[k].p.x), float(part[k].p.y), float(part[k].p.z)};
                    float pim[3] = {float(imp.x), float(imp.y), float(imp.z)};
                    nm::api.agent_apply_push(a.agent, pat, pim);
                }
            }
            // The hand held on the wound this long, not GTA IV's 3 s: where Ready or Not's joint limits keep it off the
            // wound, the arm stayed jammed there, stiff, for the whole stagger.
            if (nm::api.msg_begin(a.agent, "shot") == 0)
            {
                nm::api.msg_set_float(a.agent, "grabHoldTime", a.tase_first ? 0.0f : cfg.grab_time);
                nm::api.msg_send(a.agent, a.world);
            }
            a.still = 0;
            // Firing back, blind, now and then: a burst a moment after the hit.
            if (a.alive && !a.tase_first && !a.held.empty() && random(0, 1) < cfg.fire_back)
            {
                a.fire_from = a.time + random(0.15, 0.45);
                a.fire_until = a.fire_from + cfg.fire_burst * random(0.5, 1.5);
                a.next_fire = a.fire_from;
            }
            // A bullet in a limb weakens it, by the weapon: a leg gives under them (they kneel or go down), an arm hangs.
            // More bullets in the same limb add up.
            int limb = a.tase_first ? -1 : hit >= 14 && hit <= 16 ? 0 : hit >= 18 && hit <= 20 ? 1 : hit >= 1 && hit <= 3 ? 2 : hit >= 4 && hit <= 6 ? 3 : -1;
            if (limb >= 0)
            {
                float amount = weapon_power(a.hit.weapon) * (limb >= 2 ? cfg.leg_injury : cfg.arm_injury);
                float& hurt = a.hurt[limb];
                hurt = std::min(0.95f, std::max(hurt, amount) + (hurt > 0 ? 0.15f : 0.0f));
                if (hurt > 0 && nm::api.msg_begin(a.agent, "injury") == 0)
                {
                    nm::api.msg_set_int(a.agent, "part", hit);
                    nm::api.msg_set_float(a.agent, "amount", hurt);
                    nm::api.msg_send(a.agent, a.world);
                }
            }
            note(a, std::format("shot {} {} health {:.0f} left {:.2f} hurt {:.2f}", narrow(BONE[hit]), a.hit.weapon ? a.hit.weapon : "?", health, left,
                                limb >= 0 ? a.hurt[limb] : 0.0f));
            log(STR("[RoNDelirium]   shot in {} ({}), health {:.0f} ({:.0f}% left), limb hurt {:.2f}\n"), BONE[hit],
                a.hit.weapon ? to_wstring(a.hit.weapon) : StringType(STR("weapon unknown")), health, left * 100, limb >= 0 ? a.hurt[limb] : 0.0f);
            a.new_hit = false;
            a.tase_first = false;
        }
        else
        {
            nm::api.set_shot_body_stiffness(a.alive ? (a.shock || a.shock_pending ? cfg.taser_stiffness : 0.0f) : cfg.dying_stiffness);
            nm::api.agent_shot_update(a.agent, a.world, health, shooter);
        }
        if (a.shock_pending && a.alive)
        {
            a.shock_pending = false;
            if (nm::api.msg_begin(a.agent, "electrocute") == 0)
            {
                nm::api.msg_set_bool(a.agent, "start", 1);
                nm::api.msg_set_float(a.agent, "stunMag", cfg.taser_strength);
                nm::api.msg_set_float(a.agent, "stunInterval", cfg.taser_interval);
                nm::api.msg_set_float(a.agent, "largeMult", cfg.taser_large);
                nm::api.msg_set_bool(a.agent, "applyStiffness", cfg.taser_stiff ? 1 : 0);
                if (nm::api.msg_send(a.agent, a.world) == 0)
                {
                    a.shock = true;
                    note(a, "shock");
                }
                else Output::send<LogLevel::Warning>(STR("[RoNDelirium] electrocute: {}\n"), to_wstring(nm::api.error()));
            }
        }

        float step = std::clamp(dt, 1.0f / 240.0f, 0.25f);
        // A hitch (a frame of a quarter of a second): Delirium stepped it at once and lost the body (a tased civilian on the
        // floor, flung to the game's ragdoll). A long frame is stepped in pieces of 1/45 s, a tenth of a second at most;
        // frames of 1/30 s and shorter are one step, as before.
        const float delirium_time = std::min(step, 0.1f);
        const int pieces = delirium_time > 1.0f / 30.0f ? int(std::ceil(delirium_time * 45.0f)) : 1;
        for (int i = 0; i < pieces; ++i)
        {
            if (nm::api.agent_step(a.agent, a.world, delirium_time / float(pieces)) != 0)
            {
                Output::send<LogLevel::Error>(STR("[RoNDelirium] step: {}\n"), to_wstring(nm::api.error()));
                return end(a, STR("error")), false;
            }
            // A dying body pushing off the floor: in a few cases Delirium (as GTA IV) drove the legs of one on its way down until
            // it flew up half a metre and more. Its centre of mass no faster up than lift_cap, the limbs' own motion untouched.
            // (Not recorded: replay.py does the same from the recording's 'lift' line, after each step.)
            if (!a.alive && cfg.lift_cap > 0.0f)
            {
                float c[3], v[3];
                nm::api.agent_get_com(a.agent, c, v);
                if (v[2] > cfg.lift_cap)
                {
                    float dv[3] = {0.0f, 0.0f, cfg.lift_cap - v[2]};
                    nm::api.agent_add_velocity(a.agent, dv);
                }
            }
        }
        // A body Delirium lost (not a number anywhere, or flung far): the game's ragdoll takes it as it is, never
        // given that pose or speed. Seen offline only, on untried values.
        for (int k = 0; k < 21; ++k)
        {
            float p[3], q[4], v[3], w[3];
            nm::api.agent_get_part_transform(a.agent, k, p, q);
            nm::api.agent_get_part_velocity(a.agent, k, v, w);
            bool sane = true;
            for (float x : {p[0], p[1], p[2], q[0], q[1], q[2], q[3], v[0], v[1], v[2], w[0], w[1], w[2]}) sane = sane && std::isfinite(x);
            if (!sane || std::hypot(v[0], v[1], v[2]) > 60.0f)
            {
                Output::send<LogLevel::Warning>(STR("[RoNDelirium] Delirium lost the body (part {}): the game's ragdoll takes it\n"), k);
                note(a, "lost");
                if (a.own)  // a stagger: as one that went down (the game's ragdoll and its get-up)
                {
                    release_held(a);
                    capsule_later(a, character);
                    ufn::Call(ufn::find(character, STR("EnableRagdoll"))).set(STR("Duration"), 1.0f).set<bool>(STR("bClearBodyVelocities"), true).invoke(character);
                    a.own = false;
                }
                return end(a, STR("lost")), false;
            }
        }
        a.time += step;
        a.total += step;
        a.clock += step;
        // The burst: the game's own forced fire (as its death throes use), the gun pointing where the staggering hand has it.
        if (a.alive && a.own && !a.held.empty() && a.time >= a.next_fire && a.time >= a.fire_from && a.time < a.fire_until)
        {
            if (UFunction* fire = ufn::find(character, STR("ForceFireGun")))
            {
                if (a.next_fire == a.fire_from) note(a, "fires_back");
                if (a.next_fire == a.fire_from) log(STR("[RoNDelirium]   fires back ({:.1f} s)\n"), a.fire_until - a.fire_from);
                ufn::Call(fire).set(STR("Chance"), 1.0f).invoke(character);
            }
            a.next_fire = a.time + random(0.08, 0.3);
        }
        // On their feet and settled after the hit: back to the animation then, not stood there frozen until stagger_time.
        if (a.alive && a.own && a.blend_left < 0 && a.time > cfg.settle_after && std::max(a.hurt[2], a.hurt[3]) < 0.4f)
        {
            float v[3], w[3], hp[3], hq[4];
            nm::api.agent_get_part_velocity(a.agent, 0, v, w);
            nm::api.agent_get_part_transform(a.agent, 12, hp, hq);
            bool up = hp[2] - a.floor > a.head_start - 0.25;  // as high as they began (standing, or crouched if they were)
            a.still = up && std::hypot(v[0], v[1]) < 0.2f && std::abs(v[2]) < 0.2f ? a.still + step : 0.0;
        }

        // The puppet: each body's velocity, Delirium's part's plus a pull onto that part's pose.
        double top_dv = 0, top_w = 0, top_gap = 0;
        int top_k = -1, gap_k = -1;
        const double up = 9.81 * step;  // Chaos adds this frame's gravity: Delirium's velocity already has it
        // A dead body's last seconds: Delirium's hold eases off, so the ragdoll goes limp gently and not all at once
        // when it's let go (it dropped, a plop, held in Delirium's pose until the last frame).
        double hold = 1.0;
        if (const double fade = a.limp ? dead_for : cfg.fade; !a.alive && fade > 0)
        {
            double x = std::clamp((dead_for - a.time) / fade, 0.0, 1.0);
            hold = x * x * (3 - 2 * x);
        }
        const T bridge_actor = a.bridge_frame;
        std::map<StringType, bool> done;
        for (int k = 0; k < nm::PARTS && !a.loose; ++k)
        {
            const wchar_t* body = BODY[k];
            if (done[body] || k == 13 || k == 17) continue;  // a body follows its first part; the clavicles have none
            done[body] = true;
            T target = delirium(k);
            int spin_of = k;
            // A hand holding the gun keeps its wrist as it held it, turned (and spun) with its forearm: the ragdoll's loose
            // wrist let the gun swing across the arm.
            if (a.own && (k == 16 || k == 20))
            {
                const wchar_t* hand = k == 16 ? L"hand_LE" : L"hand_RI";
                if (std::any_of(a.held.begin(), a.held.end(), [&](const Agent::Held& h) { return std::wcscmp(h.hand, hand) == 0; }))
                {
                    target.q = mul(delirium(k - 1).q, a.wrist[k == 16 ? 0 : 1]);
                    spin_of = k - 1;
                }
            }
            float v[3], w[3], unused[3];
            nm::api.agent_get_part_velocity(a.agent, k, v, w);
            if (spin_of != k) nm::api.agent_get_part_velocity(a.agent, spin_of, unused, w);
            DVec nm_v{v[0], v[1], v[2]}, nm_w{w[0], w[1], w[2]};
            // Blending back, the bridge: the animation blueprint gives no pose while the bodies simulate (its bInRagdoll:
            // the T-pose), and the engine moves them straight to the animation when they stop (a snap). So the body is
            // driven from Delirium's pose to the one the animation had when the bullet came (where the animation picks
            // up again: within 0.13-0.22 m of it in the recordings), there by 70 % of blend_out, and the animation takes over.
            if (a.blend_left >= 0 && a.bridging)
            {
                const double progress = std::clamp((1.0 - a.blend_left / cfg.blend_out) / 0.7, 0.0, 1.0);
                const double wgt = progress * progress * (3 - 2 * progress);
                const T to = compose(bridge_actor, a.start_rel[k]);
                target.p = target.p + (to.p - target.p) * wgt;
                target.q = slerp(target.q, to.q, wgt);
                nm_v = nm_v * (1.0 - wgt);
                nm_w = nm_w * (1.0 - wgt);
            }
            // Where the body's centre of mass would be with its part on Delirium's.
            DQuat turn = mul(target.q, conj(part[k].q));
            // A centre of mass far off its body (the game reports one now and then) would pull the body across the room.
            DVec body_com = len(com[body] - part[k].p) > 0.4 ? part[k].p : com[body];
            DVec com_target = target.p + rotate(turn, body_com - part[k].p);
            DVec want_w = nm_w + rotvec(turn) * cfg.track_rot;
            DVec want_v = nm_v + cross(nm_w, com_target - target.p) + (com_target - body_com) * cfg.track_pos;
            want_v.z += up;
            auto [now_v, now_w] = motion[body];
            // A body that can't reach its part (held off by its joint limits, the floor, a wall) is pulled more gently
            // the further it is, and after a moment Delirium is set from the ragdoll: pulled at full strength it was
            // kicked every frame, and twitched.
            double gap = len(com_target - body_com);
            a.stuck[k] = gap > 0.25 ? a.stuck[k] + step : 0.0;
            // (not on one staggering on their feet: setting Delirium again mid-stagger showed as a start)
            if (a.stuck[k] > 0.35 && !a.own) a.limb_resync = true, a.stuck.fill(0.0);
            double reach = std::clamp(1.0 - (gap - 0.25) / 0.3, 0.15, 1.0);
            // The feet follow loosely: the ground holds them (pulled hard onto Delirium's, which stands on a floor of its
            // own a little off the level's, they were kicked into and off it and the body hopped).
            if (k == 3 || k == 6) reach *= 0.7;
            DVec new_v = now_v + clamp_len((want_v - now_v) * (cfg.follow * hold * reach), cfg.max_kick);
            DVec new_w = now_w + clamp_len((want_w - now_w) * (cfg.follow * hold * reach), cfg.max_spin_kick);
            // No body flung or spun wildly (one taken from the game's ragdoll came spinning at 100-170 rad/s).
            new_v = clamp_len(new_v, 9.0);
            new_w = clamp_len(new_w, 25.0);
            if (len(new_v - now_v) > top_dv) top_dv = len(new_v - now_v), top_k = k;
            top_w = std::max(top_w, len(new_w));
            if (len(com_target - body_com) > top_gap) top_gap = len(com_target - body_com), gap_k = k;
            ufn::Call lin(ufn::find(mesh, STR("SetPhysicsLinearVelocity")));
            lin.set(STR("NewVel"), to_ue_point(new_v)).set<bool>(STR("bAddToCurrent"), false).set(STR("BoneName"), name(body)).invoke(mesh);
            ufn::Call ang(ufn::find(mesh, STR("SetPhysicsAngularVelocityInRadians")));
            ang.set(STR("NewAngVel"), to_nm_axial(new_w)).set<bool>(STR("bAddToCurrent"), false).set(STR("BoneName"), name(body)).invoke(mesh);
        }
        if (a.own)
        {
            // Loose, Delirium no longer knows where the body is: the capsule stays where it was.
            if (a.blend_left >= 0 && a.bridging)
            {
                // (online, only where the game has authority over the actor, as capsule_follow)
                ufn::Call auth(ufn::find(character, STR("HasAuthority")));
                if (auth) auth.invoke(character);
                if (!auth || auth.get<bool>(STR("ReturnValue"))) {
                // The bridge: the actor held where and as it was when the blend began. Let go, the AI turned it to its
                // target at hundreds of degrees a second, and the pose on it spun round with it (a pirouette); its own
                // turning comes after, with its animation.
                if (std::abs(std::remainder(actor_rotation(character).y - a.bridge_rot.y, 360.0)) > 0.5)
                    ufn::Call(ufn::find(character, STR("K2_SetActorRotation"))).set(STR("NewRotation"), a.bridge_rot).set<bool>(STR("bTeleportPhysics"), false).invoke(character);
                ufn::Call now(ufn::find(character, STR("K2_GetActorLocation")));
                now.invoke(character);
                if (len(now.get<DVec>(STR("ReturnValue")) - a.bridge_at) > 1.0)
                    ufn::Call(ufn::find(character, STR("K2_SetActorLocation"))).set(STR("NewLocation"), a.bridge_at).set<bool>(STR("bSweep"), false)
                        .set<bool>(STR("bTeleport"), false).invoke(character);
                if (auto move = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("CharacterMovement")); move && *move)
                    ufn::Call(ufn::find(*move, STR("StopMovementImmediately"))).invoke(*move);
                }
            }
            else if (!a.loose) capsule_follow(character, delirium(0).p, step);
            watch_held(a, mesh);
            // The bodies still the physics' own: something of the game's (a civilian's) can take them back to the animation,
            // and they went where the actor went. Put back, and said so.
            if (a.time >= a.next_sim_check)
            {
                a.next_sim_check = a.time + 0.25;
                if (UFunction* is_sim = ufn::find(mesh, STR("IsSimulatingPhysics")))
                {
                    ufn::Call c(is_sim);
                    c.set(STR("BoneName"), name(L"pelvis")).invoke(mesh);
                    if (!c.get<bool>(STR("ReturnValue")))
                    {
                        simulate(mesh, true);
                        const double x = a.blend_left >= 0 ? std::clamp(a.blend_left / cfg.blend_out, 0.0, 1.0) : 1.0;
                        blend(mesh, float(peak_blend(a) * x * x * (3 - 2 * x)));
                        note(a, "resimulated");
                        log(STR("[RoNDelirium]   the bodies had stopped simulating ({:.1f} s): on again\n"), a.time);
                    }
                }
            }
        }
        // The body carried off faster than Delirium ever drives it (9 m/s at most): something else is moving it. Said once,
        // with the actor's own speed and whether the pelvis still simulated, which tell what carried it. (Diagnosis: only
        // with log or record on, like the animation blueprint's flags below.)
        if (cfg.log || cfg.record)
        {
            ufn::Call actor_at(ufn::find(character, STR("K2_GetActorLocation")));
            actor_at.invoke(character);
            DVec actor = to_nm_point(actor_at.get<DVec>(STR("ReturnValue")));
            if (a.last_pelvis_t >= 0 && a.clock > a.last_pelvis_t)
            {
                double v = len(part[0].p - a.last_pelvis) / (a.clock - a.last_pelvis_t);
                if (v > 12.0 && !a.dragged_told)
                {
                    a.dragged_told = true;
                    double av = len(actor - a.last_actor) / (a.clock - a.last_pelvis_t);
                    bool sim = simulating(mesh, L"pelvis");
                    note(a, std::format("dragged {:.0f} actor {:.0f} simulating {}", v, av, sim ? 1 : 0));
                    log(STR("[RoNDelirium]   the body is carried off at {:.0f} m/s, not by Delirium{} (the actor at {:.0f} m/s, pelvis {}simulating)\n"), v,
                        a.own ? STR(", own physics") : STR(""), av, sim ? STR("") : STR("not "));
                }
            }
            a.last_pelvis = part[0].p;
            a.last_actor = actor;
            if (uint32 f = anim_flags(mesh); f != a.anim)
            {
                a.anim = f;
                note(a, "anim " + anim_text(f));
                log(STR("[RoNDelirium]   anim {} ({:.2f} s{})\n"), to_wstring(anim_text(f)), a.time, a.blend_left >= 0 ? STR(", blending back") : STR(""));
            }
            a.last_pelvis_t = a.clock;
        }
        // The recording's poses: Delirium's parts, then the game's bodies (this frame's, as the puppet read them).
        if (a.tape && a.clock >= a.next_frame)
        {
            a.next_frame = cfg.record_fps > 0 ? a.clock + 1.0 / cfg.record_fps : a.clock;
            auto& s = *a.tape;
            std::format_to(std::back_inserter(s), "frame {:.4f} {} {} {:.3f}", a.clock, nm::api.agent_has_fallen(a.agent), a.own ? 1 : 0, a.floor);
            auto put = [&](const T& t) {
                std::format_to(std::back_inserter(s), " {:.4f} {:.4f} {:.4f} {:.4f} {:.4f} {:.4f} {:.4f}", t.p.x, t.p.y, t.p.z, t.q.x, t.q.y, t.q.z, t.q.w);
            };
            for (int k = 0; k < nm::PARTS; ++k) put(delirium(k));
            for (int k = 0; k < nm::PARTS; ++k) put(part[k]);
            s += '\n';
        }
        if (a.time >= a.next_diag)
        {
            a.next_diag = a.time + 0.5;
            log(STR("[RoNDelirium]   {:.1f} s: biggest velocity change {:.2f} m/s ({}), fastest spin {:.1f} rad/s, widest gap {:.2f} m ({}), head {:.2f} m up, "
                    "fallen {}, resyncs {} (+{} limb), moved: Delirium {:.2f} m, body {:.2f} m\n"),
                a.time, top_dv, top_k >= 0 ? BONE[top_k] : STR("-"), top_w, top_gap, gap_k >= 0 ? BONE[gap_k] : STR("-"), delirium(12).p.z - delirium(3).p.z,
                nm::api.agent_has_fallen(a.agent), a.resyncs, a.limb_resyncs, std::hypot(delirium(0).p.x - a.start_at.x, delirium(0).p.y - a.start_at.y),
                std::hypot(part[0].p.x - a.start_at.x, part[0].p.y - a.start_at.y));
        }
        return true;
    }

    // Suspects and civilians on the V2 ragdoll; never the player or the SWAT.
    inline bool v2(UObject* character)
    {
        static UClass* suspect = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/ReadyOrNot.SuspectCharacter"));
        static UClass* civilian = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/ReadyOrNot.CivilianCharacter"));
        if (!(suspect && character->IsA(suspect)) && !(civilian && character->IsA(civilian))) return false;
        auto active = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("Rep_ActiveRagdollPhysAsset"));
        return active && *active && (*active)->GetName().ends_with(STR("_V2"));
    }

    // ---- the taser ----

    inline constexpr uint8 ST_TASED = 1;  // EStunType
    struct Tase { FWeakObjectPtr character; float length; double until; const CharType* how; };
    inline std::vector<Tase> tases;  // signals that someone's been tased, waiting for the next tick

    inline void tase_signal(UObject* c, float length, const CharType* how)
    {
        if (!cfg.taser || !c) return;
        for (auto& t : tases)
        {
            if (t.character.Get() != c) continue;
            t.length = std::max(t.length, length);
            return;
        }
        tases.push_back({weak(c), length, now_s + 0.5, how});
    }

    // True when this signal is finished with (taken, or not one to take).
    inline bool on_tased(Tase& t)
    {
        UObject* c = t.character.Get();
        if (!c) return true;
        if (!cfg.enabled || !v2(c)) return true;
        if (dead(c) || being_arrested(c)) return true;
        // The game's own word when it has one; a signal may come a moment before its state changes.
        if (t.length <= 0 && game_tased(c) == 0)
        {
            if (now_s <= t.until) return false;
            log(STR("[RoNDelirium] {}: a taser signal ({}), but the game doesn't have them tased\n"), c->GetName(), t.how);
            return true;
        }
        for (auto& a : agents)
        {
            if (a.character.Get() != c) continue;
            if (!a.alive) return true;
            if (!a.shock) a.shock_pending = true;
            a.shock_since = now_s;
            a.shock_for = t.length;
            a.blend_left = -1;
            if (auto mesh = a.mesh.Get(); a.own && mesh) blend(mesh, peak_blend(a));
            log(STR("[RoNDelirium] {} tased ({}), already reacting\n"), c->GetName(), t.how);
            return true;
        }
        log(STR("[RoNDelirium] {} tased ({}, {:.1f} s)\n"), c->GetName(), t.how, t.length);
        if (std::none_of(tracked.begin(), tracked.end(), [&](const FWeakObjectPtr& x) { return x.Get() == c; })) tracked.push_back(weak(c));
        if (flag(c, STR("IsInRagdoll"))) start(c, true);
        else
        {
            Agent* a = start(c, true);
            UObject* mesh = a ? a->mesh.Get() : nullptr;
            if (a && !(mesh && own_physics_on(*a, c, mesh)))
            {
                end(*a, STR("no ragdoll setup"));
                agents.pop_back();
                return true;
            }
        }
        for (auto& a : agents)
        {
            if (a.character.Get() != c) continue;
            a.hit = Hit{};
            a.left = health_left(c);
            a.tase_first = true;
            a.shock_pending = true;
            a.shock_since = now_s;
            a.shock_for = t.length;
        }
        return true;
    }

    // A character just hit: a new reaction if Delirium already drives them, their ragdoll if they're in one, else a stagger.
    inline void on_shot(UObject* c)
    {
        if (std::none_of(tracked.begin(), tracked.end(), [&](const FWeakObjectPtr& t) { return t.Get() == c; })) tracked.push_back(weak(c));
        log(STR("[RoNDelirium] hit {} in {} ({}{})\n"), c->GetName(), last_hits[c].bone, dead(c) ? STR("dead") : STR("alive"),
            v2(c) ? STR("") : STR(", not a V2 ragdoll"));
        if (!cfg.enabled || !v2(c)) return;
        // A corpse Delirium already let go of stays the game's ragdoll: taken again, it braced and moved as if alive.
        if (dead(c) && was_laid_to_rest(c) && !driving(c))
        {
            log(STR("[RoNDelirium]   a corpse: the game's ragdoll takes the bullet\n"));
            return;
        }
        std::erase_if(released, [&](const FWeakObjectPtr& r) { return r.Get() == c || r.Get() == nullptr; });
        for (auto& a : agents)
        {
            if (a.character.Get() != c) continue;
            if (auto it = last_hits.find(c); it != last_hits.end()) a.hit = it->second;
            a.new_hit = true;  // the running shot task takes it as its next hit
            // A corpse still being riddled stays Delirium's: each bullet gives it duration seconds more, up to dead_max.
            if (!a.alive) a.time = 0;
            if (!a.alive && head_kill(c)) a.limp = true;  // still dying, and now shot in the head
            if (a.alive)
            {
                a.left = health_left(c);
                a.time = 0;
                a.still = 0;  // a bullet while settling or blending back: a fresh stagger, not "settled" again at once
                a.blend_left = -1;
                if (auto mesh = a.mesh.Get(); a.own && mesh) blend(mesh, peak_blend(a));
            }
            return;
        }
        if (flag(c, STR("IsInRagdoll")))
        {
            start(c, !dead(c));
            return;
        }
        if (dead(c))
        {
            // Dying: the game's death ragdoll comes later and the watch below catches it, its death animation playing until
            // then (0.1-0.6 s: a shotgun blast to the belly folded them the canned way first). Or Delirium takes the body
            // now, on the mesh's own physics: the next frame finds them dead and it becomes the dying reaction, which the
            // game's death ragdoll takes over when it comes, as for one killed mid-stagger.
            if (!cfg.death_hit || !v2(c)) return;
            Agent* a = start(c, true);
            if (!a) return;
            a->left = 0.0f;
            a->death_hit = true;
            UObject* mesh = a->mesh.Get();
            if (!(mesh && own_physics_on(*a, c, mesh)))
            {
                end(*a, STR("no ragdoll setup"));
                agents.pop_back();
                return;
            }
            note(*a, "death_hit");
            log(STR("[RoNDelirium]   taken at the killing bullet, before the game's death ragdoll\n"));
            return;
        }
        if (!cfg.stagger) return;
        // Just back from a stagger and still healthy: this one's shrugged off (the game's own flinch), a window to fire back.
        if (auto it = recovered_at.find(c); it != recovered_at.end() && now_s < it->second)
        {
            log(STR("[RoNDelirium]   shrugged off ({:.1f} s more)\n"), it->second - now_s);
            return;
        }
        // Healthy enough to fight on: no stagger, the game's own flinch (unless it's a leg).
        float left = health_left(c);
        int part = part_of(last_hits[c].bone);
        bool leg = part >= 1 && part <= 6;
        if (left >= cfg.react_below && !(cfg.leg_always && leg))
        {
            log(STR("[RoNDelirium]   fights on ({:.0f}% health left)\n"), left * 100);
            return;
        }
        Agent* a = start(c, true);
        if (a) a->left = left;
        UObject* mesh = a ? a->mesh.Get() : nullptr;
        if (a && !(mesh && own_physics_on(*a, c, mesh)))
        {
            end(*a, STR("no ragdoll setup"));
            agents.pop_back();
        }
    }

    inline void tick_all(float dt)
    {
        now_s += dt;
        // F9 read straight from Windows each frame (UE4SS's key events never reached the mod: RoNGore has Ctrl+F9).
        static bool f9_was = false;
        bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0 && (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0;
        if (f9 && !f9_was && cfg.record) mark_asked = true;  // only when recording (nothing to mark otherwise)
        f9_was = f9;
        if (mark_asked.exchange(false)) mark_recent();
        std::erase_if(written, [](const Written& w) { return now_s - w.at > 8.0; });
        handover_tick(dt);
        corpse_capsule_tick(dt);
        census_tick();
        std::erase_if(physical_animation_ramps, [dt](Ramp& r) {
            UObject* c = r.character.Get();
            if (!c || driving(c)) return true;
            if (killed(c)) return game_physical_animation(c, 0.0f), true;
            r.t += dt;
            game_physical_animation(c, float(std::min(r.t / 1.5, 1.0)));
            return r.t >= 1.5;
        });
        std::erase_if(capsules_later, [](const CapsuleLater& l) {
            UObject* c = l.character.Get();
            if (!c || dead(c)) return true;
            if (now_s < l.not_before || flag(c, STR("IsInRagdoll"))) return false;
            if (UObject* capsule = capsule_of(c))
            {
                ufn::Call(ufn::find(capsule, STR("SetCollisionResponseToChannel"))).set<uint8>(STR("Channel"), PHYSICS_BODY)
                    .set<uint8>(STR("NewResponse"), l.response).invoke(capsule);
            }
            return true;
        });
        std::erase_if(recovered_at, [](const auto& r) { return r.second < now_s; });
        for (auto& s : shot)
        {
            if (auto c = s.Get()) on_shot(c);
        }
        shot.clear();
        frame_hits.clear();
        std::erase_if(tases, [](Tase& t) { return on_tased(t); });
        for (auto& p : pending)
        {
            if (auto c = p.Get(); c && v2(c) && !was_released(c)) start(c, !dead(c));
        }
        pending.clear();
        std::erase_if(tracked, [](const FWeakObjectPtr& t) { return t.Get() == nullptr; });
        std::erase_if(laid_to_rest, [](const FWeakObjectPtr& t) { return t.Get() == nullptr; });
        if (cfg.enabled)
        {
            for (auto& t : tracked)
            {
                UObject* c = t.Get();
                if (!driving(c) && !was_released(c) && v2(c) && flag(c, STR("IsInRagdoll"))) start(c, !dead(c));
            }
            // Tased, and no signal came (none of the game's taser calls has been seen to pass a hook): the game's state says
            // so, four times a second. For everyone, not just those already hit: most are tased unhurt.
            static double next_look = 0;
            if (cfg.taser && now_s >= next_look)
            {
                next_look = now_s + 0.25;
                {
                    std::lock_guard guard(born_lock);
                    for (const Born& b : born)
                    {
                        // Still that object, and whole? (Collected since, its slot's empty or another's.)
                        FUObjectItem* item = FUObjectArray::IndexToObject(b.index);
                        if (item && item->GetUObject() == b.object) people.push_back(weak(b.object));
                    }
                    born.clear();
                }
                std::erase_if(people, [](const FWeakObjectPtr& t) { return t.Get() == nullptr; });
                auto look = [](UObject* c) {
                    if (!c || !v2(c) || dead(c) || game_tased(c) != 1) return;
                    if (std::any_of(agents.begin(), agents.end(), [&](const Agent& a) { return a.character.Get() == c && (a.shock || a.shock_pending); })) return;
                    tase_signal(c, 0.0f, STR("state"));
                };
                for (auto& t : people) look(t.Get());
                for (auto& t : tracked) look(t.Get());  // (made before the mod could see it)
            }
        }
        std::erase_if(agents, [&](Agent& a) { return !tick(a, dt); });
        tail_tick(dt);
    }

    // ---- hooks ----

    inline UFunction* function(const wchar_t* path)
    {
        return UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path);
    }

    // A parameter of the hooked call, by name.
    template <typename V>
    V param(UnrealScriptFunctionCallableContext& ctx, UFunction* fn, const TCHAR* p)
    {
        V out{};
        for (FProperty* prop : fn->ForEachProperty())
        {
            if (prop->GetName() == p) std::memcpy(&out, ctx.TheStack.Locals() + prop->GetOffset_Internal(), sizeof(V));
        }
        return out;
    }

    // A field of a struct parameter of the hooked call (e.g. the HitResult's ImpactPoint), by names.
    template <typename V>
    V struct_param(UnrealScriptFunctionCallableContext& ctx, UFunction* fn, const TCHAR* p, const TCHAR* field)
    {
        V out{};
        for (FProperty* prop : fn->ForEachProperty())
        {
            if (prop->GetName() != p) continue;
            auto sp = CastField<FStructProperty>(prop);
            if (!sp) break;
            UScriptStruct* s = sp->GetStruct();
            for (FProperty* f : s->ForEachPropertyInChain())
            {
                if (f->GetName() == field)
                {
                    std::memcpy(&out, ctx.TheStack.Locals() + prop->GetOffset_Internal() + f->GetOffset_Internal(), sizeof(V));
                }
            }
        }
        return out;
    }

    inline void install()
    {
        auto on_ragdoll = [](UnrealScriptFunctionCallableContext& ctx, void*) { pending.push_back(weak(ctx.Context)); };
        auto noop = [](UnrealScriptFunctionCallableContext&, void*) {};
        int hooks = 0;
        for (auto path : {STR("/Script/ReadyOrNot.ReadyOrNotCharacter:Multicast_EnableRagdoll"), STR("/Script/ReadyOrNot.ReadyOrNotCharacter:EnableRagdoll"),
                          STR("/Script/ReadyOrNot.ReadyOrNotCharacter:OnRagdollStart")})
        {
            if (auto fn = function(path)) UObjectGlobals::RegisterHook(fn, noop, on_ragdoll, nullptr), ++hooks;
            else Output::send<LogLevel::Warning>(STR("[RoNDelirium] no {}\n"), path);
        }
        // Every bullet hit (the same hook RoNGore uses): where, which way, which bone.
        if (auto blood = function(STR("/Script/ReadyOrNot.ReadyOrNotCharacter:Multicast_SpawnBloodEffects")))
        {
            UObjectGlobals::RegisterHook(blood, [blood](UnrealScriptFunctionCallableContext& ctx, void*) {
                Hit h;
                h.bone = struct_param<FName>(ctx, blood, STR("Hit"), STR("BoneName")).ToString();
                h.at = to_nm_point(struct_param<DVec>(ctx, blood, STR("Hit"), STR("ImpactPoint")));
                h.from = to_nm_point(struct_param<DVec>(ctx, blood, STR("Hit"), STR("TraceStart")));
                h.dir = to_nm_point(struct_param<DVec>(ctx, blood, STR("Hit"), STR("TraceEnd"))) - h.from;
                h.weapon = weapon_of(param<UObject*>(ctx, blood, STR("HitInstigator")));
                last_hits[ctx.Context] = h;
                auto& hits = frame_hits[ctx.Context];
                hits.push_back(h);
                if (hits.size() > 1)
                {
                    DVec mid{};
                    for (const Hit& x : hits) mid = mid + x.at;
                    mid = mid * (1.0 / double(hits.size()));
                    const Hit* best = &hits[0];
                    for (const Hit& x : hits) if (len(x.at - mid) < len(best->at - mid)) best = &x;
                    Hit all = *best;
                    all.count = std::min(int(hits.size()), 9);
                    for (const Hit& x : hits)
                        if (all.pellets.size() < 9) all.pellets.push_back(Pellet{x.bone, x.at, x.dir});
                    last_hits[ctx.Context] = all;
                }
                note_head_hit(ctx.Context, h.bone);
                if (std::none_of(shot.begin(), shot.end(), [&](const FWeakObjectPtr& s) { return s.Get() == ctx.Context; })) shot.push_back(weak(ctx.Context));
            }, noop, nullptr);
            ++hooks;
        }
        else Output::send<LogLevel::Warning>(STR("[RoNDelirium] no Multicast_SpawnBloodEffects\n"));
        // Every bullet that does damage, blood or not: one stopped by body armour spawns none (the blood hook never saw
        // it, and an armoured suspect shot in the chest stood there in the game's pain animation).
        if (auto point = function(STR("/Script/Engine.Actor:ReceivePointDamage")))
        {
            UObjectGlobals::RegisterHook(point, [point](UnrealScriptFunctionCallableContext& ctx, void*) {
                UObject* c = ctx.Context;
                if (!c || !v2(c)) return;  // doors, props, the player and the SWAT
                // The taser's probes aren't a bullet.
                if (UObject* type = param<UObject*>(ctx, point, STR("DamageType")); cfg.taser && type && type->GetName().find(STR("Taser")) != StringType::npos)
                {
                    log(STR("[RoNDelirium] {}: taser damage\n"), c->GetName());
                    tase_signal(c, 0.0f, STR("damage"));
                    return;
                }
                if (std::any_of(shot.begin(), shot.end(), [&](const FWeakObjectPtr& s) { return s.Get() == c; })) return;  // the blood hook has it
                Hit h;
                h.bone = param<FName>(ctx, point, STR("BoneName")).ToString();
                h.at = to_nm_point(param<DVec>(ctx, point, STR("HitLocation")));
                h.dir = to_nm_point(param<DVec>(ctx, point, STR("ShotFromDirection")));
                h.from = to_nm_point(struct_param<DVec>(ctx, point, STR("HitInfo"), STR("TraceStart")));
                h.weapon = weapon_of(param<UObject*>(ctx, point, STR("InstigatedBy")));
                // A bloodless hit in the torso is one the armour stopped: the game's own pain animation, no stagger.
                // Armour doesn't cover the arms, legs, neck or head: those always stagger, blood or not.
                int part = part_of(h.bone);
                if (h.bone.empty() || h.bone == STR("None") || part == 0 || (part >= 7 && part <= 10) || part == 13 || part == 17)
                {
                    log(STR("[RoNDelirium] {} in {}: stopped by the armour, no stagger\n"), c->GetName(), h.bone);
                    return;
                }
                log(STR("[RoNDelirium] point damage to {} in {} ({:.0f})\n"), c->GetName(), h.bone, param<float>(ctx, point, STR("Damage")));
                last_hits[c] = h;
                note_head_hit(c, h.bone);
                shot.push_back(weak(c));
            }, noop, nullptr);
            ++hooks;
        }
        // The taser, by every way the game may tell: which of these fire shows in the log. Calls made inside the game's own
        // code don't pass through a hook, hence several, and the state read in tick_all for those already seen.
        if (auto stun = function(STR("/Script/ReadyOrNot.ReadyOrNotCharacter:StartStun")))
        {
            UObjectGlobals::RegisterHook(stun, [stun](UnrealScriptFunctionCallableContext& ctx, void*) {
                log(STR("[RoNDelirium] {}: StartStun, type {}\n"), ctx.Context ? ctx.Context->GetName() : StringType(STR("?")), param<uint8>(ctx, stun, STR("StunType")));
                if (param<uint8>(ctx, stun, STR("StunType")) == ST_TASED) tase_signal(ctx.Context, param<float>(ctx, stun, STR("Duration")), STR("StartStun"));
            }, noop, nullptr);
            ++hooks;
        }
        else log(STR("[RoNDelirium] no StartStun\n"));
        for (auto path : {STR("/Script/ReadyOrNot.ReadyOrNotCharacter:StartBeingTasered"), STR("/Script/ReadyOrNot.CyberneticCharacter:StartBeingTasered"),
                          STR("/Script/ReadyOrNot.ReadyOrNotCharacter:Multicast_PlayTaserHitEffect"), STR("/Script/ReadyOrNot.CyberneticCharacter:Multicast_PlayTaserHitEffect"),
                          STR("/Script/ReadyOrNot.ReadyOrNotCharacter:ReactToTaser"), STR("/Script/ReadyOrNot.CyberneticCharacter:ReactToTaser"),
                          STR("/Script/ReadyOrNot.Taser:Multicast_PlayTaserHitEffect"), STR("/Script/ReadyOrNot.Taser:OnTaserStunDelivered"),
                          STR("/Script/ReadyOrNot.Taser:Server_DeliverStunToAttachedTarget")})
        {
            auto fn = function(path);
            if (!fn) continue;
            log(STR("[RoNDelirium] taser hook: {}\n"), path);
            UObjectGlobals::RegisterHook(fn, noop, [path](UnrealScriptFunctionCallableContext& ctx, void*) {
                UObject* c = ctx.Context;
                if (!c) return;
                // On the weapon: the one it has tased.
                if (auto target = c->GetValuePtrByPropertyNameInChain<UObject*>(STR("StunnedCharacter")))
                {
                    if (!*target)
                    {
                        log(STR("[RoNDelirium] {}: no one tased yet\n"), path);
                        return;
                    }
                    c = *target;
                }
                tase_signal(c, 0.0f, path);
            }, nullptr);
            ++hooks;
        }
        // Every suspect and civilian as they're made, for the taser's watch in tick_all. (Called for every object in the
        // game and from loading threads too, hence the classes found here and nothing else looked up.)
        static UClass* suspect_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/ReadyOrNot.SuspectCharacter"));
        static UClass* civilian_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/ReadyOrNot.CivilianCharacter"));
        if (suspect_class || civilian_class)
        {
            // (Read-only: the older form without data "returned" the object, and UE4SS logged an error for each one made.)
            Hook::RegisterStaticConstructObjectPostCallback([](const auto& data, const FStaticConstructObjectParameters&) {
                UObject* made = data.GetCurrentResolvedReturnValue();
                if (made && ((suspect_class && made->IsA(suspect_class)) || (civilian_class && made->IsA(civilian_class))) && !made->HasAnyFlags(RF_ClassDefaultObject))
                {
                    std::lock_guard guard(born_lock);
                    born.push_back({made, made->GetInternalIndex()});
                }
            }, {false, true, STR("RoNDelirium"), STR("People")});
        }
        else log(STR("[RoNDelirium] no suspect or civilian class: the taser's only seen on those already hit\n"));
        log(STR("[RoNDelirium] {} hooks in\n"), hooks);
        Hook::RegisterEngineTickPostCallback([](auto&, UEngine*, float dt, bool) { tick_all(dt); }, {false, false, STR("RoNDelirium"), STR("Tick")});
    }
}
