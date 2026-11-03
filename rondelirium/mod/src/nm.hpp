// delirium_physics.dll, loaded at run time from the mod's dlls folder (the C API in delirium_physics.h).
#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iterator>
#include <map>
#include <string>

namespace nm
{
    constexpr int PARTS = 21;

    struct Api
    {
        int32_t (*last_error)(char*, int32_t);
        int32_t (*unlock)(const char*);
        int32_t (*init)(const char*);
        void* (*build_load)(const char*);
        void* (*build_fit)(const void*, const float*);
        int32_t (*build_part_frame)(const void*, int32_t, float*, float*);
        int32_t (*build_part_info)(const void*, int32_t, float*, float*, float*, int32_t*);
        void* (*world_create)();
        void (*world_destroy)(void*);
        int32_t (*world_add_floor)(void*, float, float, float, float);
        int32_t (*world_set_boxes)(void*, const float*, int32_t);
        int32_t (*world_add_mesh)(void*, const float*, int32_t, const uint32_t*, int32_t, const uint8_t*);
        int32_t (*set_shot_force_scale)(float);
        void* (*agent_create)(const void*, const float*, float, float);
        void (*agent_destroy)(void*);
        int32_t (*agent_set_part_states)(void*, const float*, int32_t);
        int32_t (*agent_step)(void*, const void*, float);
        int32_t (*agent_get_part_velocity)(const void*, int32_t, float*, float*);
        int32_t (*agent_get_part_transform)(const void*, int32_t, float*, float*);
        int32_t (*agent_get_joint)(const void*, int32_t, int32_t*, float*, float*, float*, float*);
        int32_t (*agent_apply_push)(void*, const float*, const float*);
        int32_t (*agent_has_fallen)(const void*);
        int32_t (*agent_is_settled)(const void*);
        int32_t (*agent_task_shot)(void*, const void*, const char*, int32_t, const float*, const float*, float);
        int32_t (*load_weapons)(const char*);
        int32_t (*agent_shot)(void*, const void*, const char*, int32_t, const float*, const float*, float, const float*);
        int32_t (*agent_shot_update)(void*, const void*, float, const float*);
        int32_t (*msg_begin)(void*, const char*);
        int32_t (*msg_set_bool)(void*, const char*, int32_t);
        int32_t (*msg_set_float)(void*, const char*, float);
        int32_t (*msg_set_int)(void*, const char*, int32_t);
        int32_t (*msg_send)(void*, const void*);
        int32_t (*unlock_own)();
        int32_t (*init_own_character)();
        void* (*build_own)();
        int32_t (*agent_set_seed)(void*, uint32_t);
        void* (*build_with_joints_of)(const void*, const void*, uint32_t);

        // Every entry point, or false (and nothing usable) if the DLL or one of them is missing.
        bool load(const std::filesystem::path& dll)
        {
            auto lib = LoadLibraryW(dll.c_str());
            if (!lib) return false;
            bool ok = true;
            auto get = [&](auto& fn, const char* name) {
                fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(lib, name));
                ok = ok && fn;
            };
            get(last_error, "nm_last_error");
            get(unlock, "nm_unlock");
            get(init, "nm_init");
            get(build_load, "nm_build_load");
            get(build_fit, "nm_build_fit");
            get(build_part_frame, "nm_build_part_frame");
            get(build_part_info, "nm_build_part_info");
            get(world_create, "nm_world_create");
            get(world_destroy, "nm_world_destroy");
            get(world_add_floor, "nm_world_add_floor");
            get(world_set_boxes, "nm_world_set_boxes");
            get(world_add_mesh, "nm_world_add_mesh");
            get(set_shot_force_scale, "nm_set_shot_force_scale");
            get(agent_create, "nm_agent_create");
            get(agent_destroy, "nm_agent_destroy");
            get(agent_set_part_states, "nm_agent_set_part_states");
            get(agent_step, "nm_agent_step");
            get(agent_get_part_velocity, "nm_agent_get_part_velocity");
            get(agent_get_part_transform, "nm_agent_get_part_transform");
            get(agent_get_joint, "nm_agent_get_joint");
            get(agent_apply_push, "nm_agent_apply_push");
            get(agent_has_fallen, "nm_agent_has_fallen");
            get(agent_is_settled, "nm_agent_is_settled");
            get(agent_task_shot, "nm_agent_task_shot");
            get(load_weapons, "nm_load_weapons");
            get(agent_shot, "nm_agent_shot");
            get(agent_shot_update, "nm_agent_shot_update");
            get(msg_begin, "nm_msg_begin");
            get(msg_set_bool, "nm_msg_set_bool");
            get(msg_set_float, "nm_msg_set_float");
            get(msg_set_int, "nm_msg_set_int");
            get(msg_send, "nm_msg_send");
        get(unlock_own, "nm_unlock_own");
        get(init_own_character, "nm_init_own_character");
        get(build_own, "nm_build_own");
        get(agent_set_seed, "nm_agent_set_seed");
        get(build_with_joints_of, "nm_build_with_joints_of");
            return ok;
        }

        std::string error() const
        {
            char buf[512]{};
            last_error(buf, sizeof(buf));
            return buf;
        }
    };

    inline Api api{};

    // The recorder: every call on an agent or its world being recorded, written to its tape as a line of text (floats
    // in their shortest exact form), so tools/replay.py can run the same calls on the same DLL offline.
    namespace tape
    {
        inline Api raw{};
        inline std::map<const void*, std::string*> on;  // the agents and worlds being recorded, and their tapes
        inline float force = 1.0f;                     // the shot force scale (global in the DLL): set before each shot

        inline std::string* of(const void* h)
        {
            auto it = on.find(h);
            return it == on.end() ? nullptr : it->second;
        }
        inline void put(std::string& s, const float* v, int n)
        {
            for (int i = 0; i < n; ++i) std::format_to(std::back_inserter(s), " {}", v[i]);
        }

        inline int32_t world_add_floor(void* w, float x, float y, float z, float size)
        {
            if (auto s = of(w)) std::format_to(std::back_inserter(*s), "floor {} {} {} {}\n", x, y, z, size);
            return raw.world_add_floor(w, x, y, z, size);
        }
        inline int32_t world_set_boxes(void* w, const float* b, int32_t n)
        {
            if (auto s = of(w)) { std::format_to(std::back_inserter(*s), "boxes {}", n); put(*s, b, n * 13); *s += '\n'; }
            return raw.world_set_boxes(w, b, n);
        }
        inline int32_t world_add_mesh(void* w, const float* v, int32_t nv, const uint32_t* t, int32_t nt, const uint8_t* m)
        {
            if (auto s = of(w))
            {
                std::format_to(std::back_inserter(*s), "mesh {} {} {}", nv, nt, m ? 1 : 0);
                put(*s, v, nv * 3);
                for (int i = 0; i < nt * 3; ++i) std::format_to(std::back_inserter(*s), " {}", t[i]);
                if (m) for (int i = 0; i < nt; ++i) std::format_to(std::back_inserter(*s), " {}", m[i]);
                *s += '\n';
            }
            return raw.world_add_mesh(w, v, nv, t, nt, m);
        }
        inline int32_t set_shot_force_scale(float f)
        {
            force = f;
            return raw.set_shot_force_scale(f);
        }
        inline int32_t agent_set_part_states(void* a, const float* st, int32_t n)
        {
            if (auto s = of(a)) { std::format_to(std::back_inserter(*s), "states {}", n); put(*s, st, n * 13); *s += '\n'; }
            return raw.agent_set_part_states(a, st, n);
        }
        inline int32_t agent_step(void* a, const void* w, float dt)
        {
            if (auto s = of(a)) std::format_to(std::back_inserter(*s), "step {}\n", dt);
            return raw.agent_step(a, w, dt);
        }
        inline int32_t agent_apply_push(void* a, const float* at, const float* impulse)
        {
            if (auto s = of(a)) { *s += "push"; put(*s, at, 3); put(*s, impulse, 3); *s += '\n'; }
            return raw.agent_apply_push(a, at, impulse);
        }
        inline int32_t agent_shot(void* a, const void* w, const char* weapon, int32_t part, const float* at, const float* dir, float health,
                                  const float* shooter)
        {
            if (auto s = of(a))
            {
                std::format_to(std::back_inserter(*s), "shot {} {} {}", force, weapon ? weapon : "-", part);
                put(*s, at, 3);
                put(*s, dir, 3);
                std::format_to(std::back_inserter(*s), " {} {}", health, shooter ? 1 : 0);
                if (shooter) put(*s, shooter, 3);
                *s += '\n';
            }
            return raw.agent_shot(a, w, weapon, part, at, dir, health, shooter);
        }
        inline int32_t agent_shot_update(void* a, const void* w, float health, const float* shooter)
        {
            if (auto s = of(a))
            {
                std::format_to(std::back_inserter(*s), "upd {} {}", health, shooter ? 1 : 0);
                if (shooter) put(*s, shooter, 3);
                *s += '\n';
            }
            return raw.agent_shot_update(a, w, health, shooter);
        }
        inline int32_t msg_begin(void* a, const char* name)
        {
            if (auto s = of(a)) std::format_to(std::back_inserter(*s), "msg {}\n", name);
            return raw.msg_begin(a, name);
        }
        inline int32_t msg_set_bool(void* a, const char* k, int32_t v)
        {
            if (auto s = of(a)) std::format_to(std::back_inserter(*s), "mb {} {}\n", k, v);
            return raw.msg_set_bool(a, k, v);
        }
        inline int32_t msg_set_float(void* a, const char* k, float v)
        {
            if (auto s = of(a)) std::format_to(std::back_inserter(*s), "mf {} {}\n", k, v);
            return raw.msg_set_float(a, k, v);
        }
        inline int32_t msg_set_int(void* a, const char* k, int32_t v)
        {
            if (auto s = of(a)) std::format_to(std::back_inserter(*s), "mi {} {}\n", k, v);
            return raw.msg_set_int(a, k, v);
        }
        inline int32_t msg_send(void* a, const void* w)
        {
            if (auto s = of(a)) *s += "send\n";
            return raw.msg_send(a, w);
        }

        // The recorder between the mod and the DLL (after api.load).
        inline void install()
        {
            raw = api;
            api.world_add_floor = world_add_floor;
            api.world_set_boxes = world_set_boxes;
            api.world_add_mesh = world_add_mesh;
            api.set_shot_force_scale = set_shot_force_scale;
            api.agent_set_part_states = agent_set_part_states;
            api.agent_step = agent_step;
            api.agent_apply_push = agent_apply_push;
            api.agent_shot = agent_shot;
            api.agent_shot_update = agent_shot_update;
            api.msg_begin = msg_begin;
            api.msg_set_bool = msg_set_bool;
            api.msg_set_float = msg_set_float;
            api.msg_set_int = msg_set_int;
            api.msg_send = msg_send;
        }
    }
}
