// test_font_mapping.cpp - Unit tests for the FreeType TTF mapping table in
// engine/Poseidon/Graphics/Rendering/Draw/font.cpp.
//
// There is a single shipping font set (the OFL faces under fonts/). Two name
// spaces resolve via FindFontMapping:
//   - generic role names (cwrtitle / cwrbody / cwrmono / cwrserif / cwrhand)
//   - legacy RESOURCE.BIN aliases (steelfishb / tahomab / couriernewb /
//     garamond / audreyshand / ru_audreyshand) kept for back-compat
//
// These tests pin the path routing, the per-face tuning constants, the
// language-prefix stripping, and — critically — that the handwriting face is a
// single dual-script (Latin + Cyrillic) font so Russian briefings render
// (ru_audreyshand must resolve to the same cwr_hand.ttf, not a Latin-only or
// removed face).

#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontMapping.hpp>
#include <cstring>

using Poseidon::FindFontMapping;

TEST_CASE("Font mapping resolves the shipping set to fonts/*.ttf", "[font][mapping]")
{
    SECTION("cwrtitle -> fonts/cwr_title.ttf (Oswald 700, tuned)")
    {
        auto* m = FindFontMapping("cwrtitleb128");
        REQUIRE(m != nullptr);
        CHECK(std::strstr(m->ttfPath, "cwr_title.ttf") != nullptr);
        CHECK(std::strstr(m->ttfPath, "alt") == nullptr);
        CHECK(std::strstr(m->ttfPath, "legacy") == nullptr);
        CHECK(m->renderPx == 46);
        CHECK(m->widthScale == 0.628f);
        CHECK(m->baselineOffset == -6.0f);
        CHECK(m->syntheticBold == -1.5f);
    }
    SECTION("cwrbody -> fonts/cwr_body.ttf (Roboto 700)")
    {
        auto* m = FindFontMapping("cwrbodyb24");
        REQUIRE(m != nullptr);
        CHECK(std::strstr(m->ttfPath, "cwr_body.ttf") != nullptr);
        CHECK(m->renderPx == 10);
        CHECK(m->widthScale == 1.000f);
    }
    SECTION("cwrmono -> fonts/cwr_mono.ttf (Unuaranga Kuriero Bold)")
    {
        auto* m = FindFontMapping("cwrmonob64");
        REQUIRE(m != nullptr);
        CHECK(std::strstr(m->ttfPath, "cwr_mono.ttf") != nullptr);
        CHECK(m->renderPx == 28);
        CHECK(m->widthScale == 0.800f);
        CHECK(m->syntheticBold == -0.6f);
    }
    SECTION("cwrserif -> fonts/cwr_serif.ttf (Vollkorn 700)")
    {
        auto* m = FindFontMapping("cwrserif64");
        REQUIRE(m != nullptr);
        CHECK(std::strstr(m->ttfPath, "cwr_serif.ttf") != nullptr);
        CHECK(m->widthScale == 0.935f);
    }
    SECTION("cwrhand -> fonts/cwr_hand.ttf (Caveat, dual-script, tuned)")
    {
        auto* m = FindFontMapping("cwrhandi48");
        REQUIRE(m != nullptr);
        CHECK(std::strstr(m->ttfPath, "cwr_hand.ttf") != nullptr);
        CHECK(std::strstr(m->ttfPath, "alt") == nullptr);
        // Caveat is already cursive — no synthetic oblique (Cedarville needed it).
        CHECK(m->syntheticOblique == false);
        CHECK(m->renderPx == 21);
        CHECK(m->widthScale == 1.183f);
    }
}

TEST_CASE("Russian handwriting resolves to the dual-script hand face", "[font][mapping][i18n]")
{
    // Regression: the handwriting face must cover Cyrillic so Russian briefings
    // render. cwrhand = Caveat (Latin + Cyrillic), and ru_audreyshand is just
    // an alias for it. Broken state (the old Latin-only Cedarville, or a removed
    // legacy face) would resolve ru_audreyshand to a different/missing ttf.
    auto* hand = FindFontMapping("cwrhandi48");
    auto* ru = FindFontMapping("ru_audreyshandi48");
    REQUIRE(hand != nullptr);
    REQUIRE(ru != nullptr);
    CHECK(std::strstr(ru->ttfPath, "cwr_hand.ttf") != nullptr);
    CHECK(std::strstr(ru->ttfPath, "legacy") == nullptr);
    CHECK(std::strstr(ru->ttfPath, "comicz") == nullptr);
    // Same underlying face as the Latin hand.
    CHECK(std::strcmp(ru->ttfPath, hand->ttfPath) == 0);
}

TEST_CASE("Legacy RESOURCE.BIN font names still resolve (back-compat)", "[font][mapping][compat]")
{
    struct
    {
        const char* name;
        const char* ttf;
    } cases[] = {
        {"steelfishb48", "cwr_title.ttf"}, {"tahomab12", "cwr_body.ttf"},      {"couriernewb64", "cwr_mono.ttf"},
        {"garamond64", "cwr_serif.ttf"},   {"audreyshandi48", "cwr_hand.ttf"},
    };
    for (const auto& c : cases)
    {
        auto* m = FindFontMapping(c.name);
        REQUIRE(m != nullptr);
        CHECK(std::strstr(m->ttfPath, c.ttf) != nullptr);
    }
}

TEST_CASE("FindFontMapping strips language prefixes (cz_/ru_/pl_)", "[font][mapping][lang]")
{
    auto* cz = FindFontMapping("cz_tahomab12");
    REQUIRE(cz != nullptr);
    CHECK(std::strstr(cz->ttfPath, "cwr_body.ttf") != nullptr);

    auto* pl = FindFontMapping("pl_cwrtitleb128");
    REQUIRE(pl != nullptr);
    CHECK(std::strstr(pl->ttfPath, "cwr_title.ttf") != nullptr);
}

TEST_CASE("FindFontMapping returns nullptr for unknown prefix", "[font][mapping]")
{
    CHECK(FindFontMapping("nonexistentfont") == nullptr);
    CHECK(FindFontMapping("") == nullptr);
}
