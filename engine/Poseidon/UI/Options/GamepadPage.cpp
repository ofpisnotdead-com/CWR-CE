#include <Poseidon/UI/Options/GamepadPage.hpp>

#include <Poseidon/UI/Options/PressButtonPage.hpp>

#include <Poseidon/Input/InputBinding.hpp>
#include <Poseidon/Input/InputContext.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/InputProfile.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/UI/Settings/ContextControlsConfig.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <memory>
#include <iterator>
#include <utility>
#include <vector>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

namespace
{
constexpr int kGamepadButtonLB = 4;

struct ContextList
{
    const InputContext* data = nullptr;
    int count = 0;
};

ContextList ContextsForCategory(ControlsCategory cat)
{
    static constexpr InputContext onFoot[] = {InputContext::Infantry};
    static constexpr InputContext vehicles[] = {InputContext::CarDriver, InputContext::TankDriver,
                                                InputContext::ShipDriver};
    static constexpr InputContext pilot[] = {InputContext::HeliPilot, InputContext::PlanePilot};
    static constexpr InputContext gunner[] = {InputContext::TankGunner, InputContext::Gunner};
    static constexpr InputContext common[] = {
        InputContext::Menu,       InputContext::Infantry,  InputContext::CarDriver,  InputContext::TankDriver,
        InputContext::TankGunner, InputContext::HeliPilot, InputContext::PlanePilot, InputContext::ShipDriver,
        InputContext::Gunner,     InputContext::Spectator, InputContext::Map,        InputContext::Chat,
        InputContext::Editor,
    };

    switch (cat)
    {
        case ControlsCategoryOnFoot:
            return {onFoot, static_cast<int>(std::size(onFoot))};
        case ControlsCategoryVehicles:
            return {vehicles, static_cast<int>(std::size(vehicles))};
        case ControlsCategoryPilot:
            return {pilot, static_cast<int>(std::size(pilot))};
        case ControlsCategoryGunner:
            return {gunner, static_cast<int>(std::size(gunner))};
        case ControlsCategoryCommon:
            return {common, static_cast<int>(std::size(common))};
        default:
            return {onFoot, static_cast<int>(std::size(onFoot))};
    }
}

bool IsGamepadCode(int packedCode)
{
    return packedCode >= INPUT_DEVICE_STICK;
}

bool IsFreelookDirection(UserAction action)
{
    return action == UALookLeft || action == UALookRight || action == UALookUp || action == UALookDown ||
           action == UALookLeftUp || action == UALookRightUp || action == UALookLeftDown || action == UALookRightDown;
}

void ReplaceGamepadBindings(InputProfile& profile, UserAction action, const std::vector<InputBinding>& replacements)
{
    std::vector<InputBinding> bindings;
    for (const InputBinding& binding : profile.GetBindingEntries(action))
    {
        if (!IsGamepadCode(binding.code.toLegacy()))
            bindings.push_back(binding);
    }
    bindings.insert(bindings.end(), replacements.begin(), replacements.end());

    profile.ClearBindings(action);
    for (const InputBinding& binding : bindings)
        profile.Bind(action, binding);
}

void BindAxisPair(InputProfile& profile, UserAction negativeY, UserAction positiveY, UserAction negativeX,
                  UserAction positiveX, int xAxis, int yAxis, InputCode modifier = InputCode{})
{
    ReplaceGamepadBindings(profile, negativeY,
                           {InputBinding(InputCode::GamepadAx(yAxis), modifier, ActivationMode::OnHold, -1.0f)});
    ReplaceGamepadBindings(profile, positiveY,
                           {InputBinding(InputCode::GamepadAx(yAxis), modifier, ActivationMode::OnHold, 1.0f)});
    ReplaceGamepadBindings(profile, negativeX,
                           {InputBinding(InputCode::GamepadAx(xAxis), modifier, ActivationMode::OnHold, -1.0f)});
    ReplaceGamepadBindings(profile, positiveX,
                           {InputBinding(InputCode::GamepadAx(xAxis), modifier, ActivationMode::OnHold, 1.0f)});
}

int AxisPairBase(int packedCode)
{
    if (InputBindingDevice(packedCode) != INPUT_DEVICE_STICK_AXIS)
        return -1;
    int axis = InputBindingValue(packedCode);
    if (axis == 0 || axis == 1)
        return 0;
    if (axis == 3 || axis == 4)
        return 3;
    return (axis % 2 == 0) ? axis : axis - 1;
}

const char* AxisPairLabel(int base)
{
    if (base == 0)
        return "LS";
    if (base == 3)
        return "RS";
    return "";
}

const char* AxisPairLabelFor(const InputProfile& profile, UserAction action, int fallback)
{
    for (const InputBinding& binding : profile.GetBindingEntries(action))
    {
        int packed = binding.code.toLegacy();
        if (InputBindingDevice(packed) != INPUT_DEVICE_STICK_AXIS)
            continue;
        const char* label = AxisPairLabel(AxisPairBase(packed));
        if (*label)
            return label;
    }
    return AxisPairLabel(fallback);
}

InputContext PrimaryContextForCategory(ControlsCategory cat)
{
    ContextList contexts = ContextsForCategory(cat);
    return contexts.count > 0 ? contexts.data[0] : InputContext::Infantry;
}

bool IsMovementGroup(UserAction action, ControlsCategory category)
{
    return category == ControlsCategoryOnFoot && action == UAMoveForward;
}

bool IsAimGroup(UserAction action, ControlsCategory category)
{
    return (category == ControlsCategoryOnFoot || category == ControlsCategoryGunner) && action == UAAimRight;
}

bool IsFreelookGroup(UserAction action)
{
    return action == UALookAround;
}

bool UsesDirectFreelook(ControlsCategory category)
{
    return category == ControlsCategoryVehicles;
}
} // namespace

const char* GamepadPage::TitleText() const
{
    return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD");
}

const char* GamepadPage::DeviceNoun() const
{
    return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD");
}

bool GamepadPage::DeviceFilter(int packedCode) const
{
    if (packedCode < 0)
        return false;
    // Show only joystick-class entries: STICK buttons, STICK_AXIS,
    // STICK_POV.  Keyboard scancodes and mouse buttons live below
    // INPUT_DEVICE_STICK and are filtered out so the Gamepad page only
    // surfaces / edits gamepad bindings.
    return (packedCode >= INPUT_DEVICE_STICK);
}

bool GamepadPage::IsActionVisible(UserAction action, ControlsCategory category) const
{
    if (IsMovementGroup(action, category) || IsAimGroup(action, category) || IsFreelookGroup(action))
        return true;
    if (category == ControlsCategoryOnFoot && (action == UAMoveBack || action == UAMoveLeft || action == UAMoveRight ||
                                               action == UAAimUp || action == UAAimDown || action == UAAimLeft))
        return false;
    if (category == ControlsCategoryGunner && (action == UAAimUp || action == UAAimDown || action == UAAimLeft))
        return false;
    if (IsFreelookDirection(action))
        return false;
    return BindingsPage::IsActionVisible(action, category);
}

const char* GamepadPage::ActionLabelOverride(UserAction action, ControlsCategory category) const
{
    if (IsMovementGroup(action, category))
        return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_GROUP_MOVEMENT");
    if (IsAimGroup(action, category))
        return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_GROUP_AIM");
    if (IsFreelookGroup(action))
        return LocalizeString("STR_DISP_OPT_CTL_GAMEPAD_GROUP_FREELOOK");
    return nullptr;
}

const char* GamepadPage::BindingDisplayOverride(UserAction action, ControlsCategory category, int slot) const
{
    const bool grouped = IsMovementGroup(action, category) || IsAimGroup(action, category) || IsFreelookGroup(action);
    if (!grouped)
        return nullptr;

    if (slot != 0)
        return "";
    const InputProfile& profile = InputSubsystem::Instance().GetProfile(PrimaryContextForCategory(category));
    if (IsMovementGroup(action, category))
        return AxisPairLabelFor(profile, UAMoveLeft, 0);
    if (IsAimGroup(action, category))
        return AxisPairLabelFor(profile, UAAimRight, 3);
    if (action == UALookAround)
        return UsesDirectFreelook(category) ? "RS" : "LB+RS";
    return nullptr;
}

bool GamepadPage::ApplyCaptureOverride(ControlsCategory category, UserAction action, int, int packedCode, int modifier)
{
    const int baseAxis = AxisPairBase(packedCode);
    if (baseAxis < 0)
        return IsMovementGroup(action, category) || IsAimGroup(action, category) || IsFreelookGroup(action);

    auto& sub = InputSubsystem::Instance();
    ContextList contexts = ContextsForCategory(category);
    for (int i = 0; i < contexts.count; ++i)
    {
        InputProfile& profile = sub.GetProfile(contexts.data[i]);
        if (IsMovementGroup(action, category))
        {
            BindAxisPair(profile, UAMoveForward, UAMoveBack, UAMoveLeft, UAMoveRight, baseAxis, baseAxis + 1);
        }
        else if (IsAimGroup(action, category))
        {
            BindAxisPair(profile, UAAimUp, UAAimDown, UAAimLeft, UAAimRight, baseAxis, baseAxis + 1);
        }
        else if (IsFreelookGroup(action))
        {
            InputCode mod;
            if (modifier >= INPUT_DEVICE_STICK)
                mod = InputCode::FromLegacy(modifier);
            else if (!UsesDirectFreelook(category))
                mod = InputCode::GamepadBtn(kGamepadButtonLB);

            std::vector<InputBinding> around;
            if (mod.valid())
                around.push_back(InputBinding(mod));
            ReplaceGamepadBindings(profile, UALookAround, around);
            BindAxisPair(profile, UALookUp, UALookDown, UALookLeft, UALookRight, baseAxis, baseAxis + 1, mod);
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool GamepadPage::ResetCategoryOverride(ControlsCategory category)
{
    ContextControlsConfig defaults;
    defaults.LoadDefaults();

    auto& sub = InputSubsystem::Instance();
    ContextList contexts = ContextsForCategory(category);
    const UserAction* actions = GetControlsCategoryActions(category);
    for (int c = 0; c < contexts.count; ++c)
    {
        InputContext ctx = contexts.data[c];
        InputProfile& dst = sub.GetProfile(ctx);
        const InputProfile& src = defaults.profiles[static_cast<int>(ctx)];

        for (int a = 0; actions[a] != UAN; ++a)
        {
            UserAction action = actions[a];
            std::vector<InputBinding> replacements;
            for (const InputBinding& binding : src.GetBindingEntries(action))
                if (IsGamepadCode(binding.code.toLegacy()))
                    replacements.push_back(binding);
            ReplaceGamepadBindings(dst, action, replacements);
        }
    }
    return true;
}

std::unique_ptr<OptionsPage> GamepadPage::MakeCaptureModal(std::string actionLabel, std::string slotName,
                                                           SaveCallback onSave, ConflictCallback onConflict)
{
    return std::make_unique<PressButtonPage>(std::move(actionLabel), std::move(slotName), std::move(onSave),
                                             std::move(onConflict));
}

} // namespace Poseidon
