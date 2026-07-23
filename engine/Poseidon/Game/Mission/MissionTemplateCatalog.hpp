#pragma once

#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

struct MissionTemplateEntry
{
    RString name;
    RString basePath;
    bool bank = false;
};

RString GetMissionTemplateRoot(bool multiplayer);
RString GetMissionTemplateBasePath(bool multiplayer, RString templ, RString world);
RString ResolveMissionTemplateDisplayName(RString missionDirectory, RString fallback);
RString GetMissionTemplateSelectorText(const MissionTemplateEntry& templ);
void ListMissionTemplates(AutoArray<MissionTemplateEntry>& templates, bool multiplayer, RString world);

} // namespace Poseidon
