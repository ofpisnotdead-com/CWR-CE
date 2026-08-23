#pragma once

// Gamepad binding page — concrete BindingsPage that filters to
// joystick / stick-axis / stick-POV entries and pushes PressButtonPage
// as the capture modal.  Same shape as KbmPage; differs only in which
// device-class entries it shows / writes from the shared userKeys[]
// array.

#include <Poseidon/UI/Options/BindingsPage.hpp>

namespace Poseidon
{
class GamepadPage : public BindingsPage
{
  public:
    const char* TitleText() const override;

  protected:
    const char* DeviceNoun() const override;
    bool DeviceFilter(int packedCode) const override;
    bool IsActionVisible(UserAction action, ControlsCategory category) const override;
    const char* ActionLabelOverride(UserAction action, ControlsCategory category) const override;
    const char* BindingDisplayOverride(UserAction action, ControlsCategory category, int slot) const override;
    bool ApplyCaptureOverride(ControlsCategory category, UserAction action, int slot, int packedCode,
                              int modifier) override;
    bool ResetCategoryOverride(ControlsCategory category) override;
    std::unique_ptr<OptionsPage> MakeCaptureModal(UserAction action, std::string actionLabel, std::string slotName,
                                                  SaveCallback onSave, ConflictCallback onConflict) override;
};

} // namespace Poseidon
