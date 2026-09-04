#include <Poseidon/UI/Options/CapturePage.hpp>

#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <Poseidon/UI/Controls/UIControlsExtShared.hpp>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

extern RString GetKeyName(int dikCode);

namespace
{
constexpr unsigned long kCaptureDoubleTapWindowMs = 400;

class ConflictStatus final : public C3DStatic
{
  public:
    ConflictStatus(ControlsContainer* parent, int idc, const ParamEntry& cls) : C3DStatic(parent, idc, cls) {}

    void SetColoredText(RString text, std::string colorMask)
    {
        SetText(text);
        m_colorMask = std::move(colorMask);
    }

    void OnDraw(float alpha) override
    {
        if (m_colorMask.find('1') == std::string::npos)
        {
            C3DStatic::OnDraw(alpha);
            return;
        }

        Vector3 normal = _down.CrossProduct(_right).Normalized();
        Vector3 position = _position - 0.002f * normal;
        Vector3 up = -_down;
        Vector3 right = 0.75f * up.Size() * _right.Normalized();
        Vector3 width = GEngine->GetText3DWidth(right, _font, _text);
        if ((_style & ST_HPOS) == ST_RIGHT)
            position += _right - width;
        else if ((_style & ST_HPOS) == ST_CENTER)
            position += 0.5f * (_right - width);

        const PackedColor normalColor = ModAlpha(_color, alpha);
        const PackedColor actionColor = ModAlpha(PackedColor(Color(1.0f, 0.9f, 0.2f, 1.0f)), alpha);
        const char* text = _text;
        int byteStart = 0;
        int byteEnd = 0;
        for (size_t i = 0; i < m_colorMask.size(); ++i)
        {
            byteEnd += Utf8CodepointBytes(text + byteEnd);
            if (i + 1 < m_colorMask.size() && m_colorMask[i] == m_colorMask[i + 1])
                continue;

            RString segment = _text.Substring(byteStart, byteEnd);
            const PackedColor color = m_colorMask[i] == '1' ? actionColor : normalColor;
            GEngine->DrawText3D(position, up, right, ClipAll, _font, color, DisableSun, segment);
            position += GEngine->GetText3DWidth(right, _font, segment);
            byteStart = byteEnd;
        }
    }

  private:
    std::string m_colorMask;
};

class CaptureButton final : public C3DActiveText
{
  public:
    CaptureButton(ControlsContainer* parent, int idc, const ParamEntry& cls) : C3DActiveText(parent, idc, cls) {}

    void OnMouseMove(float x, float y, bool active = true) override
    {
        if (active && IsVisible() && IsEnabled())
            _parent->FocusCtrl(IDC());

        C3DActiveText::OnMouseMove(x, y, active);
    }
};
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
        if (!m_conflictActions.empty())
        {
            Resolve(shell, true, true);
            return true;
        }
        m_state = Listening;
        m_capturedCode = -1;
        m_capturedModifier = -1;
        m_capturedAtMs = 0;
        m_conflictActions.clear();
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

Control* CapturePage::OnCreateControl(OptionsShell& shell, int /*type*/, int idc, const ParamEntry& cls)
{
    if (idc == m_idcs.status)
        return new ConflictStatus(&shell, idc, cls);

    if (idc == m_idcs.save || idc == m_idcs.retry || idc == m_idcs.cancel)
        return new CaptureButton(&shell, idc, cls);

    return nullptr;
}

void CapturePage::OnSimulate(OptionsShell& shell)
{
    RefreshStatusMarquee(shell);
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

    RefreshButtons(shell);
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
    RefreshButtons(shell);
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
    RefreshButtons(shell);
    return true;
}

void CapturePage::Resolve(OptionsShell& shell, bool save, bool replaceConflict)
{
    if (m_resolved)
        return;
    m_resolved = true;
    if (save && m_state == Captured && m_onSave)
        m_onSave(m_capturedCode, m_capturedModifier, replaceConflict);
    shell.PopPage();
}

void CapturePage::RefreshButtons(OptionsShell& shell)
{
    if (auto* save = dynamic_cast<C3DActiveText*>(shell.GetCtrl(m_idcs.save)))
    {
        save->ShowCtrl(true);
        if (m_conflictActions.empty())
            save->SetText(LocalizeString("STR_DISP_OPT_CAP_SAVE"));
        else
            save->SetText(LocalizeStringWithFallback("STR_DISP_OPT_CAP_USE_ANYWAY", "Use anyway"));
    }
    if (auto* retry = dynamic_cast<C3DActiveText*>(shell.GetCtrl(m_idcs.retry)))
    {
        retry->ShowCtrl(true);
        retry->SetText(
            LocalizeString(m_conflictActions.empty() ? "STR_DISP_OPT_CAP_RETRY" : "STR_DISP_OPT_CAP_REPLACE"));
    }
}

void CapturePage::RefreshConflict()
{
    if (!m_onConflict)
    {
        m_conflictActions.clear();
        return;
    }
    m_conflictActions = m_onConflict(m_capturedCode, m_capturedModifier);
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

std::string CapturePage::BuildStatusColorMask(const std::string& status, const std::string& actions,
                                              const std::vector<std::string>& actionLabels)
{
    std::string colorMask(static_cast<size_t>(CountUtf8Codepoints(status.c_str())), '0');
    const size_t actionByteStart = status.find(actions);
    if (actions.empty() || actionByteStart == std::string::npos)
        return colorMask;

    int labelStart = CountUtf8Codepoints(status.substr(0, actionByteStart).c_str());
    for (const std::string& actionLabel : actionLabels)
    {
        const int labelLength = CountUtf8Codepoints(actionLabel.c_str());
        colorMask.replace(static_cast<size_t>(labelStart), static_cast<size_t>(labelLength),
                          static_cast<size_t>(labelLength), '1');
        labelStart += labelLength + 2;
    }
    return colorMask;
}

void CapturePage::RefreshStatus(OptionsShell& shell)
{
    char status[256];
    std::string actions;
    std::vector<std::string> actionLabels;
    for (UserAction action : m_conflictActions)
        actionLabels.emplace_back(ControlActionLabel(action));
    if (m_state == Captured && m_conflictActions.size() == 1)
    {
        actions = actionLabels.front();
        snprintf(status, sizeof(status), (const char*)LocalizeString("STR_DISP_OPT_CAP_CONFLICT"), actions.c_str());
        m_statusText = status;
    }
    else if (m_state == Captured && m_conflictActions.size() > 1)
    {
        for (const std::string& actionLabel : actionLabels)
        {
            if (!actions.empty())
                actions += ", ";
            actions += actionLabel;
        }
        const RString format = LocalizeStringWithFallback("STR_DISP_OPT_CAP_CONFLICT_MULTIPLE",
                                                          "Already assigned to multiple actions: %s.");
        const int length = snprintf(nullptr, 0, (const char*)format, actions.c_str());
        std::vector<char> formatted(static_cast<size_t>(length) + 1);
        snprintf(formatted.data(), formatted.size(), (const char*)format, actions.c_str());
        m_statusText = formatted.data();
    }
    else if (m_state == Captured)
    {
        m_statusText.clear();
    }
    else
    {
        snprintf(status, sizeof(status), "%s", (const char*)LocalizeString("STR_DISP_OPT_CAP_WAITING"));
        m_statusText = status;
    }
    m_statusColorMask = BuildStatusColorMask(m_statusText, actions, actionLabels);
    m_statusMarqueeAtMs = Foundation::GlobalTickCount();
    m_statusVisibleChars = 0;
    RefreshStatusMarquee(shell);
}

void CapturePage::RefreshStatusMarquee(OptionsShell& shell)
{
    auto* status = dynamic_cast<C3DStatic*>(shell.GetCtrl(m_idcs.status));
    if (!status)
        return;

    auto* conflictStatus = dynamic_cast<ConflictStatus*>(status);
    if (m_statusText.empty() || status->MeasureTextFraction(m_statusText.c_str()) <= 1.0f)
    {
        if (conflictStatus)
            conflictStatus->SetColoredText(m_statusText.c_str(), m_statusColorMask);
        else
            status->SetText(m_statusText.c_str());
        return;
    }

    if (m_statusVisibleChars == 0)
    {
        const int codepoints = CountUtf8Codepoints(m_statusText.c_str());
        char candidate[512];
        for (int chars = 1; chars <= codepoints; ++chars)
        {
            OptionsScrollList::FormatCell(m_statusText.c_str(), chars, false, 0, candidate, sizeof(candidate));
            if (status->MeasureTextFraction(candidate) > 1.0f)
            {
                m_statusVisibleChars = std::max(1, chars - 1);
                break;
            }
        }
        if (m_statusVisibleChars == 0)
            m_statusVisibleChars = codepoints;
    }

    char visible[512];
    char visibleColorMask[512];
    const DWORD elapsed = Foundation::GlobalTickCount() - m_statusMarqueeAtMs;
    OptionsScrollList::FormatCell(m_statusText.c_str(), m_statusVisibleChars, true, elapsed, visible, sizeof(visible));
    OptionsScrollList::FormatCell(m_statusColorMask.c_str(), m_statusVisibleChars, true, elapsed, visibleColorMask,
                                  sizeof(visibleColorMask));
    if (conflictStatus)
        conflictStatus->SetColoredText(visible, visibleColorMask);
    else
        status->SetText(visible);
}

} // namespace Poseidon
