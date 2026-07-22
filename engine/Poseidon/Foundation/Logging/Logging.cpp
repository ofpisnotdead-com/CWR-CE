#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/pattern_formatter.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctype.h>
#include <filesystem>
#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/formatter.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <ctime>
#include <mutex>
#include <ratio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#endif

// Global error counter — incremented by ErrorCountingSink for every
// LOG_ERROR-level message that flows through any registered logger.
// Read + reset via LoggingSystem::GetErrorCount / ResetErrorCount.

namespace Poseidon::Foundation
{
namespace
{
bool EnvFlagEnabled(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || !value[0])
        return false;
    return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "off") != 0;
}
} // namespace

static std::atomic<int> g_errorCount{0};
// Strict mode (--strict): set once at init. When on, the first err-level message
// latches g_strictTripped; the app's main loop polls StrictTripped() and turns it
// into a clean shutdown with a non-zero exit code. The sink can fire on any
// thread, so it only sets an atomic flag — the close happens on the main thread.
static std::atomic<bool> g_strictMode{false};
static std::atomic<bool> g_strictTripped{false};

// Counting sink — sits next to the console / JSONL / file sinks and
// bumps the global counter for every err-level message.  Doesn't write
// output of its own.
class ErrorCountingSink : public spdlog::sinks::base_sink<std::mutex>
{
  protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if (msg.level >= spdlog::level::err)
        {
            g_errorCount.fetch_add(1, std::memory_order_relaxed);
            if (g_strictMode.load(std::memory_order_relaxed))
                g_strictTripped.store(true, std::memory_order_relaxed);
        }
    }
    void flush_() override {}
};

int LoggingSystem::GetErrorCount()
{
    return g_errorCount.load(std::memory_order_relaxed);
}

void LoggingSystem::ResetErrorCount()
{
    g_errorCount.store(0, std::memory_order_relaxed);
    g_strictTripped.store(false, std::memory_order_relaxed);
}

void LoggingSystem::SetStrictMode(bool enabled)
{
    g_strictMode.store(enabled, std::memory_order_relaxed);
}

bool LoggingSystem::StrictTripped()
{
    return g_strictTripped.load(std::memory_order_relaxed);
}

bool LoggingSystem::IsStrictMode()
{
    return g_strictMode.load(std::memory_order_relaxed);
}

// JSONL stdout sink — writes one JSON object per log line.
// Logger name is the category (e.g. "Core", "Audio").
class JsonlSink : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    explicit JsonlSink(LoggingSystem* sys) : _sys(sys) {}

  protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        auto now = msg.time;
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        struct tm tm_now;
#ifdef _WIN32
        localtime_s(&tm_now, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_now);
#endif
        char ts[32];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d.%03d", tm_now.tm_year + 1900, tm_now.tm_mon + 1,
                 tm_now.tm_mday, tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, static_cast<int>(ms.count()));

        // Escape message for JSON
        std::string_view payload(msg.payload.data(), msg.payload.size());
        char escaped[4096];
        JsonEscape(payload, escaped, sizeof(escaped));

        const char* level = spdlog::level::to_string_view(msg.level).data();
        std::string_view cat(msg.logger_name.data(), msg.logger_name.size());

        char line[4096 + 256];
        const char* appTag = LoggingSystem::GetAppTagRaw();
        if (appTag[0])
            snprintf(line, sizeof(line), R"({"ts":"%s","app":"%s","level":"%s","cat":"%.*s","msg":"%s"})", ts, appTag,
                     level, (int)cat.size(), cat.data(), escaped);
        else
            snprintf(line, sizeof(line), R"({"ts":"%s","level":"%s","cat":"%.*s","msg":"%s"})", ts, level,
                     (int)cat.size(), cat.data(), escaped);
        puts(line);
        fflush(stdout);
    }

    void flush_() override { fflush(stdout); }

  private:
    static void JsonEscape(std::string_view src, char* dst, size_t dstSize)
    {
        size_t j = 0;
        for (size_t i = 0; i < src.size() && j + 6 < dstSize; ++i)
        {
            char c = src[i];
            if (c == '"')
            {
                dst[j++] = '\\';
                dst[j++] = '"';
            }
            else if (c == '\\')
            {
                dst[j++] = '\\';
                dst[j++] = '\\';
            }
            else if (c == '\n')
            {
                dst[j++] = '\\';
                dst[j++] = 'n';
            }
            else if (c == '\r')
            {
                dst[j++] = '\\';
                dst[j++] = 'r';
            }
            else if (c == '\t')
            {
                dst[j++] = '\\';
                dst[j++] = 't';
            }
            else if (static_cast<unsigned char>(c) < 0x20)
            { /* skip control chars */
            }
            else
            {
                dst[j++] = c;
            }
        }
        dst[j] = '\0';
    }

    LoggingSystem* _sys;
};

// Custom spdlog flag formatter that reads logger name (%n) to produce
// colored [LEVEL] [CATEGORY] prefix. No string parsing — category is the logger name.
// Output: [app-tag] [LEVEL] [CATEGORY] message
class PoseidonFormatter : public spdlog::custom_flag_formatter
{
  public:
    explicit PoseidonFormatter(LoggingSystem* sys, bool colored = true) : _sys(sys), _colored(colored) {}

    static LoggingSystem::Category MapCategoryPublic(const spdlog::string_view_t& name) { return MapCategory(name); }

    void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override
    {
        // Map logger name to category enum for color lookup
        auto cat = MapCategory(msg.logger_name);

        const char* appTag = LoggingSystem::GetAppTag(); // always plain
        if (appTag[0])
        {
            dest.append(std::string_view(appTag));
            dest.push_back(' ');
        }
        const char* lvl =
            _colored ? LoggingSystem::GetFormattedLevel(msg.level) : LoggingSystem::GetPlainLevel(msg.level);
        dest.append(std::string_view(lvl, strlen(lvl)));
        dest.push_back(' ');
        const char* catTag =
            _colored ? LoggingSystem::GetColoredCategoryTag(cat) : LoggingSystem::GetPlainCategoryTag(cat);
        dest.append(std::string_view(catTag, strlen(catTag)));
        dest.push_back(' ');
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return std::make_unique<PoseidonFormatter>(_sys, _colored);
    }

  private:
    static LoggingSystem::Category MapCategory(const spdlog::string_view_t& name)
    {
        struct Entry
        {
            const char* name;
            LoggingSystem::Category cat;
        };
        static const Entry table[] = {
            {"Core", LoggingSystem::Category::Core},       {"Config", LoggingSystem::Category::Config},
            {"Memory", LoggingSystem::Category::Memory},   {"Graphics", LoggingSystem::Category::Graphics},
            {"Audio", LoggingSystem::Category::Audio},     {"Input", LoggingSystem::Category::Input},
            {"Network", LoggingSystem::Category::Network}, {"World", LoggingSystem::Category::World},
            {"Script", LoggingSystem::Category::Script},   {"AI", LoggingSystem::Category::AI},
            {"Physics", LoggingSystem::Category::Physics}, {"UI", LoggingSystem::Category::UI},
            {"Mission", LoggingSystem::Category::Mission},
        };
        std::string_view sv(name.data(), name.size());
        for (auto& e : table)
            if (sv == e.name)
                return e.cat;
        return LoggingSystem::Category::Core;
    }

    LoggingSystem* _sys;
    bool _colored;
};

// Static per-process app tag — read by the log formatter/sink without a
// LoggingSystem back-pointer (see GetAppTag in the header).
char LoggingSystem::m_appTag[20] = {};
char LoggingSystem::m_appTagRaw[12] = {};
char LoggingSystem::m_logFilePath[1024] = {};

LoggingSystem::LoggingSystem()
    : m_logger(nullptr), m_initialized(false), m_jsonlMode(false), m_hasFileSink(false), m_filterActive(false)
{
    for (int i = 0; i < static_cast<int>(LogCategory::_Count); ++i)
    {
        m_categoryFilter[i] = true;
    }
    m_appTag[0] = '\0';
    m_appTagRaw[0] = '\0';
}

LoggingSystem::~LoggingSystem()
{
    Shutdown();
}

const char* LoggingSystem::GetCategoryName(Category category)
{
    switch (category)
    {
        case Category::Core:
            return "CORE";
        case Category::Config:
            return "CONFIG";
        case Category::Memory:
            return "MEMORY";
        case Category::Graphics:
            return "GRAPHICS";
        case Category::Audio:
            return "AUDIO";
        case Category::Input:
            return "INPUT";
        case Category::Network:
            return "NETWORK";
        case Category::World:
            return "WORLD";
        case Category::Script:
            return "SCRIPT";
        case Category::AI:
            return "AI";
        case Category::Physics:
            return "PHYSICS";
        case Category::UI:
            return "UI";
        case Category::Mission:
            return "MISSION";
        default:
            return "UNKNOWN";
    }
}

const char* LoggingSystem::GetCategoryColor(Category category)
{
    switch (category)
    {
        case Category::Core:
            return "\033[1;34m"; // Bright blue (system/core)
        case Category::Config:
            return "\033[0;33m"; // Yellow (configuration)
        case Category::Memory:
            return "\033[0;35m"; // Magenta (memory/resources)
        case Category::Graphics:
            return "\033[1;32m"; // Bright green (graphics/rendering)
        case Category::Audio:
            return "\033[1;36m"; // Bright cyan (audio/sound)
        case Category::Input:
            return "\033[0;36m"; // Cyan (input devices)
        case Category::Network:
            return "\033[1;35m"; // Bright magenta (network/multiplayer)
        case Category::World:
            return "\033[0;32m"; // Green (world/terrain)
        case Category::Script:
            return "\033[1;33m"; // Bright yellow (scripting)
        case Category::AI:
            return "\033[0;34m"; // Blue (AI/behavior)
        case Category::Physics:
            return "\033[1;37m"; // Bright white (physics/simulation)
        case Category::UI:
            return "\033[0;37m"; // White (UI/HUD)
        case Category::Mission:
            return "\033[1;33m"; // Bright yellow (mission script output)
        default:
            return "\033[0m"; // Reset
    }
}

const char* LoggingSystem::GetColoredCategoryTag(Category category)
{
    static thread_local char buffer[64];
    const char* color = GetCategoryColor(category);
    const char* name = GetCategoryName(category);
    const char* reset = "\033[0m";

    // Format: [ColoredName] with padding outside to align to 9 chars total ("[Network]" = 9)
    snprintf(buffer, sizeof(buffer), "[%s%s%s]%-*s", color, name, reset, (int)(9 - strlen(name) - 2),
             ""); // 9 total - name length - 2 brackets
    return buffer;
}

const char* LoggingSystem::GetFormattedLevel(spdlog::level::level_enum level)
{
    static thread_local char buffer[64];
    const char* level_colors[] = {
        "\033[1;37m", // trace - bright white
        "\033[0;36m", // debug - cyan
        "\033[0;32m", // info - green
        "\033[1;33m", // warn - yellow
        "\033[1;31m", // error - red
        "\033[1;35m"  // critical - magenta
    };
    const char* level_names[] = {"TRCE", "DBUG", "INFO", "WARN", "ERRR", "CRIT"};
    const char* reset = "\033[0m";

    int level_idx = static_cast<int>(level);
    if (level_idx < 0 || level_idx > 5)
    {
        level_idx = 2; // default to info
    }

    const char* color = level_colors[level_idx];
    const char* name = level_names[level_idx];

    // Format: [ColoredLevel] - all 4 chars, no padding needed ("[CRIT]" = 6 chars)
    snprintf(buffer, sizeof(buffer), "[%s%s%s]", color, name, reset);
    return buffer;
}

const char* LoggingSystem::GetLevelName(spdlog::level::level_enum level)
{
    const char* names[] = {"trace", "debug", "info", "warn", "error", "critical"};
    int idx = static_cast<int>(level);
    if (idx < 0 || idx > 5)
        return "info";
    return names[idx];
}

const char* LoggingSystem::GetPlainLevel(spdlog::level::level_enum level)
{
    static thread_local char buffer[16];
    const char* names[] = {"TRCE", "DBUG", "INFO", "WARN", "ERRR", "CRIT"};
    int idx = static_cast<int>(level);
    if (idx < 0 || idx > 5)
        idx = 2;
    snprintf(buffer, sizeof(buffer), "[%s]", names[idx]);
    return buffer;
}

const char* LoggingSystem::GetPlainCategoryTag(Category category)
{
    static thread_local char buffer[32];
    const char* name = GetCategoryName(category);
    // Match GetColoredCategoryTag's layout (name padded to align to 9 chars), minus colour.
    snprintf(buffer, sizeof(buffer), "[%s]%-*s", name, (int)(9 - strlen(name) - 2), "");
    return buffer;
}

void LoggingSystem::Initialize(const char* logLevel, const char* categoryFilter, const char* logFormat,
                               const char* logFile)
{
    if (m_initialized)
    {
        return;
    }

    // Parse category filter if provided
    if (categoryFilter && categoryFilter[0] != '\0')
    {
        for (int i = 0; i < static_cast<int>(LogCategory::_Count); ++i)
        {
            m_categoryFilter[i] = false;
        }
        m_filterActive = true;

        // Parse comma-separated list
        std::string filter(categoryFilter);
        size_t pos = 0;
        while (pos < filter.length())
        {
            size_t comma = filter.find(',', pos);
            if (comma == std::string::npos)
            {
                comma = filter.length();
            }

            std::string cat = filter.substr(pos, comma - pos);
            // Trim spaces
            while (!cat.empty() && cat[0] == ' ')
            {
                cat.erase(0, 1);
            }
            while (!cat.empty() && cat[cat.length() - 1] == ' ')
            {
                cat.erase(cat.length() - 1);
            }

            // Convert to lowercase for case-insensitive comparison
            for (size_t i = 0; i < cat.length(); ++i)
            {
                cat[i] = static_cast<char>(tolower(cat[i]));
            }

            // Check category name
            if (cat == "core")
            {
                m_categoryFilter[static_cast<int>(Category::Core)] = true;
            }
            else if (cat == "config")
            {
                m_categoryFilter[static_cast<int>(Category::Config)] = true;
            }
            else if (cat == "memory")
            {
                m_categoryFilter[static_cast<int>(Category::Memory)] = true;
            }
            else if (cat == "graphics")
            {
                m_categoryFilter[static_cast<int>(Category::Graphics)] = true;
            }
            else if (cat == "audio")
            {
                m_categoryFilter[static_cast<int>(Category::Audio)] = true;
            }
            else if (cat == "input")
            {
                m_categoryFilter[static_cast<int>(Category::Input)] = true;
            }
            else if (cat == "network")
            {
                m_categoryFilter[static_cast<int>(Category::Network)] = true;
            }
            else if (cat == "world")
            {
                m_categoryFilter[static_cast<int>(Category::World)] = true;
            }
            else if (cat == "script")
            {
                m_categoryFilter[static_cast<int>(Category::Script)] = true;
            }
            else if (cat == "ai")
            {
                m_categoryFilter[static_cast<int>(Category::AI)] = true;
            }
            else if (cat == "physics")
            {
                m_categoryFilter[static_cast<int>(Category::Physics)] = true;
            }
            else if (cat == "ui")
            {
                m_categoryFilter[static_cast<int>(Category::UI)] = true;
            }
            else if (cat == "mission")
            {
                m_categoryFilter[static_cast<int>(Category::Mission)] = true;
            }

            pos = comma + 1;
        }
    }
    else
    {
        for (int i = 0; i < static_cast<int>(LogCategory::_Count); ++i)
        {
            m_categoryFilter[i] = true;
        }
        m_filterActive = false;
    }

    // Check log format
    m_jsonlMode = logFormat && strcmp(logFormat, "jsonl") == 0;
    const bool suppressTestConsole = EnvFlagEnabled("POSEIDON_TEST") && !EnvFlagEnabled("POSEIDON_TEST_LOG");

    // Parse log level
    auto level = spdlog::level::info;
    if (logLevel)
    {
        if (strcmp(logLevel, "trace") == 0)
            level = spdlog::level::trace;
        else if (strcmp(logLevel, "debug") == 0)
            level = spdlog::level::debug;
        else if (strcmp(logLevel, "info") == 0)
            level = spdlog::level::info;
        else if (strcmp(logLevel, "warn") == 0 || strcmp(logLevel, "warning") == 0)
            level = spdlog::level::warn;
        else if (strcmp(logLevel, "error") == 0 || strcmp(logLevel, "err") == 0)
            level = spdlog::level::err;
        else if (strcmp(logLevel, "critical") == 0)
            level = spdlog::level::critical;
        else if (strcmp(logLevel, "off") == 0)
            level = spdlog::level::off;
    }

    // Build sinks
    std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;
    if (m_jsonlMode)
    {
        if (!suppressTestConsole)
            sinks.push_back(std::make_shared<JsonlSink>(this));
    }
    else
    {
        if (!suppressTestConsole)
        {
            // Text mode: colored console with custom formatter
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            auto formatter = std::make_unique<spdlog::pattern_formatter>();
            formatter->add_flag<PoseidonFormatter>('*', this);
            formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %*%v");
            console_sink->set_formatter(std::move(formatter));
            sinks.push_back(console_sink);
        }
    }

    // Error-counting sink — always attached, feeds the triErrorCount verb.
    sinks.push_back(std::make_shared<ErrorCountingSink>());

    // Optional file sink (--log-file)
    if (logFile && logFile[0] != '\0')
    {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
        auto formatter = std::make_unique<spdlog::pattern_formatter>();
        formatter->add_flag<PoseidonFormatter>('*', this, /*colored=*/false);
        formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %*%v");
        file_sink->set_formatter(std::move(formatter));
        sinks.push_back(file_sink);
        m_hasFileSink = true;
    }

    // Flush policy: flush every message when writing to file (game may exit abruptly),
    // otherwise flush only on warnings and above.
    auto flushLevel = m_hasFileSink ? spdlog::level::trace : spdlog::level::warn;

    // Create one spdlog logger per category, all sharing the same sink(s).
    // Logger name = category name → accessible via %n, no string parsing.
    constexpr int catCount = static_cast<int>(LogCategory::_Count);
    for (int i = 0; i < catCount; ++i)
    {
        const char* name = LogCategoryTag(static_cast<LogCategory>(i));
        auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
        logger->set_level(level);
        logger->flush_on(flushLevel);
        spdlog::register_logger(logger);
        m_categoryLoggers[i] = logger;
        LogDetail::g_loggers[i] = logger.get();
    }

    // Keep a "poseidon" default logger for non-categorized output
    m_logger = std::make_shared<spdlog::logger>("poseidon", sinks.begin(), sinks.end());
    m_logger->set_level(level);
    m_logger->flush_on(flushLevel);
    spdlog::set_default_logger(m_logger);

    m_initialized = true;
}

void LoggingSystem::AttachFileSink(const char* path)
{
    if (!m_initialized || !path || !path[0] || m_hasFileSink)
        return;

    try
    {
        std::filesystem::path p(path);
        if (p.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, true);
        auto formatter = std::make_unique<spdlog::pattern_formatter>();
        formatter->add_flag<PoseidonFormatter>('*', this, /*colored=*/false);
        formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %*%v");
        file_sink->set_formatter(std::move(formatter));

        // Runs single-threaded at boot, before any worker logs, so appending to each
        // live logger's own sink vector in place is safe. Flush every line.
        constexpr int catCount = static_cast<int>(LogCategory::_Count);
        for (int i = 0; i < catCount; ++i)
        {
            if (m_categoryLoggers[i])
            {
                m_categoryLoggers[i]->sinks().push_back(file_sink);
                m_categoryLoggers[i]->flush_on(spdlog::level::trace);
            }
        }
        if (m_logger)
        {
            m_logger->sinks().push_back(file_sink);
            m_logger->flush_on(spdlog::level::trace);
        }

        m_hasFileSink = true;
        snprintf(m_logFilePath, sizeof(m_logFilePath), "%s", path);
    }
    catch (const std::exception& e)
    {
        LOG_WARN(Core, "Could not open log file {}: {}", path, e.what());
    }
}

std::string MakeTimestampedLogName(const char* prefix)
{
    std::time_t t = std::time(nullptr);
    struct tm tmNow;
#ifdef _WIN32
    localtime_s(&tmNow, &t);
    const int pid = _getpid();
#else
    localtime_r(&t, &tmNow);
    const int pid = getpid();
#endif
    // The pid suffix stops a same-second relaunch from truncating the previous
    // run's file (basic_file_sink opens with truncate).
    char buf[160];
    snprintf(buf, sizeof(buf), "%s_%04d-%02d-%02d_%02d-%02d-%02d_%d.log", (prefix && prefix[0]) ? prefix : "log",
             tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, pid);
    return buf;
}

void WipeOldFiles(const std::string& dir, const char* prefix, const char* ext, int keepN)
{
    if (keepN < 0)
        return;

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return;

    try
    {
        const std::string pre = (prefix && prefix[0]) ? prefix : "";
        const std::string suf = (ext && ext[0]) ? ext : "";
        std::vector<std::pair<fs::file_time_type, fs::path>> matches;
        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            std::error_code fileEc;
            if (!entry.is_regular_file(fileEc))
                continue;
            const std::string name = entry.path().filename().string();
            if (!pre.empty() && name.rfind(pre, 0) != 0)
                continue;
            if (!suf.empty() &&
                (name.size() < suf.size() || name.compare(name.size() - suf.size(), suf.size(), suf) != 0))
                continue;
            matches.emplace_back(entry.last_write_time(fileEc), entry.path());
        }

        if (static_cast<int>(matches.size()) <= keepN)
            return;

        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = static_cast<size_t>(keepN); i < matches.size(); ++i)
        {
            std::error_code rmEc;
            fs::remove(matches[i].second, rmEc);
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARN(Core, "Log cleanup in {} failed: {}", dir, e.what());
    }
}

void LoggingSystem::Shutdown()
{
    if (m_initialized)
    {
        // Clear per-category logger cache
        constexpr int catCount = static_cast<int>(LogCategory::_Count);
        for (int i = 0; i < catCount; ++i)
        {
            LogDetail::g_loggers[i] = nullptr;
            if (m_categoryLoggers[i])
            {
                m_categoryLoggers[i]->flush();
                spdlog::drop(m_categoryLoggers[i]->name());
                m_categoryLoggers[i].reset();
            }
        }
        if (m_logger)
        {
            m_logger->flush();
            m_logger.reset();
        }
        m_initialized = false;
    }
}

bool LoggingSystem::IsCategoryEnabled(Category category) const
{
    int idx = static_cast<int>(category);
    if (idx < 0 || idx >= static_cast<int>(LogCategory::_Count))
    {
        return false;
    }
    return m_categoryFilter[idx];
}

void LoggingSystem::SetAppTag(const char* tag)
{
    if (!tag || !tag[0])
    {
        m_appTag[0] = '\0';
        m_appTagRaw[0] = '\0';
        return;
    }
    snprintf(m_appTagRaw, sizeof(m_appTagRaw), "%s", tag);
    // Format: "[tag     ] " — padded to 8 chars inside brackets
    snprintf(m_appTag, sizeof(m_appTag), "[%-8.8s]", tag);
}

void LoggingSystem::InitializeFromConfig(const char* appPrefix)
{
    auto& cfg = AppConfig::Instance();
    const auto& logFile = cfg.GetLogFile();
    Initialize(cfg.GetLogLevel().c_str(), cfg.GetLogCategories().c_str(), cfg.GetLogFormat().c_str(),
               logFile.empty() ? nullptr : logFile.c_str());
    SetStrictMode(cfg.Strict());

    const auto& cliTag = cfg.GetAppTag();
    if (!cliTag.empty())
    {
        SetAppTag(cliTag.c_str());
    }
    else
    {
        char tag[12];
        snprintf(tag, sizeof(tag), "%.3s-%04x", appPrefix, (unsigned)getpid() & 0xFFFF);
        SetAppTag(tag);
    }
}

} // namespace Poseidon::Foundation
