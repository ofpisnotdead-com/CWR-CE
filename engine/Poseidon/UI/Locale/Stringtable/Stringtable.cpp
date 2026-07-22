#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Asset/Formats/Common/CsvReader.hpp>
#include <Poseidon/IO/Filesystem/DirScanner.hpp>
#include <cctype>
#include <string>
#include <utility>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/HashMap.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{
using Poseidon::DirScanner;
using Poseidon::QIFStreamB;
} // namespace Poseidon
#include <algorithm>
#include <ctype.h>
#include <cstring>
#include <map>
#include <set>
#include <vector>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
namespace Poseidon
{

// Gate for Localize(int): true once a higher layer (Poseidon's
// StringtableSystem) has called the per-mod loader at least once.
// Apps that skip stringtable bring-up leave this false, and
// Localize(int) short-circuits to "" without log noise.
bool g_stringtableSystemAvailable = false;

// Bumped on every active-language switch and on stringtable load/clear so that
// LocalizedString caches can detect staleness. Starts at 1 so a zero-initialised
// cache always reads as stale on first access. See GetLanguageGeneration().
static uint32_t GLanguageGeneration = 1;

uint32_t GetLanguageGeneration()
{
    return GLanguageGeneration;
}

struct StringTableItem
{
    RString name;
    RString value;
    bool fromUtf8 = false;

    StringTableItem() = default;
    StringTableItem(RString n, RString v, bool utf8)
    {
        name = n;
        value = v;
        fromUtf8 = utf8;
    }
    const char* GetKey() const { return name; }
};

typedef MapStringToClass<StringTableItem, AutoArray<StringTableItem>> StringTableMap;

class StringTableDynamic : public StringTableMap
{
    typedef StringTableMap base;

  public:
    void Init();
    void Add(RString name, RString value, bool fromUtf8, std::set<std::string>& appliedInBatch);
    void Load(const char* filename, std::set<std::string>& appliedInBatch);
};

void StringTableDynamic::Init()
{
    base::Clear();
}

void StringTableDynamic::Add(RString name, RString value, bool fromUtf8, std::set<std::string>& appliedInBatch)
{
    const StringTableItem& check = Get(name);
    if (!IsNull(check))
    {
        // Modern UTF-8 shards are canonical once seen. Later legacy CSV loads
        // can happen through addon/package scans and must not resurrect stale
        // mojibake rows over already-normalized text. Within one resolved
        // base+shards batch, later files still follow deterministic load order.
        const std::string key = static_cast<const char*>(name);
        if (check.fromUtf8 && !fromUtf8 && appliedInBatch.find(key) == appliedInBatch.end())
        {
            return;
        }

        // A later UTF-8 file, or a legacy file replacing another legacy row,
        // still follows deterministic load order.
        Remove(name);
    }

    base::Add(StringTableItem(name, value, fromUtf8));
    appliedInBatch.insert(static_cast<const char*>(name));
}

static bool HasUtf8CsvSuffix(const char* filename)
{
    if (!filename)
    {
        return false;
    }
    size_t len = strlen(filename);
    const char* suffix = ".utf8.csv";
    size_t slen = strlen(suffix);
    if (len < slen)
    {
        return false;
    }
    return stricmp(filename + len - slen, suffix) == 0;
}

static bool HasCsvSuffix(const char* filename)
{
    if (!filename)
    {
        return false;
    }
    size_t len = strlen(filename);
    const char* suffix = ".csv";
    size_t slen = strlen(suffix);
    if (len < slen)
    {
        return false;
    }
    return stricmp(filename + len - slen, suffix) == 0;
}

static std::string LowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string StripCsvSuffix(const std::string& filename)
{
    if (HasUtf8CsvSuffix(filename.c_str()))
    {
        return filename.substr(0, filename.size() - strlen(".utf8.csv"));
    }
    if (HasCsvSuffix(filename.c_str()))
    {
        return filename.substr(0, filename.size() - strlen(".csv"));
    }
    return filename;
}

static std::string ResolvePreferredStringtableVariant(const std::string& filename)
{
    if (filename.empty())
    {
        return filename;
    }
    if (HasUtf8CsvSuffix(filename.c_str()))
    {
        return filename;
    }
    if (HasCsvSuffix(filename.c_str()))
    {
        std::string candidate = filename.substr(0, filename.size() - strlen(".csv"));
        candidate += ".utf8.csv";
        if (QIFStreamB::FileExist(candidate.c_str()))
        {
            return candidate;
        }
    }
    return filename;
}

static std::vector<std::string> ResolveStringtableLoadList(const char* filename)
{
    std::vector<std::string> files;
    if (!filename || !*filename)
    {
        return files;
    }

    const std::string requested = filename;
    const std::string baseFile = ResolvePreferredStringtableVariant(requested);
    if (QIFStreamB::FileExist(baseFile.c_str()))
    {
        files.push_back(baseFile);
    }

    size_t slashPos = requested.find_last_of("/\\");
    const std::string dir = slashPos == std::string::npos ? "." : requested.substr(0, slashPos);
    const std::string baseName = slashPos == std::string::npos ? requested : requested.substr(slashPos + 1);
    const std::string baseStem = StripCsvSuffix(baseName);
    if (baseStem == baseName)
    {
        return files;
    }

    const std::string baseStemLower = LowerAscii(baseStem);
    const std::string shardPrefix = baseStemLower + "_";
    std::map<std::string, std::string> shardFiles;

    DirScanner scan;
    if (!scan.First(dir.c_str(), nullptr))
    {
        return files;
    }

    do
    {
        const std::string leaf = scan.GetName();
        const std::string leafLower = LowerAscii(leaf);
        if (leafLower.rfind(shardPrefix, 0) != 0 || !HasCsvSuffix(leafLower.c_str()))
        {
            continue;
        }

        const std::string shardStem = StripCsvSuffix(leaf);
        const std::string shardStemLower = LowerAscii(shardStem);
        if (shardStemLower == baseStemLower)
        {
            continue;
        }

        const std::string candidate = dir == "." ? leaf : dir + "/" + leaf;
        const std::string resolved = ResolvePreferredStringtableVariant(candidate);
        if (!QIFStreamB::FileExist(resolved.c_str()))
        {
            continue;
        }

        auto it = shardFiles.find(shardStemLower);
        if (it == shardFiles.end())
        {
            shardFiles.emplace(shardStemLower, resolved);
            continue;
        }

        const bool newIsUtf8 = HasUtf8CsvSuffix(resolved.c_str());
        const bool oldIsUtf8 = HasUtf8CsvSuffix(it->second.c_str());
        if ((newIsUtf8 && !oldIsUtf8) || (newIsUtf8 == oldIsUtf8 && stricmp(resolved.c_str(), it->second.c_str()) < 0))
        {
            it->second = resolved;
        }
    } while (scan.Next());

    for (const auto& entry : shardFiles)
    {
        files.push_back(entry.second);
    }

    return files;
}

static void MergeSurplusLegacyCsvCells(std::vector<std::string>& row, int expectedColumns, int mergeColumn)
{
    if (expectedColumns <= 0 || mergeColumn < 0 || mergeColumn >= expectedColumns ||
        static_cast<int>(row.size()) <= expectedColumns)
    {
        return;
    }

    const int cellsToMerge = static_cast<int>(row.size()) - expectedColumns + 1;
    if (mergeColumn + cellsToMerge > static_cast<int>(row.size()))
    {
        return;
    }

    std::string merged = row[mergeColumn];
    for (int i = 1; i < cellsToMerge; i++)
    {
        merged += ",";
        merged += row[mergeColumn + i];
    }

    row[mergeColumn] = std::move(merged);
    row.erase(row.begin() + mergeColumn + 1, row.begin() + mergeColumn + cellsToMerge);
}

void StringTableDynamic::Load(const char* filename, std::set<std::string>& appliedInBatch)
{
    QIFStreamB f;
    f.AutoOpen(filename);
    bool fileIsUtf8 = HasUtf8CsvSuffix(filename);
    int column = -1;
    int headerColumns = 0;
    int legacyCommaColumn = -1;
    Poseidon::Codepage columnCp = Poseidon::Codepage::CP1252;
    std::set<std::string> seenNames;
    while (true)
    {
        if (f.eof())
        {
            return;
        }

        std::vector<std::string> row;
        if (!Poseidon::Asset::Formats::CsvReadRow(f, row))
        {
            return;
        }

        if (!row.empty() && stricmp(row[0].c_str(), "LANGUAGE") == 0)
        {
            headerColumns = static_cast<int>(row.size());
            for (int c = 1; c < static_cast<int>(row.size()); c++)
            {
                if (!fileIsUtf8 && legacyCommaColumn < 0 && stricmp(row[c].c_str(), "French") == 0)
                {
                    legacyCommaColumn = c;
                }
                if (stricmp(row[c].c_str(), (const char*)GLanguage) == 0)
                {
                    column = c - 1; // column index into value columns (0-based after key)
                    columnCp = fileIsUtf8 ? Poseidon::Codepage::Utf8 : Poseidon::CodepageForLanguage(row[c].c_str());
                    break;
                }
            }
            break;
        }
    }
    if (column < 0)
    {
        RptF("Unsupported language %s in %s", (const char*)GLanguage, filename);
        column = 0;
        columnCp = fileIsUtf8 ? Poseidon::Codepage::Utf8 : Poseidon::Codepage::CP1252;
    }

    while (!f.eof())
    {
        std::vector<std::string> row;
        if (!Poseidon::Asset::Formats::CsvReadRow(f, row) || row.empty())
        {
            continue;
        }
        if (!fileIsUtf8)
        {
            MergeSurplusLegacyCsvCells(row, headerColumns, legacyCommaColumn);
        }

        const std::string& name = row[0];
        if (name.empty() || stricmp(name.c_str(), "COMMENT") == 0)
        {
            continue;
        }
        const std::string nameLower = LowerAscii(name);
        if (seenNames.find(nameLower) != seenNames.end())
        {
            RptF("Item %s listed twice in %s", name.c_str(), filename);
            continue;
        }
        seenNames.insert(nameLower);

        // column is 0-based index into value columns (row[1], row[2], ...)
        int valueIdx = column + 1;
        if (valueIdx < static_cast<int>(row.size()) && !row[valueIdx].empty())
        {
            std::string value = Poseidon::DecodeLegacyTextToUtf8(row[valueIdx], columnCp);
            Add(RString(name.c_str()), RString(value.c_str()), fileIsUtf8, appliedInBatch);
        }
    }
}

// Entry remembered for runtime reload on language switch.
// Stores the ORIGINAL path the caller passed; the .utf8.csv preference
// re-runs inside Load() so the same resolution happens on replay.
struct LoadedEntry
{
    RString type;
    RString filename;
};

class StringTable
{
  protected:
    StringTableDynamic _tableGlobal;
    StringTableDynamic _tableCampaign;
    StringTableDynamic _tableMission;
    AutoArray<StringTableItem> _registered;
    AutoArray<LoadedEntry> _loaded;

  public:
    StringTable() = default;
    void Clear();

    void Load(RString type, RString filename, float priority, bool init);

    int Register(RString name);

    RString Localize(int ids);
    RString Localize(const char* str);

    bool SetLanguage(RString newLang);

  private:
    // Refresh every _registered[i].value by re-querying _tableGlobal by name.
    // Called by SetLanguage after tables are reloaded.
    void RelocalizeRegistered();
    // Reload every tracked entry with init=false (tables already cleared).
    void ReloadAll();
};

void StringTable::Clear()
{
    _tableGlobal.Clear();
    _tableCampaign.Clear();
    _tableMission.Clear();
    _registered.Clear();
    _loaded.Clear();
}

void StringTable::Load(RString type, RString filename, float priority, bool init)
{
    StringTableDynamic* table = nullptr;
    if (stricmp(type, "global") == 0)
    {
        table = &_tableGlobal;
    }
    else if (stricmp(type, "campaign") == 0)
    {
        table = &_tableCampaign;
    }
    else if (stricmp(type, "mission") == 0)
    {
        table = &_tableMission;
    }
    else
    {
        RptF("Unknown stringtable type %s", (const char*)type);
        return;
    }

    if (init)
    {
        table->Init();
    }

    const std::vector<std::string> files = ResolveStringtableLoadList(filename);
    std::set<std::string> appliedInBatch;
    for (const std::string& file : files)
    {
        table->Load(file.c_str(), appliedInBatch);
    }

    // Remember for future language switches. Re-run the same base-path scan so
    // sibling STRINGTABLE_*.csv / *.utf8.csv shards resolve again in the same order.
    bool already = false;
    for (int i = 0; i < _loaded.Size(); i++)
    {
        if (stricmp(_loaded[i].type, type) == 0 && stricmp(_loaded[i].filename, filename) == 0)
        {
            already = true;
            break;
        }
    }
    if (!already)
    {
        LoadedEntry entry;
        entry.type = type;
        entry.filename = filename;
        _loaded.Add(entry);
    }
}

int StringTable::Register(RString name)
{
    // only strings from global stringtable can be registered
    const StringTableItem& item = _tableGlobal[name];
    if (_tableGlobal.IsNull(item))
    {
        LOG_WARN(Core, "[i18n] key '{}' not found in stringtable; callers will get empty string", (const char*)name);
        return -1;
    }
    return _registered.Add(item);
}

RString StringTable::Localize(int ids)
{
    if (ids < 0 || ids >= _registered.Size())
    {
        // Apps that don't bring up StringtableSystem leave the table
        // empty by design — return empty silently in that case.
        extern bool g_stringtableSystemAvailable;
        if (!g_stringtableSystemAvailable)
            return "";

        // Context: -1 usually means an uninitialized IDS_* global (STRING() entry
        // whose STR_* key was missing from stringtable.csv). Large positive values
        // often mean an integer from another namespace (IDD, IDC, engine resource
        // ID) accidentally passed to LocalizeString(int).
        const char* hint = (ids < 0)
                               ? " [likely uninitialized IDS_* — missing STR_* in stringtable.csv]"
                               : " [likely non-IDS integer passed to LocalizeString(int) — check IDD/IDC confusion]";

        LOG_ERROR(Core, "String id {} is not registered (registered count = {}){}", ids, _registered.Size(), hint);
#if _ENABLE_CHEATS
        return "!!! UNREGISTERED STRING";
#else
        return "";
#endif
    }
    return _registered[ids].value;
}

void StringTable::RelocalizeRegistered()
{
    for (int i = 0; i < _registered.Size(); i++)
    {
        const StringTableItem& fresh = _tableGlobal[_registered[i].name];
        if (!_tableGlobal.IsNull(fresh))
        {
            _registered[i].value = fresh.value;
        }
        else
        {
            RptF("[i18n] key '%s' missing after language switch; keeping prior value",
                 (const char*)_registered[i].name);
        }
    }
}

void StringTable::ReloadAll()
{
    // Snapshot — Load() may append to _loaded and reallocate, invalidating references.
    AutoArray<LoadedEntry> snapshot;
    snapshot.Resize(_loaded.Size());
    for (int i = 0; i < _loaded.Size(); i++)
    {
        snapshot[i] = _loaded[i];
    }
    for (int i = 0; i < snapshot.Size(); i++)
    {
        // init=false: tables already cleared by SetLanguage. Priority unused today.
        Load(snapshot[i].type, snapshot[i].filename, 0.0f, false);
    }
}

bool StringTable::SetLanguage(RString newLang)
{
    if (newLang.GetLength() == 0)
    {
        LOG_ERROR(Core, "[i18n] SetLanguage called with empty language");
        return false;
    }
    if (stricmp(newLang, GLanguage) == 0)
    {
        return true; // no-op
    }

    GLanguage = newLang;

    // Wipe all tables but preserve the tracked-files list for replay.
    _tableGlobal.Init();
    _tableCampaign.Init();
    _tableMission.Init();

    // Replay every tracked Load. Load() dedupes against _loaded so entries don't multiply.
    ReloadAll();

    RelocalizeRegistered();

    LOG_INFO(Core, "[i18n] language switched to {}", (const char*)newLang);
    return true;
}

RString StringTable::Localize(const char* str)
{
    const StringTableItem& item1 = _tableMission[str];
    if (!_tableMission.IsNull(item1))
    {
        return item1.value;
    }

    const StringTableItem& item2 = _tableCampaign[str];
    if (!_tableCampaign.IsNull(item2))
    {
        return item2.value;
    }

    const StringTableItem& item3 = _tableGlobal[str];
    if (!_tableGlobal.IsNull(item3))
    {
        return item3.value;
    }

    RptF("String %s not found", (const char*)str);
#if _ENABLE_CHEATS
    return "!!! MISSING STRING";
#else
    return "";
#endif
}

// Meyer's singleton accessors - no global constructors!
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"

RString& GetLanguage()
{
    static RString instance;
    return instance;
}

inline StringTable& GetStringTable()
{
    static StringTable instance;
    return instance;
}

#pragma clang diagnostic pop

// API compatibility macro (GLanguage comes from Stringtable.hpp)
#define GStringTable GetStringTable()

void LoadStringtable(RString type, RString filename, float priority, bool init)
{
    GStringTable.Load(type, filename, priority, init);
    ++GLanguageGeneration;
}

int RegisterString(RString name)
{
    return GStringTable.Register(name);
}

RString LocalizeString(int ids)
{
    return GStringTable.Localize(ids);
}

RString LocalizeString(const char* str)
{
    return GStringTable.Localize(str);
}

const char* LocalizeStringWithFallback(const char* key, const char* fallback)
{
    RString s = LocalizeString(key);
    return s.GetLength() > 0 ? s.Data() : fallback;
}

RString Localize(RString str)
{
    // BIS RV2 convention: "@KEY" — strip the @, look up KEY.
    if (str[0] == '@')
    {
        return LocalizeString((const char*)str + 1);
    }
    // OFP/CWA convention: "$STR_…" / "$STRM_…" / "$STRN_…" — strip the $, look up
    // the rest. ParamRawValue::GetValue() does the same expansion at config-read
    // time for binary mission.sqm; this branch covers literals captured by the
    // text mission.sqm fast-search fallback in DisplaySingleMission and any
    // other site that hands a raw briefingName / onLoadMission / etc. through
    // Localize() instead of going via ParamFile.
    if (str[0] == '$' && strncmp((const char*)str + 1, "STR", 3) == 0)
    {
        return LocalizeString((const char*)str + 1);
    }
    return str;
}

RString LookupStringtableCsv(RString csvPath, const char* key)
{
    if (csvPath.GetLength() == 0 || !key || !*key)
    {
        return RString();
    }
    // Match LoadStringtable's preference: <stem>.utf8.csv wins when present.
    RString resolved = csvPath;
    if (HasCsvSuffix(csvPath) && !HasUtf8CsvSuffix(csvPath))
    {
        const int n = csvPath.GetLength() - static_cast<int>(strlen(".csv"));
        std::string candidate(static_cast<const char*>(csvPath), n);
        candidate += ".utf8.csv";
        if (QIFStreamB::FileExist(candidate.c_str()))
        {
            resolved = RString(candidate.c_str());
        }
    }
    if (!QIFStreamB::FileExist(resolved))
    {
        return RString();
    }
    QIFStreamB f;
    f.AutoOpen(resolved);
    const bool fileIsUtf8 = HasUtf8CsvSuffix(resolved);
    int column = -1;
    int englishColumn = -1;
    int headerColumns = 0;
    int legacyCommaColumn = -1;
    Poseidon::Codepage columnCp = Poseidon::Codepage::CP1252;
    Poseidon::Codepage englishCp = Poseidon::Codepage::CP1252;
    while (true)
    {
        if (f.eof())
        {
            return RString();
        }
        std::vector<std::string> row;
        if (!Poseidon::Asset::Formats::CsvReadRow(f, row))
        {
            return RString();
        }
        if (!row.empty() && stricmp(row[0].c_str(), "LANGUAGE") == 0)
        {
            headerColumns = static_cast<int>(row.size());
            for (int c = 1; c < static_cast<int>(row.size()); c++)
            {
                if (!fileIsUtf8 && legacyCommaColumn < 0 && stricmp(row[c].c_str(), "French") == 0)
                {
                    legacyCommaColumn = c;
                }
                if (column < 0 && stricmp(row[c].c_str(), (const char*)GLanguage) == 0)
                {
                    column = c - 1;
                    columnCp = fileIsUtf8 ? Poseidon::Codepage::Utf8 : Poseidon::CodepageForLanguage(row[c].c_str());
                }
                if (englishColumn < 0 && stricmp(row[c].c_str(), "English") == 0)
                {
                    englishColumn = c - 1;
                    englishCp = fileIsUtf8 ? Poseidon::Codepage::Utf8 : Poseidon::CodepageForLanguage(row[c].c_str());
                }
            }
            break;
        }
    }
    if (column < 0 && englishColumn < 0)
    {
        return RString();
    }
    while (!f.eof())
    {
        std::vector<std::string> row;
        if (!Poseidon::Asset::Formats::CsvReadRow(f, row) || row.empty())
        {
            continue;
        }
        if (!fileIsUtf8)
        {
            MergeSurplusLegacyCsvCells(row, headerColumns, legacyCommaColumn);
        }
        if (stricmp(row[0].c_str(), key) != 0)
        {
            continue;
        }
        // Prefer the requested language column; fall back to English when the
        // requested column is missing or its cell is empty. Showing English is
        // friendlier than a blank row or the raw "$STR_…" token for keys that
        // a translator hasn't filled in yet.
        if (column >= 0)
        {
            const int valueIdx = column + 1;
            if (valueIdx < static_cast<int>(row.size()) && !row[valueIdx].empty())
            {
                std::string value = Poseidon::DecodeLegacyTextToUtf8(row[valueIdx], columnCp);
                return RString(value.c_str());
            }
        }
        if (englishColumn >= 0 && englishColumn != column)
        {
            const int valueIdx = englishColumn + 1;
            if (valueIdx < static_cast<int>(row.size()) && !row[valueIdx].empty())
            {
                std::string value = Poseidon::DecodeLegacyTextToUtf8(row[valueIdx], englishCp);
                return RString(value.c_str());
            }
        }
        return RString();
    }
    return RString();
}

void ClearStringtable()
{
    GStringTable.Clear();
    ++GLanguageGeneration;
}

// Language-change callback registry

struct LangCallbackEntry
{
    int token;
    LanguageChangedCallback cb;
};

// Leaked on exit by design: UI dialogs (e.g. DisplayMain) unregister from their
// destructors, which may run during static destruction. If this vector had
// already been destroyed, the unregister would crash. Leaking a few bytes on
// shutdown is the price for crash-free teardown.
static std::vector<LangCallbackEntry>& GetLangCallbacks()
{
    static std::vector<LangCallbackEntry>* callbacks = new std::vector<LangCallbackEntry>();
    return *callbacks;
}

static int& NextLangToken()
{
    static int token = 0;
    return token;
}

int RegisterLanguageChangedCallback(LanguageChangedCallback cb)
{
    int t = ++NextLangToken();
    GetLangCallbacks().push_back({t, std::move(cb)});
    return t;
}

void UnregisterLanguageChangedCallback(int token)
{
    auto& cbs = GetLangCallbacks();
    for (auto it = cbs.begin(); it != cbs.end(); ++it)
    {
        if (it->token == token)
        {
            cbs.erase(it);
            return;
        }
    }
}

bool SetLanguage(RString newLang)
{
    // Decide if callbacks should fire before we touch state — StringTable::SetLanguage
    // returns true for both same-language no-ops and real switches, but callbacks
    // should only fire on a real change.
    const bool reallyChanged = newLang.GetLength() > 0 && stricmp(newLang, GLanguage) != 0;

    bool ok = GStringTable.SetLanguage(newLang);
    if (!ok || !reallyChanged)
    {
        return ok;
    }
    ++GLanguageGeneration;
    // Iterate a copy so a callback can safely register/unregister without
    // invalidating the iteration.
    auto snapshot = GetLangCallbacks();
    for (const auto& entry : snapshot)
    {
        if (entry.cb)
        {
            entry.cb();
        }
    }
    return true;
}

} // namespace Poseidon
