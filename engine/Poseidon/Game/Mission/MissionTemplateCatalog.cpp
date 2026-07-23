#include <Poseidon/Game/Mission/MissionTemplateCatalog.hpp>

#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/IO/Filesystem/DirScanner.hpp>
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>

#include <string.h>

namespace Poseidon
{

namespace
{

bool EndsWithIgnoreCase(const char* value, const char* suffix)
{
    const size_t valueLen = strlen(value);
    const size_t suffixLen = strlen(suffix);
    return valueLen >= suffixLen && stricmp(value + valueLen - suffixLen, suffix) == 0;
}

RString StripSuffix(RString value, const char* suffix)
{
    return value.Substring(0, value.GetLength() - (int)strlen(suffix));
}

void AddTemplate(AutoArray<MissionTemplateEntry>& templates, RString name, RString basePath, bool bank)
{
    MissionTemplateEntry& entry = templates.Append();
    entry.name = name;
    entry.basePath = basePath;
    entry.bank = bank;
}

void ScanTemplateEntries(AutoArray<MissionTemplateEntry>& templates, RString root, RString world, bool multiplayer,
                         bool bank)
{
    char suffix[256];
    snprintf(suffix, sizeof(suffix), bank ? ".%s.pbo" : ".%s", (const char*)world);

    DirScanner scanner;
    if (!scanner.First(root, nullptr))
        return;

    do
    {
        if (scanner.IsDirectory() == bank)
            continue;

        const RString filename = scanner.GetName();
        if (!EndsWithIgnoreCase(filename, suffix))
            continue;

        const RString templ = StripSuffix(filename, suffix);
        AddTemplate(templates, templ, GetMissionTemplateBasePath(multiplayer, templ, world), bank);
    } while (scanner.Next());
}

} // namespace

RString GetMissionTemplateRoot(bool multiplayer)
{
    return multiplayer ? RString("Templates") : RString("SPTemplates");
}

RString GetMissionTemplateBasePath(bool multiplayer, RString templ, RString world)
{
    return GetMissionTemplateRoot(multiplayer) + RString("\\") + templ + RString(".") + world;
}

RString ResolveMissionTemplateDisplayName(RString missionDirectory, RString fallback)
{
    RString displayName = LoadLocalizedMissionBriefingName(missionDirectory, fallback);
    return displayName.GetLength() > 0 ? displayName : fallback;
}

RString GetMissionTemplateSelectorText(const MissionTemplateEntry& templ)
{
    return templ.name;
}

void ListMissionTemplates(AutoArray<MissionTemplateEntry>& templates, bool multiplayer, RString world)
{
    templates.Clear();

    const RString root = GetMissionTemplateRoot(multiplayer);
    ScanTemplateEntries(templates, root, world, multiplayer, false);
    ScanTemplateEntries(templates, root, world, multiplayer, true);
}

} // namespace Poseidon
