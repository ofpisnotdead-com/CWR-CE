#pragma once

// Raw joystick capture modal - sibling to PressButtonPage. Polls the joystick
// state a device without a gamepad mapping reports: buttons, hat directions and
// a decisively deflected axis.

#include <Poseidon/UI/Options/CapturePage.hpp>

namespace Poseidon
{
class PressJoystickPage : public CapturePage
{
  public:
    PressJoystickPage(std::string actionLabel, std::string slotName, SaveCallback onSave, ConflictCallback onConflict)
        : CapturePage(Idcs{9301, 9303, 9302, 9380, 9381}, std::move(actionLabel), std::move(slotName),
                      std::move(onSave), std::move(onConflict))
    {
    }

    const char* ResourceClassName() const override { return "RscOptionsPagePressKey"; }

  protected:
    const char* PromptKey() const override { return "STR_DISP_OPT_CAP_PRESS_BUTTON"; }
    const char* PromptVerb() const override { return "button"; }
    bool OnKeyDown(OptionsShell& shell, unsigned nChar) override;
    Result InterpretKey(unsigned nChar, int& outPackedCode, int& outModifier) const override;
    void OnSimulate(OptionsShell& shell) override;
};

} // namespace Poseidon
