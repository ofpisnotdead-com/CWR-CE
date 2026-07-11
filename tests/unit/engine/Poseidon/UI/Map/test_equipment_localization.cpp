#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <stddef.h>
#include <catch2/catch_message.hpp>
#include <iterator>

// Regression for the briefing weapon-details ("equipment library") path.
//
// The legacy equipment HTML (`equip\equipment.html` inside DTAEXT.PBO)
// shipped only as EN/FR/IT/DE/ES — Czech, Polish, and Russian users
// have seen English text since 2001.  The remaster replaces that with
// a single UTF-8 HTML using `$STR_EQ_*` stringtable tokens, plus a
// dedicated `STRINGTABLE_EQUIPMENT.utf8.csv` shard carrying the
// translations for all 8 UI languages.
//
// This test pins two structural properties:
//   1. `GetEquipmentFile` prefers `equipment.utf8.html` over the
//      legacy `equipment.html` (so the tokenized file wins).
//   2. `STRINGTABLE_EQUIPMENT.utf8.csv` exists in the remaster bin
//      and ships at least the M16 keys with non-empty values across
//      EN/FR/IT/DE/ES/CZ/PL/RU.
//   3. The replacement HTML at
//      `packages/Combined/DTA/dtaExt/equip/equipment.utf8.html`
//      references `$STR_EQ_M16_*` tokens (proving it's the new
//      tokenized variant, not the legacy CP1252 file).

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
    size_t protoPos = src.find(prototype);
    while (protoPos != std::string::npos)
    {
        size_t bodyStart = protoPos + prototype.size();
        while (bodyStart < src.size() &&
               (src[bodyStart] == ' ' || src[bodyStart] == '\t' || src[bodyStart] == '\r' || src[bodyStart] == '\n'))
        {
            ++bodyStart;
        }
        if (bodyStart < src.size() && src[bodyStart] == ';')
        {
            protoPos = src.find(prototype, bodyStart + 1);
            continue;
        }

        if (bodyStart >= src.size() || src[bodyStart] != '{')
            return {};

        int depth = 1;
        for (size_t i = bodyStart + 1; i < src.size(); ++i)
        {
            if (src[i] == '{')
                ++depth;
            else if (src[i] == '}')
            {
                if (--depth == 0)
                    return src.substr(bodyStart, i - bodyStart + 1);
            }
        }
        return {};
    }
    return {};
}
} // namespace

TEST_CASE("GetEquipmentFile prefers .utf8.html over the legacy CP-encoded variant",
          "[ui][map][equipment][localization][regression]")
{
    const std::filesystem::path cppPath =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "Poseidon" / "UI" / "Map" / "UIMapDisplay.cpp";
    const std::string src = ReadTextFile(cppPath);
    REQUIRE_FALSE(src.empty());

    // Match the definition, not the forward declaration (`RString
    // GetEquipmentFile();` appears earlier in the file). Keep the probe
    // insensitive to CRLF-vs-LF line endings after the signature.
    const std::string body = ExtractFunctionBody(src, "RString GetEquipmentFile()");
    REQUIRE_FALSE(body.empty());

    // The UTF-8 candidate has to appear before the plain `.html`
    // candidate or the loader would never reach it.
    const size_t utf8Pos = body.find(".utf8.html");
    const size_t legacyPos = body.rfind(".html\")");
    CAPTURE(body);
    REQUIRE(utf8Pos != std::string::npos);
    REQUIRE(utf8Pos < legacyPos);
}

TEST_CASE("STRINGTABLE_EQUIPMENT.utf8.csv ships M16 keys for all 8 languages",
          "[ui][map][equipment][localization][regression][external-data]")
{
    const std::filesystem::path csvPath = std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "packages" /
                                          "Combined" / "BIN" / "STRINGTABLE_EQUIPMENT.utf8.csv";
    const std::string src = ReadTextFile(csvPath);
    CAPTURE(csvPath.string());
    REQUIRE_FALSE(src.empty());

    // 8 languages: English, French, Italian, Spanish, German, Czech,
    // Polish, Russian.  Row must have 8 non-empty columns after the
    // key (i.e. 9 comma-separated fields total).
    const std::array<const char*, 3> requiredM16Keys = {
        "STR_EQ_M16_TITLE",
        "STR_EQ_M16_SUBTITLE",
        "STR_EQ_M16_DESC",
    };
    for (const char* key : requiredM16Keys)
    {
        CAPTURE(key);
        const std::string needle = std::string(key) + ",";
        const size_t pos = src.find(needle);
        REQUIRE(pos != std::string::npos);
        const size_t eol = src.find('\n', pos);
        const std::string row = src.substr(pos, eol - pos);
        CAPTURE(row);
        // Count commas — need exactly 8 (key + 8 columns).  Empty
        // trailing cells fail this on purpose.
        int columnsAfterKey = 0;
        int nonEmptyAfterKey = 0;
        bool sawSomethingThisCol = false;
        for (size_t i = needle.size(); i < row.size(); ++i)
        {
            if (row[i] == ',')
            {
                ++columnsAfterKey;
                if (sawSomethingThisCol)
                    ++nonEmptyAfterKey;
                sawSomethingThisCol = false;
            }
            else if (row[i] != '\r' && row[i] != ' ')
            {
                sawSomethingThisCol = true;
            }
        }
        if (sawSomethingThisCol)
            ++nonEmptyAfterKey;
        ++columnsAfterKey; // last column doesn't end in comma
        CAPTURE(columnsAfterKey, nonEmptyAfterKey);
        REQUIRE(columnsAfterKey == 8);
        REQUIRE(nonEmptyAfterKey == 8);
    }
}

TEST_CASE("equipment.utf8.html lives loose and references STR_EQ_M16 tokens",
          "[ui][map][equipment][localization][regression][external-data]")
{
    const std::filesystem::path htmlPath = std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "packages" /
                                           "Combined" / "dtaExt" / "equip" / "equipment.utf8.html";
    const std::string src = ReadTextFile(htmlPath);
    CAPTURE(htmlPath.string());
    REQUIRE_FALSE(src.empty());

    REQUIRE(src.find("$STR_EQ_M16_TITLE") != std::string::npos);
    REQUIRE(src.find("$STR_EQ_M16_DESC") != std::string::npos);
    // The legacy English literal must be gone — its presence would
    // mean the tokenization step on M16 never landed.
    REQUIRE(src.find("M16 is the assault rifle") == std::string::npos);
}

TEST_CASE("equipment.utf8.html line endings are clean CRLF (no doubled CR)",
          "[ui][map][equipment][localization][regression][external-data]")
{
    // An earlier tokenization-script bug wrote the file with Python's
    // text-mode line-ending translation enabled, which turned the
    // existing `\r\n` line terminators into `\r\r\n`.  The HTML
    // parser tokenizes by line and saw the empty line between every
    // `\r` and `\r\n`, dropping sections entirely — briefing
    // equipment page rendered as a blank tab with no icons.
    //
    // Pin the byte sequence so that regression can't reappear.
    const std::filesystem::path htmlPath = std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "packages" /
                                           "Combined" / "dtaExt" / "equip" / "equipment.utf8.html";
    std::ifstream f(htmlPath, std::ios::binary);
    REQUIRE(f.is_open());
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE_FALSE(bytes.empty());

    int doubledCR = 0;
    for (size_t i = 0; i + 1 < bytes.size(); ++i)
    {
        if (bytes[i] == '\r' && bytes[i + 1] == '\r')
            ++doubledCR;
    }
    CAPTURE(doubledCR);
    REQUIRE(doubledCR == 0);
}

TEST_CASE("equipment.utf8.html preserves every EQ_ anchor and weapon image",
          "[ui][map][equipment][localization][regression][external-data]")
{
    // Before tokenization the briefing carried 29 weapon library
    // entries.  After tokenization the count must still be 29 and
    // each entry must keep its `<img src="@equip\...">` reference so
    // the inventory page renders pictures for every weapon.
    const std::filesystem::path htmlPath = std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "packages" /
                                           "Combined" / "dtaExt" / "equip" / "equipment.utf8.html";
    const std::string src = ReadTextFile(htmlPath);
    REQUIRE_FALSE(src.empty());

    // Count distinct EQ_ anchors (`<a name="EQ_xxx"></a>`).
    std::set<std::string> anchors;
    size_t pos = 0;
    const std::string needle = "<a name=\"EQ_";
    while ((pos = src.find(needle, pos)) != std::string::npos)
    {
        const size_t nameStart = pos + needle.size();
        const size_t nameEnd = src.find('"', nameStart);
        if (nameEnd == std::string::npos)
            break;
        anchors.insert(src.substr(nameStart, nameEnd - nameStart));
        pos = nameEnd;
    }
    CAPTURE(anchors.size());
    REQUIRE(anchors.size() == 29);

    // Every weapon must keep its `@equip\` image reference.  Count
    // is the simplest pin — the original file had at least one image
    // per weapon, and the parameters page reuses the same picture,
    // so >= 29 is the floor.
    int imgRefs = 0;
    size_t imgPos = 0;
    while ((imgPos = src.find("@equip\\", imgPos)) != std::string::npos)
    {
        ++imgRefs;
        ++imgPos;
    }
    CAPTURE(imgRefs);
    REQUIRE(imgRefs >= 29);

    // Subpage tokens — at least one weapon must have its parameters
    // page title tokenized (proves the page2 tokenization landed).
    REQUIRE(src.find("$STR_EQ_M4_PAGE2_TITLE") != std::string::npos);
    REQUIRE(src.find("$STR_EQ_LABEL_PARAMETERS") != std::string::npos);
    REQUIRE(src.find("$STR_EQ_LABEL_LENGTH") != std::string::npos);
    REQUIRE(src.find("$STR_EQ_LABEL_WEIGHT") != std::string::npos);

    // The literal "Parameters" subtitle (English-only) must be gone
    // from page2 headers — if it appears as bare text, page2 wasn't
    // tokenized.  Allow it only inside comments.
    // (The `<a href` to the page2 anchor's text gets tokenized too.)
    // We require zero `>Parameters<` substrings in the document.
    REQUIRE(src.find(">Parameters<") == std::string::npos);
}
