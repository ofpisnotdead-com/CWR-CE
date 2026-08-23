#pragma once

#include <Poseidon/UI/Options/CapturePage.hpp>

namespace Poseidon
{
class PressKeyPage : public CapturePage
{
  public:
    PressKeyPage(std::string actionLabel, std::string slotName, SaveCallback onSave, ConflictCallback onConflict,
                 bool allowMouseWheel = false)
        : CapturePage(Idcs{9301, 9303, 9302, 9380, 9381}, std::move(actionLabel), std::move(slotName),
                      std::move(onSave), std::move(onConflict)),
          m_allowMouseWheel(allowMouseWheel)
    {
    }

    const char* ResourceClassName() const override { return "RscOptionsPagePressKey"; }
    bool OnKeyDown(OptionsShell& shell, unsigned nChar) override;
    void OnSimulate(OptionsShell& shell) override;

  protected:
    const char* PromptKey() const override { return "STR_DISP_OPT_CAP_PRESS_KEY"; }
    const char* PromptVerb() const override { return "key"; }
    Result InterpretKey(unsigned nChar, int& outPackedCode, int& outModifier) const override;
    bool IsBareModifierCode(int packedCode) const override;

  private:
    bool m_allowMouseWheel;
};

} // namespace Poseidon
