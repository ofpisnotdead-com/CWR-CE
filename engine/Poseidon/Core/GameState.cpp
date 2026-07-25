#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>
#include <Poseidon/Foundation/Memory/MemoryBudget.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Core/ModSystem.hpp>

#include <Poseidon/Foundation/Common/Filenames.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/Core/Progress.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/IO/FileServerMT.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/UI/Locale/Languages.hpp>
#include <Poseidon/Asset/Addon/AddonInfo.hpp>
#include <Poseidon/Asset/Addon/AddonSystem.hpp>
#include <stdio.h>
#include <string.h>
#include <functional>
#include <string>
#include <Poseidon/Foundation/Containers/RStringArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::AddonInfo;
using Poseidon::AddonSystem;
using Poseidon::CheckAddonContext;
using Poseidon::LoadBanksContext;

#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Common/PlayerPrefs.hpp>
#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Core/Profile/ProfileService.hpp>

using namespace Poseidon;
static const char* ProductList[] = {"OFP: Cold War Crisis", "OFP: Resistance", "VBS", nullptr};
static bool StringInList(const char* str, const char** list);

namespace Poseidon
{
bool IsAddonMetadataAccepted(const char* product, const char* encryption, const char* formatVersion,
                             bool encryptionRequired, const char** productList)
{
    if (encryptionRequired && (!encryption || *encryption == 0))
    {
        return false;
    }

    if (formatVersion && *formatVersion != 0)
    {
        return false;
    }

    if (product && *product != 0 && !StringInList(product, productList))
    {
        return false;
    }

    return true;
}
} // namespace Poseidon

static bool StringInList(const char* str, const char** list)
{
    while (*list)
    {
        if (!strcmpi(*list, str))
        {
            return true;
        }
        list++;
    }
    return false;
}

static bool CheckProductCallback(QFBank* bank, BankContextBase* context)
{
    CheckAddonContext* cpc = static_cast<CheckAddonContext*>(context);
    RString product = bank->GetProperty("product");
    RString encryption = bank->GetProperty("encryption");
    RString formatVersion = bank->GetProperty("pboVersion");

    return Poseidon::IsAddonMetadataAccepted(product, encryption, formatVersion, cpc->encryptionRequired,
                                             cpc->productList);
}

extern bool GUseFileBanks;
extern void ClearShapes();
extern void DestroyMsgFormats();
extern RString CurrentCampaign;
extern RString CurrentBattle;
extern RString CurrentMission;
#include <Poseidon/Core/Version.hpp>

namespace Poseidon
{
RString GetUserParams();
}
using Poseidon::Foundation::Time;
using Poseidon::Foundation::UITime;

// Non-static so main.cpp can call it.
RString& GetModPathInternal()
{
    static RString ModPath;
    return ModPath;
}

bool EnumModDirectories(ModDirectoryCallback callback, void* context)
{
    RString& ModPath = GetModPathInternal();
    if (ModPath.GetLength() > 0)
    {
        Temp<char> buffer((const char*)ModPath, ModPath.GetLength() + 1);

        char* ptr;
        while (ptr = strrchr(buffer, ';'))
        {
            *ptr = 0;
            if (callback(ptr + 1, context))
            {
                return true;
            }
        }
        if (callback((const char*)buffer, context))
        {
            return true;
        }
    }
    return callback("", context);
}

static bool ParseAddonConfigCallback(QFBank* bank, BankContextBase* context)
{
    return AddonSystem::ParseAddonConfig(bank->GetPrefix());
}

static void LoadBanks(const char* path, const char* fullPath, bool emptyPrefix, bool parseConfig = false)
{
    FindArrayRStringCI bankNames;
    FindBank find;
    if (find.First(fullPath))
    {
        do
        {
            char prefix[256];
            snprintf(prefix, sizeof(prefix), "%s", (const char*)find.GetName());
            strlwr(prefix);
            char* ext = strrchr(prefix, '.');
            PoseidonAssert(ext);
            *ext = 0;
            bankNames.AddUnique(prefix);
        } while (find.Next());
        find.Close();
    }
    RString bankPrefix = RString(path) + RString("\\");
    const char* langSuffix = GetLanguagePboSuffix(GLanguage);
    for (int i = 0; i < bankNames.Size(); i++)
    {
        const RString& bName = bankNames[i];
        // Skip base PBO when language-specific variant exists (e.g., skip "1985" when "1985.cz" exists)
        if (langSuffix)
        {
            RString langVariant = bName + RString(".") + RString(langSuffix);
            if (bankNames.Find(langVariant) >= 0)
                continue;
        }
        RString prefix;
        if (emptyPrefix)
        {
            prefix = bName;
        }
        else
        {
            prefix = bankPrefix + bName;
        }
        // Runtime bank prefix remapping for language-specific PBOs
        if (stricmp(GLanguage, "Czech") == 0 && stricmp(prefix, "fonts.cz") == 0)
            prefix = "fonts";
        else if (stricmp(GLanguage, "Russian") == 0 && stricmp(prefix, "fonts.russian") == 0)
            prefix = "fonts";
        else if (stricmp(GLanguage, "Polish") == 0 && stricmp(prefix, "fonts.polish") == 0)
            prefix = "fonts";
        // Strip language PBO suffix to remap to base prefix (e.g., "campaigns\1985.cz" → "campaigns\1985")
        if (langSuffix)
        {
            char dotSuffix[16];
            snprintf(dotSuffix, sizeof(dotSuffix), ".%s", langSuffix);
            int sLen = (int)strlen(dotSuffix);
            int pLen = (int)strlen(prefix);
            if (pLen > sLen && stricmp((const char*)prefix + pLen - sLen, dotSuffix) == 0)
            {
                char buf[256];
                memcpy(buf, (const char*)prefix, pLen - sLen);
                buf[pLen - sLen] = 0;
                prefix = buf;
            }
        }
        RString prefixPath = prefix + RString("\\");
        if (QIFStreamB::AutoBank(prefixPath))
        {
            continue;
        }

        Ref<CheckAddonContext> cpc = new CheckAddonContext;
        cpc->productList = ProductList;
        cpc->encryptionRequired = parseConfig && AppConfig::Instance().RequireEncryptedAddons();
        OpenCallback addonCallback = (OpenCallback) nullptr;
        if (parseConfig)
        {
            addonCallback = ParseAddonConfigCallback;
        }

        RString bankPath = RString(fullPath) + RString("\\");
        GFileBanks.Load(bankPath, bankPrefix, bName, emptyPrefix, CheckProductCallback, addonCallback, cpc);
    }
}

static bool LoadBanksCallback(RStringB dir, void* context)
{
    LoadBanksContext* ctx = reinterpret_cast<LoadBanksContext*>(context);

    if (dir.GetLength() == 0)
    {
        dir = ctx->path;
    }
    else
    {
        dir = dir + RString("\\") + ctx->path;
    }

    LoadBanks(ctx->path, dir, ctx->emptyPrefix, ctx->parseConfig);
    return false;
}

static void LoadBanksEx(const char* path, bool emptyPrefix, bool parseConfig = false)
{
    LoadBanksContext ctx;
    ctx.path = path;
    ctx.emptyPrefix = emptyPrefix;
    ctx.parseConfig = parseConfig;
    EnumModDirectories(LoadBanksCallback, &ctx);
}

void Globals::Init()
{
    if (GUseFileBanks)
    {
        // Rebuild addon state from scratch for the current mod set: a re-mount keeps the
        // engine alive, so the previous mount's addon registry would otherwise persist.
        AddonSystem::ClearRegistry();
        GFileBanks.Clear();
        if (GFileBankPrefix.GetLength() > 0)
        {
            LoadBanksEx(RString("dta\\") + GFileBankPrefix, true);
            LoadBanksEx(RString("addons\\") + GFileBankPrefix, true, true);
        }
        LoadBanksEx("dta", true);
        LoadBanksEx("addons", true, true);
        LoadBanksEx("Campaigns", false);
        AddonSystem::ParseAllAddonConfigs();
        AddonSystem::ClearAddonConfigs();
        AddonSystem::MarkAllAddonsLockable();
    }

    remove("clipboard.txt");

    int fileMemory = ENGINE_CONFIG.fileHeapSize * (1024 * 1024);
    if (fileMemory < 1024 * 1024)
    {
        fileMemory = 1024 * 1024;
    }

    GFileServer = new FileServerST(fileMemory);
    GFileServer->Start();

    // Resolve and apply the coarse process-wide heap limits. Precedence: --maxmem CLI
    // (hard = N MB, soft = 75% of it) overrides everything; else per config field:
    // -1 = auto (a fixed-cap backstop clamped by physical RAM; inert on a normal
    // machine), 0 = unlimited, >0 = explicit MB. Idempotent: re-applied on each
    // in-place remount alongside the file cache.
    {
        const size_t mb = 1024u * 1024u;
        const size_t physBytes = Foundation::QueryPhysicalMemoryBytes();
        const int maxMemMB = AppConfig::Instance().GetMaxMemMB();
        size_t soft = 0;
        size_t hard = 0;
        const char* source = "auto";
        if (maxMemMB > 0)
        {
            hard = static_cast<size_t>(maxMemMB) * mb;
            soft = hard / 4 * 3; // soft watermark at 75% of the hard cap
            source = "--maxmem";
        }
        else
        {
            const Foundation::MemoryLimits autoLimits = Foundation::DeriveDefaultMemoryLimits(physBytes);
            auto resolve = [mb](int cfgMB, size_t autoBytes) -> size_t
            { return cfgMB < 0 ? autoBytes : static_cast<size_t>(cfgMB) * mb; };
            soft = resolve(ENGINE_CONFIG.memorySoftLimitMB, autoLimits.soft);
            hard = resolve(ENGINE_CONFIG.memoryHardLimitMB, autoLimits.hard);
            if (ENGINE_CONFIG.memorySoftLimitMB >= 0 || ENGINE_CONFIG.memoryHardLimitMB >= 0)
                source = "config";
        }
        Foundation::SetProcessMemoryLimits(soft, hard);
        if (hard == 0)
            LOG_INFO(Memory, "Process memory: {} MB physical, no limit ({})", physBytes / mb, source);
        else
            LOG_INFO(Memory, "Process memory: {} MB physical, soft={} MB hard={} MB ({})", physBytes / mb, soft / mb,
                     hard / mb, source);
    }

    drawTreshold = 2.0 * 2.0;
    shadowTreshold = reflectTreshold = 4 * 4;

    time = Time(0);
    uiTime = UITime(0);

    newGame = false;
    exit = false;
    demo = false;

    strcpy(header.filename, "Game001");
    RString worldInit = Pars >> "CfgWorlds" >> "initWorld";
    strcpy(header.worldname, worldInit);

    if (Glob.header.playerName.GetLength() == 0)
    {
        // Prefer an existing usable profile (the last-used one when it still
        // exists, otherwise a deterministic existing profile) over creating a
        // fresh OS-login/default profile; only create and remember a default
        // when no profile exists yet.
        ProfileService selector({std::string(Foundation::GamePaths::Instance().UserDir()),
                                 [] { return Foundation::prefsGetString(AppName, "PlayerName"); },
                                 [](const std::string& name)
                                 { Foundation::prefsSetString(AppName, "PlayerName", name.c_str()); },
                                 [] { return Foundation::getCurrentUserName(); }});
        header.playerName = selector.ResolveStartupProfile().c_str();
    }

    header.playerFace = "Default";
    header.playerGlasses = "None";
    header.playerSpeaker = (Pars >> "CfgVoice" >> "voices")[0];
    header.playerPitch = 1.0;
    RString filename = Poseidon::GetUserParams();
    ParamFile cfg;
    cfg.Parse(filename);
    const ParamEntry* identity = cfg.FindEntry("Identity");
    if (identity)
    {
        header.playerFace = (*identity) >> "face";
        if (identity->FindEntry("glasses"))
        {
            header.playerGlasses = (*identity) >> "glasses";
        }
        header.playerSpeaker = (*identity) >> "speaker";
        header.playerPitch = (*identity) >> "pitch";
    }

    header.playerSide = TWest;
    USER_CONFIG.easyMode = true;
#if _ENABLE_CHEATS
    config.super = false;
#endif

    LoadGameSettings();

    InputSubsystem::Instance().LoadKeys();
    UserConfig_LoadDifficulties(USER_CONFIG);

    ParamFile userCfg;
    userCfg.Parse(Poseidon::GetUserParams());
}

void Globals::Clear()
{
    QIFStreamB::ClearBanks();
    GFileServer.Free();
    ClearShapes();
    Pars.Clear();
    ExtParsCampaign.Clear();
    ExtParsMission.Clear();
    Res.Clear();
    DestroyMsgFormats();
}

namespace Poseidon
{
Globals Glob;
}
