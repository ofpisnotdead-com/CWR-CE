#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Input/ControllerUiLayout.hpp>
#include <Poseidon/Input/ControllerUiScene.hpp>

using namespace Poseidon;

namespace
{
Foundation::UITime TestTime(float seconds)
{
    return Foundation::UITime(static_cast<int>(seconds * 1000.0f));
}
} // namespace

TEST_CASE("Controller UI menu layout maps controller actions to keyboard equivalents", "[input][controller-ui]")
{
    ControllerMenuUiLayout layout;

    REQUIRE(layout.Map(ControllerUiAction::NavigateUp).key == SDLK_UP);
    REQUIRE(layout.Map(ControllerUiAction::NavigateDown).key == SDLK_DOWN);
    REQUIRE(layout.Map(ControllerUiAction::NavigateLeft).key == SDLK_LEFT);
    REQUIRE(layout.Map(ControllerUiAction::NavigateRight).key == SDLK_RIGHT);
    REQUIRE(layout.Map(ControllerUiAction::Confirm).key == SDLK_RETURN);
    REQUIRE(layout.Map(ControllerUiAction::Cancel).key == SDLK_ESCAPE);
    REQUIRE(layout.Map(ControllerUiAction::PagePrevious).key == SDLK_PAGEUP);
    REQUIRE(layout.Map(ControllerUiAction::PageNext).key == SDLK_PAGEDOWN);
}

TEST_CASE("Controller UI editor layout maps logical actions to editor dispatches", "[input][controller-ui]")
{
    ControllerEditorUiLayout layout;

    REQUIRE(layout.Map(ControllerUiAction::Confirm).kind == ControllerUiDispatchKind::PointerPrimaryClick);
    REQUIRE(layout.Map(ControllerUiAction::PrimaryDown).kind == ControllerUiDispatchKind::PointerPrimaryDown);
    REQUIRE(layout.Map(ControllerUiAction::PrimaryUp).kind == ControllerUiDispatchKind::PointerPrimaryUp);
    REQUIRE(layout.Map(ControllerUiAction::Cancel).key == SDLK_ESCAPE);
    REQUIRE(layout.Map(ControllerUiAction::NavigateUp).key == SDLK_UP);
    REQUIRE(layout.Map(ControllerUiAction::PreviousTab).kind == ControllerUiDispatchKind::PreviousTab);
    REQUIRE(layout.Map(ControllerUiAction::NextTab).kind == ControllerUiDispatchKind::NextTab);
    REQUIRE(layout.Map(ControllerUiAction::Preview).kind == ControllerUiDispatchKind::Preview);
    REQUIRE(layout.Map(ControllerUiAction::Delete).key == SDLK_DELETE);
}

TEST_CASE("Controller UI in-game layout maps pause to interrupt dispatch", "[input][controller-ui]")
{
    ControllerInGameUiLayout layout;

    REQUIRE(layout.Map(ControllerUiAction::Pause).kind == ControllerUiDispatchKind::Pause);
    REQUIRE(layout.Map(ControllerUiAction::Confirm).kind == ControllerUiDispatchKind::None);
}

TEST_CASE("Controller UI directions emit immediately then repeat after A3-style delays", "[input][controller-ui]")
{
    ControllerEditorUiLayout layout;

    REQUIRE(layout.ShouldEmitDirection(ControllerUiAction::NavigateDown, true, TestTime(10.0f)));
    REQUIRE_FALSE(layout.ShouldEmitDirection(ControllerUiAction::NavigateDown, true, TestTime(10.100f)));
    REQUIRE_FALSE(layout.ShouldEmitDirection(ControllerUiAction::NavigateDown, true, TestTime(10.332f)));
    REQUIRE(layout.ShouldEmitDirection(ControllerUiAction::NavigateDown, true, TestTime(10.333f)));
    REQUIRE_FALSE(layout.ShouldEmitDirection(ControllerUiAction::NavigateDown, true, TestTime(10.400f)));
    REQUIRE(layout.ShouldEmitDirection(ControllerUiAction::NavigateDown, true, TestTime(10.418f)));

    REQUIRE_FALSE(layout.ShouldEmitDirection(ControllerUiAction::NavigateDown, false, TestTime(10.500f)));
    REQUIRE(layout.ShouldEmitDirection(ControllerUiAction::NavigateDown, true, TestTime(10.600f)));
}

TEST_CASE("Controller UI direction state resets per direction", "[input][controller-ui]")
{
    ControllerEditorUiLayout layout;

    REQUIRE(layout.ShouldEmitDirection(ControllerUiAction::NavigateLeft, true, TestTime(1.0f)));
    REQUIRE_FALSE(layout.ShouldEmitDirection(ControllerUiAction::NavigateLeft, true, TestTime(1.1f)));
    layout.ResetDirection(ControllerUiAction::NavigateLeft);
    REQUIRE(layout.ShouldEmitDirection(ControllerUiAction::NavigateLeft, true, TestTime(1.2f)));
}

TEST_CASE("Controller UI menu scene accepts only simple navigation contract", "[input][controller-ui]")
{
    const ControllerUiScene scene = MenuControllerScene();

    REQUIRE(scene.kind == ControllerSceneKind::Menu);
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::NavigateUp));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::NavigateDown));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::NavigateLeft));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::NavigateRight));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Confirm));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Cancel));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::PagePrevious));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::PageNext));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::PreviousTab));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::NextTab));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::Preview));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::Delete));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::Pause));
    REQUIRE(BuildControllerPromptString(scene) == "A Select|B Back");
}

TEST_CASE("Controller UI main menu can advertise Start Options", "[input][controller-ui]")
{
    ControllerUiScene scene = MenuControllerScene();
    scene.sceneCapabilities |= CtrlPause;

    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Pause));
    REQUIRE(BuildControllerPromptString(scene) == "A Select|B Back|Start Options");
}

TEST_CASE("Controller UI pager scene adds page capability without extra shortcuts", "[input][controller-ui]")
{
    ControllerUiScene scene = MenuControllerScene();
    scene.activeSection = PagerSection(42);
    scene.sceneCapabilities = scene.activeSection.capabilities;

    REQUIRE(scene.activeSection.ownerIdc == 42);
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::NavigateLeft));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Confirm));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Cancel));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::PagePrevious));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::PageNext));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::Preview));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::Delete));
    REQUIRE(BuildControllerPromptString(scene) == "A Select|B Back|LT/RT Page");
}

TEST_CASE("Controller UI editor map scene accepts spatial editor controls", "[input][controller-ui]")
{
    const ControllerUiScene scene = EditorMapControllerScene(51);

    REQUIRE(scene.kind == ControllerSceneKind::EditorMap);
    REQUIRE(scene.consumesConfirmAsPointer);
    REQUIRE_FALSE(scene.menuFallbackAllowed);
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Confirm));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::PrimaryDown));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::PrimaryUp));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::PreviousTab));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::NextTab));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Preview));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Delete));
    REQUIRE(BuildControllerPromptString(scene) == "A Select|B Back|LB/RB Mode|X Preview|Y Delete");
}

TEST_CASE("Controller UI browser scenes use scene-specific secondary prompts", "[input][controller-ui]")
{
    ControllerUiScene multiplayer;
    multiplayer.kind = ControllerSceneKind::Multiplayer;
    multiplayer.activeSection = PagerSection();
    multiplayer.sceneCapabilities = multiplayer.activeSection.capabilities | CtrlPreview | CtrlDelete | CtrlEditorMode;

    REQUIRE(ControllerUiSceneAccepts(multiplayer, ControllerUiAction::Preview));
    REQUIRE(ControllerUiSceneAccepts(multiplayer, ControllerUiAction::Delete));
    REQUIRE(ControllerUiSceneAccepts(multiplayer, ControllerUiAction::PreviousTab));
    REQUIRE(BuildControllerPromptString(multiplayer) == "A Select|B Back|LT/RT Page|LB/RB Source|X Refresh|Y Filter");

    ControllerUiScene mods = multiplayer;
    mods.kind = ControllerSceneKind::ModsCatalog;
    REQUIRE(BuildControllerPromptString(mods) == "A Select|B Back|LT/RT Page|LB/RB Source|X Apply|Y Filter");
}

TEST_CASE("Controller UI gameplay scene only accepts pause at UI layer", "[input][controller-ui]")
{
    const ControllerUiScene scene = GameplayControllerScene();

    REQUIRE(scene.kind == ControllerSceneKind::Gameplay);
    REQUIRE_FALSE(scene.menuFallbackAllowed);
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Pause));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::Confirm));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::Cancel));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::NavigateUp));
    REQUIRE(BuildControllerPromptString(scene) == "Start Pause");
}

TEST_CASE("Controller UI pause menu keeps menu navigation and pause shortcut", "[input][controller-ui]")
{
    const ControllerUiScene scene = PauseMenuControllerScene();

    REQUIRE(scene.kind == ControllerSceneKind::PauseMenu);
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::NavigateDown));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Confirm));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Cancel));
    REQUIRE(ControllerUiSceneAccepts(scene, ControllerUiAction::Pause));
    REQUIRE_FALSE(ControllerUiSceneAccepts(scene, ControllerUiAction::Preview));
    REQUIRE(BuildControllerPromptString(scene) == "A Select|B Back|Start Pause");
}
