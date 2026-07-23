#include <Poseidon/UI/Options/NotebookTheme.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace Poseidon;

TEST_CASE("NotebookTheme ignores only the vanilla pure-green notebook resource color", "[UI][optionsUI]")
{
    CHECK(NotebookTheme::IsVanillaNotebookGreen(PackedColor(0, 255, 0, 255)));
    CHECK(NotebookTheme::IsVanillaNotebookGreen(PackedColor(0, 255, 0, 64)));

    CHECK_FALSE(NotebookTheme::IsVanillaNotebookGreen(PackedColor(76, 255, 76, 255)));
    CHECK_FALSE(NotebookTheme::IsVanillaNotebookGreen(PackedColor(0, 108, 169, 255)));
}

TEST_CASE("NotebookTheme applies mod resource RGB while preserving each control alpha", "[UI][optionsUI]")
{
    PackedColor fallback(76, 255, 76, 128);
    PackedColor theme(0, 108, 169, 230);

    PackedColor themed = NotebookTheme::ApplyRgbKeepingAlpha(fallback, theme);

    CHECK(themed.R8() == 0);
    CHECK(themed.G8() == 108);
    CHECK(themed.B8() == 169);
    CHECK(themed.A8() == 128);
}
