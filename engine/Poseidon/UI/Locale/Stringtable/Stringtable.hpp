#ifdef _MSC_VER
#pragma once
#endif

#ifndef _STRINGTABLE_HPP
#define _STRINGTABLE_HPP

#include <Poseidon/Foundation/Strings/RString.hpp>
#include <cstdint>
#include <functional>

// Meyer's singleton accessor (definition in .cpp)

namespace Poseidon
{
RString& GetLanguage();

// API compatibility macro
#define GLanguage ::Poseidon::GetLanguage()

void LoadStringtable(RString type, RString filename, float priority = 0, bool init = true);

int RegisterString(RString name);

RString LocalizeString(int ids);
RString LocalizeString(const char* str);

// Localized value for `key`, or `fallback` verbatim if the key is missing/empty.
const char* LocalizeStringWithFallback(const char* key, const char* fallback);

RString Localize(RString str);

// Open `csvPath` and return the value for `key` in the column matching the
// current GLanguage. Returns empty RString if the file is missing, the key is
// absent, or the language column is not present. Used by the Single Mission
// list scanner to resolve briefingName="$STR_…" / "@STR…" tokens against a
// per-mission stringtable.csv (or .utf8.csv) without polluting the global
// _tableMission. Cheap enough to call once per mission during the list build.
RString LookupStringtableCsv(RString csvPath, const char* key);

void ClearStringtable();

// Runtime language switch. Reloads every previously-loaded stringtable file with
// the new column selection and refreshes snapshots in _registered[] so existing
// IDS_* ids return values in the new language. Fires registered callbacks.
// Returns true if language actually changed (or was already the target).
bool SetLanguage(RString newLang);

// Monotonic counter bumped whenever the active language changes or a stringtable
// is loaded/cleared. Lazily-cached localized values (see LocalizedString) compare
// it against the generation their snapshot was resolved under to detect staleness
// without registering a per-value callback. Never zero, so a zero-initialised
// cache always reads as stale on first access.
uint32_t GetLanguageGeneration();

using LanguageChangedCallback = std::function<void()>;
// Returns a token used to unregister. Callbacks are invoked in registration order.
int RegisterLanguageChangedCallback(LanguageChangedCallback cb);
void UnregisterLanguageChangedCallback(int token);

#define DEFINE_STRING(name) extern int IDS_##name;
#define DECLARE_STRING(name) int IDS_##name;
#define REGISTER_STRING(name)         \
    int RegisterString(RString name); \
    IDS_##name = RegisterString("STR_" #name);

} // namespace Poseidon
#endif

