#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>

#include <Poseidon/UI/Controls/UIControls.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <string>
#include <stdio.h>
#include <string.h>
#include <cctype>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{

namespace
{
int CountPrintfConversions(const char* format)
{
    if (!format)
        return 0;

    int count = 0;
    for (const char* p = format; *p != 0; ++p)
    {
        if (*p != '%')
            continue;

        ++p;
        if (*p == 0)
            break;
        if (*p == '%')
            continue;

        while (*p != 0 && strchr("-+ #0", *p))
            ++p;
        while (*p != 0 && std::isdigit(static_cast<unsigned char>(*p)))
            ++p;
        if (*p == '.')
        {
            ++p;
            while (*p != 0 && std::isdigit(static_cast<unsigned char>(*p)))
                ++p;
        }
        while (*p != 0 && strchr("hljztL", *p))
            ++p;
        if (*p == 0)
            break;

        ++count;
    }

    return count;
}

bool FormatStartsWithPlaceholder(RString format)
{
    const char* p = format;
    while (*p != 0 && std::isspace(static_cast<unsigned char>(*p)))
        ++p;
    return *p == '%';
}

RString FormatBriefingTitleFallback(RString unitName, RString groupName, int unitId)
{
    const RString localizedTitle = LocalizeString("STR_DISP_BRIEF_TITLE");
    const char* title = localizedTitle.GetLength() > 0 ? (const char*)localizedTitle : "Briefing";

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s (%s, %s %d)", title, (const char*)unitName, (const char*)groupName, unitId);
    return RString(buffer);
}

RString GetDirectoryPath(RString filename)
{
    const char* path = filename;
    int lastSep = -1;
    for (int i = 0; path[i] != 0; ++i)
    {
        if (path[i] == '/' || path[i] == '\\')
            lastSep = i;
    }

    if (lastSep < 0)
        return RString();

    return filename.Substring(0, lastSep + 1);
}

RString EnsureTrailingSlash(RString path)
{
    if (path.GetLength() == 0)
        return path;

    const char last = path[path.GetLength() - 1];
    if (last == '/' || last == '\\')
        return path;

    // Match the separator already in use so paths read back identically
    // (filesystem::path::string() returns native, callers pass mixed forms).
    for (int i = path.GetLength() - 1; i >= 0; --i)
    {
        if (path[i] == '\\')
            return path + RString("\\");
        if (path[i] == '/')
            return path + RString("/");
    }
    return path + RString("/");
}

bool IsStringtableTokenChar(unsigned char ch)
{
    return std::isalnum(ch) || ch == '_';
}

std::string LoadHtmlUtf8Raw(RString filename)
{
    std::string utf8Buf;
    if (filename.GetLength() == 0 || !QIFStreamB::FileExist(filename))
        return utf8Buf;

    QIFStreamB raw;
    raw.AutoOpen(filename);
    while (!raw.eof() && !raw.fail())
    {
        int c = raw.get();
        if (c < 0)
            break;
        utf8Buf.push_back(static_cast<char>(c));
    }

    const size_t flen = strlen(filename);
    const bool isUtf8 = flen >= 10 && stricmp(filename + flen - 10, ".utf8.html") == 0;
    if (!isUtf8)
    {
        const Poseidon::Codepage cp = Poseidon::CodepageForLanguage((const char*)GLanguage);
        utf8Buf = Poseidon::DecodeLegacyTextToUtf8(utf8Buf, cp);
    }

    return utf8Buf;
}

RString ResolveMissionHtmlToken(const std::string& token, RString csvPath)
{
    if (token.size() < 2)
        return RString();

    const char* key = token.c_str() + 1;
    if (csvPath.GetLength() > 0)
    {
        const RString local = LookupStringtableCsv(csvPath, key);
        if (local.GetLength() > 0)
            return local;
    }

    return Localize(RString(token.c_str()));
}

RString LocalizeMissionString(RString value)
{
    if (value.GetLength() == 0)
        return value;

    const RString localized = Localize(value);
    if (localized.GetLength() > 0)
        return localized;

    const char first = value[0];
    if (first == '$' || first == '@')
        return RString();

    return value;
}

RString GetCampaignStringtableFileForMission(RString missionDirectory)
{
    const RString missionDir = EnsureTrailingSlash(missionDirectory);
    if (missionDir.GetLength() == 0)
        return RString();

    const RString missionsRoot = GetDirectoryPath(missionDir.Substring(0, missionDir.GetLength() - 1));
    if (missionsRoot.GetLength() == 0)
        return RString();

    const RString campaignDir = GetDirectoryPath(missionsRoot.Substring(0, missionsRoot.GetLength() - 1));
    if (campaignDir.GetLength() == 0)
        return RString();

    // Only a campaign root carries description.ext, so a single mission under
    // Missions/ cannot pick up whatever sits beside its parent.
    const RString description = campaignDir + RString("description.ext");
    if (!QIFStreamB::FileExist(description))
        return RString();

    return GetMissionStringtableFileForHtml(description);
}

RString LoadMissionBriefingNameRaw(RString missionDirectory)
{
    const RString sqmPath = EnsureTrailingSlash(missionDirectory) + RString("mission.sqm");
    if (!QIFStreamB::FileExist(sqmPath))
        return RString();

    ParamFile sqm;
    if (!sqm.ParseBinOrTxt(sqmPath))
        sqm.Clear();

    if (ParamEntry* mission = sqm.FindEntry("Mission"))
    {
        if (ParamEntry* intel = mission->FindEntry("Intel"))
        {
            if (ParamEntry* briefingName = intel->FindEntry("briefingName"))
            {
                const RString raw = briefingName->GetValueRaw();
                if (raw.GetLength() > 0)
                    return raw;
            }
        }
    }

    QIFStreamB file;
    file.AutoOpen(sqmPath);

    std::string buffer;
    buffer.reserve(4096);
    while (!file.eof() && !file.fail() && buffer.size() < 4096)
    {
        const int ch = file.get();
        if (ch < 0)
            break;
        buffer.push_back(static_cast<char>(ch));
    }

    const char* searchStr = "briefingName";
    const size_t searchLen = strlen(searchStr);
    const size_t keyPos = buffer.find(searchStr);
    if (keyPos == std::string::npos)
        return RString();

    size_t pos = keyPos + searchLen;
    while (pos < buffer.size() && buffer[pos] == ' ')
        ++pos;
    if (pos >= buffer.size() || buffer[pos] != '=')
        return RString();

    ++pos;
    while (pos < buffer.size() && buffer[pos] == ' ')
        ++pos;
    if (pos >= buffer.size() || buffer[pos] != '"')
        return RString();

    ++pos;
    const size_t end = buffer.find('"', pos);
    if (end == std::string::npos || end <= pos)
        return RString();

    return RString(buffer.substr(pos, end - pos).c_str());
}

} // namespace

RString GetMissionStringtableFileForHtml(RString htmlFilename)
{
    const RString dir = GetDirectoryPath(htmlFilename);
    if (dir.GetLength() == 0)
        return RString();

    const RString utf8 = dir + RString("stringtable.utf8.csv");
    if (QIFStreamB::FileExist(utf8))
        return utf8;

    const RString legacy = dir + RString("stringtable.csv");
    if (QIFStreamB::FileExist(legacy))
        return legacy;

    return RString();
}

RString FindLocalizedMissionHtmlFile(RString directory, RString stem)
{
    const RString prefix = EnsureTrailingSlash(directory) + stem + RString(".");
    const char* candidates[] = {
        ".utf8.html",
        ".html",
    };
    for (const char* suffix : candidates)
    {
        const RString name = prefix + GLanguage + RString(suffix);
        if (QIFStreamB::FileExist(name))
            return name;
    }

    const RString utf8 = prefix + RString("utf8.html");
    if (QIFStreamB::FileExist(utf8))
        return utf8;

    const RString legacy = prefix + RString("html");
    if (QIFStreamB::FileExist(legacy))
        return legacy;

    return RString();
}

RString ExpandMissionHtmlStringtableTokens(RString htmlUtf8, RString csvPath)
{
    const std::string input = (const char*)htmlUtf8;
    std::string output;
    output.reserve(input.size());

    for (size_t i = 0; i < input.size();)
    {
        const unsigned char ch = static_cast<unsigned char>(input[i]);
        const bool dollarToken = ch == '$' && i + 4 <= input.size() && input.compare(i + 1, 3, "STR") == 0;
        const bool atToken =
            ch == '@' && i + 1 < input.size() && IsStringtableTokenChar(static_cast<unsigned char>(input[i + 1]));
        if (!dollarToken && !atToken)
        {
            output.push_back(static_cast<char>(ch));
            ++i;
            continue;
        }

        size_t end = i + 1;
        while (end < input.size() && IsStringtableTokenChar(static_cast<unsigned char>(input[end])))
            ++end;

        const std::string token = input.substr(i, end - i);
        const RString localized = ResolveMissionHtmlToken(token, csvPath);
        if (localized.GetLength() > 0)
        {
            output += (const char*)localized;
            i = end;
            continue;
        }

        output += token;
        i = end;
    }

    return RString(output.c_str());
}

RString LoadLocalizedMissionHtmlUtf8(RString filename)
{
    const std::string rawUtf8 = LoadHtmlUtf8Raw(filename);
    return ExpandMissionHtmlStringtableTokens(RString(rawUtf8.c_str()), GetMissionStringtableFileForHtml(filename));
}

RString LoadLocalizedMissionBriefingName(RString missionDirectory, RString fallback)
{
    const RString raw = LoadMissionBriefingNameRaw(missionDirectory);
    if (raw.GetLength() > 0)
    {
        // briefingName is normally a per-mission stringtable token, and that
        // table isn't merged into the global one (the campaign-load screen
        // never loads it). Expand it against the mission's own
        // stringtable.csv with the same expander mission HTML uses (which
        // also falls back to the global table), then the campaign table;
        // LocalizeMissionString then returns the literal/expanded text or ""
        // for a token that resolved nowhere, so an unexpanded "$STR_..." never
        // leaks to the UI.
        const RString csv =
            GetMissionStringtableFileForHtml(EnsureTrailingSlash(missionDirectory) + RString("mission.sqm"));
        RString expanded = ExpandMissionHtmlStringtableTokens(raw, csv);
        const RString campaignCsv = GetCampaignStringtableFileForMission(missionDirectory);
        if (campaignCsv.GetLength() > 0)
            expanded = ExpandMissionHtmlStringtableTokens(expanded, campaignCsv);
        const RString localized = LocalizeMissionString(expanded);
        if (localized.GetLength() > 0)
            return localized;
    }

    return LocalizeMissionString(fallback);
}

RString FormatLocalizedBriefingTitle(RString unitName, RString groupName, int unitId)
{
    const RString format = LocalizeString("STR_BRIEFING");
    if (CountPrintfConversions(format) < 3 || FormatStartsWithPlaceholder(format))
        return FormatBriefingTitleFallback(unitName, groupName, unitId);

    char buffer[256];
    snprintf(buffer, sizeof(buffer), (const char*)format, (const char*)unitName, (const char*)groupName, unitId);
    return RString(buffer);
}

void LoadLocalizedMissionHtml(CHTMLContainer* html, RString filename)
{
    if (!html)
        return;

    html->Init();
    if (filename.GetLength() > 0)
        html->LoadBuffer(filename, LoadLocalizedMissionHtmlUtf8(filename), false);
}

RString GetCurrentHtmlSectionName(const CHTMLContainer* html)
{
    if (!html || html->NSections() <= 0)
        return RString();

    const int sectionIndex = html->CurrentSection();
    if (sectionIndex < 0 || sectionIndex >= html->NSections())
        return RString();

    const HTMLSection& section = html->GetSection(sectionIndex);
    if (section.names.Size() <= 0)
        return RString();

    return section.names[0];
}

} // namespace Poseidon
