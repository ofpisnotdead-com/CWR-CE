#pragma once

#include <string>

namespace Poseidon
{
class ParamFile;

// True when a settings file exists at `path`.
bool SettingsFileExists(const std::string& path);

// Parses `path` into `cfg`. Returns false when the file does not exist, so the
// caller keeps its in-memory defaults. A present file is always parsed; parse
// errors are left for the caller to inspect on `cfg`.
bool ReadSettingsFile(const std::string& path, ParamFile& cfg);

// Writes `cfg` to `path`, creating any missing parent directories. Returns
// whether the file exists afterwards.
bool WriteSettingsFile(const std::string& path, const ParamFile& cfg);
} // namespace Poseidon
