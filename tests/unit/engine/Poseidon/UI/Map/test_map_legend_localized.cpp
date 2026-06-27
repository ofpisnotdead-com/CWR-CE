#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stddef.h>
#include <catch2/catch_message.hpp>

// Regression for the briefing-map "contour interval / elevations
// in meters" legend strings: both must be sourced from the
// stringtable, not hardcoded ASCII.  Without this, the bottom-left
// scale legend stays English on every UI language and the
// per-mission utf8 translation never reaches the rendered glyphs.

namespace
{
std::string ReadTextFile(const std::filesystem::path& p)
{
    std::ifstream f(p);
    if (!f.is_open())
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string ExtractFunctionBody(const std::string& src, const std::string& prototype)
{
    const size_t protoPos = src.find(prototype);
    if (protoPos == std::string::npos)
        return {};
    const size_t openBrace = src.find('{', protoPos);
    if (openBrace == std::string::npos)
        return {};
    int depth = 1;
    for (size_t i = openBrace + 1; i < src.size(); ++i)
    {
        if (src[i] == '{')
            ++depth;
        else if (src[i] == '}')
        {
            if (--depth == 0)
                return src.substr(openBrace, i - openBrace + 1);
        }
    }
    return {};
}
} // namespace

TEST_CASE("CStaticMap::DrawLegend pulls scale labels from stringtable", "[ui][map][legend][localization][regression]")
{
    const std::filesystem::path cppPath =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "Poseidon" / "UI" / "Map" / "UIMap.cpp";
    const std::string src = ReadTextFile(cppPath);
    REQUIRE_FALSE(src.empty());

    const std::string body = ExtractFunctionBody(src, "CStaticMap::DrawLegend");
    REQUIRE_FALSE(body.empty());

    // Both labels must come from the stringtable.  LocalizeString
    // takes either the bare key (e.g. "STR_MAP_CONTOUR_INTERVAL")
    // or the legacy "$STR_..." form; the audit accepts either.
    CAPTURE(body);
    const bool hasContourKey = body.find("STR_MAP_CONTOUR_INTERVAL") != std::string::npos;
    const bool hasElevationKey = body.find("STR_MAP_ELEVATIONS_IN_METERS") != std::string::npos;
    REQUIRE(hasContourKey);
    REQUIRE(hasElevationKey);

    // And the hardcoded literals must be gone — they masked the
    // localization for years in OFP and shouldn't survive the
    // remaster.
    REQUIRE(body.find("COUNTOUR INTERVAL") == std::string::npos);
    REQUIRE(body.find("Elevations in meters") == std::string::npos);
}

TEST_CASE("STRINGTABLE_MAP.utf8.csv ships both legend keys with English fallbacks",
          "[ui][map][legend][localization][regression][external-data]")
{
    // The translations live in a remaster-only utf8 shard so the
    // legacy STRINGTABLE.CSV (cp1250) stays untouched.  Verify the
    // shard exists, has the right keys, and the English column is
    // non-empty (the engine falls back to English when a target
    // language column is blank — so English is the floor).
    const std::filesystem::path csvPath = std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "packages" /
                                          "Combined" / "BIN" / "STRINGTABLE_MAP.utf8.csv";
    const std::string src = ReadTextFile(csvPath);
    CAPTURE(csvPath.string());
    REQUIRE_FALSE(src.empty());

    REQUIRE(src.find("STR_MAP_CONTOUR_INTERVAL,") != std::string::npos);
    REQUIRE(src.find("STR_MAP_ELEVATIONS_IN_METERS,") != std::string::npos);

    // The English value must include the `%g` placeholder for the
    // contour-interval row so the caller can `DrawTextF(..., step)`
    // against it.
    const size_t pos = src.find("STR_MAP_CONTOUR_INTERVAL,");
    REQUIRE(pos != std::string::npos);
    const size_t eol = src.find('\n', pos);
    const std::string row = src.substr(pos, eol - pos);
    CAPTURE(row);
    REQUIRE(row.find("%g") != std::string::npos);
}
