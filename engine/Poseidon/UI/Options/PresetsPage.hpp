#pragma once

// Presets page for resetting all controls to presets. Gives a choice
// between presets and presents a confirm dialog before applying the
// change. This replaces the previous "Reset all to defaults" flow.

#include <Poseidon/UI/Options/ScrollListPage.hpp>

#include <array>
#include <string>

namespace Poseidon
{

class PresetsPage : public ScrollListPage
{
  public:
    const char* TitleText() const override;

    void OnReshown(OptionsShell& shell) override;

  protected:
    OptionsScrollList::Provider& ProviderRef() override { return m_provider; }

  private:
    static const char* CloseLabel();
    static const char* CloseDescription();

    class PresetsProvider : public OptionsScrollList::Provider
    {
      public:
        enum : int
        {
            kRowPreset = 0,
            kRowApplyPreset = 1,
            kRowCount = 2,
        };

        int RowCount() const override { return kRowCount; }
        const char* RowLabel(int row) const override;
        const char* RowDescription(int row) const override;
        OptionsScrollList::RowDef RowFor(int row) const override;
        int RowValue(int row) const override;
        void SetRowValue(int row, int value) override;
        OptionsScrollList::Kind RowKind(int row) const override;
        void OnRowAction(int row, Display& host) override;

      private:
        void RefreshStepperTexts() const;

        mutable int m_preset = 0;
        mutable std::array<std::string, 2> m_stepperText;
        mutable std::array<const char*, 2> m_stepperOptions{};
    };

    PresetsProvider m_presets_provider;
    OptionsScrollList::WithCloseRow m_provider{m_presets_provider, CloseLabel(), CloseDescription()};
};

} // namespace Poseidon
