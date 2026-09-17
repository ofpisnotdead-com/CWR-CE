#pragma once

// Binding-capture modal. Owns a small state machine:
// Listening -> Captured -> Resolved.
//
// Subclass plugs in:
//   - PromptVerb()    : "key" / "button" / etc. — displayed in the title.
//   - InterpretKey()  : turns an SDL keycode into either a packed
//                       INPUT_DEVICE_*|code value (Result::Main) or a
//                       reject (Result::Refused).  A modifier-only key
//                       captures standalone; a following key while it is
//                       held upgrades to a combo via TryUpgradeToCombo.
//   - Idcs            : the IDC range from the modal's resource bundle.
//
// Caller wiring: pushes the page with two callbacks:
//   onSave(packedCode, replaceConflict): commit; replaceConflict
//                                         indicates the user accepted
//                                         the conflict prompt's Replace.
//   onConflict(packedCode) -> actions: returns every conflicting action.

#include <Poseidon/UI/Options/OptionsPage.hpp>
#include <Poseidon/Input/UserAction.hpp>

#include <functional>
#include <string>
#include <vector>


namespace Poseidon
{
class CapturePage : public OptionsPage
{
  public:
    using SaveCallback     = std::function<void(int /*packedCode*/, int /*modifier*/, bool /*replaceConflict*/)>;
    using ConflictCallback = std::function<std::vector<UserAction>(int /*packedCode*/, int /*modifier*/)>;

    enum class Result { Main, Refused };

    struct Idcs
    {
        int save;
        int retry;
        int cancel;
        int title;
        int status;
    };

    CapturePage(Idcs idcs,
                std::string actionLabel,
                std::string slotName,
                SaveCallback onSave,
                ConflictCallback onConflict);

    const char* TitleText() const override { return ""; }
    bool IsModal() const override { return true; }

    int DefaultFocusIdc() const override { return m_idcs.cancel; }
    ControllerUiScene GetControllerUiScene() const override
    {
        ControllerUiScene scene;
        scene.kind = ControllerSceneKind::Menu;
        scene.activeSection = CaptureSection();
        scene.sceneCapabilities = scene.activeSection.capabilities;
        scene.menuFallbackAllowed = false;
        return scene;
    }

    void Mount(OptionsShell& shell) override;
    bool OnButtonClicked(OptionsShell& shell, int idc) override;
    bool OnKeyDown(OptionsShell& shell, unsigned nChar) override;
    Control* OnCreateControl(OptionsShell& shell, int type, int idc, const ParamEntry& cls) override;
    void OnSimulate(OptionsShell& shell) override;

  protected:
    // Stringtable key for the listening prompt, e.g.
    // "STR_DISP_OPT_CAP_PRESS_KEY" / "..._PRESS_BUTTON".  Localized at
    // RefreshTitle time so the modal honours the active language.
    virtual const char* PromptKey() const = 0;

    // Wording fallback used in ConflictPage / Save button label
    // contexts where a single-noun ("key" / "button") is woven into
    // a longer string.  Kept English-only for now since callers don't
    // currently use it across locales — pure historical name.
    virtual const char* PromptVerb() const = 0;

    // outPackedCode = main key, outModifier = modifier scancode held at
    // capture time (or -1 for none).
    virtual Result InterpretKey(unsigned nChar, int& outPackedCode, int& outModifier) const = 0;

    // True when packedCode is a lone-captured modifier that can still absorb a
    // following key into a combo. Keyboard-only; other devices keep the default.
    virtual bool IsBareModifierCode(int /*packedCode*/) const { return false; }

    bool IsListening() const { return m_state == Listening; }
    const std::vector<UserAction>& ConflictActions() const { return m_conflictActions; }
    static std::string BuildStatusColorMask(const std::string& status, const std::string& actions,
                                            const std::vector<std::string>& actionLabels);
    bool TryCapture(OptionsShell& shell, int packedCode, int modifier);
    bool TryUpgradeToDoubleTap(OptionsShell& shell, int packedCode, int modifier);
    bool TryUpgradeToCombo(OptionsShell& shell, int packedCode, int modifier);

  private:
    enum State
    {
        Listening,
        Captured
    };

    void Resolve(OptionsShell& shell, bool save, bool replaceConflict = false);
    void RefreshConflict();
    void RefreshButtons(OptionsShell& shell);
    void RefreshTitle(OptionsShell& shell);
    void RefreshStatus(OptionsShell& shell);
    void RefreshStatusMarquee(OptionsShell& shell);

    SaveCallback m_onSave;
    ConflictCallback m_onConflict;
    std::string m_actionLabel;
    std::string m_slotName;
    std::string m_statusText;
    std::string m_statusColorMask;
    std::vector<UserAction> m_conflictActions;
    Idcs m_idcs;
    State m_state = Listening;
    bool m_resolved = false;
    bool m_suppressNavKeys = false; // eat first nav key after gamepad capture (synthetic key-tap side-effect)
    int m_capturedCode = -1;
    int m_capturedModifier = -1;
    unsigned long m_capturedAtMs = 0;
    unsigned long m_statusMarqueeAtMs = 0;
    int m_statusVisibleChars = 0;
};

} // namespace Poseidon
