
#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>

#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>

#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Core/Progress.hpp>

#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/Core/SaveVersion.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <Random/randomGen.hpp>

#include <SDL3/SDL_keycode.h>

#include <Poseidon/Game/Scripting/Scripts.hpp>

#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Common/PlayerPrefs.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/UI/GameModule.hpp>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <optional>
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/Audio/VoiceLangPath.hpp>
#include <Poseidon/UI/Locale/MissionLanguageDetector.hpp>
#include <Poseidon/UI/DisplayUI.hpp>

#include <Poseidon/UI/OptionsUICommon.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/Languages.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <filesystem>

using namespace Poseidon;
extern bool AutoTest; // Synced from AppConfig in appConfig.cpp (global)

namespace Poseidon
{

void FormatCurrency(char* buffer, float res)
{
    if (res <= 0)
    {
        snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_CURRENCY_NONE));
    }
    else
    {
        if (res >= 9.9e9)
        {
            snprintf(buffer, sizeof(buffer), "%s", (const char*)LocalizeString(IDS_CURRENCY_LOT));
        }
        else
        {
            char temp[128];
            snprintf(buffer, sizeof(buffer), "%.0f", res);
            int digits = strlen(buffer);
            int i = 0;
            int j = 0;
            temp[i++] = buffer[j++];
            digits--;
            while (digits > 0)
            {
                if ((digits % 3) == 0)
                {
                    temp[i++] = LocalizeString(IDS_CURRENCY_BLANK)[0];
                }
                temp[i++] = buffer[j++];
                digits--;
            }
            temp[i++] = buffer[j++];
            digits--;
            temp[i++] = 0;
            snprintf(buffer, sizeof(buffer), (const char*)LocalizeString(IDS_CURRENCY_FORMAT), temp);
        }
    }
}

RString GetUserDirectory()
{
    std::string dir =
        ProfileManager::GetProfileDirPath(GamePaths::Instance().UserDir(), std::string(Glob.header.playerName));
    std::filesystem::create_directories(dir);
    return dir.c_str();
}

RString GetUserMissionsBase()
{
    if (GamePaths::Instance().OldPaths())
    {
        return GetUserDirectory();
    }

    // Editor (user-authored) missions are shared content like mods — not tied to
    // a player profile — so they live under the discoverable, non-roaming
    // user-content root (Documents on Windows), with Missions/ + MPMissions/
    // subfolders, rather than under the (roaming) per-profile UserDir.
    return GamePaths::Instance().UserContentDir().c_str();
}

RString GetUserParams()
{
    return GetUserDirectory() + RString("UserInfo.cfg");
}

RString GetCampaignDirectory(RString campaign)
{
    PoseidonAssert(campaign.GetLength() > 0);
    return RString("campaigns/") + campaign + RString("/");
}

RString GetCampaignMissionDirectory(RString campaign, RString mission)
{
    PoseidonAssert(campaign.GetLength() > 0);
    PoseidonAssert(mission.GetLength() > 0);
    return RString("campaigns/") + campaign + RString("/missions/") + mission + RString("/");
}

static RString BaseDirectory;

static RString BaseSubdirectory;
static std::optional<float> CurrentMissionViewDistance;

void UpdateCurrentMissionViewDistance()
{
    CurrentMissionViewDistance = MissionLanguageDetector::DetectPreview(GetMissionDirectory()).missionViewDistance;
}

void ApplyCurrentMissionViewDistance()
{
    if (!GScene)
        return;

    GScene->SetPreferredViewDistance(ResolveEffectiveViewDistance(
        GetSelectedPreferredViewDistance(), GetRespectMissionViewDistance(), CurrentMissionViewDistance));
}

RString GetBaseDirectory()
{
    return BaseDirectory;
}

RString GetBaseSubdirectory()
{
    return BaseSubdirectory;
}

RString GetMissionsDirectory()
{
    return BaseDirectory + BaseSubdirectory;
}

RString GetMissionDirectory()
{
    return GetMissionsDirectory() + RString(Glob.header.filename) + RString(".") + RString(Glob.header.worldname) +
           RString("/");
}

RString GetBriefingFile()
{
    return FindLocalizedMissionHtmlFile(GetMissionDirectory(), "briefing");
}

void CreatePath(RString path)
{
    // string will be changed temporarily
    char* start = (char*)path.Data();
    char* end = start;
    while (*end)
    {
        if (*end == '\\' || *end == '/')
        {
            char saved = *end;
            *end = 0;
            ::CreateDirectory(path, nullptr);
            *end = saved;
        }
        end++;
    }
}

static std::string NormalizeSlashes(std::string path)
{
    for (char& ch : path)
    {
        if (ch == '\\')
            ch = '/';
    }
    return path;
}

static RString GetSaveBaseDirectory()
{
    RString base = GetBaseDirectory();
    if (base.GetLength() == 0)
    {
        return base;
    }

    std::string basePath = NormalizeSlashes((const char*)base);
    std::string contentRoot = NormalizeSlashes(GamePaths::Instance().UserContentDir());
    if (!contentRoot.empty() && contentRoot.back() != '/')
    {
        contentRoot += '/';
    }

    if (!contentRoot.empty() && basePath.rfind(contentRoot, 0) == 0)
    {
        return RString(basePath.c_str() + contentRoot.size());
    }

    return base;
}

RString GetSaveDirectory()
{
    /*
        RString dir = GetUserDirectory() + RString("Saved");
        mkdir(dir, nullptr);
        return dir;
    */
    RString dir = GetUserDirectory() + RString("Saved/") + GetSaveBaseDirectory();
    if (!IsCampaign())
    {
        dir = dir + GetBaseSubdirectory() + RString(Glob.header.filenameReal) + RString(".") +
              RString(Glob.header.worldname) + RString("\\");
    }
    CreatePath(dir);
    return dir;
}

RString GetTmpSaveDirectory()
{
    RString dir = GetUserDirectory() + RString("Saved/Tmp/");
    CreatePath(dir);
    return dir;
}

RString GetCampaignSaveDirectory(RString campaign)
{
    if (campaign.GetLength() == 0)
    {
    }
    RString dir = GetUserDirectory() + RString("Saved/campaigns/") + campaign + RString("/");
    CreatePath(dir);
    return dir;
}

RString GetMissionSaveDirectory(RString mission)
{
    RString dir = GetUserDirectory() + RString("Saved/missions/") + mission + RString("/");
    CreatePath(dir);
    return dir;
}

void FindEnvSound(RString name, SoundPars& day, SoundPars& night)
{
    // ignore @ - not used now
    if (name[0] == '@')
    {
        name = (const char*)name + 1;
    }

    // find in mission
    const ParamEntry* cls = ExtParsMission.FindEntry("CfgEnvSounds");
    const ParamEntry* entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(day, (*entry) >> "sound");
        if (day.name.GetLength() > 0)
        {
            day.name = GetMissionDirectory() + day.name;
        }
        GetValue(night, (*entry) >> "soundNight");
        if (night.name.GetLength() > 0)
        {
            night.name = GetMissionDirectory() + night.name;
        }
        return;
    }

    // find in campaign
    cls = ExtParsCampaign.FindEntry("CfgEnvSounds");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(day, (*entry) >> "sound");
        if (day.name.GetLength() > 0)
        {
            day.name = BaseDirectory + RString("dtaExt\\") + day.name;
        }
        GetValue(night, (*entry) >> "soundNight");
        if (night.name.GetLength() > 0)
        {
            night.name = BaseDirectory + RString("dtaExt\\") + night.name;
        }
        return;
    }

    // find in config
    cls = Pars.FindEntry("CfgEnvSounds");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(day, (*entry) >> "sound");
        GetValue(night, (*entry) >> "soundNight");
        return;
    }

    Poseidon::Foundation::WarningMessage("Environmental sound %s not found", (const char*)name);
}

void FindSFX(RString name, SoundEntry& emptySound, AutoArray<SoundEntry>& sounds)
{
    // ignore @ - not used now
    if (name[0] == '@')
    {
        name = (const char*)name + 1;
    }

    // find in mission
    const ParamEntry* cls = ExtParsMission.FindEntry("CfgSFX");
    const ParamEntry* entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        // load empty sound
        emptySound = DynSound::LoadEntry((*entry) >> "empty");
        if (emptySound.name.GetLength() > 0)
        {
            emptySound.name = GetMissionDirectory() + emptySound.name;
        }
        // load sound scheme
        const ParamEntry& list = (*entry) >> "sounds";
        int n = list.GetSize();
        sounds.Resize(n);
        for (int i = 0; i < n; i++)
        {
            RString name = list[i];
            SoundEntry& sound = sounds[i];
            sound = DynSound::LoadEntry((*entry) >> name);
            if (sound.name.GetLength() > 0)
            {
                sound.name = GetMissionDirectory() + sound.name;
            }
        }
        // return sound location
        return; // ExtParsMission.GetName();
    }

    // find in campaign
    cls = ExtParsCampaign.FindEntry("CfgSFX");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        // load empty sound
        emptySound = DynSound::LoadEntry((*entry) >> "empty");
        if (emptySound.name.GetLength() > 0)
        {
            emptySound.name = BaseDirectory + RString("dtaExt\\") + emptySound.name;
        }
        // load sound scheme
        const ParamEntry& list = (*entry) >> "sounds";
        int n = list.GetSize();
        sounds.Resize(n);
        for (int i = 0; i < n; i++)
        {
            RString name = list[i];
            SoundEntry& sound = sounds[i];
            sound = DynSound::LoadEntry((*entry) >> name);
            if (sound.name.GetLength() > 0)
            {
                sound.name = BaseDirectory + RString("dtaExt\\") + sound.name;
            }
        }
        // return sound location
        return; // ExtParsCampaign.GetName();
    }

    // find in config
    cls = Pars.FindEntry("CfgSFX");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        // load empty sound
        emptySound = DynSound::LoadEntry((*entry) >> "empty");
        // load sound scheme
        const ParamEntry& list = (*entry) >> "sounds";
        int n = list.GetSize();
        sounds.Resize(n);
        for (int i = 0; i < n; i++)
        {
            RString name = list[i];
            SoundEntry& sound = sounds[i];
            sound = DynSound::LoadEntry((*entry) >> name);
        }
        return; // Pars.GetName();
    }

    Poseidon::Foundation::WarningMessage("SFX %s not found", (const char*)name);
    // "";
}

static void PreferExistingVoiceLanguageOverride(SoundPars& pars)
{
    if (pars.name.GetLength() <= 0)
        return;

    RString candidate = WithLangSuffix(pars.name, RString(GetSelectedVoiceLanguage().c_str()));
    if (candidate.GetLength() > 0 && QIFStreamB::FileExist(candidate))
        pars.name = candidate;
}

const ParamEntry* FindSound(RString name, SoundPars& pars)
{
    // ignore @ - not used now
    if (name[0] == '@')
    {
        name = (const char*)name + 1;
    }

    // find in mission
    const ParamEntry* cls = ExtParsMission.FindEntry("CfgSounds");
    const ParamEntry* entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(pars, (*entry) >> "sound");
        if (pars.name.GetLength() > 0)
        {
            pars.name = GetMissionDirectory() + pars.name;
            // Per-language voice override: prefer "<base>.<voiceLang>.<ext>" if it exists.
            // Resolved at play time so a runtime voice-language switch picks up the new
            // file on the next say/playSound — currently-playing audio is unaffected.
            PreferExistingVoiceLanguageOverride(pars);
        }
        return entry;
    }

    // find in campaign
    cls = ExtParsCampaign.FindEntry("CfgSounds");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(pars, (*entry) >> "sound");
        if (pars.name.GetLength() > 0)
        {
            pars.name = BaseDirectory + RString("dtaExt\\") + pars.name;
            PreferExistingVoiceLanguageOverride(pars);
        }
        return entry;
    }

    // find in config
    cls = Pars.FindEntry("CfgSounds");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(pars, (*entry) >> "sound");
        return entry;
    }

    // Single point of truth for an unresolvable sound key: report it here, where
    // the logical name is still in hand, then let callers play nothing. (Routed
    // through LOG_WARN rather than WarningMessage — the latter is a no-op in the
    // shipping app frame, which is why a bad `say`/sound surfaced only as the
    // file server's context-free "Empty or nullptr name not allowed".)
    LOG_WARN(Audio, "Sound '{}' not found in any CfgSounds — nothing will play", (const char*)name);
    return nullptr;
}

const ParamEntry* FindMusic(RString name, SoundPars& pars)
{
    // find in mission
    const ParamEntry* cls = ExtParsMission.FindEntry("CfgMusic");
    const ParamEntry* entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(pars, (*entry) >> "sound");
        if (pars.name.GetLength() > 0)
        {
            pars.name = GetMissionDirectory() + pars.name;
        }
        return entry;
    }

    // find in campaign
    cls = ExtParsCampaign.FindEntry("CfgMusic");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(pars, (*entry) >> "sound");
        if (pars.name.GetLength() > 0)
        {
            pars.name = BaseDirectory + RString("dtaExt\\") + pars.name;
        }
        return entry;
    }

    // find in config
    cls = Pars.FindEntry("CfgMusic");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(pars, (*entry) >> "sound");
        return entry;
    }

    Poseidon::Foundation::WarningMessage("Music %s not found", (const char*)name);
    return nullptr;
}

const ParamEntry* FindRadio(RString name, SoundPars& pars)
{
    // find in mission
    const ParamEntry* cls = ExtParsMission.FindEntry("CfgRadio");
    const ParamEntry* entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(pars, (*entry) >> "sound");
        if (pars.name.GetLength() > 0)
        {
            pars.name = GetMissionDirectory() + pars.name;
            PreferExistingVoiceLanguageOverride(pars);
        }
        return entry;
    }

    // find in campaign
    cls = ExtParsCampaign.FindEntry("CfgRadio");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(pars, (*entry) >> "sound");
        if (pars.name.GetLength() > 0)
        {
            pars.name = BaseDirectory + RString("dtaExt\\") + pars.name;
            PreferExistingVoiceLanguageOverride(pars);
        }
        return entry;
    }

    // find in config
    cls = Pars.FindEntry("CfgRadio");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        GetValue(pars, (*entry) >> "sound");
        return entry;
    }

    Poseidon::Foundation::WarningMessage("Radio message %s not found", (const char*)name);
    return nullptr;
}

} // namespace Poseidon
namespace Poseidon
{
const ParamEntry* FindCameraEffect(RString name)
{
    // ignore @ - not used now
    if (name[0] == '@')
    {
        name = (const char*)name + 1;
    }

    // find in mission
    const ParamEntry* cls = ExtParsMission.FindEntry("CfgCameraEffects");
    const ParamEntry* array = cls ? cls->FindEntry("Array") : nullptr;
    const ParamEntry* entry = array ? array->FindEntry(name) : nullptr;
    if (entry)
    {
        return entry;
    }

    // find in campaign
    cls = ExtParsCampaign.FindEntry("CfgCameraEffects");
    array = cls ? cls->FindEntry("Array") : nullptr;
    entry = array ? array->FindEntry(name) : nullptr;
    if (entry)
    {
        return entry;
    }

    // find in config
    cls = Pars.FindEntry("CfgCameraEffects");
    array = cls ? cls->FindEntry("Array") : nullptr;
    entry = array ? array->FindEntry(name) : nullptr;
    if (entry)
    {
        return entry;
    }

    Poseidon::Foundation::WarningMessage("Camera effect %s not found", (const char*)name);
    return nullptr;
}
} // namespace Poseidon
namespace Poseidon
{

RString FindScript(RString name)
{
    // find in mission
    RString fullname = GetMissionDirectory() + name;
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    // find in campaign
    fullname = BaseDirectory + RString("scripts\\") + name;
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    // find in root
    fullname = RString("scripts\\") + name;
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    Poseidon::Foundation::WarningMessage("Script %s not found", (const char*)name);
    return "";
}

RString FindShape(RString name)
{
    // ignore @ - not used now
    if (name[0] == '@')
    {
        name = (const char*)name + 1;
    }

    // find in mission
    RString fullname = GetMissionDirectory() + name;
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    // find in campaign
    fullname = BaseDirectory + RString("dtaExt\\") + name;
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    // find in root
    fullname = RString("dtaExt\\") + name;
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    // find in bank
    fullname = GetShapeName(name);
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    Poseidon::Foundation::WarningMessage("Shape %s not found", (const char*)name);
    return "";
}

RString FindPicture(RString name)
{
    // name "" used for nullptr texture
    if (name.GetLength() == 0)
    {
        return name;
    }

    // ignore @ - not used now
    if (name[0] == '@')
    {
        name = (const char*)name + 1;
    }

    name.Lower();

    // find in mission
    RString fullname = GetMissionDirectory() + name;
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    // find in campaign
    fullname = BaseDirectory + RString("dtaExt\\") + name;
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    // find in root
    fullname = RString("dtaExt\\") + name;
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    // find in bank
    fullname = GetPictureName(name);
    if (QIFStreamB::FileExist(fullname))
    {
        return fullname;
    }

    Poseidon::Foundation::WarningMessage("Picture %s not found", (const char*)name);
    return "";
}

const ParamEntry* FindRscTitle(RString name)
{
    // find in mission
    const ParamEntry* cls = ExtParsMission.FindEntry("RscTitles");
    const ParamEntry* entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        return entry;
    }

    // find in campaign
    cls = ExtParsCampaign.FindEntry("RscTitles");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        return entry;
    }

    // find in resource
    cls = Res.FindEntry("RscTitles");
    entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        return entry;
    }

    Poseidon::Foundation::WarningMessage("Resource title %s not found", (const char*)name);
    return nullptr;
}

// campaign description.ext

void SetBaseDirectory(RString dir)
{
    BaseDirectory = dir;
    ExtParsCampaign.Clear();
    if (BaseDirectory.GetLength() > 0)
    {
        // campaign stringtable
        RString filename = BaseDirectory + RString("stringtable.csv");
        LoadStringtable("Campaign", filename, 1, true);
        // campaign description
        filename = BaseDirectory + RString("description.ext");
        if (QIFStreamB::FileExist(filename))
        {
            ExtParsCampaign.Parse(filename);
        }
    }
}

void SetBaseSubdirectory(RString dir)
{
    BaseSubdirectory = dir;
}

void SetCampaign(RString name)
{
    CurrentCampaign = name;
    if (name.GetLength() == 0)
    {
        SetBaseDirectory("");
    }
    else
    {
        SetBaseDirectory(GetCampaignDirectory(name));
    }
}

// mission description.ext

RString& GetMPMissionsDir()
{
    static RString normalDir(GameDirs::MPMissionsPath().c_str());
    static RString simulateDir;

    if (AppConfig::Instance().IsSimulateMode())
    {
        if (simulateDir.GetLength() == 0)
        {
            std::string path = GamePaths::Instance().TempDir() + GameDirs::MPMissionsPath();
            simulateDir = path.c_str();
        }
        return simulateDir;
    }
    return normalDir;
}

RString& GetAnimsDir()
{
    static RString AnimsDir("anims/");
    return AnimsDir;
}

RString& GetMissionsDir()
{
    static RString MissionsDir("missions/");
    return MissionsDir;
}

void SetMission(RString world, RString mission, RString subdir)
{
    strcpy(Glob.header.worldname, world);
    strcpy(Glob.header.filename, mission);
    Glob.header.filenameReal = mission;

    BaseSubdirectory = subdir;
    ExtParsMission.Clear();
    // mission stringtable
    RString filename = GetMissionDirectory() + RString("stringtable.csv");
    LoadStringtable("Mission", filename, 2, true);
    /*
        MissionStringTable.Clear();
        if (QIFStreamB::FileExist(filename))
            MissionStringTable.Load(filename);
    */
    // mission description
    filename = GetMissionDirectory() + RString("description.ext");
    if (QIFStreamB::FileExist(filename))
    {
        ExtParsMission.Parse(filename);
    }

    UpdateCurrentMissionViewDistance();
}

void SetMission(RString world, RString mission)
{
    SetMission(world, mission, GetMissionsDir());
}

// single mission bank

} // namespace Poseidon
extern bool EnumModDirectories(ModDirectoryCallback callback, void* context);
namespace Poseidon
{

struct FindMissionPboContext
{
    const char* relPath; // e.g. "Missions\\01TakeTheCar.ABEL"
    RString result;      // full path if found in a mod dir
};

// Try mod directories first for mission PBOs (mirrors campaign bank loading)
static bool FindMissionPboCallback(RStringB dir, void* ctx)
{
    auto* c = static_cast<FindMissionPboContext*>(ctx);
    if (dir.GetLength() == 0)
        return false; // skip base dir — that's the fallback
    char path[1024];
    // Try language-specific PBO first (e.g., mission.cz.pbo for Czech)
    const char* langSuffix = GetLanguagePboSuffix(GLanguage);
    if (langSuffix)
    {
        snprintf(path, sizeof(path), "%s\\%s.%s.pbo", (const char*)dir, c->relPath, langSuffix);
        if (FilePathExists(path))
        {
            snprintf(path, sizeof(path), "%s\\%s.%s", (const char*)dir, c->relPath, langSuffix);
            c->result = path;
            return true;
        }
    }
    snprintf(path, sizeof(path), "%s\\%s.pbo", (const char*)dir, c->relPath);
    if (FilePathExists(path))
    {
        // strip .pbo — QFBank::open appends it
        snprintf(path, sizeof(path), "%s\\%s", (const char*)dir, c->relPath);
        c->result = path;
        return true;
    }
    return false;
}

RString CreateSingleMissionBank(RString filename)
{
    // suppose filename is without extension (.pbo)

    // remove bank
    const char* prefix = "missions\\__cur_sp.";
    int prefixLen = strlen(prefix);
    for (int i = 0; i < GFileBanks.Size();)
    {
        QFBank& bank = GFileBanks[i];
        if (strnicmp(bank.GetPrefix(), prefix, prefixLen) == 0)
        {
            GFileBanks.Delete(i);
        }
        else
        {
            i++;
        }
    }

    // extract island name
    const char* ext = strrchr(filename, '.');
    if (!ext)
    {
        return "";
    }
    RString island = ext + 1;

    // prefer mod directory PBO over base game
    FindMissionPboContext ctx;
    ctx.relPath = filename;
    RString openPath = filename;
    if (::EnumModDirectories(FindMissionPboCallback, &ctx))
    {
        openPath = ctx.result;
    }

    // create bank
    int index = GFileBanks.Add();
    QFBank& bank = GFileBanks[index];
    bank.open(openPath);
    RString str = RString(prefix) + island + RString("\\");
    str.Lower();
    bank.SetPrefix(str);
    return str;
}

// Interface

LSError CountedString::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("name", name, 1))
    PARAM_CHECK(ar.Serialize("count", count, 1))
    return LSOK;
}

LSError MissionHistory::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("missionName", missionName, 1))
    PARAM_CHECK(ar.Serialize("displayName", displayName, 1))
    PARAM_CHECK(ar.Serialize("completed", completed, 1))
    if (ar.GetArVersion() >= 2)
    {
        ParamArchive arSubcls;
        if (!ar.OpenSubclass("Stats", arSubcls))
        {
            return LSStructure;
        }
        stats.CampaignSerialize(arSubcls);
    }
    else if (ar.IsLoading())
    {
        stats.Clear();
    }
    PARAM_CHECK(ar.Serialize("Weapons", weapons, 1))
    PARAM_CHECK(ar.Serialize("Magazines", magazines, 1))
    PARAM_CHECK(ar.SerializeArray("Dead", dead, 1))
    return LSOK;
}

LSError BattleHistory::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("battleName", battleName, 1))
    PARAM_CHECK(ar.Serialize("Missions", missions, 1))
    return LSOK;
}

void CampaignHistory::AddMission(RString campaign, RString battle, RString mission, RString displayName)
{
    if (!campaign || campaign.GetLength() <= 0)
    {
        return;
    }
    if (stricmp(campaign, campaignName) != 0)
    {
        return;
    }
    int i = battles.Size() - 1;
    if (i < 0 || stricmp(battles[i].battleName, battle) != 0)
    {
        i = battles.Add();
        battles[i].battleName = battle;
    }
    BattleHistory& bh = battles[i];

    // check if mission already added
    i = bh.missions.Size() - 1;
    if (i >= 0 && mission == bh.missions[i].missionName)
    {
        return;
    }

    i = bh.missions.Add();
    bh.missions[i].missionName = mission;
    bh.missions[i].displayName = displayName;
    bh.missions[i].stats = GStats._campaign;

    // LOG_DEBUG(UI, "Campaign: Mission {}:{}:{} added", (const char *)campaign, (const char *)battle, (const char
    // *)mission);
}

/*
void CampaignHistory::SetDisplayName(RString campaign, RString battle, RString mission, RString displayName)
{
    if (!campaign || campaign.GetLength() <= 0) return;
    if (stricmp(campaign, campaignName) != 0) return;
    int i = battles.Size() - 1;
    if (i < 0) return;
    BattleHistory &bh = battles[i];
    if (stricmp(bh.battleName, battle) != 0) return;
    i = bh.missions.Size() - 1;
    if (i < 0) return;
    MissionHistory &mh = bh.missions[i];
    if (stricmp(mh.missionName, mission) != 0) return;
    mh.displayName = displayName;
}
*/

MissionHistory* CampaignHistory::CurrentMission()
{
    int i = battles.Size() - 1;
    if (i < 0)
    {
        return nullptr;
    }
    BattleHistory& bh = battles[i];
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        Fail("Empty battle");
        return nullptr;
    }
    return &bh.missions[i];
}

void CampaignHistory::MissionCompleted(RString campaign, RString battle, RString mission)
{
    if (!campaign || campaign.GetLength() <= 0)
    {
        return;
    }
    if (stricmp(campaign, campaignName) != 0)
    {
        return;
    }
    int i = battles.Size() - 1;
    if (i < 0)
    {
        return;
    }
    BattleHistory& bh = battles[i];
    if (stricmp(bh.battleName, battle) != 0)
    {
        return;
    }
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        return;
    }
    MissionHistory& mh = bh.missions[i];
    if (stricmp(mh.missionName, mission) != 0)
    {
        return;
    }
    mh.completed = true;
    // LOG_DEBUG(UI, "Campaign: Mission {}:{}:{} completed", (const char *)campaign, (const char *)battle, (const char
    // *)mission);
}

LSError CampaignHistory::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("campaignName", campaignName, 1))
    PARAM_CHECK(ar.Serialize("Battles", battles, 1))
    return LSOK;
}

void CampaignHistory::LoadWeaponPool(WeaponsInfo& pool)
{
    MissionHistory* mh = CurrentMission();
    RptF("Campaign: Loading weapon pool of mission %s", mh ? (const char*)mh->missionName : "<none>");
    if (!mh)
    {
        return;
    }

    for (int i = 0; i < mh->weapons.Size(); i++)
    {
        Ref<WeaponType> weapon = WeaponTypes.New(mh->weapons[i].name);
        if (!weapon)
        {
            continue;
        }
        int count = mh->weapons[i].count;
        for (int j = 0; j < count; j++)
        {
            pool._weaponsPool.Add(weapon);
        }
    }
    for (int i = 0; i < mh->magazines.Size(); i++)
    {
        Ref<MagazineType> type = MagazineTypes.New(mh->magazines[i].name);
        if (!type)
        {
            continue;
        }
        int count = mh->magazines[i].count;
        for (int j = 0; j < count; j++)
        {
            Ref<Magazine> magazine = new Magazine(type);
            magazine->_ammo = type->_maxAmmo;
            magazine->_reloadMagazine = 0;
            magazine->_reload = 0;
            if (type->_modes.Size() > 0)
            {
                float reload = 0.8 + 0.2 * GRandGen.RandomValue();
                magazine->_reload = reload * type->_modes[0]->_reloadTime;
            }
            pool._magazinesPool.Add(magazine);
        }
    }
}

void MissionHistory::AddWeapons(RString name, int count)
{
    for (int j = 0; j < weapons.Size(); j++)
    {
        if (stricmp(weapons[j].name, name) == 0)
        {
            weapons[j].count += count;
            if (weapons[j].count <= 0)
            {
                weapons.Delete(j);
            }
            return;
        }
    }
    if (count <= 0)
    {
        return;
    }
    int index = weapons.Add();
    weapons[index].name = name;
    weapons[index].count = count;
}

void MissionHistory::AddMagazines(RString name, int count)
{
    for (int j = 0; j < magazines.Size(); j++)
    {
        if (stricmp(magazines[j].name, name) == 0)
        {
            magazines[j].count += count;
            if (magazines[j].count <= 0)
            {
                magazines.Delete(j);
            }
            return;
        }
    }
    if (count <= 0)
    {
        return;
    }
    int index = magazines.Add();
    magazines[index].name = name;
    magazines[index].count = count;
}

void CampaignHistory::SaveWeaponPool(WeaponsInfo& pool)
{
    MissionHistory* mh = CurrentMission();
    if (!mh)
    {
        return;
    }

    mh->weapons.Clear();
    for (int i = 0; i < pool._weaponsPool.Size(); i++)
    {
        RString name = pool._weaponsPool[i]->GetName();
        mh->AddWeapons(name, 1);
    }

    mh->magazines.Clear();
    for (int i = 0; i < pool._magazinesPool.Size(); i++)
    {
        RString name = pool._magazinesPool[i]->_type->GetName();
        mh->AddMagazines(name, 1);
    }
}

CampaignHistory& GetGCampaignHistory()
{
    static CampaignHistory GCampaignHistory;
    return GCampaignHistory;
}

void CampaignSaveWeaponPool(WeaponsInfo& pool)
{
    GetGCampaignHistory().SaveWeaponPool(pool);
}

void CampaignLoadWeaponPool(WeaponsInfo& pool)
{
    GetGCampaignHistory().LoadWeaponPool(pool);
}

bool IsIdentityDead(RString identity)
{
    MissionHistory* mh = GetGCampaignHistory().CurrentMission();
    if (!mh)
    {
        return false;
    }

    for (int i = 0; i < mh->dead.Size(); i++)
    {
        if (mh->dead[i] == identity)
        {
            return true;
        }
    }
    return false;
}

void EmptyDeadIdentities()
{
    MissionHistory* mh = GetGCampaignHistory().CurrentMission();
    if (mh)
    {
        mh->dead.Clear();
    }
}

void AddDeadIdentity(RString identity)
{
    MissionHistory* mh = GetGCampaignHistory().CurrentMission();
    if (mh)
    {
        mh->dead.Add(identity);
    }
}

} // namespace Poseidon
Object* GetObject(GameValuePar oper);
namespace Poseidon
{

GameValue PoolAddWeapon(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (array.Size() != 2 || array[0].GetType() != GameString || array[1].GetType() != GameScalar)
    {
        return GameValue();
    }

    RString name = array[0];
    int count = toInt((float)array[1]);

    int i = GetGCampaignHistory().battles.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    BattleHistory& bh = GetGCampaignHistory().battles[i];
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    MissionHistory& mh = bh.missions[i];
    mh.AddWeapons(name, count);

    return GameValue();
}

GameValue PoolAddMagazine(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (array.Size() != 2 || array[0].GetType() != GameString || array[1].GetType() != GameScalar)
    {
        return GameValue();
    }

    RString name = array[0];
    int count = toInt((float)array[1]);

    int i = GetGCampaignHistory().battles.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    BattleHistory& bh = GetGCampaignHistory().battles[i];
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    MissionHistory& mh = bh.missions[i];
    mh.AddMagazines(name, count);

    return GameValue();
}

GameValue PoolGetWeapons(const GameState* state, GameValuePar oper1)
{
    Object* obj = ::GetObject(oper1);
    if (!obj)
    {
        return GameValue();
    }

    VehicleSupply* veh = dyn_cast<VehicleSupply>(obj);
    if (!veh)
    {
        return GameValue();
    }

    int i = GetGCampaignHistory().battles.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    BattleHistory& bh = GetGCampaignHistory().battles[i];
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    MissionHistory& mh = bh.missions[i];

    // veh->ClearWeaponCargo();
    // veh->ClearMagazineCargo();

    for (int i = 0; i < mh.weapons.Size(); i++)
    {
        RString name = mh.weapons[i].name;
        Ref<WeaponType> weapon = WeaponTypes.New(name);
        if (!weapon)
        {
            continue;
        }
        veh->AddWeaponCargo(weapon, mh.weapons[i].count);
    }

    for (int i = 0; i < mh.magazines.Size(); i++)
    {
        RString name = mh.magazines[i].name;
        Ref<MagazineType> magazine = MagazineTypes.New(name);
        if (!magazine)
        {
            continue;
        }
        veh->AddMagazineCargo(magazine, mh.magazines[i].count);
    }

    mh.weapons.Clear();
    mh.magazines.Clear();

    return GameValue();
}

GameValue PoolSetWeapons(const GameState* state, GameValuePar oper1)
{
    Object* obj = ::GetObject(oper1);
    if (!obj)
    {
        return GameValue();
    }

    VehicleSupply* veh = dyn_cast<VehicleSupply>(obj);
    if (!veh)
    {
        return GameValue();
    }

    int i = GetGCampaignHistory().battles.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    BattleHistory& bh = GetGCampaignHistory().battles[i];
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    MissionHistory& mh = bh.missions[i];

    // mh.weapons.Clear();
    // mh.magazines.Clear();

    for (int i = 0; i < veh->GetWeaponCargoSize(); i++)
    {
        const WeaponType* weapon = veh->GetWeaponCargo(i);
        if (!weapon)
        {
            continue;
        }
        RString name = weapon->GetName();
        mh.AddWeapons(name, 1);
    }

    for (int i = 0; i < veh->GetMagazineCargoSize(); i++)
    {
        const Magazine* magazine = veh->GetMagazineCargo(i);
        if (!magazine)
        {
            continue;
        }
        RString name = magazine->_type->GetName();
        mh.AddMagazines(name, 1);
    }

    veh->ClearWeaponCargo();
    veh->ClearMagazineCargo();

    return GameValue();
}

GameValue PoolClearWeapons(const GameState* state)
{
    int i = GetGCampaignHistory().battles.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    BattleHistory& bh = GetGCampaignHistory().battles[i];
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    MissionHistory& mh = bh.missions[i];

    mh.weapons.Clear();

    return GameValue();
}

GameValue PoolClearMagazines(const GameState* state)
{
    int i = GetGCampaignHistory().battles.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    BattleHistory& bh = GetGCampaignHistory().battles[i];
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        return GameValue();
    }
    MissionHistory& mh = bh.missions[i];

    mh.magazines.Clear();

    return GameValue();
}

GameValue PoolQueryWeapons(const GameState* state, GameValuePar oper1)
{
    RString name = oper1;

    int i = GetGCampaignHistory().battles.Size() - 1;
    if (i < 0)
    {
        return 0.0f;
    }
    BattleHistory& bh = GetGCampaignHistory().battles[i];
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        return 0.0f;
    }
    MissionHistory& mh = bh.missions[i];

    for (int j = 0; j < mh.weapons.Size(); j++)
    {
        if (stricmp(mh.weapons[j].name, name) == 0)
        {
            return (float)mh.weapons[j].count;
        }
    }
    return 0.0f;
}

GameValue PoolQueryMagazines(const GameState* state, GameValuePar oper1)
{
    RString name = oper1;

    int i = GetGCampaignHistory().battles.Size() - 1;
    if (i < 0)
    {
        return 0.0f;
    }
    BattleHistory& bh = GetGCampaignHistory().battles[i];
    i = bh.missions.Size() - 1;
    if (i < 0)
    {
        return 0.0f;
    }
    MissionHistory& mh = bh.missions[i];

    for (int j = 0; j < mh.magazines.Size(); j++)
    {
        if (stricmp(mh.magazines[j].name, name) == 0)
        {
            return (float)mh.magazines[j].count;
        }
    }
    return 0.0f;
}

LSError SaveMission(const char* filename)
{
    // MissionHistory *mh = GetGCampaignHistory().CurrentMission();

    ParamArchiveSave ar(CampaignVersion);
    PARAM_CHECK(ar.Serialize("Campaign", GetGCampaignHistory(), 1))
#if _ENABLE_CHEATS
    PARAM_CHECK(ar.Save(filename))
#else
    if (!ar.SaveBin(filename))
    {
        return LSUnknownError;
    }
#endif
    return LSOK;
}

LSError LoadMission(const char* filename)
{
    ParamArchiveLoad ar;
    if (!ar.LoadBin(filename) && ar.Load(filename) != LSOK)
    {
        return LSUnknownError;
    }
    PARAM_CHECK(ar.Serialize("Campaign", GetGCampaignHistory(), 1))

    // MissionHistory *mh = GetGCampaignHistory().CurrentMission();

    return LSOK;
}

/*
void SetMissionDisplayName(RString campaign, RString battle, RString mission, RString displayName)
{
    GetGCampaignHistory().SetDisplayName(campaign, battle, mission, displayName);
    RString filename = GetTmpSaveDirectory() + campaign + RString(".sqc");
    GetGCampaignHistory().campaignName = campaign;
    SaveMission(filename);
}
*/

void AddMission(RString campaign, RString battle, RString mission, RString displayName)
{
    GStats.ClearMission();
    /*
        GetGCampaignHistory().AddMission(campaign, battle, mission, displayName);
        RString filename = GetTmpSaveDirectory() + campaign + RString(".sqc");
        GetGCampaignHistory().campaignName = campaign;
        SaveMission(filename);
    */
    RString filename = GetTmpSaveDirectory() + campaign + RString(".sqc");

    // Transfer weapons pool to next mission
    WeaponsInfo pool;
    CampaignLoadWeaponPool(pool);
    LoadMission(filename);
    GetGCampaignHistory().AddMission(campaign, battle, mission, displayName);
    GetGCampaignHistory().campaignName = campaign;
    CampaignSaveWeaponPool(pool);
    SaveMission(filename);
}

// Implementation
// void SaveHeader();

// Implementation

// Implementation

void StartMission(Display* disp, bool newBattle)
{
    const ParamEntry& battleCls = ExtParsCampaign >> "Campaign" >> CurrentBattle;
    CurrentTemplate.Clear();
    ApplyCurrentMissionViewDistance();

    disp->ForceExit(-1);
    if (newBattle)
    {
        RString intro = battleCls >> "cutscene";
        disp->CreateChild(new DisplayCampaignIntro(disp, intro));
    }
    else
    {
        const ParamEntry& missionCls = battleCls >> CurrentMission;

        /*
                // enable / disable HUD, radio, map etc.
                if (GWorld->UI())
                {
                    int showHUD = missionCls >> "showHUD";
                    bool bShowHUD = showHUD != 0;
                    GWorld->UI()->ShowAll(bShowHUD);
                    GWorld->UI()->ShowMessage(UIMsgCenter, bShowHUD);
                    GWorld->UI()->ShowMessage(UIMsgGroup, bShowHUD);
                    GWorld->UI()->ShowMessage(UIMsgVehicle, bShowHUD);
                    GWorld->UI()->ShowMessage(UIMsgHelp, bShowHUD);
                }
        */

        RString epizode = missionCls >> "template";
        disp->CreateChild(new DisplayIntro(disp, epizode));
    }
}

void __cdecl PlayAward(Display* disp, RString name, const ParamEntry& cls);

void __cdecl PlayAward(Display* disp, RString name, const ParamEntry& cls)
{
    RString dir = GetTmpSaveDirectory();
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s%s.sqc", (const char*)dir, (const char*)CurrentCampaign);
    GetGCampaignHistory().campaignName = CurrentCampaign;
    SaveMission(buffer);
    // play it
    RString cutscene = cls >> Glob.header.worldname;
    disp->CreateChild(new DisplayAward(disp, cutscene));
}

bool CheckAward(Display* disp)
{
    // check if awards disabled
    const ParamEntry& campaignCls = ExtParsCampaign >> "Campaign";
    const ParamEntry& battleCls = campaignCls >> CurrentBattle;
    const ParamEntry& missionCls = battleCls >> CurrentMission;
    if (missionCls.FindEntry("noAward") && (bool)(missionCls >> "noAward"))
    {
        return false;
    }

    float oldScore = GStats._campaign._lastScore;
    float newScore = GStats._campaign._score;
    if (newScore < oldScore)
    {
        // penalties
        const ParamEntry* penalties = ExtParsCampaign.FindEntry("Penalties");
        if (penalties)
        {
            // fix - play cutscene with minimal score, mark all
            int best = INT_MAX;
            int bestIndex = -1;

            for (int i = 0; i < penalties->GetEntryCount(); i++)
            {
                const ParamEntry& cls = penalties->GetEntry(i);
                float limit = cls >> "limit";
                if (newScore <= limit)
                {
                    RString name = cls.GetName();
                    // check if cutscene was played before
                    bool found = false;
                    for (int j = 0; j < GStats._campaign._penalties.Size(); j++)
                    {
                        if (GStats._campaign._penalties[j] == name)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        // update campaign history
                        GStats._campaign._penalties.Add(name);
                        // FIX
                        /*
                        PlayAward(disp, name, cls);
                        return true;
                        */
                        if (limit < best)
                        {
                            bestIndex = i;
                            best = (int)limit;
                        }
                    }
                }
            }
            // FIX
            if (bestIndex >= 0)
            {
                const ParamEntry& cls = penalties->GetEntry(bestIndex);
                RString name = cls.GetName();
                PlayAward(disp, name, cls);
                return true;
            }
        }
    }
    else if (newScore > oldScore)
    {
        // awards
        const ParamEntry* awards = ExtParsCampaign.FindEntry("Awards");
        if (awards)
        {
            // fix - play cutscene with maximal score, mark all
            int best = INT_MAX;
            int bestIndex = -1;

            for (int i = 0; i < awards->GetEntryCount(); i++)
            {
                const ParamEntry& cls = awards->GetEntry(i);
                float limit = cls >> "limit";
                if (newScore >= limit)
                {
                    RString name = cls.GetName();
                    // check if cutscene was played before
                    bool found = false;
                    for (int j = 0; j < GStats._campaign._awards.Size(); j++)
                    {
                        if (GStats._campaign._awards[j] == name)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        // update campaign history
                        GStats._campaign._awards.Add(name);
                        // FIX
                        /*
                        PlayAward(disp, name, cls);
                        return true;
                        */
                        if (limit > best)
                        {
                            bestIndex = i;
                            best = (int)limit;
                        }
                    }
                }
            }
            // FIX
            if (bestIndex >= 0)
            {
                const ParamEntry& cls = awards->GetEntry(bestIndex);
                RString name = cls.GetName();
                PlayAward(disp, name, cls);
                return true;
            }
        }
    }
    return false;
}

bool NextMission(Display* disp, EndMode mode)
{
    SetBaseDirectory(GetCampaignDirectory(CurrentCampaign));
    GetGCampaignHistory().MissionCompleted(CurrentCampaign, CurrentBattle, CurrentMission);
    RString filename = GetTmpSaveDirectory() + CurrentCampaign + RString(".sqc");
    SaveMission(filename);

    const ParamEntry& campaignCls = ExtParsCampaign >> "Campaign";
    const ParamEntry& battleCls = campaignCls >> CurrentBattle;
    const ParamEntry& missionCls = battleCls >> CurrentMission;

    if (missionCls.FindEntry("noAward") && (bool)(missionCls >> "noAward"))
    {
        // award cutscene disabled
    }
    else
    {
        if (ExtParsCampaign.FindEntry("exitScore"))
        {
            float exitScore = ExtParsCampaign >> "exitScore";
            if (GStats._campaign._score <= exitScore)
            {
                // total end of campaign because of terrible score
                SetCampaign("");
                CurrentBattle = "";
                CurrentMission = "";
                return false;
            }
        }
    }

    switch (mode)
    {
        case EMLoser:
            CurrentMission = missionCls >> "lost";
            break;
        case EMEnd1:
            CurrentMission = missionCls >> "end1";
            break;
        case EMEnd2:
            CurrentMission = missionCls >> "end2";
            break;
        case EMEnd3:
            CurrentMission = missionCls >> "end3";
            break;
        case EMEnd4:
            CurrentMission = missionCls >> "end4";
            break;
        case EMEnd5:
            CurrentMission = missionCls >> "end5";
            break;
        case EMEnd6:
            CurrentMission = missionCls >> "end6";
            break;
    }

    bool showCampaignDisplay = false;
    if (CurrentMission.GetLength() == 0)
    {
        showCampaignDisplay = true;
        switch (mode)
        {
            case EMLoser:
                CurrentBattle = battleCls >> "lost";
                break;
            case EMEnd1:
                CurrentBattle = battleCls >> "end1";
                break;
            case EMEnd2:
                CurrentBattle = battleCls >> "end2";
                break;
            case EMEnd3:
                CurrentBattle = battleCls >> "end3";
                break;
            case EMEnd4:
                CurrentBattle = battleCls >> "end4";
                break;
            case EMEnd5:
                CurrentBattle = battleCls >> "end5";
                break;
            case EMEnd6:
                CurrentBattle = battleCls >> "end6";
                break;
        }
        if (CurrentBattle.GetLength() == 0)
        {
            SetCampaign("");
            CurrentBattle = "";
            CurrentMission = "";
            return false;
        }
        CurrentMission = campaignCls >> CurrentBattle >> "firstMission";
    }

    RString epizode;
    char buffer[256];
    {
        const ParamEntry& battleCls = campaignCls >> CurrentBattle;
        const ParamEntry& missionCls = battleCls >> CurrentMission;

        epizode = missionCls >> "template";
        if (!ProcessTemplateName(epizode))
        {
            RptF("Invalid mission name %s", (const char*)epizode);
            Poseidon::Foundation::ErrorMessage("Error in campaign structure");
            return false;
        }

        snprintf(buffer, sizeof(buffer), "%smission.sqm", (const char*)GetMissionDirectory());
        if (!QIFStreamB::FileExist(buffer))
        {
            RptF("Cannot find mission %s", buffer);
            Poseidon::Foundation::ErrorMessage("Error in campaign structure");
            return false;
        }
    }

    // new campaign index
    /*
        GStats.ClearMission();
        GetGCampaignHistory().AddMission(CurrentCampaign, CurrentBattle, CurrentMission);
        RString dir = GetTmpSaveDirectory();
        snprintf(buffer, sizeof(buffer), "%s%s.sqc", (const char *)dir, (const char *)CurrentCampaign);
        GetGCampaignHistory().campaignName = CurrentCampaign;
        SaveMission(buffer);
    */
    StartMission(disp, showCampaignDisplay);
    return true;
}

// Interrupt display

DisplayInterrupt::DisplayInterrupt(ControlsContainer* parent) : Display(parent)
{
    _enableSimulation = false;
    _enableUI = false;
    Load("RscDisplayInterrupt");
    UnregisterLanguageChangedCallback(_langCbToken);
    _langCbToken = RegisterLanguageChangedCallback(
        [this]()
        {
            RefreshLocalizedText();
            RefreshLanguage();
        });

    if (GWorld->GetMode() == GModeNetware)
    {
        GetCtrl(IDC_INT_SAVE)->ShowCtrl(false);
        GetCtrl(IDC_INT_LOAD)->ShowCtrl(false);
        GetCtrl(IDC_INT_RETRY)->ShowCtrl(false);
        //		GetCtrl(IDC_INT_RESTART)->ShowCtrl(false);
    }
    else
    {
        RString name = GetSaveDirectory() + RString("save.fps");
        if (QIFStream::FileExists(name))
        {
            GetCtrl(IDC_INT_SAVE)->ShowCtrl(false);
        }
        else
        {
            GetCtrl(IDC_INT_LOAD)->ShowCtrl(false);
        }
    }
    InputSubsystem::Instance().ChangeGameFocus(+1);
}

DisplayInterrupt::~DisplayInterrupt()
{
    UnregisterLanguageChangedCallback(_langCbToken);
    InputSubsystem::Instance().ChangeGameFocus(-1);
}

Control* DisplayInterrupt::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    switch (idc)
    {
        case IDC_INT_TITLE:
        {
            CStatic* text = new CStatic(this, idc, cls);
            if (GWorld->GetMode() == GModeNetware)
            {
                text->SetText(LocalizeString(IDS_DISP_INT_TITLE_MP));
            }
            else
            {
                text->SetText(LocalizeString(IDS_DISP_INT_TITLE));
            }
            return text;
        }
        default:
            return Display::OnCreateCtrl(type, idc, cls);
    }
}

void DisplayInterrupt::OnButtonClicked(int idc)
{
    switch (idc)
    {
        case IDC_INT_OPTIONS:
            if (Res.FindEntry("RscOptionsShell"))
                CreateChild(new OptionsShell(this, false, false));
            else
                CreateChild(new DisplayOptions(this, false, false));
            break;
        default:
            Exit(idc);
            break;
    }
}

ControllerUiScene DisplayInterrupt::GetControllerUiScene() const
{
    return PauseMenuControllerScene();
}

bool DisplayInterrupt::DoControllerUiAction(ControllerUiAction action)
{
    if (action == ControllerUiAction::Pause)
    {
        Exit(IDC_CANCEL);
        return true;
    }
    return Display::DoControllerUiAction(action);
}

void DisplayInterrupt::OnChildDestroyed(int idd, int exit)
{
    Display::OnChildDestroyed(idd, exit);
}

void DisplayInterrupt::RefreshLanguage()
{
    if (auto* text = dynamic_cast<CStatic*>(GetCtrl(IDC_INT_TITLE)))
    {
        if (GWorld->GetMode() == GModeNetware)
            text->SetText(LocalizeString(IDS_DISP_INT_TITLE_MP));
        else
            text->SetText(LocalizeString(IDS_DISP_INT_TITLE));
    }
}

// Main display
/*

LSError ContinueInfo::Serialize(ParamArchive &ar)
{
    PARAM_CHECK(ar.SerializeEnum("rank", rank, 1))
    PARAM_CHECK(ar.Serialize("time", time, 1))
    PARAM_CHECK(ar.Serialize("island", island, 1))
    PARAM_CHECK(ar.Serialize("mission", mission, 1))
    return LSOK;
}

bool IsHeader()
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%scontinue.sqh", (const char *)GetSaveDirectory());
    return QIFStream::FileExists(buffer);
}

void DisplayMain::LoadHeader()
{

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%scontinue.sqh", (const char *)GetSaveDirectory());

    if (QIFStream::FileExists(buffer))
    {
        ParamArchiveLoad ar(buffer);
        ContinueInfo header;
        if (header.Serialize(ar) != LSOK)
        {
            Poseidon::Foundation::WarningMessage("Cannot load header %s", buffer);
            goto LoadHeaderFailed;
        }

        C3DStatic *text = dynamic_cast<C3DStatic *>(GetCtrl(IDC_MAIN_RANK));
        if (text)
        {
            snprintf(buffer, sizeof(buffer),
                LocalizeString(IDS_MAIN_RANK),
                (const char *)LocalizeString(IDS_PRIVATE + ClampRankIndex(header.rank))
            );
            text->SetText(buffer);
        }
        text = dynamic_cast<C3DStatic *>(GetCtrl(IDC_MAIN_ISLAND));
        if (text) text->SetText(Pars>>"CfgWorlds">>header.island>>"description");
        text = dynamic_cast<C3DStatic *>(GetCtrl(IDC_MAIN_MISSION));
        if (text)
        {
            snprintf(buffer, sizeof(buffer),
                LocalizeString(IDS_MAIN_MISSION),
                (const char *)header.mission
            );
            text->SetText(buffer);
        }
    }
    else
    {
LoadHeaderFailed:
        C3DStatic *text = dynamic_cast<C3DStatic *>(GetCtrl(IDC_MAIN_RANK));
        if (text) text->SetText("");
        text = dynamic_cast<C3DStatic *>(GetCtrl(IDC_MAIN_ISLAND));
        if (text) text->SetText("");
        text = dynamic_cast<C3DStatic *>(GetCtrl(IDC_MAIN_MISSION));
        if (text) text->SetText("");
    }
}

void SaveHeader()
{

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%scontinue.sqh", (const char *)GetSaveDirectory());

    ContinueInfo header;
    Person *veh = GWorld->GetRealPlayer();
    AIUnit *unit = veh ? veh->Brain() : nullptr;
    if (unit)
        header.rank = unit->GetPerson()->GetRank();
    else
        header.rank = GStats._campaign._playerInfo.rank;
    header.time = Glob.clock.GetTimeInYear();
    header.island = Glob.header.worldname;
    if (CurrentTemplate.intel.briefingName.GetLength() > 0)
        header.mission = CurrentTemplate.intel.briefingName;
    else
        header.mission = Glob.header.filename;

    ParamArchiveSave ar(1);
    header.Serialize(ar);
    ar.Save(buffer);
}
*/

bool ContinueSaved = true;

void StartRandomCutscene(RString world)
{
    if (world.GetLength() == 0)
    {
        world = GetMenuInitWorld();
    }

    const ParamEntry& cls = Pars >> "CfgWorlds" >> world >> "cutscenes";
    int n = cls.GetSize();
    if (n <= 0)
    {
        return;
    }
    int i = toIntFloor(n * GRandGen.RandomValue());

    RString name = cls[i];

    SetMission(world, name, GetAnimsDir());
    //	SetCampaign("");
    SetBaseDirectory("");

    bool parsed = ParseIntro();

    if (parsed && CurrentTemplate.groups.Size() > 0)
    {
        GLOB_WORLD->SwitchLandscape(GetWorldName(world));
        GWorld->ActivateAddons(CurrentTemplate.addOns);
        GLOB_WORLD->InitGeneral(CurrentTemplate.intel);
        GLOB_WORLD->InitVehicles(GModeIntro, CurrentTemplate);
        //		GWorld->EnableSimulation(true);
    }
}

} // namespace Poseidon
#include <Poseidon/Foundation/Platform/VersionNo.h>
