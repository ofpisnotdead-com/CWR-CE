// Game-side UserConfig helpers; the core implementation lives in Profile/UserConfig.cpp.

#include <Poseidon/Core/Config/UserConfig.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>

#include <fstream>
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
    const std::string path = DifficultyConfigPath();
    std::error_code ec;
    if (std::filesystem::exists(FilesystemPathFromUtf8(path), ec))
        userCfg.Parse(RString(path.c_str()));
    else
        userCfg.Parse(::Poseidon::GetUserParams());

    DifficultyDesc* descs = GetDifficultyDescs();
    for (int i = 0; i < DTN; i++)
    {
        RString name = RString("diff") + RString(descs[i].name);
        const ParamEntry* cfg = userCfg.FindEntry(name);
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
    if (entry)
        uc.showTitles = *entry;
}

void UserConfig_SaveDifficulties(const UserConfig& uc)
{
    const std::string path = DifficultyConfigPath();
    std::ofstream out(FilesystemPathFromUtf8(path), std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        LOG_WARN(Config, "DifficultyConfig: failed to open '{}' for writing", path);
        return;
    }

    DifficultyDesc* descs = GetDifficultyDescs();
    for (int i = 0; i < DTN; i++)
        out << "diff" << descs[i].name << "[]={" << static_cast<int>(uc.cadetDifficulty[i]) << ","
            << static_cast<int>(uc.veteranDifficulty[i]) << "};\n";
    out << "showTitles=" << static_cast<int>(uc.showTitles) << ";\n";

    if (!out.good())
        LOG_WARN(Config, "DifficultyConfig: failed to write '{}'", path);
}
} // namespace Poseidon
