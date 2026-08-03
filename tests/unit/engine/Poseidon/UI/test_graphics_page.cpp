#include <Poseidon/UI/Options/GraphicsPage.hpp>
#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using namespace Poseidon;
namespace
{
class TestableGraphicsPage : public GraphicsPage
{
  public:
    OptionsScrollList::Provider& Provider() { return ProviderRef(); }
};
} // namespace
TEST_CASE("GraphicsPage brightness slider conversion clamps into range", "[UI][GraphicsPage]")
{
    CHECK(GraphicsPage::BrightnessToSlider(0.1f) == 0);
    CHECK(GraphicsPage::BrightnessToSlider(0.4f) == 0);
    CHECK(GraphicsPage::BrightnessToSlider(1.1f) == 50);
    CHECK(GraphicsPage::BrightnessToSlider(1.8f) == 100);
    CHECK(GraphicsPage::BrightnessToSlider(2.2f) == 100);

    CHECK(GraphicsPage::SliderToBrightness(-10) == Catch::Approx(0.4f));
    CHECK(GraphicsPage::SliderToBrightness(50) == Catch::Approx(1.1f));
    CHECK(GraphicsPage::SliderToBrightness(100) == Catch::Approx(1.8f));
    CHECK(GraphicsPage::SliderToBrightness(120) == Catch::Approx(1.8f));
}

TEST_CASE("GraphicsPage gamma slider conversion clamps into range", "[UI][GraphicsPage]")
{
    CHECK(GraphicsPage::GammaToSlider(0.1f) == 0);
    CHECK(GraphicsPage::GammaToSlider(0.5f) == 0);
    CHECK(GraphicsPage::GammaToSlider(1.4f) == 50);
    CHECK(GraphicsPage::GammaToSlider(2.3f) == 100);
    CHECK(GraphicsPage::GammaToSlider(2.8f) == 100);

    CHECK(GraphicsPage::SliderToGamma(-10) == Catch::Approx(0.5f));
    CHECK(GraphicsPage::SliderToGamma(50) == Catch::Approx(1.4f));
    CHECK(GraphicsPage::SliderToGamma(100) == Catch::Approx(2.3f));
    CHECK(GraphicsPage::SliderToGamma(120) == Catch::Approx(2.3f));
}

// The row text prints one decimal, so every slider position must land on one.
TEST_CASE("GraphicsPage slider values snap to one decimal", "[UI][GraphicsPage]")
{
    for (int s = 0; s <= 100; ++s)
    {
        const float g = GraphicsPage::SliderToGamma(s);
        CHECK(g == Catch::Approx(std::round(g * 10.0f) / 10.0f));
        const float b = GraphicsPage::SliderToBrightness(s);
        CHECK(b == Catch::Approx(std::round(b * 10.0f) / 10.0f));
    }

    CHECK(GraphicsPage::SliderToGamma(GraphicsPage::GammaToSlider(1.2f)) == Catch::Approx(1.2f));
    CHECK(GraphicsPage::SliderToBrightness(GraphicsPage::BrightnessToSlider(1.6f)) == Catch::Approx(1.6f));
}

// Pins brightness and gamma to value text; other slider rows keep percent.
TEST_CASE("GraphicsPage brightness and gamma rows show values, not percentages", "[UI][GraphicsPage]")
{
    TestableGraphicsPage page;
    auto& p = page.Provider();

    // Rows 7/8 are Brightness/Gamma; an unmounted page holds the cfg defaults.
    REQUIRE(p.SliderValueText(7) != nullptr);
    CHECK(std::string(p.SliderValueText(7)) == "1.6");
    REQUIRE(p.SliderValueText(8) != nullptr);
    CHECK(std::string(p.SliderValueText(8)) == "1.2");

    CHECK(p.SliderValueText(5) == nullptr);
}

TEST_CASE("GraphicsPage fps-cap index mapping falls back to unlimited for unknown values", "[UI][GraphicsPage]")
{
    CHECK(GraphicsPage::FpsCapValueToIndex(0) == 0);
    CHECK(GraphicsPage::FpsCapValueToIndex(30) == 1);
    CHECK(GraphicsPage::FpsCapValueToIndex(60) == 2);
    CHECK(GraphicsPage::FpsCapValueToIndex(144) == 5);
    CHECK(GraphicsPage::FpsCapValueToIndex(240) == 6);
    CHECK(GraphicsPage::FpsCapValueToIndex(165) == 0);
}

TEST_CASE("GraphicsPage MSAA index mapping falls back to off for unknown counts", "[UI][GraphicsPage]")
{
    CHECK(GraphicsPage::MsaaSamplesToIndex(0) == 0);
    CHECK(GraphicsPage::MsaaSamplesToIndex(2) == 1);
    CHECK(GraphicsPage::MsaaSamplesToIndex(4) == 2);
    CHECK(GraphicsPage::MsaaSamplesToIndex(8) == 3);
    CHECK(GraphicsPage::MsaaSamplesToIndex(16) == 0);

    for (int i = 0; i < 4; ++i)
        CHECK(GraphicsPage::MsaaSamplesToIndex(GraphicsPage::MsaaIndexToSamples(i)) == i);
    CHECK(GraphicsPage::MsaaIndexToSamples(4) == 0);
    CHECK(GraphicsPage::MsaaIndexToSamples(-1) == 0);
}

TEST_CASE("GraphicsPage render-scale index mapping picks the nearest step", "[UI][GraphicsPage]")
{
    CHECK(GraphicsPage::RenderScaleToIndex(1.0f) == 0);
    CHECK(GraphicsPage::RenderScaleToIndex(1.25f) == 1);
    CHECK(GraphicsPage::RenderScaleToIndex(1.5f) == 2);
    CHECK(GraphicsPage::RenderScaleToIndex(1.75f) == 3);
    CHECK(GraphicsPage::RenderScaleToIndex(2.0f) == 4);

    CHECK(GraphicsPage::RenderScaleToIndex(1.6f) == 2);
    CHECK(GraphicsPage::RenderScaleToIndex(1.05f) == 0);

    for (int i = 0; i < 5; ++i)
        CHECK(GraphicsPage::RenderScaleToIndex(GraphicsPage::RenderScaleIndexToValue(i)) == i);
    CHECK(GraphicsPage::RenderScaleIndexToValue(5) == Catch::Approx(1.0f));
    CHECK(GraphicsPage::RenderScaleIndexToValue(-1) == Catch::Approx(1.0f));
}

TEST_CASE("GraphicsPage anti-aliasing rows round-trip through the provider", "[UI][GraphicsPage]")
{
    TestableGraphicsPage page;
    auto& p = page.Provider();

    CHECK(p.RowValue(10) == 0);
    CHECK(p.RowValue(11) == 0);

    p.SetRowValue(10, 2);
    CHECK(p.RowValue(10) == 2);
    p.SetRowValue(11, 4);
    CHECK(p.RowValue(11) == 4);
}

TEST_CASE("GraphicsPage multitexturing row is a boolean defaulting to on", "[UI][GraphicsPage]")
{
    TestableGraphicsPage page;
    auto& p = page.Provider();

    CHECK(p.RowKind(12) == OptionsScrollList::KindBoolean);
    CHECK(p.RowValue(12) == 1);

    p.SetRowValue(12, 0);
    CHECK(p.RowValue(12) == 0);
    p.SetRowValue(12, 1);
    CHECK(p.RowValue(12) == 1);
}

// A focusable divider would strand keyboard nav on a row with no value to change.
TEST_CASE("GraphicsPage advanced divider is an unfocusable header", "[UI][GraphicsPage]")
{
    TestableGraphicsPage page;
    auto& p = page.Provider();

    CHECK(p.RowKind(9) == OptionsScrollList::KindHeader);
    CHECK_FALSE(OptionsScrollList::CanRowReceiveFocus(p.RowKind(9)));
}
