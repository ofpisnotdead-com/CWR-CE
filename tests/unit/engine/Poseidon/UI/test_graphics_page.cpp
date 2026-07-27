#include <Poseidon/UI/Options/GraphicsPage.hpp>
#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

// Pins brightness and gamma to value text; other slider rows keep percent.
TEST_CASE("GraphicsPage brightness and gamma rows show values, not percentages", "[UI][GraphicsPage]")
{
    TestableGraphicsPage page;
    auto& p = page.Provider();

    // Rows 7/8 are Brightness/Gamma; an unmounted page holds the cfg defaults.
    REQUIRE(p.SliderValueText(7) != nullptr);
    CHECK(std::string(p.SliderValueText(7)) == "1.6");
    REQUIRE(p.SliderValueText(8) != nullptr);
    CHECK(std::string(p.SliderValueText(8)) == "1.0");

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
