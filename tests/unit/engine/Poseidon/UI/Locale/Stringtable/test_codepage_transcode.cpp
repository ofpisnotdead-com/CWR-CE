#include <catch2/catch_test_macros.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
#include <string>

using Poseidon::Codepage;
using Poseidon::CodepageForLanguage;
using Poseidon::DecodeLegacyTextToRString;
using Poseidon::DecodeLegacyTextToUtf8;
using Poseidon::SelectLegacyTextCodepage;
using Poseidon::TranscodeToUtf8;

// Language -> Codepage mapping

TEST_CASE("CodepageForLanguage - Western European -> CP1252", "[codepage][mapping]")
{
    REQUIRE(CodepageForLanguage("English") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("French") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("German") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("Italian") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("Spanish") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("Dutch") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("Portuguese") == Codepage::CP1252);
}

TEST_CASE("CodepageForLanguage - Central European -> CP1250", "[codepage][mapping]")
{
    REQUIRE(CodepageForLanguage("Czech") == Codepage::CP1250);
    REQUIRE(CodepageForLanguage("Polish") == Codepage::CP1250);
    REQUIRE(CodepageForLanguage("Slovak") == Codepage::CP1250);
    REQUIRE(CodepageForLanguage("Hungarian") == Codepage::CP1250);
    REQUIRE(CodepageForLanguage("Slovenian") == Codepage::CP1250);
    REQUIRE(CodepageForLanguage("Croatian") == Codepage::CP1250);
}

TEST_CASE("CodepageForLanguage - Cyrillic -> CP1251", "[codepage][mapping]")
{
    REQUIRE(CodepageForLanguage("Russian") == Codepage::CP1251);
    REQUIRE(CodepageForLanguage("Ukrainian") == Codepage::CP1251);
    REQUIRE(CodepageForLanguage("Bulgarian") == Codepage::CP1251);
}

TEST_CASE("CodepageForLanguage - Case-insensitive match", "[codepage][mapping]")
{
    REQUIRE(CodepageForLanguage("english") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("ENGLISH") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("EnGlIsH") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("czech") == Codepage::CP1250);
    REQUIRE(CodepageForLanguage("RUSSIAN") == Codepage::CP1251);
}

TEST_CASE("CodepageForLanguage - Unknown falls back to CP1252", "[codepage][mapping]")
{
    REQUIRE(CodepageForLanguage("Klingon") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage("") == Codepage::CP1252);
    REQUIRE(CodepageForLanguage(nullptr) == Codepage::CP1252);
}

// TranscodeToUtf8 -- pass-through for UTF-8 and ASCII

TEST_CASE("TranscodeToUtf8 - UTF-8 mode passes through unchanged", "[codepage][utf8]")
{
    REQUIRE(TranscodeToUtf8("plain ASCII", Codepage::Utf8) == "plain ASCII");

    // UTF-8 encoded "café" -- already multi-byte, should not be re-encoded
    std::string cafe = "caf\xC3\xA9";
    REQUIRE(TranscodeToUtf8(cafe, Codepage::Utf8) == cafe);

    // Empty string
    REQUIRE(TranscodeToUtf8("", Codepage::Utf8).empty());
}

TEST_CASE("TranscodeToUtf8 - ASCII (< 0x80) passes through all codepages", "[codepage][ascii]")
{
    std::string ascii = "Hello, World! 0123456789 @#$%^&*()";

    REQUIRE(TranscodeToUtf8(ascii, Codepage::CP1252) == ascii);
    REQUIRE(TranscodeToUtf8(ascii, Codepage::CP1250) == ascii);
    REQUIRE(TranscodeToUtf8(ascii, Codepage::CP1251) == ascii);
    REQUIRE(TranscodeToUtf8(ascii, Codepage::Utf8) == ascii);
}

TEST_CASE("TranscodeToUtf8 - Empty string", "[codepage][edge]")
{
    REQUIRE(TranscodeToUtf8("", Codepage::CP1252).empty());
    REQUIRE(TranscodeToUtf8("", Codepage::CP1250).empty());
    REQUIRE(TranscodeToUtf8("", Codepage::CP1251).empty());
}

// CP1252 (Western European) -- key character mappings

TEST_CASE("TranscodeToUtf8 - CP1252 copyright symbol", "[codepage][cp1252]")
{
    // © is 0xA9 in CP1252, U+00A9 in Unicode, 0xC2 0xA9 in UTF-8
    std::string input = "\xA9 2026 Fixture Lab";
    std::string expected = "\xC2\xA9 2026 Fixture Lab";
    REQUIRE(TranscodeToUtf8(input, Codepage::CP1252) == expected);
}

TEST_CASE("TranscodeToUtf8 - CP1252 French accents", "[codepage][cp1252]")
{
    // é = 0xE9 (CP1252) -> U+00E9 -> 0xC3 0xA9 (UTF-8)
    // è = 0xE8 -> 0xC3 0xA8
    // à = 0xE0 -> 0xC3 0xA0
    // ê = 0xEA -> 0xC3 0xAA
    // ï = 0xEF -> 0xC3 0xAF
    REQUIRE(TranscodeToUtf8("caf\xE9", Codepage::CP1252) == "caf\xC3\xA9");
    REQUIRE(TranscodeToUtf8("\xE0 la mode", Codepage::CP1252) == "\xC3\xA0 la mode");
    REQUIRE(TranscodeToUtf8("na\xEF"
                            "ve",
                            Codepage::CP1252) == "na\xC3\xAF"
                                                 "ve");
    REQUIRE(TranscodeToUtf8("fen\xEA"
                            "tre",
                            Codepage::CP1252) == "fen\xC3\xAA"
                                                 "tre");
}

TEST_CASE("TranscodeToUtf8 - CP1252 German umlauts and eszett", "[codepage][cp1252]")
{
    // ä = 0xE4 -> U+00E4 -> 0xC3 0xA4
    // ö = 0xF6 -> U+00F6 -> 0xC3 0xB6
    // ü = 0xFC -> U+00FC -> 0xC3 0xBC
    // ß = 0xDF -> U+00DF -> 0xC3 0x9F
    REQUIRE(TranscodeToUtf8("K\xF6ln", Codepage::CP1252) == "K\xC3\xB6ln");
    REQUIRE(TranscodeToUtf8("Gr\xFC\xDF", Codepage::CP1252) == "Gr\xC3\xBC\xC3\x9F");
    REQUIRE(TranscodeToUtf8("Stra\xDF"
                            "e",
                            Codepage::CP1252) == "Stra\xC3\x9F"
                                                 "e");
}

TEST_CASE("TranscodeToUtf8 - CP1252 Spanish", "[codepage][cp1252]")
{
    // n = 0xF1 -> U+00F1 -> 0xC3 0xB1
    // ¿ = 0xBF -> U+00BF -> 0xC2 0xBF
    // ¡ = 0xA1 -> U+00A1 -> 0xC2 0xA1
    REQUIRE(TranscodeToUtf8("se\xF1"
                            "or",
                            Codepage::CP1252) == "se\xC3\xB1"
                                                 "or");
    REQUIRE(TranscodeToUtf8("\xBFQu\xE9?", Codepage::CP1252) == "\xC2\xBFQu\xC3\xA9?");
    REQUIRE(TranscodeToUtf8("\xA1Hola!", Codepage::CP1252) == "\xC2\xA1Hola!");
}

TEST_CASE("TranscodeToUtf8 - CP1252 Euro sign and smart quotes", "[codepage][cp1252]")
{
    // € = 0x80 (CP1252) -> U+20AC -> 0xE2 0x82 0xAC (3-byte UTF-8)
    REQUIRE(TranscodeToUtf8("100\x80", Codepage::CP1252) == "100\xE2\x82\xAC");
    // " = 0x93 -> U+201C -> 0xE2 0x80 0x9C
    // " = 0x94 -> U+201D -> 0xE2 0x80 0x9D
    REQUIRE(TranscodeToUtf8("\x93"
                            "hello\x94",
                            Codepage::CP1252) == "\xE2\x80\x9C"
                                                 "hello\xE2\x80\x9D");
}

// CP1250 (Central European) -- Czech, Polish, Slovak, Hungarian

TEST_CASE("TranscodeToUtf8 - CP1250 Czech diacritics", "[codepage][cp1250]")
{
    // ž = 0x9E (CP1250) -> U+017E -> 0xC5 0xBE
    // š = 0x9A -> U+0161 -> 0xC5 0xA1
    // č = 0xE8 -> U+010D -> 0xC4 0x8D
    // ř = 0xF8 -> U+0159 -> 0xC5 0x99
    // ý = 0xFD -> U+00FD -> 0xC3 0xBD
    // á = 0xE1 -> U+00E1 -> 0xC3 0xA1
    // í = 0xED -> U+00ED -> 0xC3 0xAD
    // é = 0xE9 -> U+00E9 -> 0xC3 0xA9
    REQUIRE(TranscodeToUtf8("\x9E", Codepage::CP1250) == "\xC5\xBE"); // ž
    REQUIRE(TranscodeToUtf8("\x9A", Codepage::CP1250) == "\xC5\xA1"); // š
    REQUIRE(TranscodeToUtf8("\xE8", Codepage::CP1250) == "\xC4\x8D"); // č
    REQUIRE(TranscodeToUtf8("\xF8", Codepage::CP1250) == "\xC5\x99"); // ř
    // Word "čtyři"
    REQUIRE(TranscodeToUtf8("\xE8"
                            "ty\xF8"
                            "i",
                            Codepage::CP1250) == "\xC4\x8D"
                                                 "ty\xC5\x99"
                                                 "i");
}

TEST_CASE("TranscodeToUtf8 - CP1250 Polish diacritics", "[codepage][cp1250]")
{
    // l = 0xB3 -> U+0142 -> 0xC5 0x82
    // ą = 0xB9 -> U+0105 -> 0xC4 0x85
    // ś = 0x9C -> U+015B -> 0xC5 0x9B
    // c = 0xE6 -> U+0107 -> 0xC4 0x87
    // ę = 0xEA -> U+0119 -> 0xC4 0x99
    // z = 0xBF -> U+017C -> 0xC5 0xBC
    REQUIRE(TranscodeToUtf8("\xB3", Codepage::CP1250) == "\xC5\x82"); // l
    REQUIRE(TranscodeToUtf8("\x9C", Codepage::CP1250) == "\xC5\x9B"); // ś
    REQUIRE(TranscodeToUtf8("\xE6", Codepage::CP1250) == "\xC4\x87"); // c
    // "Cześc"
    REQUIRE(TranscodeToUtf8("Cze\x9C\xE6", Codepage::CP1250) == "Cze\xC5\x9B\xC4\x87");
}

TEST_CASE("TranscodeToUtf8 - CP1250 distinguishes from CP1252", "[codepage][cp1250]")
{
    // Byte 0xE8: in CP1252 = è (U+00E8), in CP1250 = č (U+010D)
    // This proves the transcoder uses the codepage it's given.
    REQUIRE(TranscodeToUtf8("\xE8", Codepage::CP1252) == "\xC3\xA8"); // è
    REQUIRE(TranscodeToUtf8("\xE8", Codepage::CP1250) == "\xC4\x8D"); // č

    // Byte 0x9E: CP1252 = ž (U+017E), CP1250 also = ž -- in this case same.
    // Byte 0xFD: CP1252 = ý, CP1250 = ý -- same.
    // Pick a clearer differentiator -- 0xB3:
    // CP1252 0xB3 = ³ (U+00B3), CP1250 0xB3 = l (U+0142)
    REQUIRE(TranscodeToUtf8("\xB3", Codepage::CP1252) == "\xC2\xB3"); // superscript 3
    REQUIRE(TranscodeToUtf8("\xB3", Codepage::CP1250) == "\xC5\x82"); // Polish l
}

TEST_CASE("DecodeLegacyTextToUtf8 - CP1252 fallback detects Central European mod names", "[codepage][legacy][cp1250]")
{
    REQUIRE(SelectLegacyTextCodepage("\xC8"
                                     "MOD",
                                     Codepage::CP1252) == Codepage::CP1250);
    REQUIRE(SelectLegacyTextCodepage("P\xF8"
                                     "iklad \xE8"
                                     "as",
                                     Codepage::CP1252) == Codepage::CP1250);

    REQUIRE(DecodeLegacyTextToUtf8("\xC8"
                                   "MOD",
                                   Codepage::CP1252) == "\xC4\x8C"
                                                        "MOD");

    REQUIRE(DecodeLegacyTextToUtf8("P\xF8"
                                   "iklad \xE8"
                                   "as",
                                   Codepage::CP1252) == "P\xC5\x99"
                                                        "iklad \xC4\x8D"
                                                        "as");
}

TEST_CASE("DecodeLegacyTextToUtf8 - modern UTF-8 is never double encoded", "[codepage][legacy][utf8]")
{
    const std::string utf8 = "\xC4\x8C"
                             "MOD - P\xC5\x99"
                             "iklad";
    REQUIRE(SelectLegacyTextCodepage(utf8, Codepage::CP1252) == Codepage::Utf8);
    REQUIRE(SelectLegacyTextCodepage(utf8, Codepage::CP1250) == Codepage::Utf8);
    REQUIRE(DecodeLegacyTextToUtf8(utf8, Codepage::CP1252) == utf8);
    REQUIRE(DecodeLegacyTextToUtf8(utf8, Codepage::CP1250) == utf8);
}

TEST_CASE("DecodeLegacyTextToUtf8 - CP1250 that is valid UTF-8 still follows Czech column codepage",
          "[codepage][legacy][cp1250]")
{
    const std::string cp1250 = "NEM\xD9\x8E"
                               "U";
    REQUIRE(SelectLegacyTextCodepage(cp1250, Codepage::CP1250) == Codepage::CP1250);
    REQUIRE(DecodeLegacyTextToUtf8(cp1250, Codepage::CP1250) == "NEM\xC5\xAE\xC5\xBD"
                                                                "U");
}

TEST_CASE("DecodeLegacyTextToUtf8 - CP1252 Western accents stay Western", "[codepage][legacy][cp1252]")
{
    REQUIRE(SelectLegacyTextCodepage("citt\xE0"
                                     " \xE8"
                                     " pronta",
                                     Codepage::CP1252) == Codepage::CP1252);
    REQUIRE(DecodeLegacyTextToUtf8("citt\xE0"
                                   " \xE8"
                                   " pronta",
                                   Codepage::CP1252) == "citt\xC3\xA0"
                                                        " \xC3\xA8"
                                                        " pronta");
}

TEST_CASE("DecodeLegacyTextToUtf8 - all supported language preferences repair Central European mod names",
          "[codepage][legacy][detect]")
{
    const char* languages[] = {"English", "French", "Italian", "Spanish", "German", "Czech", "Polish", "Russian"};
    const std::string cp1250 = "\xC8"
                               "MOD - P\xF8"
                               "iklad";
    const std::string expected = "\xC4\x8C"
                                 "MOD - P\xC5\x99"
                                 "iklad";

    for (const char* language : languages)
    {
        REQUIRE(DecodeLegacyTextToUtf8(cp1250, CodepageForLanguage(language)) == expected);
    }
}

TEST_CASE("DecodeLegacyTextToUtf8 - all supported language preferences repair Cyrillic mod text",
          "[codepage][legacy][detect]")
{
    const char* languages[] = {"English", "French", "Italian", "Spanish", "German", "Czech", "Polish", "Russian"};
    const std::string cp1251 = "\xCF\xF0\xE8\xE2\xE5\xF2"; // Привет
    const std::string expected = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";

    for (const char* language : languages)
    {
        REQUIRE(SelectLegacyTextCodepage(cp1251, CodepageForLanguage(language)) == Codepage::CP1251);
        REQUIRE(DecodeLegacyTextToUtf8(cp1251, CodepageForLanguage(language)) == expected);
    }
}

TEST_CASE("DecodeLegacyTextToUtf8 - Western supported languages keep Western accents", "[codepage][legacy][cp1252]")
{
    const char* languages[] = {"English", "French", "Italian", "Spanish", "German"};
    const std::string cp1252 = "\xBF"
                               "Qu\xE9? citt\xE0"
                               " \xE8"
                               " pronta";
    const std::string expected = "\xC2\xBF"
                                 "Qu\xC3\xA9? citt\xC3\xA0"
                                 " \xC3\xA8"
                                 " pronta";

    for (const char* language : languages)
    {
        REQUIRE(DecodeLegacyTextToUtf8(cp1252, CodepageForLanguage(language)) == expected);
    }
}

// CP1251 (Cyrillic) -- Russian

TEST_CASE("TranscodeToUtf8 - CP1251 Cyrillic uppercase range", "[codepage][cp1251]")
{
    // А = 0xC0 -> U+0410 -> 0xD0 0x90
    // Я = 0xDF -> U+042F -> 0xD0 0xAF
    REQUIRE(TranscodeToUtf8("\xC0", Codepage::CP1251) == "\xD0\x90"); // А
    REQUIRE(TranscodeToUtf8("\xDF", Codepage::CP1251) == "\xD0\xAF"); // Я
}

TEST_CASE("TranscodeToUtf8 - CP1251 Cyrillic lowercase range", "[codepage][cp1251]")
{
    // а = 0xE0 -> U+0430 -> 0xD0 0xB0
    // я = 0xFF -> U+044F -> 0xD1 0x8F
    REQUIRE(TranscodeToUtf8("\xE0", Codepage::CP1251) == "\xD0\xB0"); // а
    REQUIRE(TranscodeToUtf8("\xFF", Codepage::CP1251) == "\xD1\x8F"); // я
}

TEST_CASE("TranscodeToUtf8 - CP1251 real Russian word", "[codepage][cp1251]")
{
    // "Воскресенье" (Sunday) in CP1251:
    // В=0xC2, о=0xEE, с=0xF1, к=0xEA, р=0xF0, е=0xE5, с=0xF1, е=0xE5, н=0xED, ь=0xFC, е=0xE5
    // In UTF-8: each Cyrillic char is 2 bytes
    std::string cp1251 = "\xC2\xEE\xF1\xEA\xF0\xE5\xF1\xE5\xED\xFC\xE5";
    std::string utf8 = TranscodeToUtf8(cp1251, Codepage::CP1251);

    // Expected UTF-8 bytes for "Воскресенье"
    // В=U+0412->D0 92, о=U+043E->D0 BE, с=U+0441->D1 81, к=U+043A->D0 BA, р=U+0440->D1 80
    // е=U+0435->D0 B5, с=U+0441->D1 81, е=U+0435->D0 B5, н=U+043D->D0 BD, ь=U+044C->D1 8C, е=U+0435->D0 B5
    std::string expected = "\xD0\x92\xD0\xBE\xD1\x81\xD0\xBA\xD1\x80\xD0\xB5"
                           "\xD1\x81\xD0\xB5\xD0\xBD\xD1\x8C\xD0\xB5";
    REQUIRE(utf8 == expected);
}

TEST_CASE("TranscodeToUtf8 - CP1251 Ukrainian special letters", "[codepage][cp1251]")
{
    // Є = 0xAA -> U+0404, є = 0xBA -> U+0454
    // І = 0xB2 -> U+0406, і = 0xB3 -> U+0456
    // Ї = 0xAF -> U+0407, ї = 0xBF -> U+0457
    REQUIRE(TranscodeToUtf8("\xAA", Codepage::CP1251) == "\xD0\x84"); // Є
    REQUIRE(TranscodeToUtf8("\xB2", Codepage::CP1251) == "\xD0\x86"); // І
    REQUIRE(TranscodeToUtf8("\xAF", Codepage::CP1251) == "\xD0\x87"); // Ї
}

// Edge cases

TEST_CASE("TranscodeToUtf8 - Mixed ASCII and high bytes", "[codepage][mixed]")
{
    // "© 2026 Fixture Lab" in CP1252
    std::string input = "\xA9 2026 Fixture Lab";
    std::string expected = "\xC2\xA9 2026 Fixture Lab";
    REQUIRE(TranscodeToUtf8(input, Codepage::CP1252) == expected);
}

TEST_CASE("DecodeLegacyTextToRString - repairs user-facing CP1250 config literals", "[codepage][decode][rstring]")
{
    const RString decoded = DecodeLegacyTextToRString("Gener\xE1l Novotn\xFD - \xC8SLA", Codepage::CP1252);
    REQUIRE(std::string(decoded.Data()) == "Gener\xC3\xA1l Novotn\xC3\xBD - \xC4\x8CSLA");
}

TEST_CASE("DecodeLegacyTextToRString - repairs Western European place names in script chat",
          "[codepage][decode][rstring]")
{
    const RString decoded = DecodeLegacyTextToRString("Warnem\xFCnde", Codepage::CP1250);
    REQUIRE(std::string(decoded.Data()) == "Warnem\xC3\xBCnde");
}

TEST_CASE("TranscodeToUtf8 - Undefined codepage slot becomes U+FFFD", "[codepage][undefined]")
{
    // In CP1252, 0x81 is undefined -> should become U+FFFD (0xEF 0xBF 0xBD in UTF-8)
    std::string out = TranscodeToUtf8("\x81", Codepage::CP1252);
    REQUIRE(out == "\xEF\xBF\xBD");

    // 0x8D, 0x8F, 0x90, 0x9D are undefined in CP1252
    REQUIRE(TranscodeToUtf8("\x8D", Codepage::CP1252) == "\xEF\xBF\xBD");
    REQUIRE(TranscodeToUtf8("\x9D", Codepage::CP1252) == "\xEF\xBF\xBD");
}

TEST_CASE("TranscodeToUtf8 - All bytes 0x00-0x7F are identity for every codepage", "[codepage][ascii]")
{
    for (int b = 0x01; b < 0x80; ++b)
    {
        std::string in(1, static_cast<char>(b));
        REQUIRE(TranscodeToUtf8(in, Codepage::CP1252) == in);
        REQUIRE(TranscodeToUtf8(in, Codepage::CP1250) == in);
        REQUIRE(TranscodeToUtf8(in, Codepage::CP1251) == in);
    }
}

TEST_CASE("TranscodeToUtf8 - Output is valid UTF-8", "[codepage][utf8_validity]")
{
    // Round-trip every 0x80-0xFF byte through each codepage, verify output is decodable
    auto isValidUtf8 = [](const std::string& s)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
        const unsigned char* end = p + s.size();
        while (p < end)
        {
            if (*p < 0x80)
            {
                p++;
            }
            else if ((*p & 0xE0) == 0xC0)
            {
                if (p + 1 >= end || (p[1] & 0xC0) != 0x80)
                    return false;
                p += 2;
            }
            else if ((*p & 0xF0) == 0xE0)
            {
                if (p + 2 >= end || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
                    return false;
                p += 3;
            }
            else if ((*p & 0xF8) == 0xF0)
            {
                if (p + 3 >= end || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
                    return false;
                p += 4;
            }
            else
            {
                return false;
            }
        }
        return true;
    };

    for (int b = 0x80; b < 0x100; ++b)
    {
        std::string in(1, static_cast<char>(b));
        REQUIRE(isValidUtf8(TranscodeToUtf8(in, Codepage::CP1252)));
        REQUIRE(isValidUtf8(TranscodeToUtf8(in, Codepage::CP1250)));
        REQUIRE(isValidUtf8(TranscodeToUtf8(in, Codepage::CP1251)));
    }
}
