#include <Poseidon/UI/Options/PressJoystickPage.hpp>

#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>

namespace Poseidon
{

namespace
{
int CapturedJoystickInput()
{
    auto& sub = InputSubsystem::Instance();
    if (!sub.IsJoystickConnected())
        return -1;

    for (int i = 0; i < N_RAW_JOYSTICK_BUTTONS; i++)
        if (sub.GetJoystickButtonToDo(i))
            return INPUT_DEVICE_JOYSTICK + i;
    for (int i = 0; i < N_RAW_JOYSTICK_POV; i++)
        if (sub.GetJoystickPovToDo(i))
            return INPUT_DEVICE_JOYSTICK_POV + i;
    for (int i = 0; i < N_RAW_JOYSTICK_AXES; i++)
        if (sub.IsJoystickAxisDeflected(i))
            return INPUT_DEVICE_JOYSTICK_AXIS + i;
    return -1;
}
} // namespace

CapturePage::Result PressJoystickPage::InterpretKey(unsigned /*nChar*/, int& outPackedCode, int& outModifier) const
{
    const int captured = CapturedJoystickInput();
    if (captured < 0)
        return Result::Refused;
    outPackedCode = captured;
    outModifier = -1;
    return Result::Main;
}

bool PressJoystickPage::OnKeyDown(OptionsShell& shell, unsigned nChar)
{
    if (IsListening())
    {
        const int captured = CapturedJoystickInput();
        if (captured >= 0)
            return TryCapture(shell, captured, -1);
    }
    return CapturePage::OnKeyDown(shell, nChar);
}

void PressJoystickPage::OnSimulate(OptionsShell& shell)
{
    CapturePage::OnSimulate(shell);
    if (!IsListening())
        return;
    const int captured = CapturedJoystickInput();
    if (captured >= 0)
        TryCapture(shell, captured, -1);
}

} // namespace Poseidon
