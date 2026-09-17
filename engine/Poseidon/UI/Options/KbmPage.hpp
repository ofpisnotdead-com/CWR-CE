#pragma once

// Keyboard & Mouse binding page — concrete BindingsPage that filters
// to keyboard / mouse entries (excludes the gamepad / joystick bits in
// the same userKeys[] slots) and pushes PressKeyPage as the capture
// modal.

#include <Poseidon/UI/Options/BindingsPage.hpp>

namespace Poseidon
{
class KbmPage : public BindingsPage
{
  public:
    const char* TitleText() const override;

  protected:
    const char* DeviceNoun() const override;
    bool DeviceFilter(int packedCode) const override;
    bool IsActionVisible(UserAction action, ControlsCategory category) const override;
    std::unique_ptr<OptionsPage> MakeCaptureModal(UserAction action, std::string actionLabel, std::string slotName,
                                                  SaveCallback onSave, ConflictCallback onConflict) override;
};

} // namespace Poseidon
