#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <Poseidon/UI/Options/IndexPage.hpp>
#include <Poseidon/UI/Options/NotebookTheme.hpp>

#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/World/World.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <SDL3/SDL_keycode.h>
#include <utility>
#include <Poseidon/Foundation/Strings/RString.hpp>

// Defined at global scope in World/World.cpp.  Avoid pulling in
// optionsUICommon.hpp / displayUI.hpp here, which would drag in the
// whole display-tree and break this TU's lighter-weight build.
void ShowCinemaBorder(bool show);

namespace Poseidon
{
void StartRandomCutscene(RString);
}

namespace Poseidon
{

OptionsShell::OptionsShell(ControlsContainer* parent, bool enableSimulation, bool credits)
    : Display(parent), _credits(credits)
{
    _enableSimulation = enableSimulation;
    Load("RscOptionsShell");
    PushPage(std::make_unique<IndexPage>());
    _langUiCbToken = RegisterLanguageChangedCallback(
        [this]()
        {
            if (auto* page = TopPage())
                page->OnReshown(*this);
            RefreshTitle();
        });
}

OptionsShell::OptionsShell(ControlsContainer* parent) : OptionsShell(parent, true, true) {}

OptionsShell::~OptionsShell()
{
    TeardownShell();
}

ControlObjectContainer* OptionsShell::GetNotebook()
{
    return dynamic_cast<ControlObjectContainer*>(GetCtrl(105));
}

bool OptionsShell::FocusNotebookCtrl(int idc)
{
    auto* notebook = GetNotebook();
    if (notebook)
    {
        IControl* ctrl = notebook->GetCtrl(idc);
        if (!ctrl || !ctrl->IsVisible() || !ctrl->IsEnabled())
            return false;
        FocusCtrl(idc);
        return GetFocusedNotebookIdc() == idc;
    }

    for (int mountedIdc : m_debugNotebookIdcs)
    {
        if (mountedIdc == idc)
        {
            _debugFocusedNotebookIdc = idc;
            return true;
        }
    }
    return false;
}

int OptionsShell::GetFocusedNotebookIdc()
{
    auto* notebook = GetNotebook();
    return notebook ? notebook->GetFocusedIdc() : _debugFocusedNotebookIdc;
}

void OptionsShell::DebugSetNotebookMountedIdcs(std::initializer_list<int> idcs)
{
    m_debugNotebookIdcs.assign(idcs.begin(), idcs.end());
    if (_debugFocusedNotebookIdc >= 0)
    {
        bool stillMounted = false;
        for (int mountedIdc : m_debugNotebookIdcs)
        {
            if (mountedIdc == _debugFocusedNotebookIdc)
            {
                stillMounted = true;
                break;
            }
        }
        if (!stillMounted)
            _debugFocusedNotebookIdc = -1;
    }
}

void OptionsShell::DebugClearNotebookMountedIdcs()
{
    m_debugNotebookIdcs.clear();
    _debugFocusedNotebookIdc = -1;
}

void OptionsShell::RefreshTitle()
{
    const char* text = LocalizeString("STR_DISP_OPTIONS_TITLE");
    if (auto* page = TopNonModalPage())
        text = page->TitleText();
    if (auto* t = dynamic_cast<CStatic*>(GetCtrl(100)))
        t->SetText(RString(text));
}

void OptionsShell::PushPage(std::unique_ptr<OptionsPage> page)
{
    if (!page)
        return;
    // Hide the underlying page's controls regardless of modal-ness.
    // For non-modal transitions, this is the obvious "the new page
    // takes the screen" behaviour.  For modals it also matters: modal
    // buttons share the notebook surface with the underlying page's
    // transparent click overlays (slot Hover / chevron / bar zones),
    // and overlapping rows otherwise win the hit-test in declaration
    // order — which is why clicking a modal button sometimes only
    // hovers/highlights it but doesn't activate (the click resolved
    // to the underlying slot's overlay, never reached the modal IDC).
    // Hiding makes click dispatch unambiguous and matches the semantic
    // that a modal owns input.  The visible modal panel is opaque so
    // the user still sees a frame and label; the laptop chrome and
    // scene around it stay visible because they're not part of the
    // page's mounted controls.
    if (auto* prev = TopPage())
        ShowPageControls(prev, false);

    OptionsPage* p = page.get();
    m_stack.push_back(std::move(page));
    p->Mount(*this);
    ApplyNotebookTheme(p);
    int focusIdc = p->DefaultFocusIdc();
    if (focusIdc >= 0)
        FocusNotebookCtrl(focusIdc);
    p->OnReshown(*this);
    RefreshTitle();
}

void OptionsShell::PopPage()
{
    if (m_stack.empty())
    {
        BeginCloseAnim(IDC_CANCEL);
        return;
    }
    auto top = std::move(m_stack.back());
    m_stack.pop_back();
    top->Unmount(*this);
    top.reset();

    // PushPage hides the previous page's controls in all cases (modal
    // or not — see comment there).  Symmetric re-show on pop.
    if (auto* now = TopPage())
    {
        ShowPageControls(now, true);
        // Default focus first, OnReshown last — the latter is the place
        // pages override focus to remember per-page state (e.g.
        // FlatMenuPage's last-nav-idc memory).  Without this order the
        // default would trample the remembered focus.
        int focusIdc = now->DefaultFocusIdc();
        if (focusIdc >= 0)
            FocusNotebookCtrl(focusIdc);
        now->OnReshown(*this);
        RefreshTitle();
    }
    else
    {
        BeginCloseAnim(IDC_CANCEL);
    }
}

OptionsPage* OptionsShell::TopPage()
{
    return m_stack.empty() ? nullptr : m_stack.back().get();
}

OptionsPage* OptionsShell::TopNonModalPage()
{
    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it)
    {
        if (!(*it)->IsModal())
            return it->get();
    }
    return nullptr;
}

void OptionsShell::ShowPageControls(OptionsPage* page, bool show)
{
    if (!page)
        return;
    auto* nb = GetNotebook();
    if (!nb)
        return;
    for (int idc : page->MountedIdcs())
    {
        if (auto* c = nb->GetCtrl(idc))
            c->ShowCtrl(show);
    }
    if (show)
        ApplyNotebookTheme(page);
}

void OptionsShell::ApplyNotebookTheme(OptionsPage* page)
{
    if (!page)
        return;
    auto* nb = GetNotebook();
    if (!nb)
        return;

    PackedColor textColor;
    bool hasTextColor = NotebookTheme::ResourceColor("RscObjNotebookText", "color", textColor);
    PackedColor buttonColor;
    bool hasButtonColor = NotebookTheme::ResourceColor("RscObjNotebookButton", "color", buttonColor);
    PackedColor buttonActiveColor;
    bool hasButtonActiveColor = NotebookTheme::ResourceColor("RscObjNotebookButton", "colorActive", buttonActiveColor);

    for (int idc : page->MountedIdcs())
    {
        IControl* c = nb->GetCtrl(idc);
        if (auto* active = dynamic_cast<C3DActiveText*>(c))
        {
            if (hasButtonColor)
                active->SetColor(buttonColor);
            if (hasButtonActiveColor)
                active->SetActiveColor(buttonActiveColor);
        }
        else if (auto* text = dynamic_cast<C3DStatic*>(c))
        {
            if (hasTextColor)
                text->SetColor(textColor);
        }
    }
}

void OptionsShell::TeardownShell()
{
    if (_langUiCbToken >= 0)
    {
        UnregisterLanguageChangedCallback(_langUiCbToken);
        _langUiCbToken = -1;
    }
    while (!m_stack.empty())
    {
        auto top = std::move(m_stack.back());
        m_stack.pop_back();
        top->Unmount(*this);
        top.reset();
    }
    DebugClearNotebookMountedIdcs();
}

Control* OptionsShell::OnCreateCtrl(int type, int idc, const ParamEntry& cls)
{
    if (auto* page = TopPage())
    {
        if (Control* ctrl = page->OnCreateControl(*this, type, idc, cls))
            return ctrl;
    }
    return Display::OnCreateCtrl(type, idc, cls);
}

void OptionsShell::OnButtonClicked(int idc)
{
    if (auto* page = TopPage())
    {
        if (page->OnButtonClicked(*this, idc))
            return;
    }
    // Intercept the dialog-exit IDCs the base ControlsContainer would
    // turn into an immediate Exit(idc).  Defer the Exit until the
    // notebook close animation has finished — see BeginCloseAnim.
    if (idc == IDC_OK || idc == IDC_CANCEL)
    {
        BeginCloseAnim(idc);
        return;
    }
    Display::OnButtonClicked(idc);
}

void OptionsShell::BeginCloseAnim(int exitIdc)
{
    // If we're already mid-close (defensive — could happen if a page
    // forwards a second Cancel during the animation), don't restart it.
    if (_exitWhenClose >= 0)
        return;
    auto* nb = dynamic_cast<ControlObjectContainerAnim*>(GetCtrl(IDC_OPTIONS_NOTEBOOK));
    if (!nb)
    {
        // No notebook (or it's the non-animated container variant).
        // Fall back to immediate exit so Cancel still closes the screen.
        Exit(exitIdc);
        return;
    }
    _exitWhenClose = exitIdc;
    nb->Close();
}

void OptionsShell::OnCtrlClosed(int idc)
{
    if (idc == IDC_OPTIONS_NOTEBOOK)
    {
        Exit(_exitWhenClose);
        return;
    }
    Display::OnCtrlClosed(idc);
}

void OptionsShell::OnChildDestroyed(int idd, int exit)
{
    Display::OnChildDestroyed(idd, exit);

    // Mirror DisplayOptions::OnChildDestroyed: when the credits
    // cutscene's DisplayIntro tears down, drop the credits world and
    // restart the main-menu background cutscene.  Without this the
    // credits world map (with its right-side panel overlay) stays mounted
    // behind the laptop after Esc.
    if (idd == IDD_INTRO)
    {
        GWorld->DestroyMap(IDC_OK);
        ::ShowCinemaBorder(true);
        Poseidon::StartRandomCutscene(Glob.header.worldname);
    }
}

bool OptionsShell::OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (auto* page = TopPage())
    {
        if (page->OnKeyDown(*this, nChar))
            return true;
    }
    return Display::OnKeyDown(nChar, nRepCnt, nFlags);
}

ControllerUiScene OptionsShell::GetControllerUiScene() const
{
    if (m_stack.empty())
        return Display::GetControllerUiScene();
    return m_stack.back()->GetControllerUiScene();
}

void OptionsShell::OnSimulate(EntityAI* vehicle)
{
    // Page first so scroll-list pages can drain the cursor wheel
    // before Display::OnSimulate forwards it to the notebook (whose
    // OnMouseZChanged would otherwise dolly the camera).
    if (auto* page = TopPage())
        page->OnSimulate(*this);
    Display::OnSimulate(vehicle);
}

void OptionsShell::Destroy()
{
    TeardownShell();
    Display::Destroy();
    // In production the shell is a child of the main menu (or in-game
    // Interrupt display) — Display::Destroy returns control to the
    // parent.  Esc / Close click route through Exit(IDC_CANCEL) →
    // Destroy → here, which is what DisplayOptions does.
}

} // namespace Poseidon
