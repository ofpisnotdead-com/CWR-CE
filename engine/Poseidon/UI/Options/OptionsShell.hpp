#pragma once

// Single Display that owns the laptop notebook for the entire Options
// session.  Replaces the DisplayOptions (engine/poseidon/UI/
// optionsUIImpl.cpp) — same constructor signature, same lifetime
// semantics (parented to the main menu or in-game Interrupt display,
// returns control on Exit(IDC_CANCEL)) — but the body is a push/pop
// stack of OptionsPage objects mounted into a single notebook instead
// of the display-tree-of-children.
//
// All pages (Index, Audio, Graphics, Display, Controls, KB&M, Gamepad)
// and all modals (Confirm, ConfirmRevert, PressKey, PressButton) live
// inside this one shell as `OptionsPage` objects.  Pages get pushed
// onto / popped off a stack; the shell mounts the active page's 3D
// sub-controls into its own notebook (no second notebook gets
// instantiated, no second laptop gets rendered).
//
// Modals stack on top of the active page: when a modal page is
// pushed, the page beneath has its mounted controls hidden so IDC
// collisions across the stack stay impossible while the modal is up.
//
// Resource: RscOptionsShell (idd 9099) — top + bottom 2D chrome
// strips, a 2D Title (idc 100) driven from active page's TitleText(),
// and a 3D Notebook (idc 105) into which pages mount their controls.

#include <Poseidon/UI/Options/OptionsPage.hpp>

#include <Poseidon/UI/Controls/UIControls.hpp>

#include <initializer_list>
#include <memory>
#include <vector>


namespace Poseidon
{
class OptionsShell : public Display
{
public:
	// Mirrors DisplayOptions(parent, enableSim, credits).
	// `credits` controls whether the Index's Credits nav row is visible
	// (hidden for the in-game Interrupt menu).
	OptionsShell(ControlsContainer* parent, bool enableSimulation, bool credits);
	// Convenience overload for non-game callers (PoseidonOptionsTest)
	// that don't have an enableSimulation/credits context — defaults
	// to the same values DisplayMain passes for the in-game flow.
	explicit OptionsShell(ControlsContainer* parent);
	~OptionsShell() override;

	bool EnableSimulation() const { return SimulationEnabled(); }
	bool ShouldShowCredits() const { return _credits; }

	// Pages.  Push transfers ownership; the page is Mounted immediately
	// and its DefaultFocusIdc is focused.  Pop tears down the top page
	// and re-shows the page beneath (if any).
	void PushPage(std::unique_ptr<OptionsPage> page);
	void PopPage();

	// The notebook every page mounts into when the shell resources are
	// loaded.  Unit tests can also drive a debug fallback focus path.
	ControlObjectContainer* GetNotebook();
	bool FocusNotebookCtrl(int idc);
	int GetFocusedNotebookIdc();
	void DebugSetNotebookMountedIdcs(std::initializer_list<int> idcs);
	void DebugClearNotebookMountedIdcs();

	// Title text re-resolved on every page transition.  Pages override
	// TitleText() to return what they want shown.
	void RefreshTitle();

	// Display overrides — dispatch to the top page on the stack first
	// (modal-aware: modal sees input before the page beneath).
	Control* OnCreateCtrl(int type, int idc, const ParamEntry& cls) override;
	void OnButtonClicked(int idc) override;
	bool OnKeyDown(unsigned nChar, unsigned nRepCnt, unsigned nFlags) override;
	ControllerUiScene GetControllerUiScene() const override;
	void OnSimulate(EntityAI* vehicle) override;
	// Notebook close animation completed → finish the deferred Exit.
	void OnCtrlClosed(int idc) override;
	// Credits cutscene (DisplayIntro) ended → tear down its world map and
	// restart the main-menu background cutscene.  Without this the credits
	// world's right-panel overlay stays visible behind the laptop after the
	// intro display destroys itself.
	void OnChildDestroyed(int idd, int exit) override;

	// Destroy unwinds the page stack then defers to Display::Destroy
	// which returns control to the parent.
	void Destroy() override;

private:
	OptionsPage* TopPage();
	OptionsPage* TopNonModalPage();
	void ShowPageControls(OptionsPage* page, bool show);
	void ApplyNotebookTheme(OptionsPage* page);
	void TeardownShell();

	// Trigger the notebook close-animation, then Exit(exitIdc) when it
	// completes (see OnCtrlClosed).  Mirrors the DisplayLogin /
	// DisplayOptions / DisplaySingleMission pattern so the laptop closes
	// with the same flourish it opened with — instead of vanishing
	// instantly on Back/Cancel.
	void BeginCloseAnim(int exitIdc);

	std::vector<std::unique_ptr<OptionsPage>> m_stack;
	std::vector<int> m_debugNotebookIdcs;
	int _debugFocusedNotebookIdc = -1;
	bool _credits;
	int _langUiCbToken = -1;
	int _exitWhenClose = -1;
};

} // namespace Poseidon
