#pragma once

// Touch scalar settings page — aim/look sensitivity and cursor sensitivity.

#include <Poseidon/UI/Options/ScrollListPage.hpp>

namespace Poseidon
{
class TouchPage : public ScrollListPage
{
  public:
    const char* TitleText() const override;

    static int SensitivityToPercent(float value);
    static float PercentToSensitivity(int percent);
    static int CursorSensitivityToPercent(float value);
    static float PercentToCursorSensitivity(int percent);

    void OnReshown(OptionsShell& shell) override;
    void Unmount(OptionsShell& shell) override;

  protected:
    OptionsScrollList::Provider& ProviderRef() override { return m_provider; }

  private:
    static const char* CloseLabel();
    static const char* CloseDescription();

    class TouchProvider : public OptionsScrollList::Provider
    {
      public:
        enum : int
        {
            kRowAimSensitivity = 0,
            kRowCursorSensitivity = 1,
            kRowCount = 2,
        };

        int RowCount() const override { return kRowCount; }
        const char* RowLabel(int row) const override;
        const char* RowDescription(int row) const override;
        OptionsScrollList::RowDef RowFor(int row) const override;
        int RowValue(int row) const override;
        void SetRowValue(int row, int value) override;
    };

    TouchProvider m_touch;
    OptionsScrollList::WithCloseRow m_provider{m_touch, CloseLabel(), CloseDescription()};
};

} // namespace Poseidon
