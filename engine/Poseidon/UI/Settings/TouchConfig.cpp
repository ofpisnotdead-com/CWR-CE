#include <Poseidon/UI/Settings/TouchConfig.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

namespace
{
constexpr float kMinSensitivity = 0.25f;
constexpr float kMaxSensitivity = 3.0f;
} // namespace

void TouchConfig::LoadDefaults()
{
    *this = TouchConfig{};
}

bool TouchConfig::Normalize()
{
    bool changed = false;
    auto clamp = [&](float& v)
    {
        float c = std::clamp(v, kMinSensitivity, kMaxSensitivity);
        if (c != v)
        {
            v = c;
            changed = true;
        }
    };
    clamp(aimSensitivity);
    clamp(cursorSensitivity);
    if (displayMode < 0 || displayMode > 2)
    {
        displayMode = 0;
        changed = true;
    }
    return changed;
}

bool TouchConfig::Load(const std::string& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;

    ParamFile cfg;
    cfg.Parse(RString(path.c_str()));

    if (auto* e = cfg.FindEntry("aimSensitivity"))
        aimSensitivity = (float)*e;
    if (auto* e = cfg.FindEntry("cursorSensitivity"))
        cursorSensitivity = (float)*e;
    if (auto* e = cfg.FindEntry("displayMode"))
        displayMode = (int)*e;

    return true;
}

bool TouchConfig::Save(const std::string& path) const
{
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);

    ParamFile cfg;
    cfg.Add("aimSensitivity", aimSensitivity);
    cfg.Add("cursorSensitivity", cursorSensitivity);
    cfg.Add("displayMode", displayMode);

    cfg.Save(RString(path.c_str()));
    return std::filesystem::exists(path, ec);
}

} // namespace Poseidon
