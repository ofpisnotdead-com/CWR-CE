#pragma once

#include <spdlog/spdlog.h>

// Unified logging for the engine stack. Each category gets its own named spdlog logger.
// Before LoggingSystem::Initialize(), calls fall back to the spdlog default (stdout);
// after init, per-category loggers share the configured sinks. The category name is the
// logger name, read via %n — no string parsing.

namespace Poseidon::Foundation
{
// Log categories (subsystem tags)
enum class LogCategory
{
    Core,
    Config,
    Memory,
    Graphics,
    Audio,
    Input,
    Network,
    World,
    Script,
    AI,
    Physics,
    UI,
    Mission,
    _Count
};

inline const char* LogCategoryTag(LogCategory cat)
{
    static const char* tags[] = {"Core",  "Config", "Memory", "Graphics", "Audio", "Input",  "Network",
                                 "World", "Script", "AI",     "Physics",  "UI",    "Mission"};
    return tags[static_cast<int>(cat)];
}
} // namespace Poseidon::Foundation

// Per-category logger cache. Populated by LoggingSystem::Initialize().
// Before init: nullptr → fallback to spdlog default logger.
namespace LogDetail
{
inline spdlog::logger* g_loggers[static_cast<int>(Poseidon::Foundation::LogCategory::_Count)] = {};

inline spdlog::logger* Get(Poseidon::Foundation::LogCategory cat)
{
    auto* l = g_loggers[static_cast<int>(cat)];
    return l ? l : spdlog::default_logger_raw();
}
} // namespace LogDetail

namespace Poseidon::Foundation
{
// True the first time a call site is reached with a given key. Keys accumulate for the process
// lifetime, so log with a bounded key set such as a config class name.
bool LogOnceForKey(const void* site, const char* key);

void ForgetLogOnceKeys();
} // namespace Poseidon::Foundation

#define LOG_TRACE(category, ...) \
    LogDetail::Get(::Poseidon::Foundation::LogCategory::category)->log(spdlog::source_loc{}, spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(category, ...) \
    LogDetail::Get(::Poseidon::Foundation::LogCategory::category)->log(spdlog::source_loc{}, spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(category, ...) \
    LogDetail::Get(::Poseidon::Foundation::LogCategory::category)->log(spdlog::source_loc{}, spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(category, ...) \
    LogDetail::Get(::Poseidon::Foundation::LogCategory::category)->log(spdlog::source_loc{}, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(category, ...) \
    LogDetail::Get(::Poseidon::Foundation::LogCategory::category)->log(spdlog::source_loc{}, spdlog::level::err, __VA_ARGS__)

#define LOG_WARN_ONCE_PER(category, key, ...)                           \
    {                                                                   \
        static const char logOnceSite = 0;                              \
        if (::Poseidon::Foundation::LogOnceForKey(&logOnceSite, (key))) \
            LOG_WARN(category, __VA_ARGS__);                            \
    }
