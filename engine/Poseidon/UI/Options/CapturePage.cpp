#include <Poseidon/UI/Options/CapturePage.hpp>

#include <Poseidon/UI/Options/OptionsShell.hpp>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <SDL3/SDL_keycode.h>

#include <cstdio>
#include <utility>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

extern RString GetKeyName(int dikCode);

namespace
{
constexpr unsigned long kCaptureDoubleTapWindowMs = 400;
} // namespace

CapturePage::CapturePage(Idcs idcs, std::string actionLabel, std::string slotName, SaveCallback onSave,
                         ConflictCallback onConflict)
    : m_onSave(std::move(onSave)), m_onConflict(std::move(onConflict)), m_actionLabel(std::move(actionLabel)),
      m_slotName(std::move(slotName)), m_idcs(idcs)
{
}

void CapturePage::Mount(OptionsShell& shell)
{
    OptionsPage::Mount(shell);
    RefreshTitle(shell);
    RefreshStatus(shell);

    // Save / Retry stay hidden until something is captured.
    if (auto* c = shell.GetCtrl(m_idcs.save))
        c->ShowCtrl(false);
    if (auto* c = shell.GetCtrl(m_idcs.retry))
        c->ShowCtrl(false);
}

bool CapturePage::OnButtonClicked(OptionsShell& shell, int idc)
{
    if (m_resolved)
        return true;

    if (idc == m_idcs.save)
    {
        Resolve(shell, true);
        return true;
    }
    if (idc == m_idcs.cancel || idc == IDC_CANCEL)
    {
        Resolve(shell, false);
        return true;
    }
    if (idc == m_idcs.retry)
    {
        m_state = Listening;
        m_capturedCode = -1;
        m_capturedModifier = -1;
        m_capturedAtMs = 0;
        m_conflictAction = UAN;
        m_conflictActionLabel.clear();
        if (auto* c = shell.GetCtrl(m_idcs.save))
            c->ShowCtrl(false);
        if (auto* c = shell.GetCtrl(m_idcs.retry))
            c->ShowCtrl(false);
        RefreshTitle(shell);
        RefreshStatus(shell);
        return true;
    }
    return false;
}

bool CapturePage::OnKeyDown(OptionsShell& shell, unsigned nChar)
{
    if (m_resolved)
        return false;
    if (nChar == SDLK_ESCAPE)
    {
        if (m_state == Listening)
            Resolve(shell, false);
        return true; // eat in Captured state — avoids race with synthetic key tap from triGpadButton
    }

    if (m_state != Listening)
    {
        if (m_suppressNavKeys)
        {
            m_suppressNavKeys = false;
            // Only suppress nav (focus-cycling) keys — e.g. the UP tap injected
            // alongside triGpadPov.  Activation keys (RETURN/SPACE) must fall through.
            if (nChar == SDLK_UP || nChar == SDLK_DOWN || nChar == SDLK_LEFT || nChar == SDLK_RIGHT)
                return true;
        }
        const int kCycle[] = {m_idcs.save, m_idcs.retry, m_idcs.cancel};
        if (WrapFocus(shell, nChar, kCycle, 3, AnyAxis))
            return true;
        // RETURN / SPACE / numpad-Enter → activate the focused button.
        if (nChar == SDLK_RETURN || nChar == SDLK_KP_ENTER || nChar == SDLK_SPACE)
            return OnButtonClicked(shell, shell.GetFocusedNotebookIdc());
        return false;
    }

    int packed = -1;
    int modifier = -1;
    Result r = InterpretKey(nChar, packed, modifier);
    if (r != Result::Main || packed < 0)
        return true; // refused / modifier-only — eat the key, stay listening

    TryCapture(shell, packed, modifier);
    return true;
}

bool CapturePage::TryCapture(OptionsShell& shell, int packedCode, int modifier)
{
    if (m_resolved || m_state != Listening || packedCode < 0)
        return false;

    m_capturedCode = packedCode;
    m_capturedModifier = modifier;
    m_capturedAtMs = Foundation::GlobalTickCount();
    m_state = Captured;
    m_suppressNavKeys = true; // eat the synthetic key tap that accompanies triGpadPov
    RefreshConflict();
    RefreshTitle(shell);
    RefreshStatus(shell);

    if (auto* c = shell.GetCtrl(m_idcs.save))
        c->ShowCtrl(true);
    if (auto* c = shell.GetCtrl(m_idcs.retry))
        c->ShowCtrl(true);

    if (auto* save = dynamic_cast<C3DActiveText*>(shell.GetCtrl(m_idcs.save)))
        save->SetText(LocalizeString(m_conflictAction == UAN ? "STR_DISP_OPT_CAP_SAVE" : "STR_DISP_OPT_CAP_REPLACE"));
    shell.FocusCtrl(m_idcs.save);
    return true;
}

bool CapturePage::TryUpgradeToDoubleTap(OptionsShell& shell, int packedCode, int modifier)
{
    if (m_resolved || m_state != Captured || packedCode < 0)
        return false;
    if (InputBindingIsDoubleTap(m_capturedCode))
        return false;
    if (modifier != m_capturedModifier)
        return false;
    if (InputBindingBaseCode(packedCode) != InputBindingBaseCode(m_capturedCode))
        return false;

    const unsigned long now = Foundation::GlobalTickCount();
    if (now - m_capturedAtMs > kCaptureDoubleTapWindowMs)
        return false;

    m_capturedCode = InputBindingDoubleTapCode(packedCode);
    m_capturedAtMs = now;
    RefreshConflict();
    RefreshTitle(shell);
    RefreshStatus(shell);
    if (auto* save = dynamic_cast<C3DActiveText*>(shell.GetCtrl(m_idcs.save)))
        save->SetText(LocalizeString(m_conflictAction == UAN ? "STR_DISP_OPT_CAP_SAVE" : "STR_DISP_OPT_CAP_REPLACE"));
    return true;
}

bool CapturePage::TryUpgradeToCombo(OptionsShell& shell, int packedCode, int modifier)
{
    if (m_resolved || m_state != Captured || packedCode < 0)
        return false;
    // Only a modifier captured on its own, with no qualifier of its own, can
    // absorb a following key.
    if (m_capturedModifier >= 0 || !IsBareModifierCode(m_capturedCode))
        return false;
    // The following key must be a real key still qualified by the modifier we
    // captured; a different or missing one means it was released (a fresh press).
    if (IsBareModifierCode(packedCode) || modifier != m_capturedCode)
        return false;

    m_capturedModifier = m_capturedCode;
    m_capturedCode = packedCode;
    m_capturedAtMs = Foundation::GlobalTickCount();
    RefreshConflict();
    RefreshTitle(shell);
    RefreshStatus(shell);
    if (auto* save = dynamic_cast<C3DActiveText*>(shell.GetCtrl(m_idcs.save)))
        save->SetText(LocalizeString(m_conflictAction == UAN ? "STR_DISP_OPT_CAP_SAVE" : "STR_DISP_OPT_CAP_REPLACE"));
    return true;
}

void CapturePage::Resolve(OptionsShell& shell, bool save)
{
    if (m_resolved)
        return;
    m_resolved = true;
    if (save && m_state == Captured && m_onSave)
        m_onSave(m_capturedCode, m_capturedModifier, m_conflictAction != UAN);
    shell.PopPage();
}

void CapturePage::RefreshConflict()
{
    if (!m_onConflict)
    {
        m_conflictAction = UAN;
        m_conflictActionLabel.clear();
        return;
    }
    m_conflictAction = m_onConflict(m_capturedCode, m_capturedModifier);
    m_conflictActionLabel = (m_conflictAction != UAN) ? ControlActionLabel(m_conflictAction) : "";
}

void CapturePage::RefreshTitle(OptionsShell& shell)
{
    char title[160];
    if (m_state == Captured)
    {
        // "Captured: %s" — single short line, fits dialog title at
        // OPT_DLG_TITLE_3D_SIZE.  Modifier prefix included for combos.
        RString mainName = GetKeyName(m_capturedCode);
        std::string formatted;
        if (m_capturedModifier >= 0)
        {
            RString modName = GetKeyName(m_capturedModifier);
            formatted = (const char*)modName;
            formatted += "+";
            formatted += (const char*)mainName;
        }
        else
        {
            formatted = (const char*)mainName;
        }
        snprintf(title, sizeof(title), (const char*)LocalizeString("STR_DISP_OPT_CAP_CAPTURED"), formatted.c_str());
    }
    else
    {
        // OptionsTest brevity — the page title strip already shows
        // "Keyboard & Mouse" and the user knows which row they clicked.
        // "Press a key" / "Press a button" via PromptKey() — localized.
        // Cast RString → const char* before varargs snprintf so the
        // pointer reaches the formatter correctly.
        snprintf(title, sizeof(title), "%s", (const char*)LocalizeString(PromptKey()));
    }
    SetCtrlText(shell, m_idcs.title, title);
}

void CapturePage::RefreshStatus(OptionsShell& shell)
{
    char status[256];
    if (m_state == Captured && m_conflictAction != UAN)
    {
        snprintf(status, sizeof(status), (const char*)LocalizeString("STR_DISP_OPT_CAP_CONFLICT"),
                 m_conflictActionLabel.c_str());
    }
    else if (m_state == Captured)
    {
        // Empty — Save / Retry / Cancel buttons explain themselves.
        SetCtrlText(shell, m_idcs.status, "");
        return;
    }
    else
    {
        snprintf(status, sizeof(status), "%s", (const char*)LocalizeString("STR_DISP_OPT_CAP_WAITING"));
    }
    SetCtrlText(shell, m_idcs.status, status);
}

} // namespace Poseidon
