#pragma once

// PoseidonOptionsTest — abstract base for an Options page mounted into
// the shared OptionsShell notebook.
//
// Lifecycle
//   ctor                     — page object exists, no UI yet
//   Mount(shell)             — load the page's 3D sub-controls into the
//                              shell's notebook; remember their IDCs so
//                              Unmount can drop them
//   OnButtonClicked / OnKeyDown / OnSimulate — input from the shell
//   Unmount(shell)           — RemoveControl every IDC mounted; reset
//                              focus state so the next page starts clean
//   dtor                     — page object freed by the shell when it
//                              pops the page off the stack
//
// Mounting model
//   Each page ships a config-class name (e.g. "RscOptionsPageIndex")
//   whose `controls[]` holds the 3D sub-controls the page wants on the
//   notebook surface.  MountFromClass() walks the list and calls
//   Notebook::LoadControl on each one — same machinery the engine uses
//   at Display::Load time, just driven on demand.
//
//   Pages that want to *layer* on top of the active page (modals)
//   override KeepBeneathInStack() to true.  The shell hides the
//   beneath page's controls when a modal mounts and re-shows them on
//   pop, so IDC collisions across the stack stay impossible while the
//   modal is up.

#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Input/ControllerUiScene.hpp>
#include <Poseidon/Input/UserAction.hpp>

#include <vector>


namespace Poseidon
{
class OptionsShell;

class OptionsPage
{
public:
	virtual ~OptionsPage() = default;

	// One-line page name — shown in the shell's title strip while this
	// page is the active (top-of-stack-not-modal) page.
	virtual const char* TitleText() const = 0;

	// True for modal pages (Confirm, ConfirmRevert, PressKey,
	// PressButton).  When pushed, the page beneath stays mounted but is
	// hidden so its IDCs don't conflict and so its content doesn't
	// fight the modal panel for the same screen pixels.
	virtual bool IsModal() const { return false; }

	// Default focus IDC for this page — focused on Mount.  Pages with
	// no focusable controls return -1.
	virtual int DefaultFocusIdc() const = 0;

	// Resource class name (in the global Pars tree) whose `controls[]`
	// list defines the 3D sub-controls this page mounts.  Empty for
	// pages that build their controls programmatically.
	virtual const char* ResourceClassName() const = 0;
	virtual ControllerUiScene GetControllerUiScene() const { return MenuControllerScene(); }

	virtual void Mount(OptionsShell& shell);
	virtual void Unmount(OptionsShell& shell);

	// Called when the shell un-hides this page's controls after popping
	// a page that was on top.  ShowPageControls flips ShowCtrl(true) on
	// every mounted IDC, but pages may have applied per-row visibility
	// (e.g. hiding the left-aligned label on Action rows so the centred
	// stretched label doesn't double up); pages that depend on that
	// state should override and re-apply.  Default no-op.
	virtual void OnReshown(OptionsShell& /*shell*/) {}

	// Input dispatch from the shell.  Return true to mark the event as
	// handled (the shell stops walking the stack).
	virtual bool OnButtonClicked(OptionsShell& shell, int idc) { return false; }
	virtual bool OnKeyDown(OptionsShell& shell, unsigned nChar) { return false; }
	virtual void OnSimulate(OptionsShell& shell) {}
	virtual Control* OnCreateControl(OptionsShell& /*shell*/, int /*type*/, int /*idc*/, const ParamEntry& /*cls*/)
	{
		return nullptr;
	}

	// IDCs this page mounted — populated by MountFromClass.  Subclasses
	// that mount additional controls via shell.GetNotebook()->LoadControl
	// should append their idcs here too so Unmount cleans them up.
	const std::vector<int>& MountedIdcs() const { return m_mountedIdcs; }

protected:
	// Helper: walk a config class's controls[] list and LoadControl each
	// entry into the shell's notebook.  Called automatically by Mount()
	// when ResourceClassName() returns non-empty.  Subclasses can call
	// it directly with a different class name to mount auxiliary content.
	void MountFromClass(OptionsShell& shell, const char* className);

	// Up/Down (and optional Left/Right/Tab) wrap helper for pages with a
	// small fixed cycle of focusable IDCs — flat menus, modal button
	// rows, etc.  Returns true if the key was an arrow / Tab and focus
	// was moved (in which case the caller should return true from
	// OnKeyDown so the engine cascade doesn't also process it).
	//
	//   nChar       — the SDL keycode the page received
	//   idcs/count  — ordered cycle; current focus is matched by IDC
	//   mode        — VerticalOnly: only Up/Down advance.  AnyAxis:
	//                 Left/Up = prev, Right/Down/Tab = next.
	enum WrapMode { VerticalOnly, AnyAxis };
	static bool ContainsCycleIdc(int idc, const int* idcs, int count);
	static bool WrapFocus(OptionsShell& shell, unsigned nChar,
	                      const int* idcs, int count, WrapMode mode = VerticalOnly);
	static bool FocusCycleIdc(OptionsShell& shell, int idc, const int* idcs, int count);

	// SetText helper — finds the control at idc and sets its text via
	// whichever of the three text-bearing types matches (CStatic, C3DStatic,
	// C3DActiveText).  No-op if the idc isn't bound or its type is none of
	// these.  Saves the dynamic_cast<T*>(GetCtrl(idc))->SetText(RString(...))
	// dance that every modal repeats.
	static void SetCtrlText(class Display& display, int idc, const char* text);

	std::vector<int> m_mountedIdcs;
};

const char* ControlActionLabel(UserAction action);

} // namespace Poseidon
