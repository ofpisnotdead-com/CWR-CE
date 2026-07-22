#include <Poseidon/Game/Mission/MissionTemplateCatalog.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace Poseidon;

TEST_CASE("wizard template selector uses template identity, not localized briefing name", "[ui][wizard][localization]")
{
    MissionTemplateEntry entry;
    entry.name = "1-10_T_TeamFlagFight";
    entry.basePath = "Templates\\1-10_T_TeamFlagFight.Intro";
    entry.bank = true;

    CHECK(GetMissionTemplateSelectorText(entry) == RString("1-10_T_TeamFlagFight"));
}
