#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
#include <Poseidon/UI/Locale/LanguageRegistry.hpp>
#include <array>
#include <cstring>
#include <cstdint>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{

// Unicode codepoint for each byte in 0x80..0xFF. 0x0000 marks "undefined in this codepage".
// Source: Unicode Consortium mapping tables (WindowsBestFit).

// Windows-1252 (Western European)
static constexpr uint16_t kCp1252[128] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000, // 0x88-0x8F
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178, // 0x98-0x9F
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, // 0xA0-0xA7
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF, // 0xA8-0xAF
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, // 0xB0-0xB7
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF, // 0xB8-0xBF
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7, // 0xC0-0xC7
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF, // 0xC8-0xCF
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, // 0xD0-0xD7
    0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF, // 0xD8-0xDF
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, // 0xE0-0xE7
    0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF, // 0xE8-0xEF
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, // 0xF0-0xF7
    0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF, // 0xF8-0xFF
};

// Windows-1250 (Central European: Czech, Polish, Slovak, Hungarian, ...)
static constexpr uint16_t kCp1250[128] = {
    0x20AC, 0x0000, 0x201A, 0x0000, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
    0x0000, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179, // 0x88-0x8F
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
    0x0000, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A, // 0x98-0x9F
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, // 0xA0-0xA7
    0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B, // 0xA8-0xAF
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, // 0xB0-0xB7
    0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C, // 0xB8-0xBF
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, // 0xC0-0xC7
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E, // 0xC8-0xCF
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, // 0xD0-0xD7
    0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF, // 0xD8-0xDF
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, // 0xE0-0xE7
    0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F, // 0xE8-0xEF
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, // 0xF0-0xF7
    0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9, // 0xF8-0xFF
};

// Windows-1251 (Cyrillic: Russian, Ukrainian, Bulgarian, ...)
static constexpr uint16_t kCp1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F, // 0x88-0x8F
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
    0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, // 0x98-0x9F
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, // 0xA0-0xA7
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, // 0xA8-0xAF
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, // 0xB0-0xB7
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, // 0xB8-0xBF
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, // 0xC0-0xC7
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, // 0xC8-0xCF
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, // 0xD0-0xD7
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, // 0xD8-0xDF
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, // 0xE0-0xE7
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, // 0xE8-0xEF
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, // 0xF0-0xF7
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F, // 0xF8-0xFF
};

Codepage CodepageForLanguage(const char* lang)
{
    if (!lang)
    {
        return Codepage::CP1252;
    }

    // Per-game override: a registered language (from CfgLanguages, else built-in defaults) wins.
    if (const CfgLib::LanguageInfo* info = CfgLib::LanguageRegistry::Instance().Find(lang))
    {
        return info->codepage;
    }

    // Fallback table for languages the game doesn't list but whose legacy encoding we still know.
    struct Entry
    {
        const char* name;
        Codepage cp;
    };
    static constexpr Entry kTable[] = {
        {"English", Codepage::CP1252},    {"French", Codepage::CP1252},    {"German", Codepage::CP1252},
        {"Italian", Codepage::CP1252},    {"Spanish", Codepage::CP1252},   {"Dutch", Codepage::CP1252},
        {"Portuguese", Codepage::CP1252}, {"Danish", Codepage::CP1252},    {"Swedish", Codepage::CP1252},
        {"Norwegian", Codepage::CP1252},  {"Finnish", Codepage::CP1252},   {"Icelandic", Codepage::CP1252},
        {"Czech", Codepage::CP1250},      {"Polish", Codepage::CP1250},    {"Slovak", Codepage::CP1250},
        {"Hungarian", Codepage::CP1250},  {"Slovenian", Codepage::CP1250}, {"Croatian", Codepage::CP1250},
        {"Romanian", Codepage::CP1250},   {"Russian", Codepage::CP1251},   {"Ukrainian", Codepage::CP1251},
        {"Bulgarian", Codepage::CP1251},
    };
    for (const auto& e : kTable)
    {
        if (stricmp(lang, e.name) == 0)
        {
            return e.cp;
        }
    }
    return Codepage::CP1252;
}

static bool IsValidUtf8(const std::string& input)
{
    const auto* p = reinterpret_cast<const unsigned char*>(input.data());
    const auto* end = p + input.size();
    while (p < end)
    {
        if (*p < 0x80)
        {
            ++p;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            if (p + 1 >= end || (p[1] & 0xC0) != 0x80 || *p < 0xC2)
            {
                return false;
            }
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            if (p + 2 >= end || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
            {
                return false;
            }
            p += 3;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            if (p + 3 >= end || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
            {
                return false;
            }
            p += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
}

static bool ContainsCp1252UndefinedByte(const std::string& input)
{
    for (unsigned char b : input)
    {
        switch (b)
        {
            case 0x8C:
            case 0x8D:
            case 0x8F:
            case 0x9C:
            case 0x9D:
            case 0x9F:
                return true;
            default:
                break;
        }
    }
    return false;
}

static bool ContainsUtf8(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

static bool HasUtf8Replacement(const std::string& text)
{
    return ContainsUtf8(text, "\xEF\xBF\xBD");
}

static bool IsAsciiAlpha(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool IsAsciiUpper(unsigned char c)
{
    return c >= 'A' && c <= 'Z';
}

static bool IsCentralEuropeanCodepoint(uint16_t cp)
{
    switch (cp)
    {
        case 0x0104: // Ą
        case 0x0105: // ą
        case 0x0106: // Ć
        case 0x0107: // ć
        case 0x010C: // Č
        case 0x010D: // č
        case 0x010E: // Ď
        case 0x010F: // ď
        case 0x011A: // Ě
        case 0x011B: // ě
        case 0x0141: // Ł
        case 0x0142: // ł
        case 0x0147: // Ň
        case 0x0148: // ň
        case 0x0158: // Ř
        case 0x0159: // ř
        case 0x015A: // Ś
        case 0x015B: // ś
        case 0x0160: // Š
        case 0x0161: // š
        case 0x0164: // Ť
        case 0x0165: // ť
        case 0x0179: // Ź
        case 0x017A: // ź
        case 0x017B: // Ż
        case 0x017C: // ż
        case 0x017D: // Ž
        case 0x017E: // ž
            return true;
        default:
            return false;
    }
}

static bool IsLikelyCentralEuropeanMojibakeInCp1252(uint16_t cp)
{
    switch (cp)
    {
        case 0x0000: // undefined in CP1252
        case 0x00B3: // ³
        case 0x00B9: // ¹
        case 0x00BC: // ¼
        case 0x00BE: // ¾
        case 0x00C8: // È
        case 0x00CC: // Ì
        case 0x00CF: // Ï
        case 0x00D8: // Ø
        case 0x00DE: // Þ
        case 0x00E8: // è
        case 0x00EC: // ì
        case 0x00EF: // ï
        case 0x00F8: // ø
        case 0x00FE: // þ
            return true;
        default:
            return false;
    }
}

static int CentralEuropeanRawEvidence(const std::string& input)
{
    int evidence = 0;
    for (size_t i = 0; i < input.size(); ++i)
    {
        const unsigned char b = static_cast<unsigned char>(input[i]);
        if (b < 0x80)
        {
            continue;
        }

        const uint16_t cp1250 = kCp1250[b - 0x80];
        const uint16_t cp1252 = kCp1252[b - 0x80];
        if (!IsCentralEuropeanCodepoint(cp1250) || cp1250 == cp1252 || !IsLikelyCentralEuropeanMojibakeInCp1252(cp1252))
        {
            continue;
        }

        const bool leftAlpha = i > 0 && IsAsciiAlpha(static_cast<unsigned char>(input[i - 1]));
        const bool rightAlpha = i + 1 < input.size() && IsAsciiAlpha(static_cast<unsigned char>(input[i + 1]));
        const bool leftUpper = i > 0 && IsAsciiUpper(static_cast<unsigned char>(input[i - 1]));
        const bool rightUpper = i + 1 < input.size() && IsAsciiUpper(static_cast<unsigned char>(input[i + 1]));

        if (cp1252 == 0)
        {
            evidence += 3;
        }
        else if (leftUpper || rightUpper)
        {
            evidence += 2;
        }
        else if (leftAlpha || rightAlpha)
        {
            evidence += 1;
        }
    }
    return evidence;
}

static bool ContainsStrongCentralEuropeanWord(const std::string& text)
{
    static constexpr const char* kStrongLetters[] = {
        "\xC4\x8C", // Č
        "\xC4\x8D", // č
        "\xC4\x9A", // Ě
        "\xC4\x9B", // ě
        "\xC5\x98", // Ř
        "\xC5\x99", // ř
        "\xC5\xA0", // Š
        "\xC5\xA1", // š
        "\xC5\xBD", // Ž
        "\xC5\xBE", // ž
        "\xC5\xA4", // Ť
        "\xC5\xA5", // ť
        "\xC4\x8E", // Ď
        "\xC4\x8F", // ď
        "\xC5\x87", // Ň
        "\xC5\x88", // ň
        "\xC5\x81", // Ł
        "\xC5\x82", // ł
        "\xC4\x84", // Ą
        "\xC4\x85", // ą
        "\xC4\x98", // Ę
        "\xC4\x99", // ę
        "\xC5\x9A", // Ś
        "\xC5\x9B", // ś
        "\xC5\xB9", // Ź
        "\xC5\xBA", // ź
        "\xC5\xBB", // Ż
        "\xC5\xBC", // ż
        "\xC4\x86", // Ć
        "\xC4\x87", // ć
    };

    for (const char* letter : kStrongLetters)
    {
        size_t pos = text.find(letter);
        while (pos != std::string::npos)
        {
            const bool leftAscii = pos > 0 && IsAsciiAlpha(static_cast<unsigned char>(text[pos - 1]));
            const size_t after = pos + std::strlen(letter);
            const bool rightAscii = after < text.size() && IsAsciiAlpha(static_cast<unsigned char>(text[after]));
            if (leftAscii || rightAscii)
            {
                return true;
            }
            pos = text.find(letter, pos + 1);
        }
    }
    return false;
}

static int CountUtf8CodepointsInRange(const std::string& text, uint32_t first, uint32_t last)
{
    int count = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    const auto* end = p + text.size();
    while (p < end)
    {
        uint32_t cp = 0;
        if (*p < 0x80)
        {
            cp = *p++;
        }
        else if ((*p & 0xE0) == 0xC0 && p + 1 < end)
        {
            cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0 && p + 2 < end)
        {
            cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        }
        else
        {
            ++p;
            continue;
        }

        if (cp >= first && cp <= last)
        {
            ++count;
        }
    }
    return count;
}

static int CountAsciiLetters(const std::string& text)
{
    int count = 0;
    for (unsigned char c : text)
    {
        if (IsAsciiAlpha(c))
        {
            ++count;
        }
    }
    return count;
}

static int CountAsciiLettersOutsideMarkup(const std::string& text)
{
    int count = 0;
    bool inTag = false;
    for (unsigned char c : text)
    {
        if (c == '<')
        {
            inTag = true;
            continue;
        }
        if (c == '>')
        {
            inTag = false;
            continue;
        }
        if (!inTag && IsAsciiAlpha(c))
        {
            ++count;
        }
    }
    return count;
}

static bool IsCyrillicDominant(const std::string& text)
{
    const int cyrillic = CountUtf8CodepointsInRange(text, 0x0400, 0x04FF);
    if (cyrillic < 2)
    {
        return false;
    }
    return cyrillic > CountAsciiLettersOutsideMarkup(text);
}

static bool ContainsCyrillicMojibake(const std::string& text)
{
    int suspicious = 0;
    static constexpr const char* kChars[] = {
        "\xC3\x80", // À
        "\xC3\x82", // Â
        "\xC3\x85", // Å
        "\xC3\x88", // È
        "\xC3\x8F", // Ï
        "\xC3\xB0", // ð
        "\xC3\xA8", // è
        "\xC3\xA2", // â
        "\xC3\xA5", // å
        "\xC3\xB2", // ò
    };
    for (const char* ch : kChars)
    {
        if (ContainsUtf8(text, ch))
        {
            ++suspicious;
        }
    }
    return suspicious >= 2;
}

static int BaseDecodeScore(const std::string& decoded, Codepage cp, Codepage preferredCp)
{
    int score = cp == preferredCp ? 0 : 25;
    if (HasUtf8Replacement(decoded))
    {
        score += 1000;
    }
    const int cyrillic = CountUtf8CodepointsInRange(decoded, 0x0400, 0x04FF);
    const int latin = CountAsciiLetters(decoded);
    if (cyrillic > 0 && latin >= cyrillic)
    {
        score += 120; // Western text accidentally decoded as CP1251 becomes mixed-script noise.
    }
    return score;
}

static inline void AppendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string TranscodeToUtf8(const std::string& input, Codepage cp)
{
    if (cp == Codepage::Utf8)
    {
        return input;
    }

    const uint16_t* table = nullptr;
    switch (cp)
    {
        case Codepage::CP1252:
            table = kCp1252;
            break;
        case Codepage::CP1250:
            table = kCp1250;
            break;
        case Codepage::CP1251:
            table = kCp1251;
            break;
        default:
            table = kCp1252;
            break;
    }

    std::string out;
    out.reserve(input.size() + input.size() / 4);
    for (unsigned char b : input)
    {
        if (b < 0x80)
        {
            out.push_back(static_cast<char>(b));
        }
        else
        {
            uint16_t u = table[b - 0x80];
            if (u == 0)
            {
                u = 0xFFFD; // undefined slot in codepage
            }
            AppendUtf8(out, u);
        }
    }
    return out;
}

Codepage SelectLegacyTextCodepage(const std::string& input, Codepage preferredCp)
{
    if (input.empty() || preferredCp == Codepage::Utf8)
    {
        return Codepage::Utf8;
    }
    if (IsValidUtf8(input))
    {
        if (preferredCp == Codepage::CP1250 && CountUtf8CodepointsInRange(input, 0x0600, 0x06FF) > 0)
        {
            return Codepage::CP1250;
        }
        return Codepage::Utf8;
    }

    struct Candidate
    {
        Codepage cp;
        std::string decoded;
        int score;
    };

    std::array<Candidate, 3> candidates = {{
        {Codepage::CP1252, TranscodeToUtf8(input, Codepage::CP1252), 0},
        {Codepage::CP1250, TranscodeToUtf8(input, Codepage::CP1250), 0},
        {Codepage::CP1251, TranscodeToUtf8(input, Codepage::CP1251), 0},
    }};

    const int centralEuropeanEvidence = CentralEuropeanRawEvidence(input);
    const std::string& cp1252 = candidates[0].decoded;
    for (Candidate& candidate : candidates)
    {
        candidate.score = BaseDecodeScore(candidate.decoded, candidate.cp, preferredCp);
        if (candidate.cp == Codepage::CP1250)
        {
            if (ContainsStrongCentralEuropeanWord(candidate.decoded) &&
                (ContainsCp1252UndefinedByte(input) || centralEuropeanEvidence >= 2))
            {
                candidate.score -= 180;
            }
        }
        else if (candidate.cp == Codepage::CP1251)
        {
            if (IsCyrillicDominant(candidate.decoded) && ContainsCyrillicMojibake(cp1252))
            {
                candidate.score -= 220;
            }
        }
    }

    const Candidate* best = nullptr;
    for (const Candidate& candidate : candidates)
    {
        if (!best || candidate.score < best->score)
        {
            best = &candidate;
        }
    }
    return best ? best->cp : preferredCp;
}

std::string DecodeLegacyTextToUtf8(const std::string& input, Codepage preferredCp)
{
    const Codepage selected = SelectLegacyTextCodepage(input, preferredCp);
    if (selected == Codepage::Utf8)
    {
        return input;
    }
    return TranscodeToUtf8(input, selected);
}

RString DecodeLegacyTextToRString(RString input, Codepage preferredCp)
{
    const std::string utf8 = DecodeLegacyTextToUtf8(input.Data(), preferredCp);
    return RString(utf8.c_str());
}

RString DecodeLegacyTextToRString(RString input, const char* languageName)
{
    return DecodeLegacyTextToRString(input, CodepageForLanguage(languageName));
}

} // namespace Poseidon
