#include <Poseidon/Foundation/PoseidonPCH.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/IO/ParamFile/InitLibraryElement.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/World/Entities/Weapons/Recoil.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>

#include <cstring>
#include <filesystem>
#include <string>

// The weapon firing-mode / muzzle name shown in the upper-left HUD panel stayed
// in the previously-loaded language after a runtime language switch (no game
// restart).  Repro: start in English, switch to Czech, open the editor, place a
// soldier, press Space to cycle the firing mode — the readout shows the English
// "Burst" / "Hand grenade" instead of the Czech text.
//
// Root cause: WeaponModeType::Init and MuzzleType::Init resolve their
// `displayName` ($STR_ token -> localized string) ONCE at config-parse time and
// cache the result in `_displayName`.  ParamEntry string reads localize lazily
// against the CURRENT language (ParamFile.cpp ParamRawValue::GetValue: a "$STR"
// value is routed through ParamFile::LocalizeString on every read), so
// re-reading the config entry after Poseidon::SetLanguage yields the new
// language -- but the cached `_displayName` snapshot is never refreshed.
//
// This is the same caching shape that bit AIGroup group names (fixed by
// re-resolving live in the accessor; see
// tests/integration/ui/briefing/briefing_group_name_lang_switch.test.sqf).
//
// Fix: the `_displayName` members are now LocalizedString -- they bind to the
// config value node and resolve on demand, re-resolving when the language
// generation advances (see engine/Poseidon/IO/ParamFile/LocalizedString.*).
// GetDisplayName() therefore tracks the live language, and the HUD reads through
// GetDisplayName().  This test exercises that accessor, which is exactly what
// InGameUIDraw.cpp DrawUnitInfo renders.  The LocalizedString primitive itself is
// unit-tested in isolation in
// tests/unit/engine/Poseidon/IO/ParamFile/test_localized_string.cpp.
//
// Contract under test: after Poseidon::SetLanguage, GetDisplayName() on the
// firing mode / muzzle reflects the new language.
//
// Broken-state delta: revert the LocalizedString change (snapshot the value at
// Init) and GetDisplayName() returns the English "Burst" / "Hand grenade" after
// SetLanguage("Czech") instead of the Czech "Davka" / "Rucni granat".  The
// fixture uses ASCII Czech so the assertion is exact-match (no codepage transcode
// in play) and isolates the caching bug from any encoding behaviour.

using namespace Poseidon;

namespace
{
// Load the two-language (English/Czech) weapon-name stringtable from the source
// fixtures and reset the active language to English, matching the steady state a
// fresh boot would have before the player switches language.
void LoadWeaponNameStringtable()
{
    // $STR_ -> stringtable routing for ParamFile reads (idempotent; test_main's
    // InitLibraryElement() already does this, repeated here for self-containment).
    Poseidon::InitParamFileStringtable();

    Poseidon::ClearStringtable();
    GLanguage = "English";
    const std::string csv =
        (std::filesystem::path(TESTS_ROOT_DIR) / "fixtures" / "stringtable" / "weapon_modes_lang.csv").string();
    Poseidon::LoadStringtable("global", csv.c_str(), 0, true);

    // Guard: the pipeline itself must work, otherwise a later mismatch would be
    // a load failure rather than the caching regression we are targeting.
    REQUIRE(std::string(Poseidon::LocalizeString("STR_DN_BURST").Data()) == "Burst");
    REQUIRE(Poseidon::SetLanguage("Czech"));
    REQUIRE(std::string(Poseidon::LocalizeString("STR_DN_BURST").Data()) == "Davka");
    REQUIRE(Poseidon::SetLanguage("English"));
}

ParamFile ParseConfig(const char* text)
{
    ParamFile pf;
    QIStream in(text, static_cast<int>(strlen(text)));
    pf.Parse(in);
    return pf;
}
} // namespace

TEST_CASE("Weapon firing-mode display name follows runtime language switch", "[weapons][localization][switch]")
{
    LoadWeaponNameStringtable();

    ParamFile pf = ParseConfig("class TestBurst\n"
                               "{\n"
                               "    displayName = \"$STR_DN_BURST\";\n"
                               "};\n");
    REQUIRE(pf.FindEntry("TestBurst") != nullptr);

    WeaponModeType mode;
    mode.Init(pf >> "TestBurst");

    // Resolved under English.
    REQUIRE(std::string(mode.GetDisplayName().Data()) == "Burst");

    // After switching the UI language at runtime the firing-mode name must follow.
    REQUIRE(Poseidon::SetLanguage("Czech"));
    REQUIRE(std::string(mode.GetDisplayName().Data()) == "Davka");

    // ...and back, to prove it tracks the live language rather than latching once.
    REQUIRE(Poseidon::SetLanguage("English"));
    REQUIRE(std::string(mode.GetDisplayName().Data()) == "Burst");
}

TEST_CASE("Muzzle display name follows runtime language switch", "[weapons][localization][switch]")
{
    LoadWeaponNameStringtable();

    ParamFile pf = ParseConfig("class TestThrow\n"
                               "{\n"
                               "    displayName = \"$STR_MZL_HANDGRENADE\";\n"
                               "};\n");
    REQUIRE(pf.FindEntry("TestThrow") != nullptr);

    MuzzleType muzzle;
    muzzle.Init(pf >> "TestThrow", nullptr);

    REQUIRE(std::string(muzzle.GetDisplayName().Data()) == "Hand grenade");

    REQUIRE(Poseidon::SetLanguage("Czech"));
    REQUIRE(std::string(muzzle.GetDisplayName().Data()) == "Rucni granat");

    REQUIRE(Poseidon::SetLanguage("English"));
    REQUIRE(std::string(muzzle.GetDisplayName().Data()) == "Hand grenade");
}

TEST_CASE("Recoil lookup does not follow an expired inherited config", "[weapons][recoil][crash14192]")
{
    struct ParsReset
    {
        ParsReset() { Pars.Clear(); }
        ~ParsReset() { Pars.Clear(); }
    } reset;

    ParamClass* cfgRecoils = Pars.AddClass("CfgRecoils");

    {
        ParamFile temporary;
        ParamClass* base = temporary.AddClass("TemporaryRecoils");
        ParamEntry* inherited = base->AddArray("temporaryRecoil");
        inherited->AddValue(0.05f);
        inherited->AddValue(0.02f);
        inherited->AddValue(0.04f);
        cfgRecoils->SetBase(base);
    }

    RecoilFunction recoil("temporaryRecoil");

    REQUIRE(recoil.GetName() == RStringB("temporaryRecoil"));
    REQUIRE(recoil.GetTerminated(0));
}
