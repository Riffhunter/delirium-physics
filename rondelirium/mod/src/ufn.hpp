// Calling a game UFunction by name: parameters are laid out by reflection (each property's offset), so no struct
// layouts are hard-coded. UE5 maths types are doubles.
#pragma once

#include <cstring>
#include <vector>

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>

namespace ufn
{
    using namespace RC;
    using namespace RC::Unreal;

    struct DVec { double x, y, z; };
    struct DQuat { double x, y, z, w; };
    // FTransform (double): rotation, translation and scale, each a 32-byte vector register.
    struct DTransform { DQuat rot; DVec pos; double pad0; DVec scale; double pad1; };
    static_assert(sizeof(DTransform) == 96);

    // 0 when the class has no such function; cache the UFunction per class where it's called often.
    inline UFunction* find(UObject* obj, const TCHAR* name)
    {
        return obj ? obj->GetFunctionByNameInChain(name) : nullptr;
    }

    class Call
    {
      public:
        explicit Call(UFunction* fn) : fn(fn)
        {
            if (fn) buf.assign(static_cast<size_t>(fn->GetStructureSize()) + 16, 0);
        }

        explicit operator bool() const { return fn != nullptr; }

        template <typename T>
        Call& set(const TCHAR* param, const T& value)
        {
            if (auto at = slot(param)) std::memcpy(at, &value, sizeof(T));
            return *this;
        }

        Call& name(const TCHAR* param, const TCHAR* value) { return set(param, FName(value, FNAME_Add)); }

        template <typename T>
        T get(const TCHAR* param) const
        {
            T out{};
            if (auto at = const_cast<Call*>(this)->slot(param)) std::memcpy(&out, at, sizeof(T));
            return out;
        }

        // A field of a struct parameter (e.g. a returned HitResult's ImpactPoint).
        template <typename T>
        T field(const TCHAR* param, const TCHAR* name) const
        {
            T out{};
            if (!fn) return out;
            for (FProperty* p : fn->ForEachProperty())
            {
                if (p->GetName() != param) continue;
                auto sp = CastField<FStructProperty>(p);
                if (!sp) break;
                UScriptStruct* s = sp->GetStruct();
                for (FProperty* f : s->ForEachPropertyInChain())
                {
                    if (f->GetName() == name) std::memcpy(&out, buf.data() + p->GetOffset_Internal() + f->GetOffset_Internal(), sizeof(T));
                }
            }
            return out;
        }

        void invoke(UObject* obj)
        {
            if (fn && obj) obj->ProcessEvent(fn, buf.data());
        }

      private:
        uint8* slot(const TCHAR* param)
        {
            if (!fn) return nullptr;
            for (FProperty* p : fn->ForEachProperty())
            {
                if (p->GetName() == param) return buf.data() + p->GetOffset_Internal();
            }
            return nullptr;
        }

        UFunction* fn;
        std::vector<uint8> buf;
    };
}
