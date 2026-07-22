// test_logging.cpp - Tests for unified LOG_* macros and Poseidon::Foundation::LoggingSystem utilities

#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <stddef.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// Helper: captures spdlog messages via callback sink installed on category loggers
struct TestLogCapture
{
    struct Entry
    {
        spdlog::level::level_enum level;
        std::string logger_name;
        std::string message;
    };
    std::vector<Entry> entries;
    std::shared_ptr<spdlog::sinks::callback_sink_mt> sink;

    void Install()
    {
        sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [this](const spdlog::details::log_msg& msg)
            {
                entries.push_back({msg.level, std::string(msg.logger_name.data(), msg.logger_name.size()),
                                   std::string(msg.payload.data(), msg.payload.size())});
            });
        // Install on each category logger if initialized, otherwise on default
        bool any = false;
        for (int i = 0; i < static_cast<int>(Poseidon::Foundation::LogCategory::_Count); ++i)
        {
            if (LogDetail::g_loggers[i])
            {
                LogDetail::g_loggers[i]->sinks().push_back(sink);
                any = true;
            }
        }
        if (!any)
            spdlog::default_logger()->sinks().push_back(sink);
    }

    void Uninstall()
    {
        auto removeSink = [this](spdlog::logger* l)
        {
            auto& sinks = l->sinks();
            sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());
        };
        for (int i = 0; i < static_cast<int>(Poseidon::Foundation::LogCategory::_Count); ++i)
            if (LogDetail::g_loggers[i])
                removeSink(LogDetail::g_loggers[i]);
        removeSink(spdlog::default_logger_raw());
        sink.reset();
    }

    size_t Count() const { return entries.size(); }

    size_t CountByLevel(spdlog::level::level_enum level) const
    {
        size_t n = 0;
        for (auto& e : entries)
            if (e.level == level)
                ++n;
        return n;
    }

    bool HasMessage(const std::string& substr) const
    {
        for (auto& e : entries)
            if (e.message.find(substr) != std::string::npos)
                return true;
        return false;
    }

    bool HasCategory(const std::string& cat) const
    {
        for (auto& e : entries)
            if (e.logger_name == cat)
                return true;
        return false;
    }

    void Clear() { entries.clear(); }
};

TEST_CASE("LOG_* macros produce structured spdlog output with category tags", "[logging][sink]")
{
    // Ensure category loggers exist (Initialize creates them)
    Poseidon::Foundation::LoggingSystem logSys;
    logSys.Initialize("debug");

    TestLogCapture capture;
    capture.Install();

    LOG_INFO(Core, "hello sink");
    LOG_WARN(Audio, "audio warning {}", 42);
    LOG_ERROR(Graphics, "gl error");

    REQUIRE(capture.Count() == 3);
    CHECK(capture.entries[0].level == spdlog::level::info);
    CHECK(capture.entries[0].logger_name == "Core");
    CHECK(capture.entries[0].message.find("hello sink") != std::string::npos);
    CHECK(capture.entries[1].level == spdlog::level::warn);
    CHECK(capture.entries[1].logger_name == "Audio");
    CHECK(capture.entries[1].message.find("audio warning 42") != std::string::npos);
    CHECK(capture.entries[2].level == spdlog::level::err);
    CHECK(capture.entries[2].logger_name == "Graphics");

    capture.Uninstall();
    logSys.Shutdown();
}

TEST_CASE("LOG_* category tags and levels are correct", "[logging][sink]")
{
    // Lower level on all loggers to capture debug messages
    auto prevLevel = spdlog::default_logger()->level();
    auto setAll = [](spdlog::level::level_enum lvl)
    {
        spdlog::default_logger()->set_level(lvl);
        for (int i = 0; i < static_cast<int>(Poseidon::Foundation::LogCategory::_Count); ++i)
            if (LogDetail::g_loggers[i])
                LogDetail::g_loggers[i]->set_level(lvl);
    };
    setAll(spdlog::level::debug);

    TestLogCapture capture;
    capture.Install();

    LOG_INFO(Core, "init complete");
    LOG_DEBUG(Audio, "loaded sound");
    LOG_WARN(Core, "fallback used");
    LOG_INFO(Audio, "audio init");

    CHECK(capture.CountByLevel(spdlog::level::info) == 2);
    CHECK(capture.CountByLevel(spdlog::level::warn) == 1);
    CHECK(capture.HasMessage("loaded sound"));
    CHECK_FALSE(capture.HasMessage("nonexistent"));

    capture.Clear();
    CHECK(capture.Count() == 0);
    capture.Uninstall();

    setAll(prevLevel);
}

TEST_CASE("Removing spdlog sink stops capturing", "[logging][sink]")
{
    Poseidon::Foundation::LoggingSystem logSys;
    logSys.Initialize("debug");

    TestLogCapture capture;
    capture.Install();

    LOG_INFO(Core, "before remove");
    REQUIRE(capture.Count() == 1);

    capture.Uninstall();

    LOG_INFO(Core, "after remove");
    CHECK(capture.Count() == 1); // should not grow

    logSys.Shutdown();
}

TEST_CASE("GetLevelName returns correct strings", "[logging]")
{
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetLevelName(spdlog::level::trace)) == "trace");
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetLevelName(spdlog::level::debug)) == "debug");
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetLevelName(spdlog::level::info)) == "info");
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetLevelName(spdlog::level::warn)) == "warn");
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetLevelName(spdlog::level::err)) == "error");
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetLevelName(spdlog::level::critical)) == "critical");
}

TEST_CASE("GetCategoryName returns correct strings", "[logging]")
{
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetCategoryName(
              Poseidon::Foundation::LoggingSystem::Category::Core)) == "CORE");
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetCategoryName(
              Poseidon::Foundation::LoggingSystem::Category::Audio)) == "AUDIO");
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetCategoryName(
              Poseidon::Foundation::LoggingSystem::Category::Graphics)) == "GRAPHICS");
}

TEST_CASE("LOG_* messages contain no ANSI codes", "[logging]")
{
    Poseidon::Foundation::LoggingSystem logSys;
    logSys.Initialize("debug");

    TestLogCapture capture;
    capture.Install();

    LOG_INFO(Core, "test message with special chars");
    LOG_WARN(Audio, "value={}", 42);

    REQUIRE(capture.Count() == 2);
    for (auto& e : capture.entries)
        CHECK(e.message.find("\033[") == std::string::npos);
    CHECK(capture.HasMessage("special chars"));
    CHECK(capture.HasMessage("value=42"));

    capture.Uninstall();
    logSys.Shutdown();
}

TEST_CASE("strict mode: err-level log latches the trip; warn/info do not", "[logging][strict]")
{
    // The --strict path: ErrorCountingSink latches StrictTripped() on the first
    // err-level message while strict mode is on. GameApplication's main loop polls
    // it and turns it into a clean non-zero exit. Tested here at the mechanism
    // level (no main loop / GApp needed — latching is harmless without a poller).
    using LS = Poseidon::Foundation::LoggingSystem;
    LS logSys;
    logSys.Initialize("trace"); // attaches ErrorCountingSink to the category loggers

    // Strict OFF: an error bumps the count but must not latch the trip.
    LS::SetStrictMode(false);
    LS::ResetErrorCount();
    LOG_ERROR(Core, "strict-off error");
    CHECK(LS::GetErrorCount() >= 1);
    CHECK_FALSE(LS::StrictTripped());

    // Strict ON: info/warn do not trip; the first err-level message does.
    LS::SetStrictMode(true);
    LS::ResetErrorCount();
    CHECK_FALSE(LS::StrictTripped());
    LOG_INFO(Core, "info line");
    LOG_WARN(Core, "warn line");
    CHECK_FALSE(LS::StrictTripped());
    LOG_ERROR(Core, "strict-on error");
    CHECK(LS::StrictTripped());

    // ResetErrorCount clears the latch so tests can re-baseline at a quiet moment.
    LS::ResetErrorCount();
    CHECK_FALSE(LS::StrictTripped());

    LS::SetStrictMode(false); // don't leak strict into sibling tests in this binary
}

TEST_CASE("AttachFileSink captures log lines into the file", "[logging][file]")
{
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "cwr_attach_sink_test.log";
    std::error_code ec;
    fs::remove(path, ec);

    Poseidon::Foundation::LoggingSystem logSys;
    logSys.Initialize("info");
    logSys.AttachFileSink(path.string().c_str());
    CHECK(std::string(Poseidon::Foundation::LoggingSystem::GetLogFilePath()) == path.string());

    LOG_INFO(Core, "attach sink probe 12345");
    logSys.Shutdown();

    std::ifstream in(path);
    bool found = false;
    bool hasAnsi = false;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find("attach sink probe 12345") != std::string::npos)
            found = true;
        if (line.find("\033[") != std::string::npos)
            hasAnsi = true;
    }
    CHECK(found);
    CHECK_FALSE(hasAnsi);

    fs::remove(path, ec);
}

TEST_CASE("MakeTimestampedLogName builds a fixed-width dated .log name", "[logging][wiper]")
{
    const std::string name = Poseidon::Foundation::MakeTimestampedLogName("cwr");
    // Shape: cwr_YYYY-MM-DD_HH-MM-SS_<pid>.log
    CHECK(name.rfind("cwr_", 0) == 0);
    CHECK(name.substr(name.size() - 4) == ".log");
    CHECK(name[8] == '-');
    CHECK(name[11] == '-');
    CHECK(name[14] == '_');
    CHECK(name[17] == '-');
    CHECK(name[20] == '-');
    CHECK(name[23] == '_');
    CHECK(name.size() > 24);
}

TEST_CASE("WipeOldFiles keeps the newest N matching files and ignores the rest", "[logging][wiper]")
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "cwr_wipe_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    const auto base = fs::file_time_type::clock::now();
    auto touch = [&](const char* fmt, int i, int secs)
    {
        char n[32];
        snprintf(n, sizeof(n), fmt, i);
        const fs::path p = root / n;
        std::ofstream(p) << "x";
        fs::last_write_time(p, base + std::chrono::seconds(secs));
        return p;
    };

    std::vector<fs::path> logs;
    for (int i = 0; i < 15; ++i)
        logs.push_back(touch("cwr_%02d.log", i, i));
    const fs::path other = root / "keepme.txt";
    std::ofstream(other) << "y";

    Poseidon::Foundation::WipeOldFiles(root.string(), "cwr_", ".log", 10);

    for (int i = 0; i < 5; ++i)
        CHECK_FALSE(fs::exists(logs[i]));
    for (int i = 5; i < 15; ++i)
        CHECK(fs::exists(logs[i]));
    CHECK(fs::exists(other));

    std::vector<fs::path> crashes;
    for (int i = 0; i < 4; ++i)
        crashes.push_back(touch("crash_%02d.txt", i, i));
    Poseidon::Foundation::WipeOldFiles(root.string(), "crash_", ".txt", 2);
    CHECK_FALSE(fs::exists(crashes[0]));
    CHECK_FALSE(fs::exists(crashes[1]));
    CHECK(fs::exists(crashes[2]));
    CHECK(fs::exists(crashes[3]));
    CHECK(fs::exists(logs[14]));
    CHECK(fs::exists(other));

    fs::remove_all(root, ec);
}
