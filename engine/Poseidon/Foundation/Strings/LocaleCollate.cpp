#include <Poseidon/Foundation/Strings/LocaleCollate.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>

#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#else
// newlocale/locale_t/LC_*_MASK come from <locale.h>; strcoll_l from <string.h>
// (POSIX.1-2008). glibc declares them under _GNU_SOURCE, which this build defines.
#include <locale.h>
#include <string.h>
#endif

namespace Poseidon
{
namespace Foundation
{
namespace
{
// Diacritic-fold tables: one ASCII base letter per code point, '.' where the code
// point has no ASCII base (kept sorted by its own value). Latin-1 Supplement covers
// U+00C0..U+00FF, Latin Extended-A covers U+0100..U+017F. Together they cover every
// Czech accented letter plus the common Western European ones.
constexpr char kLatin1Fold[] = "aaaaaaaceeeeiiii"  // C0-CF  À..Ï
                               "dnooooo.ouuuuy.s"  // D0-DF  Ð..ß  (× and Þ kept)
                               "aaaaaaaceeeeiiii"  // E0-EF  à..ï
                               "dnooooo.ouuuuy.y"; // F0-FF  ð..ÿ  (÷ and þ kept)
static_assert(sizeof(kLatin1Fold) == 64 + 1, "Latin-1 Supplement fold must cover 64 code points");

constexpr char kLatinAFold[] = "aaaaaaccccccccdd"  // 100-10F  Ā..ď
                               "ddeeeeeeeeeegggg"  // 110-11F  Đ..ğ
                               "gggghhhhiiiiiiii"  // 120-12F  Ġ..į
                               "iiiijjkkklllllll"  // 130-13F  İ..Ŀ
                               "lllnnnnnnnnnoooo"  // 140-14F  ŀ..ŏ
                               "oooorrrrrrssssss"  // 150-15F  Ő..ş
                               "ssttttttuuuuuuuu"  // 160-16F  Š..ů
                               "uuuuwwyyyzzzzzzs"; // 170-17F  Ű..ſ
static_assert(sizeof(kLatinAFold) == 128 + 1, "Latin Extended-A fold must cover 128 code points");

// Primary sort key for one code point: the folded, lowercased base letter for the
// Latin ranges, otherwise the code point itself. NUL folds to 0, so it always sorts
// first and marks end-of-string in the comparison loops.
uint32_t FoldPrimary(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z')
        return cp - 'A' + 'a';
    if (cp < 0x80)
        return cp;
    if (cp >= 0xC0 && cp <= 0xFF)
    {
        char c = kLatin1Fold[cp - 0xC0];
        return c == '.' ? cp : static_cast<uint32_t>(c);
    }
    if (cp >= 0x100 && cp <= 0x17F)
        return static_cast<uint32_t>(kLatinAFold[cp - 0x100]);
    return cp;
}

// Break primary ties on the code points with ASCII case ignored, so the order is
// stable and case-insensitive while still separating accents (e.g. "café" after
// "cafe", "Š…" after "S…"). End-of-string is the NUL byte; DecodeUtf8Codepoint is
// never handed it (it reports U+FFFD / 0 bytes there, which would stall the loop).
int FoldSecondaryTieBreak(const char* a, const char* b)
{
    for (const char *pa = a, *pb = b;;)
    {
        if (!*pa || !*pb)
            return *pa ? 1 : (*pb ? -1 : 0);
        uint32_t ca = 0, cb = 0;
        int na = DecodeUtf8Codepoint(pa, &ca);
        int nb = DecodeUtf8Codepoint(pb, &cb);
        uint32_t ka = (ca >= 'A' && ca <= 'Z') ? ca + 32 : ca;
        uint32_t kb = (cb >= 'A' && cb <= 'Z') ? cb + 32 : cb;
        if (ka != kb)
            return ka < kb ? -1 : 1;
        pa += na > 0 ? na : 1;
        pb += nb > 0 ? nb : 1;
    }
}
} // namespace

int FoldCompareUtf8(const char* a, const char* b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";

    // Primary pass: compare folded base letters, then break exact ties on raw code
    // points. End-of-string is the NUL byte and is never decoded.
    for (const char *pa = a, *pb = b;;)
    {
        if (!*pa || !*pb)
        {
            if (*pa)
                return 1;
            if (*pb)
                return -1;
            return FoldSecondaryTieBreak(a, b);
        }
        uint32_t ca = 0, cb = 0;
        int na = DecodeUtf8Codepoint(pa, &ca);
        int nb = DecodeUtf8Codepoint(pb, &cb);
        uint32_t ka = FoldPrimary(ca), kb = FoldPrimary(cb);
        if (ka != kb)
            return ka < kb ? -1 : 1;
        pa += na > 0 ? na : 1; // never stall on a malformed byte
        pb += nb > 0 ? nb : 1;
    }
}

#ifdef _WIN32
namespace
{
std::wstring Utf8ToWide(const char* s)
{
    if (!s || !*s)
        return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n - 1), L'\0'); // n includes the NUL
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}
} // namespace

int CollateUtf8(const char* a, const char* b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    std::wstring wa = Utf8ToWide(a), wb = Utf8ToWide(b);
    int r = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | NORM_LINGUISTIC_CASING, wa.c_str(),
                            static_cast<int>(wa.size()), wb.c_str(), static_cast<int>(wb.size()), nullptr, nullptr, 0);
    if (r == 0) // conversion or comparison failed — use the deterministic fallback
        return FoldCompareUtf8(a, b);
    return r - CSTR_EQUAL; // LESS_THAN(1)->-1, EQUAL(2)->0, GREATER_THAN(3)->1
}
#else
namespace
{
locale_t CollateLocale()
{
    // Built once from the environment (LC_ALL/LC_COLLATE/LANG). LC_CTYPE is needed
    // so strcoll_l decodes the UTF-8 bytes per the locale's encoding.
    static locale_t loc = newlocale(LC_COLLATE_MASK | LC_CTYPE_MASK, "", static_cast<locale_t>(0));
    return loc;
}

bool OsCollationUsable()
{
    // Probe once: a real UTF-8 collation sorts "é" (U+00E9) before "z"; the C/POSIX
    // locale compares raw bytes, where 0xC3 (é's lead byte) sorts after 'z'.
    static bool usable = []
    {
        locale_t loc = CollateLocale();
        return loc != static_cast<locale_t>(0) && strcoll_l("\xC3\xA9", "z", loc) < 0;
    }();
    return usable;
}
} // namespace

int CollateUtf8(const char* a, const char* b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    if (!OsCollationUsable())
        return FoldCompareUtf8(a, b);
    int r = strcoll_l(a, b, CollateLocale());
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}
#endif
} // namespace Foundation
} // namespace Poseidon
