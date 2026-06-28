#include <Poseidon/UI/Options/ControlsPage.hpp>

#include <Poseidon/UI/Options/ConfirmPage.hpp>
#include <Poseidon/UI/Options/GamepadPage.hpp>
#include <Poseidon/UI/Options/GamepadTuningPage.hpp>
#include <Poseidon/UI/Options/KbmPage.hpp>
#include <Poseidon/UI/Options/MousePage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <Poseidon/UI/Options/TouchPage.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <memory>
#include <string>
#include <utility>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

const char* ControlsPage::TitleText() const
{
    return LocalizeString("STR_DISP_OPT_CONTROLS");
}

bool ControlsPage::OnNav(OptionsShell& shell, int idc)
{
    switch (idc)
    {
        case 1401: // Keyboard & Mouse
            shell.PushPage(std::make_unique<KbmPage>());
            return true;

        case 1402: // Mouse
            shell.PushPage(std::make_unique<MousePage>());
            return true;

        case 1405: // Gamepad bindings
            shell.PushPage(std::make_unique<GamepadPage>());
            return true;

        case 1406: // Gamepad Tuning
            shell.PushPage(std::make_unique<GamepadTuningPage>());
            return true;

        case 1407: // Touch controls
            shell.PushPage(std::make_unique<TouchPage>());
            return true;

        case 1403: // Reset all to defaults — confirm first
        {
            auto onYes = []()
            {
                auto& sub = InputSubsystem::Instance();
                for (int c = 0; c < ControlsCategoryCount; ++c)
                    sub.ResetCategoryDefaults((ControlsCategory)c);
                sub.SaveKeys();
            };
            shell.PushPage(std::make_unique<ConfirmPage>(
                std::string((const char*)LocalizeString("STR_DISP_OPT_CONFIRM_RESET_TITLE")),
                std::string((const char*)LocalizeString("STR_DISP_OPT_CONFIRM_RESET_BODY")), std::move(onYes),
                std::string((const char*)LocalizeString("STR_DISP_OPT_CTL_RESET_ALL")),
                std::string((const char*)LocalizeString("STR_DISP_OPT_CAP_CANCEL"))));
            return true;
        }
    }
    return false;
}

} // namespace Poseidon
