#pragma once

#include "RE/Skyrim.h"
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

#include <atomic>
#define WIN32_LEAN_AND_MEAN
#include <ClibUtil/simpleINI.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <windows.h>

#include "Plugin.h"

using namespace std::literals;

namespace logger = SKSE::log;

namespace stl
{
    using namespace SKSE::stl;
}

#define DLLEXPORT __declspec(dllexport)
