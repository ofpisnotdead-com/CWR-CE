#include <Poseidon/Core/Profile/UserConfig.hpp>
#include <Poseidon/Core/SaveVersion.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{
namespace
{
constexpr float kDefaultFovLeft = 1.0f;
constexpr float kDefaultFovTop = 0.75f;

bool ValidFov(float left, float top)
{
    return std::isfinite(left) && std::isfinite(top) && left > 0.0f && top > 0.0f;
}

bool SameFov(float leftA, float topA, float leftB, float topB)
{
    constexpr float tolerance = 0.0001f;
    return std::abs(leftA - leftB) <= tolerance && std::abs(topA - topB) <= tolerance;
}
} // namespace

UserConfig::UserConfig()
{
    InitDefaults();
}

void UserConfig::InitDefaults()
{
    DifficultyDesc* descs = GetDifficultyDescs();
    for (int i = 0; i < DTN; i++)
    {
        cadetDifficulty[i] = descs[i].defaultCadet;
        veteranDifficulty[i] = descs[i].defaultVeteran;
    }
    easyMode = true;
    showTitles = true;
    SetAutomaticFov();
}

void UserConfig::LoadFromFile(const char* filepath)
{
    InitDefaults();

    ParamFile userCfg;
    LSError err = userCfg.Parse(filepath);
    if (err != LSOK)
        return;

    LoadFromParamFile(userCfg);
}

void UserConfig::SaveToFile(const char* filepath) const
{
    ParamFile userCfg;
    // Load existing to preserve other settings (identity, keybindings, etc.)
    userCfg.Parse(filepath);

    SaveToParamFile(userCfg);
    userCfg.Save(filepath);
}

void UserConfig::LoadFromParamFile(const ParamFile& cfg)
{
    DifficultyDesc* descs = GetDifficultyDescs();
    for (int i = 0; i < DTN; i++)
    {
        RString name = RString("diff") + RString(descs[i].name);
        const ParamEntry* cfgEntry = cfg.FindEntry(name);
        if (cfgEntry)
        {
            // Restore both modes unconditionally: SaveToParamFile writes cadet AND veteran for
            // every flag and the difficulty UI lets either mode be edited, so gating the veteran
            // read on enabledInVeteran silently drops persisted veteran values — e.g. "enemy info"
            // (EnemyTag, enabledInVeteran=false) enabled in veteran mode reverts to default on reload.
            cadetDifficulty[i] = (*cfgEntry)[0];
            veteranDifficulty[i] = (*cfgEntry)[1];
        }
    }

    const ParamEntry* entry = cfg.FindEntry("showTitles");
    if (entry)
        showTitles = *entry;

    entry = cfg.FindEntry("easyMode");
    if (entry)
        easyMode = *entry;

    _loadedVersion = 0;
    entry = cfg.FindEntry("version");
    if (entry)
        _loadedVersion = (int)*entry;

    const ParamEntry* topEntry = cfg.FindEntry("fovTop");
    const ParamEntry* leftEntry = cfg.FindEntry("fovLeft");
    const bool hasBoth = topEntry && leftEntry;
    const float loadedTop = topEntry ? (float)*topEntry : 0.0f;
    const float loadedLeft = leftEntry ? (float)*leftEntry : 0.0f;
    const bool validPair = hasBoth && ValidFov(loadedLeft, loadedTop);
    if (validPair)
    {
        fovTop = loadedTop;
        fovLeft = loadedLeft;
    }
    _customFov = validPair;

    if (_loadedVersion >= UserInfoVersion)
    {
        _fovMigrationPending = (topEntry || leftEntry) && !validPair;
    }
    else
    {
        _fovMigrationPending = true;
    }
}

void UserConfig::SaveToParamFile(ParamFile& cfg) const
{
    DifficultyDesc* descs = GetDifficultyDescs();
    for (int i = 0; i < DTN; i++)
    {
        RString name = RString("diff") + RString(descs[i].name);
        ParamEntry* entry = cfg.AddArray(name);
        entry->Clear();
        entry->AddValue(cadetDifficulty[i]);
        entry->AddValue(veteranDifficulty[i]);
    }
    cfg.Add("showTitles", showTitles);
    cfg.Add("easyMode", easyMode);
    cfg.Add("version", _fovMigrationPending ? _loadedVersion : UserInfoVersion);
    if (_customFov)
    {
        cfg.Add("fovTop", fovTop);
        cfg.Add("fovLeft", fovLeft);
    }
    else
    {
        cfg.Delete("fovTop");
        cfg.Delete("fovLeft");
    }
}

bool UserConfig::MigrateFov(float automaticLeft, float automaticTop)
{
    if (!_fovMigrationPending)
        return false;

    if (_loadedVersion < UserInfoVersion && _customFov)
    {
        _customFov = !SameFov(fovLeft, fovTop, kDefaultFovLeft, kDefaultFovTop) &&
                     !SameFov(fovLeft, fovTop, automaticLeft, automaticTop);
    }
    if (!_customFov)
    {
        SetAutomaticFov();
        return true;
    }
    _fovMigrationPending = false;
    _loadedVersion = UserInfoVersion;
    return true;
}

void UserConfig::SetCustomFov(float left, float top)
{
    if (!ValidFov(left, top))
    {
        SetAutomaticFov();
        return;
    }
    fovLeft = left;
    fovTop = top;
    _customFov = true;
    _fovMigrationPending = false;
    _loadedVersion = UserInfoVersion;
}

void UserConfig::SetAutomaticFov()
{
    fovLeft = kDefaultFovLeft;
    fovTop = kDefaultFovTop;
    _customFov = false;
    _fovMigrationPending = false;
    _loadedVersion = UserInfoVersion;
}

void UserConfig::InitDifficulties()
{
    DifficultyDesc* descs = GetDifficultyDescs();
    for (int i = 0; i < DTN; i++)
    {
        cadetDifficulty[i] = descs[i].defaultCadet;
        veteranDifficulty[i] = descs[i].defaultVeteran;
    }
    showTitles = true;
}

bool UserConfig::IsEnabled(DifficultyType type) const
{
    if (_serverDifficultyActive)
        return _serverDifficulty[type];

    if (easyMode)
        return cadetDifficulty[type];
    else
        return veteranDifficulty[type];
}

void UserConfig::SetServerDifficulty(const bool* flags)
{
    if (!flags)
    {
        ClearServerDifficulty();
        return;
    }
    for (int i = 0; i < DTN; i++)
        _serverDifficulty[i] = flags[i];
    _serverDifficultyActive = true;
}

void UserConfig::ClearServerDifficulty()
{
    _serverDifficultyActive = false;
}
} // namespace Poseidon
