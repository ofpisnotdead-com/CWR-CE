#pragma once

#include <Poseidon/UI/Options/OptionsPage.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Poseidon
{
class NumberEntryPage : public OptionsPage
{
  public:
    using ApplyCallback = std::function<void(float)>;

    NumberEntryPage(std::string title, float value, float minimum, float maximum, int decimalPlaces, std::string unit,
                    ApplyCallback onApply);

    const char* TitleText() const override { return ""; }
    bool IsModal() const override { return true; }
    int DefaultFocusIdc() const override { return kApplyIdc; }
    const char* ResourceClassName() const override;

    void Mount(OptionsShell& shell) override;
    void Unmount(OptionsShell& shell) override;
    bool OnButtonClicked(OptionsShell& shell, int idc) override;
    bool OnKeyDown(OptionsShell& shell, unsigned nChar) override;
    void OnSimulate(OptionsShell& shell) override;
    static std::optional<float> ParseValue(const char* text, float minimum, float maximum);

  private:
    static constexpr int kApplyIdc = 9401;
    static constexpr int kCancelIdc = 9402;
    static constexpr int kTitleIdc = 9480;
    static constexpr int kEntryIdc = 9481;

    bool Apply(OptionsShell& shell);
    void ShowRangePrompt(OptionsShell& shell);
    void ShowInvalidPrompt(OptionsShell& shell);
    std::string FormatValue(float value) const;

    std::string m_title;
    float m_value;
    float m_minimum;
    float m_maximum;
    int m_decimalPlaces;
    std::string m_unit;
    ApplyCallback m_onApply;
    PackedColor m_promptColor;
    uint32_t m_invalidUntilMs = 0;
    bool m_promptColorSaved = false;
    float m_lastCursorX = -2.0f;
    float m_lastCursorY = -2.0f;
};
} // namespace Poseidon
