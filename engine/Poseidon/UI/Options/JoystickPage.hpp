#pragma once

// Joystick binding page - concrete BindingsPage for a device without a gamepad
// mapping. Filters to the raw joystick device classes and pushes PressJoystickPage
// as the capture modal.

#include <Poseidon/UI/Options/BindingsPage.hpp>

namespace Poseidon
{
class JoystickPage : public BindingsPage
{
  public:
    const char* TitleText() const override;

  protected:
    const char* DeviceNoun() const override;
    bool DeviceFilter(int packedCode) const override;
    bool IsActionVisible(UserAction action, ControlsCategory category) const override;
    bool ResetCategoryOverride(ControlsCategory category) override;
    std::unique_ptr<OptionsPage> MakeCaptureModal(UserAction action, std::string actionLabel, std::string slotName,
                                                  SaveCallback onSave, ConflictCallback onConflict) override;
};

} // namespace Poseidon
