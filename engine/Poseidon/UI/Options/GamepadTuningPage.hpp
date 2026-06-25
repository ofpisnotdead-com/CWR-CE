#pragma once

// Gamepad scalar settings page — Enable, Y-Axis Inversion, Stick
// Deadzone, Trigger Deadzone, Look Sensitivity.  Sibling to
// MousePage; same lifecycle (apply live, persist on Unmount via
// SaveKeys → gamepad.cfg).

#include <Poseidon/UI/Options/ScrollListPage.hpp>

#include <array>
#include <string>


namespace Poseidon
{
class GamepadTuningPage : public ScrollListPage
{
  public:
    const char* TitleText() const override;

    static int DeadzoneToPercent(float v);
    static float PercentToDeadzone(int pct);
    static int LookSensitivityToPercent(float v);
    static float PercentToLookSensitivity(int pct);

    void OnReshown(OptionsShell& shell) override;
    void Unmount(OptionsShell& shell) override;

  protected:
    OptionsScrollList::Provider& ProviderRef() override { return m_provider; }

  private:
    static const char* CloseLabel();
    static const char* CloseDescription();

    class GamepadProvider : public OptionsScrollList::Provider
    {
      public:
        enum : int
        {
            kRowEnabled         = 0,
            kRowReverseYStick   = 1,
            kRowDeadzoneStick   = 2,
            kRowDeadzoneTrigger = 3,
            kRowLookSensitivity = 4,
            kRowCount           = 5,
        };

        int RowCount() const override { return kRowCount; }
        const char* RowLabel(int row) const override;
        const char* RowDescription(int row) const override;
        OptionsScrollList::RowDef RowFor(int row) const override;
        int RowValue(int row) const override;
        void SetRowValue(int row, int value) override;

      private:
        void RefreshToggleTexts() const;

        mutable std::array<std::string, 2> m_toggleText;
        mutable std::array<const char*, 2> m_toggleOptions{};
    };

    GamepadProvider m_gamepad;
    OptionsScrollList::WithCloseRow m_provider{m_gamepad, CloseLabel(), CloseDescription()};
};

} // namespace Poseidon
