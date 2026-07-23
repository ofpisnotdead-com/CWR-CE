// ControlsCategory — pure-data table mapping the 5 user-facing
// Controls UI categories (On foot / Vehicles / Pilot / Gunner / Common)
// to UserAction lists.  No global state, no engine init required.

#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Input/ControlsCategory.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/UserActionDesc.hpp>

#include <set>
#include <catch2/catch_message.hpp>
#include <initializer_list>
#include <string>
#include <utility>

using namespace Poseidon;
TEST_CASE("ControlsCategory: every category has a non-empty list", "[Input][ControlsCategory]")
{
    for (int c = 0; c < ControlsCategoryCount; c++)
    {
        ControlsCategory cat = (ControlsCategory)c;
        CHECK(GetControlsCategoryActionCount(cat) > 0);
        CHECK(GetControlsCategoryActions(cat) != nullptr);
        // First entry is a real action, not the UAN terminator.
        CHECK(GetControlsCategoryActions(cat)[0] != UAN);
    }
}

TEST_CASE("ControlsCategory: category names match the doc", "[Input][ControlsCategory]")
{
    CHECK(std::string(GetControlsCategoryName(ControlsCategoryOnFoot)) == "On foot");
    CHECK(std::string(GetControlsCategoryName(ControlsCategoryVehicles)) == "Vehicles");
    CHECK(std::string(GetControlsCategoryName(ControlsCategoryPilot)) == "Pilot");
    CHECK(std::string(GetControlsCategoryName(ControlsCategoryGunner)) == "Gunner");
    CHECK(std::string(GetControlsCategoryName(ControlsCategoryCommon)) == "Common");
}

TEST_CASE("ControlsCategory: out-of-range cat returns OnFoot defensively", "[Input][ControlsCategory]")
{
    // Defensive fallback — the helper shouldn't crash on a bad cast.
    CHECK(GetControlsCategoryActions((ControlsCategory)-1)[0] != UAN);
    CHECK(GetControlsCategoryActions((ControlsCategory)999)[0] != UAN);
    CHECK(std::string(GetControlsCategoryName((ControlsCategory)-1)).empty());
}

TEST_CASE("ControlsCategory: Fire shows in OnFoot/Vehicles/Pilot/Gunner, not Common", "[Input][ControlsCategory]")
{
    CHECK(IsActionInControlsCategory(UAFire, ControlsCategoryOnFoot));
    CHECK(IsActionInControlsCategory(UAFire, ControlsCategoryVehicles));
    CHECK(IsActionInControlsCategory(UAFire, ControlsCategoryPilot));
    CHECK(IsActionInControlsCategory(UAFire, ControlsCategoryGunner));
    CHECK_FALSE(IsActionInControlsCategory(UAFire, ControlsCategoryCommon));
}

TEST_CASE("ControlsCategory: Aim* lives under OnFoot and Gunner", "[Input][ControlsCategory]")
{
    for (UserAction a : {UAAimUp, UAAimDown, UAAimLeft, UAAimRight})
    {
        CHECK(IsActionInControlsCategory(a, ControlsCategoryOnFoot));
        CHECK_FALSE(IsActionInControlsCategory(a, ControlsCategoryVehicles));
        CHECK_FALSE(IsActionInControlsCategory(a, ControlsCategoryPilot));
        CHECK(IsActionInControlsCategory(a, ControlsCategoryGunner));
        CHECK_FALSE(IsActionInControlsCategory(a, ControlsCategoryCommon));
    }
}

TEST_CASE("ControlsCategory: Map/Compass/Watch are Common-only", "[Input][ControlsCategory]")
{
    for (UserAction a : {UAMap, UACompass, UAWatch})
    {
        CHECK(IsActionInControlsCategory(a, ControlsCategoryCommon));
        CHECK_FALSE(IsActionInControlsCategory(a, ControlsCategoryOnFoot));
        CHECK_FALSE(IsActionInControlsCategory(a, ControlsCategoryGunner));
    }
}

TEST_CASE("ControlsCategory: hidden actions appear in no category", "[Input][ControlsCategory]")
{
    // Joystick axes (Gamepad page disabled in v1) and cheat keys
    // (always hidden in this UI) must not show up anywhere.
    UserAction hidden[] = {UAAxisTurn, UAAxisDive, UAAxisRudder, UAAxisThrust};
    for (UserAction a : hidden)
        for (int c = 0; c < ControlsCategoryCount; c++)
            CHECK_FALSE(IsActionInControlsCategory(a, (ControlsCategory)c));

#if _ENABLE_CHEATS
    UserAction cheats[] = {UACheat1, UACheat2};
    for (UserAction a : cheats)
        for (int c = 0; c < ControlsCategoryCount; c++)
            CHECK_FALSE(IsActionInControlsCategory(a, (ControlsCategory)c));
#endif
}

TEST_CASE("ControlsCategory: Walk (UASlow) shows under OnFoot only", "[Input][ControlsCategory]")
{
    // "Walk" (UASlow, default F) must stay listed in the Controls UI, else a
    // player who rebound F off it has no way to restore it.
    CHECK(IsActionInControlsCategory(UASlow, ControlsCategoryOnFoot));
    CHECK_FALSE(IsActionInControlsCategory(UASlow, ControlsCategoryVehicles));
    CHECK_FALSE(IsActionInControlsCategory(UASlow, ControlsCategoryPilot));
    CHECK_FALSE(IsActionInControlsCategory(UASlow, ControlsCategoryGunner));
    CHECK_FALSE(IsActionInControlsCategory(UASlow, ControlsCategoryCommon));
}

TEST_CASE("ControlsCategory: VoN toggle and push-to-talk are adjacent Common actions", "[Input][ControlsCategory]")
{
    const UserAction* actions = GetControlsCategoryActions(ControlsCategoryCommon);
    for (int i = 0; actions[i] != UAN; i++)
    {
        if (actions[i] == UAVoiceOverNet)
        {
            REQUIRE(actions[i + 1] == UAVoiceOverNetPushToTalk);
            return;
        }
    }
    FAIL("VoiceOverNet action missing from Common controls");
}

TEST_CASE("ControlsCategory: every bindable action is reachable from some category", "[Input][ControlsCategory]")
{
    // Every keyboard-bindable action must appear in some category, else it's
    // stranded with no way to rebind or reset it (the "Walk" regression).
    // Only gamepad axes (axis=true) and cheat keys are legitimately hidden.
    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();

    std::set<int> covered;
    for (int c = 0; c < ControlsCategoryCount; c++)
    {
        const UserAction* list = GetControlsCategoryActions((ControlsCategory)c);
        for (int i = 0; list[i] != UAN; i++)
            covered.insert((int)list[i]);
    }

    std::set<int> exempt;
#if _ENABLE_CHEATS
    exempt.insert(UACheat1);
    exempt.insert(UACheat2);
#endif

    for (int a = 0; a < UAN; a++)
    {
        if (descs[a].axis || exempt.count(a))
            continue;
        CAPTURE(a, descs[a].name);
        CHECK(covered.count(a) > 0);
    }
}

TEST_CASE("ControlsCategory: no duplicate UserActions within a single category", "[Input][ControlsCategory]")
{
    for (int c = 0; c < ControlsCategoryCount; c++)
    {
        std::set<int> seen;
        const UserAction* list = GetControlsCategoryActions((ControlsCategory)c);
        for (int i = 0; list[i] != UAN; i++)
        {
            int v = (int)list[i];
            CAPTURE(c, i, v);
            CHECK(seen.insert(v).second);
        }
    }
}

TEST_CASE("ControlsCategory: union of all categories covers > 30 unique actions", "[Input][ControlsCategory]")
{
    // Sanity: doc claims 54 visible actions across 5 categories.
    // Most actions appear in multiple, so the unique union is smaller —
    // but it's still well over 30.  This catches catastrophic regressions
    // (whole categories accidentally cleared).
    std::set<int> unique;
    for (int c = 0; c < ControlsCategoryCount; c++)
    {
        const UserAction* list = GetControlsCategoryActions((ControlsCategory)c);
        for (int i = 0; list[i] != UAN; i++)
            unique.insert((int)list[i]);
    }
    CHECK(unique.size() > 30);
    CHECK(unique.size() < UAN); // should not include axes / cheats
}
