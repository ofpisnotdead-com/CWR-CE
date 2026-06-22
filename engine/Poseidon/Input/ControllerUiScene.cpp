#include <Poseidon/Input/ControllerUiScene.hpp>

namespace Poseidon
{

namespace
{
unsigned SceneCaps(const ControllerUiScene& scene)
{
    return scene.sceneCapabilities | scene.activeSection.capabilities;
}
} // namespace

ControllerUiSection FocusListSection(int ownerIdc)
{
    return {ControllerSectionKind::FocusList, CtrlNav | CtrlConfirm | CtrlCancel, ownerIdc};
}

ControllerUiSection PagerSection(int ownerIdc)
{
    return {ControllerSectionKind::Pager, CtrlNav | CtrlConfirm | CtrlCancel | CtrlPage, ownerIdc};
}

ControllerUiSection ModalButtonsSection(int ownerIdc)
{
    return {ControllerSectionKind::ModalButtons, CtrlNav | CtrlConfirm | CtrlCancel, ownerIdc};
}

ControllerUiSection SpatialCursorSection(int ownerIdc)
{
    return {ControllerSectionKind::SpatialCursor,
            CtrlNav | CtrlConfirm | CtrlCancel | CtrlPointer | CtrlEditorMode | CtrlPreview | CtrlDelete, ownerIdc};
}

ControllerUiSection TextFieldSection(int ownerIdc)
{
    return {ControllerSectionKind::TextField, CtrlNav | CtrlConfirm | CtrlCancel, ownerIdc};
}

ControllerUiSection CaptureSection(int ownerIdc)
{
    return {ControllerSectionKind::Capture, CtrlCapture | CtrlCancel, ownerIdc};
}

ControllerUiSection GameplaySection(int ownerIdc)
{
    return {ControllerSectionKind::Gameplay, CtrlPause, ownerIdc};
}

ControllerUiScene MenuControllerScene()
{
    ControllerUiScene scene;
    scene.kind = ControllerSceneKind::Menu;
    scene.activeSection = FocusListSection();
    scene.sceneCapabilities = scene.activeSection.capabilities;
    scene.menuFallbackAllowed = true;
    return scene;
}

ControllerUiScene EditorMapControllerScene(int ownerIdc)
{
    ControllerUiScene scene;
    scene.kind = ControllerSceneKind::EditorMap;
    scene.activeSection = SpatialCursorSection(ownerIdc);
    scene.sceneCapabilities = scene.activeSection.capabilities;
    scene.menuFallbackAllowed = false;
    scene.consumesLeftStick = true;
    scene.consumesConfirmAsPointer = true;
    return scene;
}

ControllerUiScene EditorDialogControllerScene(int ownerIdc)
{
    ControllerUiScene scene = MenuControllerScene();
    scene.kind = ControllerSceneKind::EditorDialog;
    scene.activeSection.ownerIdc = ownerIdc;
    return scene;
}

ControllerUiScene GameplayControllerScene()
{
    ControllerUiScene scene;
    scene.kind = ControllerSceneKind::Gameplay;
    scene.activeSection = GameplaySection();
    scene.sceneCapabilities = scene.activeSection.capabilities;
    scene.menuFallbackAllowed = false;
    return scene;
}

ControllerUiScene PauseMenuControllerScene()
{
    ControllerUiScene scene = MenuControllerScene();
    scene.kind = ControllerSceneKind::PauseMenu;
    scene.sceneCapabilities |= CtrlPause;
    return scene;
}

bool ControllerUiSceneAccepts(const ControllerUiScene& scene, ControllerUiAction action)
{
    const unsigned caps = SceneCaps(scene);
    switch (action)
    {
        case ControllerUiAction::NavigateUp:
        case ControllerUiAction::NavigateDown:
        case ControllerUiAction::NavigateLeft:
        case ControllerUiAction::NavigateRight:
            return (caps & CtrlNav) != 0;
        case ControllerUiAction::Confirm:
            return (caps & (CtrlConfirm | CtrlPointer | CtrlCapture)) != 0;
        case ControllerUiAction::Cancel:
            return (caps & CtrlCancel) != 0;
        case ControllerUiAction::PagePrevious:
        case ControllerUiAction::PageNext:
            return (caps & CtrlPage) != 0;
        case ControllerUiAction::PrimaryDown:
        case ControllerUiAction::PrimaryUp:
        case ControllerUiAction::PrimaryClick:
            return (caps & CtrlPointer) != 0;
        case ControllerUiAction::PreviousTab:
        case ControllerUiAction::NextTab:
            return (caps & CtrlEditorMode) != 0;
        case ControllerUiAction::Preview:
            return (caps & CtrlPreview) != 0;
        case ControllerUiAction::Delete:
            return (caps & CtrlDelete) != 0;
        case ControllerUiAction::Pause:
            return (caps & CtrlPause) != 0;
        default:
            return false;
    }
}

std::string BuildControllerPromptString(const ControllerUiScene& scene)
{
    const unsigned caps = SceneCaps(scene);
    std::string out;
    auto add = [&out](const char* prompt)
    {
        if (!out.empty())
            out += "|";
        out += prompt;
    };

    if (caps & CtrlConfirm)
        add("A Select");
    else if (caps & CtrlPointer)
        add("A Place");
    else if (caps & CtrlCapture)
        add("A Capture");

    if (caps & CtrlCancel)
        add("B Back");
    if (caps & CtrlPage)
        add("LT/RT Page");
    if (caps & CtrlEditorMode)
    {
        if (scene.kind == ControllerSceneKind::Multiplayer || scene.kind == ControllerSceneKind::ModsCatalog)
            add("LB/RB Source");
        else
            add("LB/RB Mode");
    }
    if (caps & CtrlPreview)
    {
        if (scene.kind == ControllerSceneKind::Multiplayer)
            add("X Refresh");
        else if (scene.kind == ControllerSceneKind::ModsCatalog)
            add("X Apply");
        else
            add("X Preview");
    }
    if (caps & CtrlDelete)
    {
        if (scene.kind == ControllerSceneKind::Multiplayer || scene.kind == ControllerSceneKind::ModsCatalog)
            add("Y Filter");
        else
            add("Y Delete");
    }
    if (caps & CtrlPause)
    {
        if (scene.kind == ControllerSceneKind::Menu)
            add("Start Options");
        else
            add("Start Pause");
    }

    return out;
}

const char* ControllerSceneKindName(ControllerSceneKind kind)
{
    switch (kind)
    {
        case ControllerSceneKind::None:
            return "None";
        case ControllerSceneKind::Menu:
            return "Menu";
        case ControllerSceneKind::Modal:
            return "Modal";
        case ControllerSceneKind::TextEntry:
            return "TextEntry";
        case ControllerSceneKind::Gameplay:
            return "Gameplay";
        case ControllerSceneKind::PauseMenu:
            return "PauseMenu";
        case ControllerSceneKind::Notebook:
            return "Notebook";
        case ControllerSceneKind::Map:
            return "Map";
        case ControllerSceneKind::EditorMap:
            return "EditorMap";
        case ControllerSceneKind::EditorDialog:
            return "EditorDialog";
        case ControllerSceneKind::Multiplayer:
            return "Multiplayer";
        case ControllerSceneKind::ModsCatalog:
            return "ModsCatalog";
        case ControllerSceneKind::DownloadModal:
            return "DownloadModal";
        case ControllerSceneKind::CommandMenu:
            return "CommandMenu";
        case ControllerSceneKind::Chat:
            return "Chat";
        case ControllerSceneKind::Viewer:
            return "Viewer";
        default:
            return "Unknown";
    }
}

const char* ControllerSectionKindName(ControllerSectionKind kind)
{
    switch (kind)
    {
        case ControllerSectionKind::None:
            return "None";
        case ControllerSectionKind::FocusList:
            return "FocusList";
        case ControllerSectionKind::Pager:
            return "Pager";
        case ControllerSectionKind::SpatialCursor:
            return "SpatialCursor";
        case ControllerSectionKind::TextField:
            return "TextField";
        case ControllerSectionKind::ModalButtons:
            return "ModalButtons";
        case ControllerSectionKind::Gameplay:
            return "Gameplay";
        case ControllerSectionKind::Capture:
            return "Capture";
        default:
            return "Unknown";
    }
}

} // namespace Poseidon
