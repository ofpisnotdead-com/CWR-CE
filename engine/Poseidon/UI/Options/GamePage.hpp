#pragma once

#include <Poseidon/UI/Options/ScrollListPage.hpp>

#include <array>
#include <string>


namespace Poseidon
{
class GamePage : public ScrollListPage
{
  public:
    const char* TitleText() const override;

    static int ViewDistanceToSlider(float value);
    static float SliderToViewDistance(int slider);

    void OnReshown(OptionsShell& shell) override;
    void Unmount(OptionsShell& shell) override;

  protected:
    OptionsScrollList::Provider& ProviderRef() override { return m_provider; }

  private:
    static const char* CloseLabel();
    static const char* CloseDescription();

    class GameProvider : public OptionsScrollList::Provider
    {
      public:
        enum : int
        {
            kRowTextLanguage = 0,
            kRowVoiceLanguage = 1,
            kRowViewDistance = 2,
            kRowRespectMissionViewDistance = 3,
            kRowBlood = 4,
            kRowSubtitles = 5,
            kRowRadioSubtitles = 6,
            kRowCount = 7,
        };

        int RowCount() const override { return kRowCount; }
        const char* RowLabel(int row) const override;
        const char* RowDescription(int row) const override;
        OptionsScrollList::RowDef RowFor(int row) const override;
        int RowValue(int row) const override;
        void SetRowValue(int row, int value) override;
        OptionsScrollList::Kind RowKind(int row) const override;
        const char* SliderValueText(int row) const override;
        void OnNumericEntry(int row, Display& host) override;

      private:
        void RefreshToggleTexts() const;
        mutable std::string m_sliderValueText;

        mutable std::array<std::string, 2> m_toggleText;
        mutable std::array<const char*, 2> m_toggleOptions{};
    };

    GameProvider m_game;
    OptionsScrollList::WithCloseRow m_provider{m_game, CloseLabel(), CloseDescription()};
};

} // namespace Poseidon
