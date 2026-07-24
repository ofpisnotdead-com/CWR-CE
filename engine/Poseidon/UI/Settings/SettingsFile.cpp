#include <Poseidon/UI/Settings/SettingsFile.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <Poseidon/Foundation/Strings/RString.hpp>

#include <filesystem>
#include <system_error>

namespace Poseidon
{

bool SettingsFileExists(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool ReadSettingsFile(const std::string& path, ParamFile& cfg)
{
    if (!SettingsFileExists(path))
        return false;
    cfg.Parse(RString(path.c_str()));
    return true;
}

bool WriteSettingsFile(const std::string& path, const ParamFile& cfg)
{
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);

    cfg.Save(RString(path.c_str()));
    return SettingsFileExists(path);
}

} // namespace Poseidon
