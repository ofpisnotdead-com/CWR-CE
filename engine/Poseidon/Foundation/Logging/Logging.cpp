#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/pattern_formatter.h>
#include <atomic>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctype.h>
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
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
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
    explicit PoseidonFormatter(LoggingSystem* sys, bool useColor) : _sys(sys), _useColor(useColor) {}

    static LoggingSystem::Category MapCategoryPublic(const spdlog::string_view_t& name) { return MapCategory(name); }

    void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override
    {
        // Map logger name to category enum for color lookup
        auto cat = MapCategory(msg.logger_name);

        const char* appTag = LoggingSystem::GetAppTag();
        if (appTag[0])
        {
            dest.append(std::string_view(appTag));
            dest.push_back(' ');
        }
        const char* lvl = _useColor ? LoggingSystem::GetFormattedLevel(msg.level) : PlainLevel(msg.level);
        dest.append(std::string_view(lvl, strlen(lvl)));
        dest.push_back(' ');
        const char* catTag = _useColor ? LoggingSystem::GetColoredCategoryTag(cat) : PlainCategory(cat);
        dest.append(std::string_view(catTag, strlen(catTag)));
        dest.push_back(' ');
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return std::make_unique<PoseidonFormatter>(_sys, _useColor);
    }

  private:
    static const char* PlainLevel(spdlog::level::level_enum level)
    {
        static const char* names[] = {"[TRCE]", "[DBUG]", "[INFO]", "[WARN]", "[ERRR]", "[CRIT]"};
        const int index = static_cast<int>(level);
        return names[index >= 0 && index <= 5 ? index : 2];
    }

    static const char* PlainCategory(LoggingSystem::Category category)
    {
        static thread_local char buffer[32];
        const char* name = LoggingSystem::GetCategoryName(category);
        snprintf(buffer, sizeof(buffer), "[%s]%-*s", name, static_cast<int>(9 - strlen(name) - 2), "");
        return buffer;
    }

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
    bool _useColor;
};

// Static per-process app tag — read by the log formatter/sink without a
// LoggingSystem back-pointer (see GetAppTag in the header).
char LoggingSystem::m_appTag[20] = {};
char LoggingSystem::m_appTagRaw[12] = {};

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
            bool useConsoleColor = true;
#if defined(__APPLE__) && TARGET_OS_IPHONE
            // Xcode presents its debug console as a color-capable pseudo-TTY,
            // but displays ANSI escapes literally. The generated Xcode scheme
            // marks its launches explicitly; plain devicectl/terminal launches
            // retain spdlog's normal TTY-based color detection.
            const auto colorMode = std::getenv("POSEIDON_XCODE_CONSOLE") ? spdlog::color_mode::never
                                                                        : spdlog::color_mode::automatic;
            useConsoleColor = colorMode != spdlog::color_mode::never;
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>(colorMode);
#else
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
#endif
            auto formatter = std::make_unique<spdlog::pattern_formatter>();
            formatter->add_flag<PoseidonFormatter>('*', this, useConsoleColor);
            formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %*%v");
            console_sink->set_formatter(std::move(formatter));
            sinks.push_back(console_sink);
        }
    }

    // Error-counting sink — always attached, feeds the triErrorCount verb.
    sinks.push_back(std::make_shared<ErrorCountingSink>());

    // Optional file sink (--log-file). A bad path (unwritable directory,
    // sandbox-forbidden location, etc.) must degrade to console-only rather
    // than take the whole process down -- basic_file_sink_mt's constructor
    // throws spdlog_ex on open failure, and this runs early enough in
    // startup (before any top-level catch is installed) that an uncaught
    // throw here is a guaranteed SIGABRT. Confirmed on iOS with a relative
    // --log-file value that resolved against an unwritable cwd.
    if (logFile && logFile[0] != '\0')
    {
        try
        {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
            auto formatter = std::make_unique<spdlog::pattern_formatter>();
            formatter->add_flag<PoseidonFormatter>('*', this, false);
            formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %*%v");
            file_sink->set_formatter(std::move(formatter));
            sinks.push_back(file_sink);
            m_hasFileSink = true;
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "LoggingSystem: could not open log file '%s': %s (continuing console-only)\n", logFile,
                    e.what());
        }
    }

    // Flush policy: always flush every message immediately. The "only flush
    // on warn+" console policy this used to have was a no-op perf tweak on a
    // real terminal (a TTY line-buffers at the libc level regardless of
    // spdlog's own flush() calls, so output always appeared live there
    // anyway) but silently broke live output anywhere the console sink's
    // fd is a pipe instead of a TTY -- e.g. Xcode's captured stdout for a
    // debugger-attached iOS run, which is fully block-buffered and only
    // flushes when spdlog explicitly asks it to. Below warn level, nothing
    // showed up until either the buffer filled or process exit -- and on
    // iOS, exit doesn't reliably run libc's atexit stdio flush either, so
    // it could show literally nothing, ever. Trace-level flush costs an
    // extra syscall per log call; already paid unconditionally in the
    // file-sink case below for the same reason (game may exit abruptly),
    // so extending it to console-only is free of new tradeoffs.
    auto flushLevel = spdlog::level::trace;

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
