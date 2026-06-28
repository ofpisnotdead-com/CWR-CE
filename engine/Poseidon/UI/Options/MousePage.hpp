#pragma once

// Mouse settings page — basic mouse toggles/sensitivity plus advanced feel
// tuning. All values apply live and are persisted through MouseConfig.

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
    static int FloatRangeToPercent(float value, float lo, float hi);
    static float PercentToFloatRange(int percent, float lo, float hi);

    // Mouse DPI stepper mapping (pure, unit-tested). Index 0 = "Off"; 1..N map to
    // kMouseDpiPresets. DpiToIndex returns the nearest preset for display without
    // changing the stored value, so a hand-edited non-preset mouseDpi survives.
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
            kRowBasicHeader = 0,
            kRowReverseY = 1,
            kRowButtonsReversed = 2,
            kRowSensitivityX = 3,
            kRowSensitivityY = 4,
            kRowMouseDpi = 5,
            kRowAdvancedHeader = 6,
            kRowInputDeadZone = 7,
            kRowSmoothing = 8,
            kRowAcceleration = 9,
            kRowAimMode = 10,
            kRowFreeAimZoneX = 11,
            kRowFreeAimZoneY = 12,
            kRowMenuCursorScale = 13,
            kRowCount = 14,
        };

        int RowCount() const override { return kRowCount; }
        const char* RowLabel(int row) const override;
        const char* RowDescription(int row) const override;
        OptionsScrollList::RowDef RowFor(int row) const override;
        OptionsScrollList::Kind RowKind(int row) const override;
        bool IsDisabled(int row) const override;
        int RowValue(int row) const override;
        void SetRowValue(int row, int value) override;
        const char* SliderValueText(int row) const override;

      private:
        void RefreshToggleTexts() const;
        void RefreshAimModeTexts() const;

        mutable std::array<std::string, 2> m_toggleText;
        mutable std::array<const char*, 2> m_toggleOptions{};
        mutable std::array<std::string, 4> m_aimModeText;
        mutable std::array<const char*, 4> m_aimModeOptions{};
        mutable std::string m_sliderValueText;
    };

    MouseProvider m_mouse;
    OptionsScrollList::WithCloseRow m_provider{m_mouse, CloseLabel(), CloseDescription()};

    bool m_savedReverseY = false;
    bool m_savedButtonsReversed = false;
    float m_savedSensitivityX = 1.0f;
    float m_savedSensitivityY = 1.0f;
    bool m_savedDpiNormalize = false;
    int m_savedMouseDpi = 1600;
};

} // namespace Poseidon
