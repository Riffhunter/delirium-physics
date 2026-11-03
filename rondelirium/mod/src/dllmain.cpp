// RoNDelirium: Delirium Physics (GTA IV-style shot reactions) driving Ready or Not's ragdolls.
// Mods/RoNDelirium: dlls/{main.dll, delirium_physics.dll} (our fork of Delirium, with its own numbers and character:
// no other game needed), delirium.ini (optional settings), recordings/ (record = 1: one file per activation; F9 marks
// the latest). Built with RONDELIRIUM_DEV (cmake -DRONDELIRIUM_DEV=ON) for the developer's tools: F7's probe and the
// comparison runs against their own copy of GTA IV (dev_gta.txt). Never in a release.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Mod/CppUserModBase.hpp>

#include "rondelirium.hpp"
#include "nm.hpp"
#ifdef RONDELIRIUM_DEV
#include "probe.hpp"
#endif

using namespace RC;

namespace
{
    // Folder this DLL was loaded from (Mods/RoNDelirium/dlls).
    std::filesystem::path own_dir()
    {
        HMODULE self{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&own_dir), &self);
        wchar_t buf[MAX_PATH]{};
        GetModuleFileNameW(self, buf, MAX_PATH);
        return std::filesystem::path(buf).parent_path();
    }

    // Delirium (our fork) with its own numbers and our own character; false (logged) if the DLL's missing or old.
    bool load_delirium()
    {
        auto dlls = own_dir();
        auto mod = dlls.parent_path();
        if (!nm::api.load(dlls / L"delirium_physics.dll"))
        {
            Output::send<LogLevel::Error>(STR("[RoNDelirium] delirium_physics.dll missing or not RoNDelirium's own build in {}\n"), dlls.wstring());
            return false;
        }
#ifdef RONDELIRIUM_DEV
        // The developer's comparison runs only (never in a release): dev_gta.txt beside the dlls folder, its first line the
        // GTAIV folder of their own copy, its second the folder with fred.xrt and build.txt. GTA IV's numbers and
        // character, read from that install as the original Delirium does, for recordings to compare ours against.
        std::wifstream dev(mod / L"dev_gta.txt");
        std::wstring gta, character;
        if (dev && std::getline(dev, gta) && std::getline(dev, character))
        {
            auto trim = [](std::wstring& s) { s.erase(s.find_last_not_of(L" \t\r") + 1); };
            trim(gta), trim(character);
            std::filesystem::path chars(character);
            if (nm::api.unlock(std::filesystem::path(gta).string().c_str()) != 0 || nm::api.init((chars / L"fred.xrt").string().c_str()) < 0
                || !(rondelirium::base_build = nm::api.build_load((chars / L"build.txt").string().c_str())))
            {
                Output::send<LogLevel::Error>(STR("[RoNDelirium] dev_gta.txt: {}\n"), to_wstring(nm::api.error()));
                return false;
            }
            rondelirium::load_settings(mod / "delirium.ini");
            nm::tape::install();
            rondelirium::rec_dir = mod / "recordings";
            rondelirium::engine = "gta";
            Output::send(STR("[RoNDelirium] Delirium ready: GTA IV's numbers and character (developer comparison, dev_gta.txt)\n"));
            return true;
        }
#endif
        int numbers = nm::api.unlock_own();
        if (numbers <= 0 || nm::api.init_own_character() < 0 || !(rondelirium::base_build = nm::api.build_own()))
        {
            Output::send<LogLevel::Error>(STR("[RoNDelirium] Delirium: {}\n"), to_wstring(nm::api.error()));
            return false;
        }
        rondelirium::load_settings(mod / "delirium.ini");
        nm::tape::install();
        rondelirium::rec_dir = mod / "recordings";
        Output::send(STR("[RoNDelirium] Delirium ready: its own numbers ({}) and character ({})\n"), numbers,
                     rondelirium::cfg.enabled ? STR("on, F8 toggles") : STR("off in delirium.ini, F8 toggles"));
        return true;
    }
}

class RoNDelirium : public CppUserModBase
{
  public:
    RoNDelirium() : CppUserModBase()
    {
        ModName = STR("RoNDelirium");
        ModVersion = STR("1.0");
        ModDescription = STR("Delirium Physics for Ready or Not's ragdolls");
        ModAuthors = STR("Riffhunter");

        ready = load_delirium();
#ifdef RONDELIRIUM_DEV
        register_keydown_event(Input::Key::F7, [] { probe::run(); });
#endif
        register_keydown_event(Input::Key::F8, [] {
            rondelirium::cfg.enabled = !rondelirium::cfg.enabled;
            Output::send(STR("[RoNDelirium] Delirium {}\n"), rondelirium::cfg.enabled ? STR("on") : STR("off"));
        });
    }

    ~RoNDelirium() override = default;

    auto on_unreal_init() -> void override
    {
        if (ready) rondelirium::install();
    }

  private:
    bool ready = false;
};

#define RONDELIRIUM_API __declspec(dllexport)
extern "C"
{
    RONDELIRIUM_API CppUserModBase* start_mod()
    {
        return new RoNDelirium();
    }

    RONDELIRIUM_API void uninstall_mod(CppUserModBase* mod)
    {
        delete mod;
    }
}
