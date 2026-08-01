#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Asset/Addon/AddonSystem.hpp>
#include <Poseidon/Asset/Addon/AddonInfo.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <filesystem>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <windows.h>
#endif

extern Poseidon::ParamFile Pars;

#include <Poseidon/Core/Version.hpp>

namespace Poseidon
{

AddonSystem::AddonInfoMap AddonSystem::s_addonRegistry;
AutoArray<SRef<ParamFile>> AddonSystem::s_addonConfigs;
AutoArray<AddonMergeRecord> AddonSystem::s_mergeOrder;

namespace
{
bool ResolveAddonFile(RStringB dir, const char* name, RString& resolvedPath)
{
    resolvedPath = dir + RString(name);
    if (QIFStreamB::FileExist(resolvedPath))
        return true;

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dirPath(dir.Data());
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec))
        return false;

    for (const auto& entry : fs::directory_iterator(dirPath, ec))
    {
        if (ec)
            return false;
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc))
            continue;
        const std::string leaf = entry.path().filename().string();
        if (stricmp(leaf.c_str(), name) != 0)
            continue;
        resolvedPath = entry.path().string().c_str();
        return true;
    }

    return false;
}
} // namespace

void AddonSystem::RegisterAddon(RString name, RString prefix)
{
    s_addonRegistry.Add(AddonInfo(name, prefix));
}

const AddonInfo* AddonSystem::FindAddonInfo(RString name)
{
    const AddonInfo& info = s_addonRegistry[name];
    return s_addonRegistry.IsNull(info) ? nullptr : &info;
}

void AddonSystem::ClearRegistry()
{
    s_addonRegistry.Clear();
}

void AddonSystem::LoadAddon(const char* addon)
{
    const AddonInfo* info = FindAddonInfo(addon);
    if (!info)
    {
        LOG_DEBUG(Config, "Cannot load addon {} - addon does not exist", addon);
        return;
    }
}

void AddonSystem::UnloadAddon(const char* addon)
{
    const AddonInfo* info = FindAddonInfo(addon);
    if (!info)
    {
        LOG_DEBUG(Config, "Cannot unload addon {} - addon does not exist", addon);
        return;
    }
    for (int i = 0; i < GFileBanks.Size(); i++)
    {
        QFBank& bank = GFileBanks[i];
        if (stricmp(bank.GetPrefix(), info->GetPrefix()) == 0)
            GFileServer->FlushBank(&bank);
    }
}

void AddonSystem::UnlockAddonCallback(const AddonInfo& addon, AddonInfoMap* map, void* context)
{
    RString prefix = addon.GetPrefix();
    GFileBanks.Unlock(prefix);
}

void AddonSystem::MarkAddonLockableCallback(const AddonInfo& addon, AddonInfoMap* map, void* context)
{
    RString prefix = addon.GetPrefix();
    GFileBanks.SetLockable(prefix, true);
}

void AddonSystem::UnlockAllAddons()
{
    for (AddonInfoMap::Iterator it(s_addonRegistry); it; ++it)
        UnlockAddonCallback(*it, &s_addonRegistry, nullptr);
}

void AddonSystem::MarkAllAddonsLockable()
{
    for (AddonInfoMap::Iterator it(s_addonRegistry); it; ++it)
        MarkAddonLockableCallback(*it, &s_addonRegistry, nullptr);
}

void AddonSystem::ForEachAddon(void (*fn)(const AddonInfo&, void*), void* context)
{
    for (AddonInfoMap::Iterator it(s_addonRegistry); it; ++it)
        fn(*it, context);
}

const AutoArray<AddonMergeRecord>& AddonSystem::GetMergeOrder()
{
    return s_mergeOrder;
}

RString AddonSystem::CheckAddonName(const ParamEntry& addon)
{
    const ParamEntry* patches = addon.FindEntry("CfgPatches");
    if (!patches || patches->GetEntryCount() == 0)
        return RString();
    return patches->GetEntry(0).GetName();
}

bool AddonSystem::CheckVersion(const RString& prefix, const ParamEntry& addon)
{
    const ParamEntry* patches = addon.FindEntry("CfgPatches");
    if (!patches || patches->GetEntryCount() == 0)
        return false;

    RString strVersion = GetAppVersion();
    int version = VersionToInt(strVersion);

    for (int i = 0; i < patches->GetEntryCount(); i++)
    {
        const ParamEntry& patch = patches->GetEntry(i);
        const ParamEntry* entry = patch.FindEntry("requiredVersion");
        if (entry)
        {
            RString required = *entry;
            if (VersionToInt(required) > version)
            {
                WarningMessage("Addon '%s' requires version %s or higher", (const char*)patch.GetName(),
                               (const char*)required);
                return false;
            }
        }
        const AddonInfo& info = s_addonRegistry[patch.GetName()];
        if (s_addonRegistry.NotNull(info))
            // Registration is first-wins, so this addon's config still merges but
            // the name keeps resolving to the earlier bank. RptF compiles out, so
            // the clash used to be silent in every build.
            LOG_WARN(Config, "Conflicting addon '{}' in '{}', previous definition in '{}'",
                     (const char*)patch.GetName(), (const char*)prefix, (const char*)info.GetPrefix());
        else
            s_addonRegistry.Add(AddonInfo(patch.GetName(), prefix));
    }

    return true;
}

bool AddonSystem::ParseAddonConfig(const RString& prefixPath)
{
    RString filename;

    filename = prefixPath + RString("config.cpp");
    if (QIFStreamB::FileExist(filename))
    {
        SRef<ParamFile> addon = new ParamFile;
        if (QIFStream::FileExists(filename))
        {
            RString folderPath;
            int n = prefixPath.GetLength();
            DoAssert(prefixPath[n - 1] == '\\' || prefixPath[n - 1] == '/');
            folderPath = prefixPath.Substring(0, n - 1);
            char buf[1024];
            GetCurrentDirectory(sizeof(buf), buf);
#ifndef _WIN32
            unixPath((char*)(const char*)folderPath);
#endif
            SetCurrentDirectory(folderPath);
            addon->Parse("config.cpp");
            SetCurrentDirectory(buf);
        }
        else
        {
            addon->Parse(filename);
        }

        if (CheckVersion(prefixPath, *addon))
        {
            addon->SetOwner(CheckAddonName(*addon));
            s_addonConfigs.Add(addon);
        }
        else
        {
            return false;
        }
    }
    else
    {
        filename = prefixPath + RString("config.bin");
        if (QIFStreamB::FileExist(filename))
        {
            SRef<ParamFile> addon = new ParamFile;
            addon->ParseBin(filename);
            if (CheckVersion(prefixPath, *addon))
            {
                addon->SetOwner(CheckAddonName(*addon));
                s_addonConfigs.Add(addon);
            }
            else
            {
                return false;
            }
        }
    }

    if (ResolveAddonFile(prefixPath, "stringtable.csv", filename))
        LoadStringtable("Global", filename, 0, false);

    return true;
}

RString AddonSystem::GetAddonName(const ParamFile& config)
{
    const ParamEntry* cfg = config.FindEntry("CfgPatches");
    if (cfg)
    {
        for (int i = 0; i < cfg->GetEntryCount(); i++)
        {
            const ParamEntry& entry = cfg->GetEntry(i);
            if (!entry.IsClass())
                continue;
            return entry.GetName();
        }
    }
    return "";
}

const ParamFile* AddonSystem::FindAddonConfig(RStringB name)
{
    for (int i = 0; i < s_addonConfigs.Size(); i++)
    {
        const ParamFile& config = *s_addonConfigs.Get(i);
        const ParamEntry* cfg = config.FindEntry("CfgPatches");
        if (cfg)
        {
            for (int j = 0; j < cfg->GetEntryCount(); j++)
            {
                const ParamEntry& entry = cfg->GetEntry(j);
                if (!entry.IsClass())
                    continue;
                if (!strcmpi(entry.GetName(), name))
                    return &config;
            }
        }
    }
    return nullptr;
}

bool AddonSystem::IsPreloadedAddon(RStringB name)
{
    const ParamEntry& def = Pars >> "CfgAddons" >> "PreloadAddons";
    for (int c = 0; c < def.GetEntryCount(); c++)
    {
        const ParamEntry& cc = def.GetEntry(c);
        if (!cc.IsClass())
            continue;
        if (!cc.FindEntry("list"))
            continue;
        const ParamEntry& cl = cc >> "list";
        for (int i = 0; i < cl.GetSize(); i++)
        {
            RStringB addon = cl[i];
            if (!strcmpi(name, addon))
                return true;
        }
    }
    return false;
}

void AddonDependency::Resolved(const AddonDependency& dep)
{
    for (int i = 0; i < dependsOn.Size(); i++)
    {
        if (dependsOn[i] != dep.addon)
            continue;
        dependsOn.Delete(i--);
    }
}

void AddonSystem::ParseAllAddonConfigs()
{
    AutoArray<AddonDependency> dependencies;

    for (int i = 0; i < s_addonConfigs.Size(); i++)
    {
        const ParamFile& config = *s_addonConfigs.Get(i);
        AddonDependency& dep = dependencies.Append();
        dep.addon = &config;
        dep.preloaded = false;

        const ParamEntry* cfg = config.FindEntry("CfgPatches");
        if (cfg)
        {
            for (int j = 0; j < cfg->GetEntryCount(); j++)
            {
                const ParamEntry& entry = cfg->GetEntry(j);
                if (!entry.IsClass())
                    continue;
                dep.preloaded = IsPreloadedAddon(entry.GetName());
                const ParamEntry* required = entry.FindEntry("requiredAddons");
                if (!required)
                    continue;
                for (int k = 0; k < required->GetSize(); k++)
                {
                    RStringB requiredName = (*required)[k];
                    const ParamFile* file = FindAddonConfig(requiredName);
                    if (!file)
                    {
                        WarningMessage("Addon '%s' requires addon '%s'", (const char*)GetAddonName(config),
                                       (const char*)requiredName);
                        continue;
                    }
                    dep.dependsOn.Add(file);
                }
            }
        }
    }

    AutoArray<AddonDependency> resolved;
    while (dependencies.Size() > 0)
    {
        bool someResolved = false;
        for (int preloaded = 1; preloaded >= 0; preloaded--)
        {
            for (int i = 0; i < dependencies.Size(); i++)
            {
                const AddonDependency& dep = dependencies[i];
                if ((int)dep.preloaded != preloaded)
                    continue;
                if (dep.dependsOn.Size() > 0)
                    continue;
                resolved.Add(dep);
                for (int j = 0; j < dependencies.Size(); j++)
                    dependencies[j].Resolved(dep);
                dependencies.Delete(i--);
                someResolved = true;
            }
        }
        if (!someResolved)
        {
            RStringB addonName = GetAddonName(*dependencies[0].addon);
            ErrorMessage("Circular addon dependency in '%s'", (const char*)addonName);
            break;
        }
    }

    s_mergeOrder.Clear();
    for (int i = 0; i < resolved.Size(); i++)
    {
        RString name = GetAddonName(*resolved[i].addon);
        // Log() compiles out under NDEBUG, so the merge order -- which decides
        // who wins a config collision -- was invisible in every shipping build.
        LOG_DEBUG(Config, "Merging addon config [{}] '{}'{}", i, (const char*)name,
                  resolved[i].preloaded ? " (preloaded)" : "");
        s_mergeOrder.Add({name, resolved[i].preloaded, true});
        Pars.Update(*resolved[i].addon);
    }

    // The cycle above reports only the addon it stopped on; name the rest too,
    // since their config is silently missing from Pars for the whole session.
    for (int i = 0; i < dependencies.Size(); i++)
    {
        RString name = GetAddonName(*dependencies[i].addon);
        ErrorMessage("Addon '%s' was never merged: unresolved dependency cycle", (const char*)name);
        s_mergeOrder.Add({name, dependencies[i].preloaded, false});
    }

    Pars.SetFile(&Pars);
}

void AddonSystem::ClearAddonConfigs()
{
    s_addonConfigs.Clear();
}

} // namespace Poseidon
