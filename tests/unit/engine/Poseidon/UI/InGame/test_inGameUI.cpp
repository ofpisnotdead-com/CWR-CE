// InGameUIImpl.hpp is not self-contained — it needs World/AI types (TargetId, AIRadio)
// defined first, the same order InGameUIMenuSim.cpp includes them.
#include <Poseidon/World/World.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/InGame/InGameUI.hpp>
#include <Poseidon/UI/InGame/InGameUIImpl.hpp>
#include <Poseidon/UI/InGame/InGameUIGroupUnitLabel.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

using namespace Poseidon;

namespace
{
class ZeroEvaluatorFunctions final : public EvaluatorFunctions
{
  public:
    float EvaluateFloat(const char* /*expr*/, GameVarSpace* /*vars*/) override { return 0.0f; }
    float EvaluateFloatInternal(const char* /*expr*/) override { return 0.0f; }
};

class ScopedEvaluatorFunctions final
{
  public:
    explicit ScopedEvaluatorFunctions(EvaluatorFunctions* replacement) : _previous(ParamFile::DefaultEvalFunctions())
    {
        ParamFile::SetDefaultEvalFunctions(replacement);
    }

    ~ScopedEvaluatorFunctions() { ParamFile::SetDefaultEvalFunctions(_previous); }

    ScopedEvaluatorFunctions(const ScopedEvaluatorFunctions&) = delete;
    ScopedEvaluatorFunctions& operator=(const ScopedEvaluatorFunctions&) = delete;

  private:
    EvaluatorFunctions* _previous;
};
} // namespace

TEST_CASE("inGameUI compiles", "[inGameUI][tier3]")
{
    REQUIRE(sizeof(AbstractUI) > 0);
}

TEST_CASE("InGameUI group unit labels hide debug subgroup leaders in release HUD", "[inGameUI][groupInfo]")
{
    char label[8];

    FormatGroupUnitLabel(label, sizeof(label), 2, 2, false);
    REQUIRE(std::string(label) == "2");

    FormatGroupUnitLabel(label, sizeof(label), 2, 2, true);
    REQUIRE(std::string(label) == "2(2)");
}

// A mission's description.ext can override/break RscMainMenu so it has no movement
// command sub-menu. InGameUI::InitMenu then read a null FindMenu(CMD_MOVE_FIRST) result
// and dereferenced it (menuDist->_parent at +0x30) — 0xC0000005 in InitMenu+0xbd
// The fix guards the null and
// skips the move-menu wiring; without it, WireMovementCommandMenu(&emptyMenu) AVs.
TEST_CASE("InGameUI move-menu wiring tolerates a RscMainMenu without move commands", "[inGameUI][menu]")
{
    Menu* menu = new Menu(); // empty — no movement command sub-menu

    REQUIRE(menu->FindMenu(CMD_MOVE_FIRST, true) == nullptr); // the crashing precondition

    InGameUI::WireMovementCommandMenu(menu); // must not crash (was a null deref pre-fix)

    SUCCEED("survived a RscMainMenu missing the movement command menu");
    delete menu;
}

TEST_CASE("InGameUI menu loader resolves legacy command names from resources", "[inGameUI][menu]")
{
    const char* resource =
        "class RscSubmenu\n"
        "{\n"
        "    atomic = 0;\n"
        "    items[] = {\"Back\"};\n"
        "    class Back { title = \"\"; key = 14; character = 0; command = -4; };\n"
        "};\n"
        "class RscMainMenu\n"
        "{\n"
        "    title = \"\";\n"
        "    atomic = 0;\n"
        "    items[] = {\"Reply\"};\n"
        "    class Reply { title = \"Reply\"; key = 10; character = 9; command = -2; menu = \"RscReply\"; };\n"
        "};\n"
        "class RscReply: RscSubmenu\n"
        "{\n"
        "    title = \"Reply\";\n"
        "    items[] = {\"UserRadio\", \"Back\"};\n"
        "    class UserRadio\n"
        "    {\n"
        "        title = \"Custom\";\n"
        "        key = 10;\n"
        "        character = 9;\n"
        "        command = \"CMD_RADIO_CUSTOM\";\n"
        "        menu = \"RscUserRadio\";\n"
        "    };\n"
        "};\n"
        "class RscUserRadio: RscSubmenu\n"
        "{\n"
        "    title = \"Custom\";\n"
        "    atomic = 1;\n"
        "    items[] = {\"Back\"};\n"
        "};\n";

    QIStream in(resource, static_cast<int>(strlen(resource)));
    Res.Parse(in);

    ZeroEvaluatorFunctions zeroEvaluator;
    ScopedEvaluatorFunctions scopedEvaluator(&zeroEvaluator);

    Menu menu;
    menu.Load(&(Res >> "RscMainMenu"));

    MenuItem* item = menu.Find(CMD_RADIO_CUSTOM, true);
    REQUIRE(item != nullptr);
    REQUIRE(item->_cmd == CMD_RADIO_CUSTOM);
    REQUIRE(item->_submenu != nullptr);
}
