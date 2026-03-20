#pragma once

namespace Papyrus
{
    inline constexpr std::string_view McmScriptName{ "DLLTweaksVRMCM" };

    bool Register(RE::BSScript::IVirtualMachine* a_vm);
}
