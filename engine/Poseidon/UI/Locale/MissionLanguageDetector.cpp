#include <Poseidon/UI/Locale/MissionLanguageDetector.hpp>
#include <Poseidon/UI/Locale/LanguageRegistry.hpp>

#include <Poseidon/Asset/Formats/Common/CsvReader.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <fmt/format.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::LSOK;
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/Audio/VoiceLangPath.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

using Poseidon::WithLangSuffix;

namespace Poseidon
{
namespace
{

RString EnsureTrailingSlash(RString root)
{
    if (root.GetLength() == 0)
        return root;

    const char tail = root[root.GetLength() - 1];
    if (tail == '\\' || tail == '/')
        return root;

    return root + RString("/");
}

bool HasLanguageHtml(RString root, const char* stem, const char* language)
{
    const char* suffixes[] = {".utf8.html", ".html"};
    for (const char* suffix : suffixes)
    {
        RString path = root + stem + RString(".") + language + suffix;
        if (QIFStreamB::FileExist(path))
            return true;
    }
    return false;
}

bool HasLanguageHtml(const QFBank& bank, const char* stem, const char* language)
{
    const char* suffixes[] = {".utf8.html", ".html"};
    char path[256];
    for (const char* suffix : suffixes)
    {
        snprintf(path, sizeof(path), "%s.%s%s", stem, language, suffix);
        if (bank.FileExists(path))
            return true;
    }
    return false;
}

bool HasGenericHtml(RString root, const char* stem)
{
    const char* suffixes[] = {".utf8.html", ".html"};
    for (const char* suffix : suffixes)
    {
        RString path = root + stem + suffix;
        if (QIFStreamB::FileExist(path))
            return true;
    }
    return false;
}

bool HasGenericHtml(const QFBank& bank, const char* stem)
{
    char path[256];
    const char* suffixes[] = {".utf8.html", ".html"};
    for (const char* suffix : suffixes)
    {
        snprintf(path, sizeof(path), "%s%s", stem, suffix);
        if (bank.FileExists(path))
            return true;
    }
    return false;
}

bool IsMissionRelativeAssetPath(RString relPath)
{
    const char* path = relPath;
    return relPath.GetLength() > 0 && relPath[0] != '\\' && relPath[0] != '/' && !strchr(path, '/') &&
           !strchr(path, '\\');
}

RString ResolveMissionVoicePath(RString root, RString relPath)
{
    RString directPath = root + relPath;
    if (QIFStreamB::FileExist(directPath))
        return directPath;

    for (int lang = 0; lang < CfgLib::LanguageRegistry::Instance().Count(); ++lang)
    {
        RString localized =
            WithLangSuffix(directPath, RString(CfgLib::LanguageRegistry::Instance().Languages()[lang].name.c_str()));
        if (localized.GetLength() > 0 && QIFStreamB::FileExist(localized))
            return directPath;
    }

    if (IsMissionRelativeAssetPath(relPath))
    {
        RString soundPath = root + RString("sound/") + relPath;
        if (QIFStreamB::FileExist(soundPath))
            return soundPath;

        for (int lang = 0; lang < CfgLib::LanguageRegistry::Instance().Count(); ++lang)
        {
            RString localized =
                WithLangSuffix(soundPath, RString(CfgLib::LanguageRegistry::Instance().Languages()[lang].name.c_str()));
            if (localized.GetLength() > 0 && QIFStreamB::FileExist(localized))
                return soundPath;
        }
    }

    return RString();
}

RString ResolveMissionVoicePath(const QFBank& bank, RString relPath)
{
    if (bank.FileExists(relPath))
        return relPath;

    for (int lang = 0; lang < CfgLib::LanguageRegistry::Instance().Count(); ++lang)
    {
        RString localized =
            WithLangSuffix(relPath, RString(CfgLib::LanguageRegistry::Instance().Languages()[lang].name.c_str()));
        if (localized.GetLength() > 0 && bank.FileExists(localized))
            return relPath;
    }

    if (IsMissionRelativeAssetPath(relPath))
    {
        RString soundPath = RString("sound/") + relPath;
        if (bank.FileExists(soundPath))
            return soundPath;

        for (int lang = 0; lang < CfgLib::LanguageRegistry::Instance().Count(); ++lang)
        {
            RString localized =
                WithLangSuffix(soundPath, RString(CfgLib::LanguageRegistry::Instance().Languages()[lang].name.c_str()));
            if (localized.GetLength() > 0 && bank.FileExists(localized))
                return soundPath;
        }
    }

    return RString();
}

RString GetMissionStringtablePath(RString root)
{
    RString utf8 = root + RString("stringtable.utf8.csv");
    if (QIFStreamB::FileExist(utf8))
        return utf8;

    RString legacy = root + RString("stringtable.csv");
    if (QIFStreamB::FileExist(legacy))
        return legacy;

    return RString();
}

const char* GetMissionStringtableName(const QFBank& bank)
{
    if (bank.FileExists("stringtable.utf8.csv"))
        return "stringtable.utf8.csv";
    if (bank.FileExists("stringtable.csv"))
        return "stringtable.csv";
    return nullptr;
}

std::optional<float> ClampViewDistance(float value)
{
    if (!std::isfinite(value))
        return std::nullopt;

    return Poseidon::ClampPreferredViewDistance(value);
}

std::optional<float> ReadMissionViewDistance(const ParamFile& sqm)
{
    const ParamEntry* mission = sqm.FindEntry("Mission");
    if (!mission)
        return std::nullopt;

    const ParamEntry* intel = mission->FindEntry("Intel");
    if (!intel)
        return std::nullopt;

    const ParamEntry* entry = intel->FindEntry("viewDistance");
    if (!entry)
        entry = intel->FindEntry("missionViewDistance");
    if (!entry)
        return std::nullopt;

    return ClampViewDistance((float)*entry);
}

std::optional<float> DetectMissionViewDistance(RString root)
{
    RString sqmPath = EnsureTrailingSlash(root) + RString("mission.sqm");
    if (!QIFStreamB::FileExist(sqmPath))
        return std::nullopt;

    // mission.sqm may be binary (raP signature) or plain text.  Plain
    // text-only Parse() runs the preprocessor; on a binary file the
    // raw bytes look like garbage `#directives` and the preprocessor
    // fires a fatal ErrorMessage that exits the game.
    // ParseBinOrTxt sniffs the format and routes to the right parser.
    ParamFile sqm;
    if (!sqm.ParseBinOrTxt(sqmPath))
        return std::nullopt;

    return ReadMissionViewDistance(sqm);
}

std::optional<float> DetectMissionViewDistance(const QFBank& bank)
{
    if (!bank.FileExists("mission.sqm"))
        return std::nullopt;

    ParamFile sqm;
    QFBank& mutableBank = const_cast<QFBank&>(bank);
    if (!sqm.ParseBin(mutableBank, "mission.sqm"))
    {
        QIFStreamB stream;
        stream.open(bank, "mission.sqm");
        sqm.Parse(stream);
    }
    return ReadMissionViewDistance(sqm);
}

void DetectStringtableLanguages(QIFStreamB& stream, MissionLanguageDetector::MissionLanguageInfo& info)
{
    std::vector<std::string> row;
    int columns[CfgLib::kMaxSupportedLanguages];
    memset(columns, -1, sizeof(columns));

    while (!stream.eof())
    {
        if (!Poseidon::Asset::Formats::CsvReadRow(stream, row) || row.empty())
            continue;
        if (stricmp(row[0].c_str(), "LANGUAGE") != 0)
            continue;

        for (int c = 1; c < static_cast<int>(row.size()); ++c)
        {
            for (int i = 0; i < CfgLib::LanguageRegistry::Instance().Count(); ++i)
            {
                if (columns[i] < 0 &&
                    stricmp(row[c].c_str(), CfgLib::LanguageRegistry::Instance().Languages()[i].name.c_str()) == 0)
                    columns[i] = c;
            }
        }
        break;
    }

    while (!stream.eof())
    {
        if (!Poseidon::Asset::Formats::CsvReadRow(stream, row) || row.empty())
            continue;

        for (int i = 0; i < CfgLib::LanguageRegistry::Instance().Count(); ++i)
        {
            if (info.flags[i].stringtable || columns[i] < 0)
                continue;
            if (columns[i] < static_cast<int>(row.size()) && !row[columns[i]].empty())
                info.flags[i].stringtable = true;
        }
    }
}

void DetectStringtableLanguages(RString root, MissionLanguageDetector::MissionLanguageInfo& info)
{
    RString csvPath = GetMissionStringtablePath(root);
    if (csvPath.GetLength() == 0)
        return;

    QIFStreamB stream;
    stream.AutoOpen(csvPath);
    DetectStringtableLanguages(stream, info);
}

void DetectStringtableLanguages(const QFBank& bank, MissionLanguageDetector::MissionLanguageInfo& info)
{
    const char* csvName = GetMissionStringtableName(bank);
    if (!csvName)
        return;

    QIFStreamB stream;
    stream.open(bank, csvName);
    DetectStringtableLanguages(stream, info);
}

void DetectHtmlLanguages(RString root, const char* stem, bool MissionLanguageDetector::MissionLanguageFlags::* member,
                         MissionLanguageDetector::MissionLanguageInfo& info)
{
    const bool hasGeneric = HasGenericHtml(root, stem);
    for (int i = 0; i < CfgLib::LanguageRegistry::Instance().Count(); ++i)
    {
        const bool hasSpecific =
            HasLanguageHtml(root, stem, CfgLib::LanguageRegistry::Instance().Languages()[i].name.c_str());
        info.flags[i].*member = hasSpecific || (hasGeneric && i == 0);
    }
}

void DetectHtmlLanguages(const QFBank& bank, const char* stem,
                         bool MissionLanguageDetector::MissionLanguageFlags::* member,
                         MissionLanguageDetector::MissionLanguageInfo& info)
{
    const bool hasGeneric = HasGenericHtml(bank, stem);
    for (int i = 0; i < CfgLib::LanguageRegistry::Instance().Count(); ++i)
    {
        const bool hasSpecific =
            HasLanguageHtml(bank, stem, CfgLib::LanguageRegistry::Instance().Languages()[i].name.c_str());
        info.flags[i].*member = hasSpecific || (hasGeneric && i == 0);
    }
}

void DetectVoiceLanguages(RString root, MissionLanguageDetector::MissionLanguageInfo& info)
{
    RString descriptionPath = root + RString("description.ext");
    if (!QIFStreamB::FileExist(descriptionPath))
        return;

    ParamFile desc;
    if (desc.Parse(descriptionPath) != LSOK)
        return;

    const ParamEntry* cfgSounds = desc.FindEntry("CfgSounds");
    if (!cfgSounds)
        return;

    bool hasSharedVoice = false;
    for (int i = 0; i < cfgSounds->GetEntryCount(); ++i)
    {
        const ParamEntry& soundClass = cfgSounds->GetEntry(i);
        const ParamEntry* sound = soundClass.FindEntry("sound");
        if (!sound || !sound->IsArray() || sound->GetSize() <= 0)
            continue;

        RString relPath = (*sound)[0].GetValue();
        if (relPath.GetLength() == 0)
            continue;

        RString basePath = ResolveMissionVoicePath(root, relPath);
        if (basePath.GetLength() == 0)
            continue;

        if (QIFStreamB::FileExist(basePath))
            hasSharedVoice = true;

        for (int lang = 0; lang < CfgLib::LanguageRegistry::Instance().Count(); ++lang)
        {
            RString localized =
                WithLangSuffix(basePath, RString(CfgLib::LanguageRegistry::Instance().Languages()[lang].name.c_str()));
            if (localized.GetLength() > 0 && QIFStreamB::FileExist(localized))
                info.flags[lang].voice = true;
        }
    }

    if (hasSharedVoice)
        info.flags[0].voice = true;
}

void DetectVoiceLanguages(const QFBank& bank, MissionLanguageDetector::MissionLanguageInfo& info)
{
    if (!bank.FileExists("description.ext"))
        return;

    QIFStreamB stream;
    stream.open(bank, "description.ext");

    ParamFile desc;
    desc.Parse(stream);

    const ParamEntry* cfgSounds = desc.FindEntry("CfgSounds");
    if (!cfgSounds)
        return;

    bool hasSharedVoice = false;
    for (int i = 0; i < cfgSounds->GetEntryCount(); ++i)
    {
        const ParamEntry& soundClass = cfgSounds->GetEntry(i);
        const ParamEntry* sound = soundClass.FindEntry("sound");
        if (!sound || !sound->IsArray() || sound->GetSize() <= 0)
            continue;

        RString relPath = (*sound)[0].GetValue();
        if (relPath.GetLength() == 0)
            continue;

        RString basePath = ResolveMissionVoicePath(bank, relPath);
        if (basePath.GetLength() == 0)
            continue;

        if (bank.FileExists(basePath))
            hasSharedVoice = true;
        for (int lang = 0; lang < CfgLib::LanguageRegistry::Instance().Count(); ++lang)
        {
            RString localized =
                WithLangSuffix(basePath, RString(CfgLib::LanguageRegistry::Instance().Languages()[lang].name.c_str()));
            if (localized.GetLength() > 0 && bank.FileExists(localized))
                info.flags[lang].voice = true;
        }
    }

    if (hasSharedVoice)
        info.flags[0].voice = true;
}

template <class Predicate>
RString FormatLanguageList(const MissionLanguageDetector::MissionLanguageInfo& info, Predicate&& include)
{
    RString result;
    for (int i = 0; i < CfgLib::LanguageRegistry::Instance().Count(); ++i)
    {
        if (!include(info.flags[i]))
            continue;

        if (result.GetLength() > 0)
            result = result + RString(" ");
        result = result + RString(CfgLib::LanguageRegistry::Instance().Languages()[i].code.c_str());
    }

    if (result.GetLength() == 0)
        return RString("-");

    return result;
}

RString HtmlEscape(RString text)
{
    RString out;
    for (const char* p = text; *p; ++p)
    {
        switch (*p)
        {
            case '&':
                out = out + RString("&amp;");
                break;
            case '<':
                out = out + RString("&lt;");
                break;
            case '>':
                out = out + RString("&gt;");
                break;
            case '"':
                out = out + RString("&quot;");
                break;
            default:
            {
                char ch[2] = {*p, 0};
                out = out + RString(ch);
                break;
            }
        }
    }
    return out;
}

} // namespace

namespace MissionLanguageDetector
{

MissionLanguageInfo Detect(RString root)
{
    root = EnsureTrailingSlash(root);

    MissionLanguageInfo info;
    DetectStringtableLanguages(root, info);
    DetectHtmlLanguages(root, "briefing", &MissionLanguageFlags::briefing, info);
    DetectHtmlLanguages(root, "overview", &MissionLanguageFlags::overview, info);
    DetectVoiceLanguages(root, info);
    return info;
}

MissionLanguageInfo Detect(const QFBank& bank)
{
    MissionLanguageInfo info;
    DetectStringtableLanguages(bank, info);
    DetectHtmlLanguages(bank, "briefing", &MissionLanguageFlags::briefing, info);
    DetectHtmlLanguages(bank, "overview", &MissionLanguageFlags::overview, info);
    DetectVoiceLanguages(bank, info);
    return info;
}

MissionPreviewInfo DetectPreview(RString root)
{
    MissionPreviewInfo info;
    info.languages = Detect(root);
    info.missionViewDistance = DetectMissionViewDistance(root);
    return info;
}

MissionPreviewInfo DetectPreview(const QFBank& bank)
{
    MissionPreviewInfo info;
    info.languages = Detect(bank);
    info.missionViewDistance = DetectMissionViewDistance(bank);
    return info;
}

RString FormatTextLanguages(const MissionLanguageInfo& info)
{
    return FormatLanguageList(info, [](const MissionLanguageFlags& flags)
                              { return flags.stringtable || flags.briefing || flags.overview; });
}

RString FormatVoiceLanguages(const MissionLanguageInfo& info)
{
    return FormatLanguageList(info, [](const MissionLanguageFlags& flags) { return flags.voice; });
}

RString FormatViewDistance(const MissionPreviewInfo& info)
{
    if (!info.missionViewDistance.has_value())
        return RString("-");

    std::string formatted = fmt::format("{:.0f} m", *info.missionViewDistance);
    return RString(formatted.c_str());
}

namespace
{
bool s_showLocalizationDebugInfo = false;
} // namespace

bool ShowLocalizationDebugInfo()
{
    return s_showLocalizationDebugInfo;
}

void SetShowLocalizationDebugInfo(bool show)
{
    s_showLocalizationDebugInfo = show;
}

RString BuildPreviewHtml(const MissionPreviewInfo& info, RString overviewHtmlUtf8)
{
    if (!s_showLocalizationDebugInfo)
        return overviewHtmlUtf8;

    RString text = HtmlEscape(FormatTextLanguages(info.languages));
    RString voice = HtmlEscape(FormatVoiceLanguages(info.languages));
    RString distance = HtmlEscape(FormatViewDistance(info));

    std::string block;
    block += "<table><tr><td width=\"36\"><b>TXT</b></td><td><nobr>";
    block += (const char*)text;
    block += "</nobr></td></tr><tr><td width=\"36\"><b>VO</b></td><td><nobr>";
    block += (const char*)voice;
    block += "</nobr></td></tr><tr><td width=\"36\"><b>VD</b></td><td><nobr>";
    block += (const char*)distance;
    block += "</nobr></td></tr></table><br/>";

    std::string overview = (const char*)overviewHtmlUtf8;
    std::string lower = overview;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    const size_t imageStart = lower.find("<img");
    if (imageStart != std::string::npos)
    {
        const size_t imageBreakStart = lower.find("<br", imageStart);
        if (imageBreakStart != std::string::npos)
        {
            const size_t imageBreakEnd = overview.find('>', imageBreakStart);
            if (imageBreakEnd != std::string::npos)
            {
                overview.insert(imageBreakEnd + 1, block);
                return RString(overview.c_str());
            }
        }
    }

    const size_t bodyStart = lower.find("<body");
    if (bodyStart != std::string::npos)
    {
        const size_t bodyEnd = overview.find('>', bodyStart);
        if (bodyEnd != std::string::npos)
        {
            overview.insert(bodyEnd + 1, block);
            return RString(overview.c_str());
        }
    }

    overview = "<html><body>" + block + overview + "</body></html>";
    return RString(overview.c_str());
}

} // namespace MissionLanguageDetector

} // namespace Poseidon
