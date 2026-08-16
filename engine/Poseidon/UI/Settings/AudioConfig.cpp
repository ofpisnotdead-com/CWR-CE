#include <Poseidon/UI/Settings/AudioConfig.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Settings/SettingsFile.hpp>

#include <algorithm>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

namespace
{
int ClampVolume(int v)
{
    return std::clamp(v, 0, 100);
}

bool DeviceListed(const std::string& name, const std::vector<std::string>& list)
{
    return std::find(list.begin(), list.end(), name) != list.end();
}
} // namespace

void AudioConfig::LoadDefaults()
{
    *this = AudioConfig{};
}

bool AudioConfig::Normalize(const Environment& env)
{
    bool changed = false;

    auto clampField = [&](int& v)
    {
        int c = ClampVolume(v);
        if (c != v)
        {
            v = c;
            changed = true;
        }
    };
    clampField(musicVolume);
    clampField(effectsVolume);
    clampField(speechVolume);

    // Device fields: empty string is always valid (system default).
    // A non-empty value must appear in the live device list.
    if (!outputDevice.empty() && !DeviceListed(outputDevice, env.ListOutputDevices()))
    {
        outputDevice.clear();
        changed = true;
    }
    if (!inputDevice.empty() && !DeviceListed(inputDevice, env.ListInputDevices()))
    {
        inputDevice.clear();
        changed = true;
    }

    return changed;
}

bool AudioConfig::Load(const std::string& path)
{
    ParamFile cfg;
    if (!ReadSettingsFile(path, cfg))
        return false;

    // Each field optional — partial files are tolerated, missing keys
    // keep the current in-memory value.  This makes forward-compat easy:
    // add a new field, default it in the class, old files still load.
    if (auto* e = cfg.FindEntry("musicVolume"))
        musicVolume = (int)*e;
    if (auto* e = cfg.FindEntry("effectsVolume"))
        effectsVolume = (int)*e;
    if (auto* e = cfg.FindEntry("speechVolume"))
        speechVolume = (int)*e;
    if (auto* e = cfg.FindEntry("eaxEnabled"))
        eaxEnabled = (bool)*e;
    if (auto* e = cfg.FindEntry("outputDevice"))
        outputDevice = ((RString)*e).Data();
    if (auto* e = cfg.FindEntry("inputDevice"))
        inputDevice = ((RString)*e).Data();

    return true;
}

bool AudioConfig::Save(const std::string& path) const
{
    ParamFile cfg;
    cfg.Add("musicVolume", musicVolume);
    cfg.Add("effectsVolume", effectsVolume);
    cfg.Add("speechVolume", speechVolume);
    cfg.Add("eaxEnabled", eaxEnabled);
    cfg.Add("outputDevice", RString(outputDevice.c_str()));
    cfg.Add("inputDevice", RString(inputDevice.c_str()));

    return WriteSettingsFile(path, cfg);
}

} // namespace Poseidon
