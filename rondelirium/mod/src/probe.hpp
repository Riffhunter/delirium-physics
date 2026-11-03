// Probe (F7): what Ready or Not offers around ragdolls, logged to UE4SS.log.
// Functions of the ragdoll/character classes whose names match the keywords, and every live character's physics assets.
#pragma once

#include <algorithm>
#include <cwctype>
#include <map>
#include <string>
#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include "ufn.hpp"

namespace probe
{
    using namespace RC;
    using namespace RC::Unreal;

    inline const wchar_t* KEYWORDS[] = {L"ragdoll", L"phys", L"death", L"die", L"kill", L"incapac", L"stun", L"stagger",
                                        L"fall", L"damage", L"hit", L"impulse", L"blend", L"wound", L"down", L"surrender"};

    // Firing and what stops it (for suspects firing back while they stagger).
    inline const wchar_t* FIRE_KEYWORDS[] = {L"fire", L"shoot", L"trigger", L"aim", L"stun", L"flinch", L"react", L"suppress", L"attack",
                                             L"canuse", L"busy", L"montage"};

    template <size_t N>
    inline bool interesting(StringType name, const wchar_t* const (&keywords)[N] = KEYWORDS)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return std::any_of(std::begin(keywords), std::end(keywords), [&](const wchar_t* k) { return name.find(k) != StringType::npos; });
    }

    template <size_t N = std::size(KEYWORDS)>
    inline void functions(const wchar_t* path, const wchar_t* const (&keywords)[N] = KEYWORDS)
    {
        auto cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
        if (!cls)
        {
            Output::send<LogLevel::Warning>(STR("[RoNDelirium] probe: {} not found\n"), path);
            return;
        }
        Output::send(STR("[RoNDelirium] probe: functions of {}\n"), path);
        for (UFunction* fn : cls->ForEachFunction())
        {
            auto name = fn->GetName();
            if (!interesting(name, keywords)) continue;
            StringType params;
            for (FProperty* p : fn->ForEachProperty())
            {
                if (!params.empty()) params += STR(", ");
                params += p->GetClass().GetName() + STR(" ") + p->GetName();
            }
            Output::send(STR("[RoNDelirium]   {}({})\n"), name, params);
        }
    }

    inline StringType object_name(UObject** slot)
    {
        return slot && *slot ? (*slot)->GetFullName() : StringType(STR("null"));
    }

    inline void characters()
    {
        auto base = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/ReadyOrNot.CyberneticCharacter"));
        if (!base) return;
        int count = 0;
        UObjectGlobals::ForEachUObject([&](UObject* obj, int32, int32) {
            if (!obj || !obj->IsA(base) || obj->GetName().starts_with(STR("Default__"))) return LoopAction::Continue;
            ++count;
            Output::send(STR("[RoNDelirium] probe: {} ({})\n"), obj->GetName(), obj->GetClassPrivate()->GetName());
            Output::send(STR("[RoNDelirium]   default ragdoll {}\n"), object_name(obj->GetValuePtrByPropertyNameInChain<UObject*>(STR("DefaultRagdollPhysAsset"))));
            Output::send(STR("[RoNDelirium]   active ragdoll  {}\n"), object_name(obj->GetValuePtrByPropertyNameInChain<UObject*>(STR("Rep_ActiveRagdollPhysAsset"))));
            Output::send(STR("[RoNDelirium]   ragdoll comp    {}\n"), object_name(obj->GetValuePtrByPropertyNameInChain<UObject*>(STR("RagdollComponent"))));
            return LoopAction::Continue;
        });
        Output::send(STR("[RoNDelirium] probe: {} characters\n"), count);
    }

    // The bodies of PHYS_RoN_Player_Master_Shared_V2 (suspects and civilians).
    inline const wchar_t* BONES[] = {L"root", L"pelvis", L"spine_1", L"spine_2", L"spine_3", L"neck_1", L"head",
                                     L"thigh_LE", L"calf_LE", L"foot_LE", L"thigh_RI", L"calf_RI", L"foot_RI",
                                     L"clavicle_LE", L"upperarm_LE", L"lowerarm_LE", L"hand_LE",
                                     L"clavicle_RI", L"upperarm_RI", L"lowerarm_RI", L"hand_RI"};

    // World transforms of one character's bones, to line Delirium's parts up with them.
    inline void bones(UObject* character)
    {
        auto mesh_slot = character->GetValuePtrByPropertyNameInChain<UObject*>(STR("Mesh"));
        if (!mesh_slot || !*mesh_slot) return;
        UObject* mesh = *mesh_slot;
        auto fn = ufn::find(mesh, STR("GetSocketTransform"));
        if (!fn)
        {
            Output::send<LogLevel::Warning>(STR("[RoNDelirium] probe: no GetSocketTransform\n"));
            return;
        }
        Output::send(STR("[RoNDelirium] bones of {} (params {} bytes)\n"), character->GetName(), fn->GetStructureSize());
        for (auto bone : BONES)
        {
            ufn::Call call(fn);
            call.name(STR("InSocketName"), bone).set<uint8>(STR("TransformSpace"), 0).invoke(mesh);
            auto t = call.get<ufn::DTransform>(STR("ReturnValue"));
            Output::send(STR("[RoNDelirium] bone {} pos {:.3f} {:.3f} {:.3f} rot {:.5f} {:.5f} {:.5f} {:.5f}\n"), bone, t.pos.x, t.pos.y, t.pos.z,
                         t.rot.x, t.rot.y, t.rot.z, t.rot.w);
        }
    }

    inline void first_suspect_bones()
    {
        auto suspect = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/ReadyOrNot.SuspectCharacter"));
        if (!suspect) return;
        UObjectGlobals::ForEachUObject([&](UObject* obj, int32, int32) {
            if (!obj || !obj->IsA(suspect) || obj->GetName().starts_with(STR("Default__"))) return LoopAction::Continue;
            bones(obj);
            return LoopAction::Break;
        });
    }

    // Logged once at start: how a suspect fires, to have them fire while they stagger.
    inline void fire_functions()
    {
        for (auto path : {STR("/Script/ReadyOrNot.CyberneticCharacter"), STR("/Script/ReadyOrNot.ReadyOrNotCharacter"), STR("/Script/ReadyOrNot.BaseWeapon"),
                          STR("/Script/ReadyOrNot.BaseMagazineWeapon"), STR("/Script/ReadyOrNot.CyberneticController")})
        {
            functions(path, FIRE_KEYWORDS);
        }
    }

    // What runs when a character is hit (an armoured one's chest above all, which spawns no blood): every function on
    // the character and armour classes whose name says hit, damage, armour or pain, logged when it's called.
    inline const wchar_t* HIT_KEYWORDS[] = {L"armo", L"pain", L"damage", L"hit", L"impact", L"bullet", L"penetrat", L"flinch", L"react", L"wound"};

    inline void trace_hits()
    {
        static std::map<std::pair<UFunction*, UObject*>, ULONGLONG> last;  // one line per function and object every 0.3 s
        int hooked = 0;
        for (auto path : {STR("/Script/Engine.Actor"), STR("/Script/ReadyOrNot.ReadyOrNotCharacter"), STR("/Script/ReadyOrNot.CyberneticCharacter"),
                          STR("/Script/ReadyOrNot.SuspectCharacter"), STR("/Script/ReadyOrNot.BaseArmour"), STR("/Script/ReadyOrNot.BaseBodyArmor"),
                          STR("/Script/ReadyOrNot.ArmourResourceComponent"), STR("/Script/ReadyOrNot.CharacterHealthComponent"),
                          STR("/Script/ReadyOrNot.HealthComponent"), STR("/Script/ReadyOrNot.ResourceComponent")})
        {
            auto cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
            if (!cls)
            {
                Output::send(STR("[RoNDelirium] trace: no {}\n"), path);
                continue;
            }
            for (UFunction* fn : cls->ForEachFunction())
            {
                auto name = fn->GetName();
                if (!interesting(name, HIT_KEYWORDS) || name.find(STR("__DelegateSignature")) != StringType::npos) continue;
                // Getters run every frame and say nothing.
                bool getter = false;
                for (auto pre : {STR("Get"), STR("Is"), STR("Has"), STR("Can"), STR("Should"), STR("Was")})
                {
                    if (name.starts_with(pre)) getter = true;
                }
                if (getter) continue;
                UObjectGlobals::RegisterHook(fn, [](UnrealScriptFunctionCallableContext& ctx, void* data) {
                    auto f = static_cast<UFunction*>(data);
                    auto now = GetTickCount64();
                    auto& t = last[{f, ctx.Context}];
                    if (now - t < 300) return;
                    t = now;
                    Output::send(STR("[RoNDelirium] trace: {} on {}\n"), f->GetName(), ctx.Context ? ctx.Context->GetName() : StringType(STR("?")));
                }, [](UnrealScriptFunctionCallableContext&, void*) {}, fn);
                ++hooked;
            }
        }
        Output::send(STR("[RoNDelirium] trace: {} hit functions watched\n"), hooked);
    }

    inline void run()
    {
        functions(STR("/Script/ReadyOrNot.ReadyOrNotCharacter"));
        characters();
        first_suspect_bones();
    }
}
