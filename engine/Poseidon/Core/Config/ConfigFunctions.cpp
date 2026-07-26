#include <Poseidon/Foundation/Common/Win.h>

// Include ai.hpp before chat/network to avoid incomplete AIUnit type
#include <Poseidon/AI/AI.hpp>

#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

using namespace Poseidon;
namespace Poseidon
{
RString GetUserParams();
}

// Forward declarations
namespace Poseidon::Foundation
{
class RString;
}
using Poseidon::Foundation::RString;
namespace Poseidon
{

// Game-side copy of difficulty descriptors with string IDs patched at runtime.
// ProfileLib owns the canonical data (GetDifficultyDescs()); this array adds
// localized string IDs that are only available after stringtable registration.
DifficultyDesc Config::diffDesc[DTN] = {
    DifficultyDesc("Armor", 0, true, false, false),        DifficultyDesc("FriendlyTag", 0, true, false, false),
    DifficultyDesc("EnemyTag", 0, false, false, false),    DifficultyDesc("HUD", 0, true, false, false),
    DifficultyDesc("AutoSpot", 0, true, false, false),     DifficultyDesc("Map", 0, true, false, false),
    DifficultyDesc("WeaponCursor", 0, true, true, true),   DifficultyDesc("AutoGuideAT", 0, true, false, false),
    DifficultyDesc("ClockIndicator", 0, true, true, true), DifficultyDesc("3rdPersonView", 0, true, true, true),
    DifficultyDesc("Tracers", 0, true, true, true),        DifficultyDesc("UltraAI", 0, false, false, true),
};

void Config::InitDifficulties()
{
    // Patch stringId fields now that IDS_* globals are initialized
    diffDesc[0].stringId = IDS_DIFF_ARMOR;
    diffDesc[1].stringId = IDS_DIFF_FRIENDLY_TAG;
    diffDesc[2].stringId = IDS_DIFF_ENEMY_TAG;
    diffDesc[3].stringId = IDS_DIFF_HUD;
    diffDesc[4].stringId = IDS_DIFF_AUTO_SPOT;
    diffDesc[5].stringId = IDS_DIFF_MAP;
    diffDesc[6].stringId = IDS_DIFF_WEAPON_CURSOR;
    diffDesc[7].stringId = IDS_DIFF_AUTO_GUIDE_AT;
    diffDesc[8].stringId = IDS_DIFF_CLOCK_INDICATOR;
    diffDesc[9].stringId = IDS_DIFF_3RD_PERSON_VIEW;
    diffDesc[10].stringId = IDS_DIFF_TRACERS;
    diffDesc[11].stringId = IDS_ULTRA_AI;

    GChatList.Enable(true);
}

bool Config::IsEnabled(DifficultyType type)
{
    const MissionHeader* header = GetNetworkManager().GetMissionHeader();
    if (header)
    {
        return header->difficulty[type]; // multiplayer
    }
    return USER_CONFIG.IsEnabled(type);
}

// Game-specific load/save that uses current player path
extern void UserConfig_LoadDifficulties(UserConfig& uc);
extern void UserConfig_SaveDifficulties(const UserConfig& uc);

void Config::LoadDifficulties()
{
    UserConfig_LoadDifficulties(USER_CONFIG);

    ParamFile userCfg;
    userCfg.Parse(::GetUserParams());
    const ParamEntry* entry = userCfg.FindEntry("showRadio");
    if (entry)
    {
        GChatList.Enable(*entry);
    }
}

void Config::SaveDifficulties()
{
    UserConfig_SaveDifficulties(USER_CONFIG);

    ParamFile userCfg;
    userCfg.Parse(::GetUserParams());
    userCfg.Add("showRadio", GChatList.Enabled());
    userCfg.Save(::GetUserParams());
}
} // namespace Poseidon
