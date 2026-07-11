// Game-side UserConfig helpers; the core implementation lives in Profile/UserConfig.cpp.

#include <Poseidon/Core/Config/UserConfig.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{
RString GetUserParams();
}

namespace Poseidon
{

namespace
{
std::string DifficultyConfigPath()
{
    return GamePaths::Instance().UserDir() + "difficulty.cfg";
}
} // namespace

void UserConfig_LoadDifficulties(UserConfig& uc)
{
    uc.InitDifficulties();

    ParamFile userCfg;
    userCfg.Parse(::Poseidon::GetUserParams());

    const std::string path = DifficultyConfigPath();
    std::error_code ec;
    const bool hasDifficultyCfg = std::filesystem::exists(path, ec);
    ParamFile legacyCfg;
    if (hasDifficultyCfg)
        legacyCfg.Parse(RString(path.c_str()));

    DifficultyDesc* descs = GetDifficultyDescs();
    for (int i = 0; i < DTN; i++)
    {
        RString name = RString("diff") + RString(descs[i].name);
        const ParamEntry* cfg = userCfg.FindEntry(name);
        if (!cfg && hasDifficultyCfg)
            cfg = legacyCfg.FindEntry(name);
        if (cfg)
        {
            // Restore both modes unconditionally — SaveDifficulties writes cadet AND veteran for
            // every flag and the difficulty UI lets either mode be edited. Gating the veteran read
            // on enabledInVeteran silently drops persisted veteran values (e.g. "enemy info" enabled
            // in veteran mode reverts to default-off on restart).
            uc.cadetDifficulty[i] = (*cfg)[0];
            uc.veteranDifficulty[i] = (*cfg)[1];
        }
    }

    const ParamEntry* entry = userCfg.FindEntry("showTitles");
    if (!entry && hasDifficultyCfg)
        entry = legacyCfg.FindEntry("showTitles");
    if (entry)
        uc.showTitles = *entry;

    entry = userCfg.FindEntry("easyMode");
    if (!entry && hasDifficultyCfg)
        entry = legacyCfg.FindEntry("easyMode");
    if (entry)
        uc.easyMode = *entry;
}

void UserConfig_SaveDifficulties(const UserConfig& uc)
{
    ParamFile userCfg;
    const RString path = ::Poseidon::GetUserParams();
    userCfg.Parse(path);

    DifficultyDesc* descs = GetDifficultyDescs();
    for (int i = 0; i < DTN; i++)
    {
        RString name = RString("diff") + RString(descs[i].name);
        ParamEntry* entry = userCfg.AddArray(name);
        entry->Clear();
        entry->AddValue(uc.cadetDifficulty[i]);
        entry->AddValue(uc.veteranDifficulty[i]);
    }
    userCfg.Add("showTitles", uc.showTitles);
    userCfg.Add("easyMode", uc.easyMode);

    if (userCfg.Save(path) != LSOK)
        LOG_WARN(Config, "DifficultyConfig: failed to write '{}'", (const char*)path);
}
} // namespace Poseidon
