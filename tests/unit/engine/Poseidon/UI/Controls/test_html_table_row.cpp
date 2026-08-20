#include <Poseidon/UI/Controls/UIControlsBase.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace Poseidon;

namespace
{
// Page width of the briefing notepad on a 16:9 viewport, with the RscHTML text
// heights the game data defines.
constexpr float PageWidth = 0.2446f;
constexpr float SizeP = 0.47f * 0.048f;
constexpr float SizeH2 = 0.7f * 0.048f;

class TestHtmlContainer : public CHTMLContainer
{
  public:
    TestHtmlContainer()
    {
        _sizeP = SizeP;
        _sizeH2 = SizeH2;
        _filename = "test.html";
    }

    float GetPageWidth() const override { return PageWidth; }
    float GetPageHeight() const override { return 1.0f; }
    float GetTextWidth(float, Font*, const char* text) const override { return 0.01f * std::strlen(text); }
};

// The gear page's unit header: left arrow, unit name, right arrow, as three cells
// filling the page. The arrows carry no picture, so their width comes from the
// requested height - the same width the square arrow artwork resolves to.
void AddUnitHeaderRow(TestHtmlContainer& html, int section)
{
    const float pw = html.GetPageWidth();
    const float arrowHeight = 480.0f * 1.5f * html.GetPHeight();
    html.AddImage(section, "", HACenter, false, -1.0f, arrowHeight, "Gear:Prev", RString(), 0.1f * pw);
    html.AddText(section, "1: Ngwala Bahalai", HFH2, HACenter, false, false, "", 0.8f * pw);
    html.AddImage(section, "", HACenter, false, -1.0f, arrowHeight, "Gear:Next", RString(), 0.1f * pw);
    html.AddBreak(section, false);
}
} // namespace

TEST_CASE("FormatSection keeps a row of cells that fills the page on one row", "[ui][html]")
{
    TestHtmlContainer html;
    const int section = html.AddSection();
    AddUnitHeaderRow(html, section);
    html.FormatSection(section);

    const HTMLSection& formatted = html.GetSection(section);
    REQUIRE(formatted.rows.Size() >= 1);
    // Fields 0..2 are the three cells, field 3 the break that closes the row; the
    // row reaches past the break only when all three cells stayed on it.
    CHECK(formatted.rows[0].firstField == 0);
    CHECK(formatted.rows[0].lastField == 4);
    CHECK(formatted.rows[0].width == Catch::Approx(PageWidth).epsilon(0.001));
}

TEST_CASE("FormatSection measures a picture cell by its cell width", "[ui][html]")
{
    TestHtmlContainer html;
    const int section = html.AddSection();
    const float cell = 0.5f * PageWidth;
    html.AddImage(section, "", HALeft, false, -1.0f, 16.0f, "", RString(), cell);
    html.FormatSection(section);

    CHECK(html.GetSection(section).rows[0].width == Catch::Approx(cell).epsilon(0.001));
}
