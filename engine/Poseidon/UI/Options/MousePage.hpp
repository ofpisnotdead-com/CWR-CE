#pragma once

// Mouse settings page — Y-axis inversion, button swap, sensitivity X / Y, Mouse DPI.
//
// 5 rows + auto-appended Close.  All apply live (matching the
// way the RscDisplayConfigure handled them).  Cancel via Close
// snapshots the values on Mount and restores them — same pattern as
// DisplayConfigure's `_old*` members, lifted into the page.
//
// Persistence: MouseConfig (mouse.cfg) — saved on Unmount via
// InputSubsystem::SaveKeys(). Mouse button action bindings live in
// contextControls.cfg through KbmPage.

#include <Poseidon/UI/Options/ScrollListPage.hpp>

#include <array>
#include <string>


namespace Poseidon
{
class MousePage : public ScrollListPage
{
  public:
    const char* TitleText() const override;

    static int SensitivityToPercent(float value);
    static float PercentToSensitivity(int percent);

    // Mouse DPI stepper mapping (pure, unit-tested).  Index 0 = "Off"; 1..N map to
    // kMouseDpiPresets.  DpiToIndex returns the nearest preset for display without
    // changing the stored value, so a hand-edited non-preset DPI survives.
    static int DpiToIndex(bool normalize, int mouseDpi);
    static int IndexToDpi(int index);

    void Mount(OptionsShell& shell) override;
    void OnReshown(OptionsShell& shell) override;
    void Unmount(OptionsShell& shell) override;

  protected:
    OptionsScrollList::Provider& ProviderRef() override { return m_provider; }

  private:
    static const char* CloseLabel();
    static const char* CloseDescription();

    class MouseProvider : public OptionsScrollList::Provider
    {
      public:
        enum : int
        {
            kRowReverseY        = 0,
            kRowButtonsReversed = 1,
            kRowSensitivityX    = 2,
            kRowSensitivityY    = 3,
            kRowMouseDpi        = 4,
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

    MouseProvider m_mouse;
    OptionsScrollList::WithCloseRow m_provider{m_mouse, CloseLabel(), CloseDescription()};

    // Snapshotted on Mount, restored on Unmount-via-cancel (i.e. the
    // page is unmounting without an explicit save action — same posture
    // as DisplayPage's pending/applied bookkeeping, simpler shape since
    // every Mouse setting applies live with no Apply button).
    bool  m_savedReverseY        = false;
    bool  m_savedButtonsReversed = false;
    float m_savedSensitivityX    = 1.0f;
    float m_savedSensitivityY    = 1.0f;
    bool  m_savedDpiNormalize    = false;
    int   m_savedMouseDpi        = 1600;
};

} // namespace Poseidon
