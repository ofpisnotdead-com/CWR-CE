#pragma once

namespace Poseidon
{
namespace Foundation
{
// Order two UTF-8 strings for display, case-insensitively. Uses the OS user-locale
// collation when the process has a usable one (Windows CompareStringEx, POSIX
// strcoll_l), so an accented letter sorts next to its base letter — e.g. "Č" groups
// with "C" instead of after "Z". Falls back to FoldCompareUtf8 when the process has
// no usable collation (a bare C/POSIX locale). Returns <0, 0, >0; null is treated
// as the empty string.
int CollateUtf8(const char* a, const char* b);

// Deterministic, locale-independent fallback used when the OS offers no collation.
// Compares by diacritic-folded base letter (Latin-1 Supplement + Latin Extended-A
// mapped to ASCII), case-insensitively, with the raw code points breaking exact
// ties so the order is stable. Exposed so it can be tested without a system locale.
int FoldCompareUtf8(const char* a, const char* b);
} // namespace Foundation
} // namespace Poseidon
