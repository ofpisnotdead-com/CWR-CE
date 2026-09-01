#include <Poseidon/UI/Options/JoystickPage.hpp>

#include <Poseidon/UI/Options/PressJoystickPage.hpp>

#include <Poseidon/Input/InputBinding.hpp>
#include <Poseidon/Input/InputContext.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/InputProfile.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/UI/Settings/ContextControlsConfig.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace Poseidon
{

namespace
{
void ReplaceJoystickBindings(InputProfile& profile, UserAction action, const std::vector<InputBinding>& replacements)
{
    std::vector<InputBinding> bindings;
    for (const InputBinding& binding : profile.GetBindingEntries(action))
        if (!InputBindingIsRawJoystick(binding.code.toLegacy()))
            bindings.push_back(binding);
    bindings.insert(bindings.end(), replacements.begin(), replacements.end());

    profile.ClearBindings(action);
    for (const InputBinding& binding : bindings)
        profile.Bind(action, binding);
}
} // namespace

const char* JoystickPage::TitleText() const
{
    return LocalizeStringWithFallback("STR_DISP_OPT_CTL_JOYSTICK", "Joystick");
}

const char* JoystickPage::DeviceNoun() const
{
    return LocalizeStringWithFallback("STR_DISP_OPT_CTL_JOYSTICK", "Joystick");
}

bool JoystickPage::DeviceFilter(int packedCode) const
{
    return InputBindingIsRawJoystick(packedCode);
}

bool JoystickPage::IsActionVisible(UserAction action, ControlsCategory category) const
{
    return IsActionVisibleOnJoystick(action, category);
}

bool JoystickPage::ResetCategoryOverride(ControlsCategory category)
{
    ContextControlsConfig defaults;
    defaults.LoadDefaults();

    auto& sub = InputSubsystem::Instance();
    const UserAction* actions = GetControlsCategoryActions(category);
    for (int c = 0; c < ContextControlsConfig::ContextCount; ++c)
    {
        InputProfile& dst = sub.GetProfile(static_cast<InputContext>(c));
        const InputProfile& src = defaults.profiles[c];
        for (int a = 0; actions[a] != UAN; ++a)
        {
            const UserAction action = actions[a];
            std::vector<InputBinding> replacements;
            for (const InputBinding& binding : src.GetBindingEntries(action))
                if (InputBindingIsRawJoystick(binding.code.toLegacy()))
                    replacements.push_back(binding);
            ReplaceJoystickBindings(dst, action, replacements);
        }
    }
    return true;
}

std::unique_ptr<OptionsPage> JoystickPage::MakeCaptureModal(UserAction, std::string actionLabel, std::string slotName,
                                                            SaveCallback onSave, ConflictCallback onConflict)
{
    return std::make_unique<PressJoystickPage>(std::move(actionLabel), std::move(slotName), std::move(onSave),
                                               std::move(onConflict));
}

} // namespace Poseidon
