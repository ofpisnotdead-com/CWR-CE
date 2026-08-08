#pragma once

#include <Poseidon/Asset/Addon/AddonInfo.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Containers/HashMap.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

namespace Poseidon
{

//! One addon as the config merge saw it. Later entries win scalar collisions.
struct AddonMergeRecord
{
    RString name;
    //! listed in CfgAddons >> PreloadAddons, so resolved ahead of non-preloaded peers
    bool    preloaded;
    //! false when a dependency cycle stopped the sort before this addon merged
    bool    merged;
};

class AddonSystem
{
  public:
    static void            RegisterAddon(RString name, RString prefix);
    static const AddonInfo* FindAddonInfo(RString name);
    static void            ClearRegistry();
    static void            ForEachAddon(void (*fn)(const AddonInfo&, void*), void* context);

    //! Order ParseAllAddonConfigs resolved, kept so config precedence stays
    //! inspectable after the per-addon ParamFiles are released.
    static const AutoArray<AddonMergeRecord>& GetMergeOrder();

    static void LoadAddon(const char* addon);
    static void UnloadAddon(const char* addon);
    static void UnlockAllAddons();
    static void MarkAllAddonsLockable();

    static bool   ParseAddonConfig(const RString& prefixPath);
    static void   ParseAllAddonConfigs();
    static void   ClearAddonConfigs();

    static RString          CheckAddonName(const ParamEntry& addon);
    static bool             CheckVersion(const RString& prefix, const ParamEntry& addon);
    static RString          GetAddonName(const ParamFile& config);
    static const ParamFile* FindAddonConfig(RStringB name);
    static bool             IsPreloadedAddon(RStringB name);

  private:
    typedef MapStringToClass<AddonInfo, AutoArray<AddonInfo>> AddonInfoMap;
    static AddonInfoMap                 s_addonRegistry;
    static AutoArray<SRef<ParamFile>>   s_addonConfigs;
    static AutoArray<AddonMergeRecord>  s_mergeOrder;

    static void UnlockAddonCallback(const AddonInfo& addon, AddonInfoMap* map, void* context);
    static void MarkAddonLockableCallback(const AddonInfo& addon, AddonInfoMap* map, void* context);
};

} // namespace Poseidon
