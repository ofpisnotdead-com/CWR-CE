#include <Poseidon/Core/Profile/UserConfig.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <cstdio>
#include <cstring>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

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
    fovTop = 0.75f;
    fovLeft = 1.0f;
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

    entry = cfg.FindEntry("fovTop");
    if (entry)
        fovTop = (float)*entry;

    entry = cfg.FindEntry("fovLeft");
    if (entry)
        fovLeft = (float)*entry;
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
    cfg.Add("fovTop", fovTop);
    cfg.Add("fovLeft", fovLeft);
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
