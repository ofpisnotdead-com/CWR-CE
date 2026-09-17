#include <Poseidon/UI/Options/KbmPage.hpp>

#include <Poseidon/UI/Options/PressKeyPage.hpp>

#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <memory>
#include <utility>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

const char* KbmPage::TitleText() const
{
    return LocalizeString("STR_DISP_OPT_CTL_KBM");
}

const char* KbmPage::DeviceNoun() const
{
    return LocalizeString("STR_DISP_OPT_CTL_KBM");
}

bool KbmPage::DeviceFilter(int packedCode) const
{
    if (packedCode < 0)
        return false;
    const int device = InputBindingDevice(packedCode);
    return device == INPUT_DEVICE_KEYBOARD || device == INPUT_DEVICE_MOUSE || device == INPUT_DEVICE_MOUSE_AXIS;
}

bool KbmPage::IsActionVisible(UserAction action, ControlsCategory category) const
{
    return IsActionVisibleOnKeyboard(action, category);
}

std::unique_ptr<OptionsPage> KbmPage::MakeCaptureModal(UserAction action, std::string actionLabel, std::string slotName,
                                                       SaveCallback onSave, ConflictCallback onConflict)
{
    return std::make_unique<PressKeyPage>(std::move(actionLabel), std::move(slotName), std::move(onSave),
                                          std::move(onConflict), action == UAMapZoomIn || action == UAMapZoomOut);
}

} // namespace Poseidon
