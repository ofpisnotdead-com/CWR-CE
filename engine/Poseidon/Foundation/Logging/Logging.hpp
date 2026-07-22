#pragma once

#include <Poseidon/Foundation/Framework/Log.hpp>
#include <memory>
#include <string>
#include <spdlog/common.h>

namespace Poseidon::Foundation
{
/// Logging subsystem - owned by Application
class LoggingSystem
{
public:
	/// Log categories (subsystem tags)
	enum class Category {
		Core,       // Core engine and initialization
		Config,     // Configuration parsing and registry
		Memory,     // Memory management and allocation
		Graphics,   // Graphics engine, rendering, D3D9
		Audio,      // Sound system, music, effects
		Input,      // Keyboard, mouse, joystick
		Network,    // Multiplayer networking
		World,      // World loading, missions, islands
		Script,     // SQF/SQS scripting engine
		AI,         // AI and pathfinding
		Physics,    // Physics simulation
		UI,         // User interface, menus, HUD
		Mission     // Mission script output (simulate mode)
	};

	LoggingSystem();
	~LoggingSystem();

	/// Initialize with log level from config
	void Initialize(const char* logLevel = "info", const char* categoryFilter = "",
	                const char* logFormat = "text", const char* logFile = nullptr);

	/// Initialize from AppConfig with a 3-letter app prefix (e.g. "app", "srv", "viw").
	/// Generates a tag like "app-1a2b" using PID. CLI --app-tag overrides.
	void InitializeFromConfig(const char* appPrefix);

	/// Attach a file sink to every logger after Initialize (the per-run log, once the
	/// user dir is known). No-op if uninitialized, path empty, or already attached.
	void AttachFileSink(const char* path);

	/// Absolute path of the attached log file, or "" if none.
	static const char* GetLogFilePath() { return m_logFilePath; }

	/// Shutdown logging (flushes buffers)
	void Shutdown();
	
	/// Check if category is enabled
	bool IsCategoryEnabled(Category category) const;

	/// Set app tag for log line identification (e.g. "server", "game-a3f2")
	/// Tag is right-padded to 10 chars for aligned output.
	void SetAppTag(const char* tag);

	/// Get formatted app tag (e.g. "[server    ]"). Static: the app tag is a
	/// per-process value, and the log formatter/sink read it without holding a
	/// back-pointer to a LoggingSystem instance (which a test may destroy while
	/// its formatter is still installed on a global logger -> use-after-return).
	static const char* GetAppTag() { return m_appTag; }
	static const char* GetAppTagRaw() { return m_appTagRaw; }

	/// Number of error-level (LOG_ERROR) messages emitted since process start.
	/// Powers the `triErrorCount` tri verb so integration tests can assert
	/// that a particular flow (e.g. viewer F5 hot-reload) doesn't fire any
	/// new errors.  Thread-safe.
	static int GetErrorCount();

	/// Reset the error counter to zero (also clears the strict trip latch).
	/// Tests call this at a known-quiet moment to baseline.
	static void ResetErrorCount();

	/// Enable/disable strict mode (--strict). When on, any err-level message
	/// latches StrictTripped(). Set once at init from AppConfig.
	static void SetStrictMode(bool enabled);

	/// True once an err-level message has been logged while strict mode is on.
	/// The app's main loop polls this and turns it into a clean non-zero exit.
	/// Thread-safe.
	static bool StrictTripped();

	/// True when strict mode (--strict) is enabled. Set once at init from AppConfig.
	/// The unconditional-fatal paths (ErrorMessage, scene preload) read this to stay
	/// fatal under --strict but log-and-continue under --no-strict (the build players run),
	/// so a recoverable error no longer hard-exits the player build. Thread-safe.
	static bool IsStrictMode();

	/// Category metadata (used by PoseidonFormatter)
	static const char* GetCategoryName(Category category);
	static const char* GetCategoryColor(Category category);
	static const char* GetColoredCategoryTag(Category category);
	static const char* GetFormattedLevel(spdlog::level::level_enum level);
	static const char* GetLevelName(spdlog::level::level_enum level);
	// Plain (no ANSI) variants for file sinks; a log file must never carry colour codes.
	static const char* GetPlainCategoryTag(Category category);
	static const char* GetPlainLevel(spdlog::level::level_enum level);

private:
	std::shared_ptr<spdlog::logger> m_logger;
	std::shared_ptr<spdlog::logger> m_categoryLoggers[static_cast<int>(LogCategory::_Count)];
	bool m_initialized;
	bool m_jsonlMode;
	bool m_hasFileSink;
	bool m_categoryFilter[static_cast<int>(LogCategory::_Count)];
	bool m_filterActive;
	static char m_appTag[20];
	static char m_appTagRaw[12];
	static char m_logFilePath[1024];
};

// LOG_* macros live in Poseidon/Foundation/Framework/Log.hpp.

/// Build a per-run log filename like "<prefix>_YYYY-MM-DD_HH-MM-SS.log" from the
/// local time. Chronological, so a lexical or mtime sort orders runs.
std::string MakeTimestampedLogName(const char* prefix);

/// Keep the newest `keepN` `dir` files matching `prefix`*`ext`; delete the rest,
/// oldest first. Swallows all filesystem errors; never throws.
void WipeOldFiles(const std::string& dir, const char* prefix, const char* ext, int keepN);

} // namespace Poseidon::Foundation
