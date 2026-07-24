
#include <Poseidon/UI/Controls/UIControlsExtShared.hpp>
#include <Poseidon/UI/Controls/HtmlTextWrap.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/World/World.hpp>
#include <ctype.h>
#include <Evaluator/express.hpp>
#include <unordered_map>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/GlobalAlive.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
namespace Poseidon
{
RString FindPicture(RString name);
RString GetBaseDirectory();
RString GetMissionDirectory();
} // namespace Poseidon

#define ISSPACE(c) ((c) >= 0 && (c) <= 32)

Texture* HTMLField::GetTexture()
{
    if (condition.GetLength() > 0)
    {
        GameState* gstate = GWorld->GetGameState();
        bool result = gstate->EvaluateBool(condition);
        return result ? texture1 : texture2;
    }
    else
    {
        return texture1;
    }
}

CHTMLContainer::CHTMLContainer(const ParamEntry& cls)
{
    _bgColor = GetPackedColor(cls >> "colorBackground");
    _textColor = GetPackedColor(cls >> "colorText");
    _boldColor = GetPackedColor(cls >> "colorBold");
    _linkColor = GetPackedColor(cls >> "colorLink");
    _activeLinkColor = GetPackedColor(cls >> "colorLinkActive");

    _fontH1 = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H1" >> "font"));
    _fontH1Bold = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H1" >> "fontBold"));
    const ParamEntry* entry = (cls >> "H1").FindEntry("size");
    if (entry)
    {
        _sizeH1 = (float)(*entry) * _fontH1->Height();
    }
    else
    {
        _sizeH1 = cls >> "H1" >> "sizeEx";
    }
    _fontH2 = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H2" >> "font"));
    _fontH2Bold = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H2" >> "fontBold"));
    entry = (cls >> "H2").FindEntry("size");
    if (entry)
    {
        _sizeH2 = (float)(*entry) * _fontH2->Height();
    }
    else
    {
        _sizeH2 = cls >> "H2" >> "sizeEx";
    }
    _fontH3 = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H3" >> "font"));
    _fontH3Bold = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H3" >> "fontBold"));
    entry = (cls >> "H3").FindEntry("size");
    if (entry)
    {
        _sizeH3 = (float)(*entry) * _fontH3->Height();
    }
    else
    {
        _sizeH3 = cls >> "H3" >> "sizeEx";
    }
    _fontH4 = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H4" >> "font"));
    _fontH4Bold = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H4" >> "fontBold"));
    entry = (cls >> "H4").FindEntry("size");
    if (entry)
    {
        _sizeH4 = (float)(*entry) * _fontH4->Height();
    }
    else
    {
        _sizeH4 = cls >> "H4" >> "sizeEx";
    }
    _fontH5 = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H5" >> "font"));
    _fontH5Bold = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H5" >> "fontBold"));
    entry = (cls >> "H5").FindEntry("size");
    if (entry)
    {
        _sizeH5 = (float)(*entry) * _fontH5->Height();
    }
    else
    {
        _sizeH5 = cls >> "H5" >> "sizeEx";
    }
    _fontH6 = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H6" >> "font"));
    _fontH6Bold = GLOB_ENGINE->LoadFont(GetFontID(cls >> "H6" >> "fontBold"));
    entry = (cls >> "H6").FindEntry("size");
    if (entry)
    {
        _sizeH6 = (float)(*entry) * _fontH6->Height();
    }
    else
    {
        _sizeH6 = cls >> "H6" >> "sizeEx";
    }
    _fontP = GLOB_ENGINE->LoadFont(GetFontID(cls >> "P" >> "font"));
    _fontPBold = GLOB_ENGINE->LoadFont(GetFontID(cls >> "P" >> "fontBold"));
    entry = (cls >> "P").FindEntry("size");
    if (entry)
    {
        _sizeP = (float)(*entry) * _fontP->Height();
    }
    else
    {
        _sizeP = cls >> "P" >> "sizeEx";
    }

    Init();
    _filename = cls >> "filename";

    _indent = 0;
    /*
        RString filename = cls>>"filename";
        if (filename.GetLength() > 0)
            Load(filename);
    */
}

int CHTMLContainer::FindSection(const char* name)
{
    for (int s = 0; s < _sections.Size(); s++)
    {
        HTMLSection& to = _sections[s];
        for (int n = 0; n < to.names.Size(); n++)
        {
            if (stricmp(name, to.names[n]) == 0)
            {
                return s;
            }
        }
    }
    return -1;
}

void CHTMLContainer::SwitchSection(const char* name)
{
    int s = FindSection(name);
    if (s >= 0)
    {
        _currentSection = s;
        float mouseX = 0.5 + InputSubsystem::Instance().GetCursorX() * 0.5;
        float mouseY = 0.5 + InputSubsystem::Instance().GetCursorY() * 0.5;
        _activeField = FindField(mouseX, mouseY);
    }
}

int CHTMLContainer::ActiveBookmark()
{
    if (_sections.Size() <= 0)
    {
        return -1;
    }
    HTMLSection& section = _sections[_currentSection];
    int n = section.names.Size();
    int m = _bookmarks.Size();
    for (int i = 0; i < n; i++)
    {
        RString name = section.names[i];
        for (int j = 0; j < m; j++)
        {
            if (stricmp(name, _bookmarks[j]) == 0)
            {
                return j;
            }
        }
    }
    return -1;
}

const HTMLField* CHTMLContainer::GetActiveField() const
{
    if (_activeField < 0)
    {
        return nullptr;
    }
    if (_sections.Size() <= 0)
    {
        return nullptr;
    }
    const HTMLSection& section = _sections[_currentSection];
    return &section.fields[_activeField];
}

void CHTMLContainer::Init()
{
    _sections.Clear();
    _currentSection = 0;
    _activeField = -1;
}

void CHTMLContainer::InitSection(int section)
{
    _sections[section].fields.Clear();
    _sections[section].rows.Clear();
    _sections[section].names.Clear();
}

int CHTMLContainer::AddSection()
{
    return _sections.Add();
}

void CHTMLContainer::AddBreak(int section, bool bottom)
{
    if (section < 0 || section >= _sections.Size())
    {
        return;
    }

    HTMLSection& sec = _sections[section];
    int i = sec.fields.Add();
    HTMLField& fld = sec.fields[i];

    //	fld.first = 0;
    fld.nextline = true;
    fld.exclude = false;
    fld.text = "";
    fld.format = HFP;
    fld.href = "";
    fld.bottom = bottom;
    fld.indent = _indent;
    fld.tableWidth = 0;
}

void CHTMLContainer::AddText(int section, RString text, HTMLFormat format, HTMLAlign align, bool bottom, bool bold,
                             RString href, float tableWidth)
{
    if (section < 0 || section >= _sections.Size())
    {
        return;
    }

    HTMLSection& sec = _sections[section];
    int i = sec.fields.Add();
    HTMLField& fld = sec.fields[i];

    //	fld.first = 0;
    fld.nextline = false;
    fld.exclude = false;
    fld.text = text;
    fld.format = format;
    fld.align = align;
    fld.bold = bold;
    fld.href = href;
    fld.bottom = bottom;
    fld.indent = _indent;
    fld.tableWidth = tableWidth;
}

// Truncate `buffer` (a file path) in place to its directory part, keeping the
// trailing separator. Honors both '\\' and '/' so an HTML <img src> base dir
// resolves whether the host path used Windows ('missions\\...') or
// forward-slash ('campaigns/.../') separators — campaign overview paths use
// the latter, which a backslash-only scan would miss, appending the image name
// to the whole HTML filename and blanking the preview.
static void TruncateToDirectory(char* buffer)
{
    char* sep = strrchr(buffer, '\\');
    char* fwd = strrchr(buffer, '/');
    if (fwd && (!sep || fwd > sep))
    {
        sep = fwd;
    }
    if (sep)
    {
        *(++sep) = 0;
    }
}

HTMLField* CHTMLContainer::AddImage(int section, RString image, HTMLAlign align, bool bottom, float w, float h,
                                    RString href, RString text, float tableWidth)
{
    if (section < 0 || section >= _sections.Size())
    {
        return nullptr;
    }

    HTMLSection& sec = _sections[section];
    int i = sec.fields.Add();
    HTMLField& fld = sec.fields[i];

    fld.format = HFImg;
    fld.align = align;
    fld.nextline = false;
    fld.exclude = false;
    fld.text = text;
    fld.href = href;
    fld.bottom = bottom;
    fld.indent = _indent;
    fld.tableWidth = tableWidth;

    char buffer[256];
    const char* q = strchr(image, '?');
    if (q)
    {
        fld.condition = image.Substring(0, q - image);
        RString image1 = q + 1;
        const char* p = strchr(image1, ':');
        if (p)
        {
            RString image2 = p + 1;
            image1 = image1.Substring(0, p - image1);

            if (image2[0] == '\\')
            {
                image2 = image2.Substring(1, INT_MAX);
            }
            else
            {
                // find in current directory
                snprintf(buffer, sizeof(buffer), "%s", (const char*)_filename);
                TruncateToDirectory(buffer);
                strncat(buffer, image2, sizeof(buffer) - strlen(buffer) - 1);
                if (QIFStreamB::FileExist(buffer))
                {
                    image2 = buffer;
                }
                else
                {
                    image2 = Poseidon::FindPicture(image2);
                }
            }
            image2.Lower();
            I_AM_ALIVE();
            fld.texture2 = GlobLoadTexture(image2);
            I_AM_ALIVE();
        }
        if (image1[0] == '\\')
        {
            image1 = image1.Substring(1, INT_MAX);
        }
        else
        {
            // find in current directory
            snprintf(buffer, sizeof(buffer), "%s", (const char*)_filename);
            TruncateToDirectory(buffer);
            strncat(buffer, image1, sizeof(buffer) - strlen(buffer) - 1);
            if (QIFStreamB::FileExist(buffer))
            {
                image1 = buffer;
            }
            else
            {
                image1 = Poseidon::FindPicture(image1);
            }
        }
        image1.Lower();
        I_AM_ALIVE();
        fld.texture1 = GlobLoadTexture(image1);
        I_AM_ALIVE();
        if (!fld.texture1)
        {
            fld.texture1 = fld.texture2;
        }
    }
    else
    {
        if (image[0] == '\\')
        {
            image = image.Substring(1, INT_MAX);
        }
        else
        {
            // find in current directory
            snprintf(buffer, sizeof(buffer), "%s", (const char*)_filename);
            TruncateToDirectory(buffer);
            strncat(buffer, image, sizeof(buffer) - strlen(buffer) - 1);
            if (QIFStreamB::FileExist(buffer))
            {
                image = buffer;
            }
            else
            {
                image = Poseidon::FindPicture(image);
            }
        }
        image.Lower();
        I_AM_ALIVE();
        if (image.GetLength() > 0)
        {
            fld.texture1 = GlobLoadTexture(image);
        }
        else
        {
            fld.texture1 = nullptr;
        }
        fld.texture2 = fld.texture1;
        I_AM_ALIVE();
    }
    if (fld.texture1)
    {
        fld.texture1->SetMaxSize(1024); // no limits
        if (w < 0)
        {
            MipInfo mip = GLOB_ENGINE->TextBank()->UseMipmap(fld.texture1, 0, 0);
            if (h < 0)
            {
                w = fld.texture1->AWidth();
                h = fld.texture1->AHeight();
            }
            else
            {
                w = fld.texture1->AWidth() * h / fld.texture1->AHeight();
            }
        }
        else if (h < 0)
        {
            h = fld.texture1->AHeight() * w / fld.texture1->AWidth();
        }

        fld.width = w * (1.0 / 640.0);
        fld.height = h * (1.0 / 480.0);
    }
    else
    {
        fld.width = (w > 0 ? w : h) * (1.0 / 640.0);
        fld.height = (h > 0 ? h : w) * (1.0 / 480.0);
        saturateMax(fld.width, 0);
        saturateMax(fld.height, 0);
    }
    return &fld;
}

void CHTMLContainer::AddName(int section, RString name)
{
    if (section < 0 || section >= _sections.Size())
    {
        return;
    }

    HTMLSection& sec = _sections[section];
    sec.names.Add(name);
}

void CHTMLContainer::AddBookmark(RString link)
{
    _bookmarks.Add(link);
}

static RString ReadTag(QIStream& in)
{
    char buf[256];
    int len = 0;
    int c = in.get();
    while (!in.eof() && !in.fail() && (isalnum(c) || c == '/'))
    {
        if (len < sizeof(buf) - 1)
        {
            buf[len++] = c;
        }
        c = in.get();
    }
    in.unget();
    buf[len] = 0;
    return buf;
}

static void SkipTag(QIStream& in)
{
    int c = in.get();
    while (!in.eof() && !in.fail() && c != '>')
    {
        c = in.get();
    }
}

static void SkipSpaces(QIStream& in)
{
    int c = in.get();
    while (!in.eof() && !in.fail() && ISSPACE(c))
    {
        c = in.get();
    }
    in.unget();
}

static RString ReadPropertyName(QIStream& in)
{
    char buf[256];
    int len = 0;
    int c = in.get();
    while (!in.eof() && !in.fail() && !ISSPACE(c) && c != '=')
    {
        if (len < sizeof(buf) - 1)
        {
            buf[len++] = c;
        }
        c = in.get();
    }
    in.unget();
    PoseidonAssert(len < sizeof(buf));
    buf[len] = 0;
    return buf;
}

static RString ReadPropertyValue(QIStream& in)
{
    SkipSpaces(in);
    int c = in.get();
    if (c != '=')
    {
        in.unget();
        return "";
    }

    SkipSpaces(in);
    c = in.get();
    if (c != '"')
    {
        in.unget();
        return "";
    }

    char buf[256];
    int len = 0;
    c = in.get();
    while (!in.eof() && !in.fail() && c != '"')
    {
        if (len < sizeof(buf) - 1)
        {
            buf[len++] = c;
        }
        c = in.get();
    }
    PoseidonAssert(len < sizeof(buf));
    buf[len] = 0;
    return buf;
}

static char ReadChar(QIStream& in)
{
    char buf[2048];
    int len = 0;
    int c = in.get();
    while (!in.eof() && !in.fail() && c != ';')
    {
        if (len < sizeof(buf) - 1)
        {
            buf[len++] = c;
        }
        c = in.get();
    }
    buf[len] = 0;
    if (buf[0] == '#')
    {
        return atoi(buf + 1);
    }
    else if (strcmp(buf, "amp") == 0)
    {
        return '&';
    }
    else if (strcmp(buf, "quot") == 0)
    {
        return '\"';
    }
    else if (strcmp(buf, "lt") == 0)
    {
        return '<';
    }
    else if (strcmp(buf, "gt") == 0)
    {
        return '>';
        //	else if (stricmp(buf, "nbsp") == 0) return ' ';
    }
    else if (strcmp(buf, "nbsp") == 0)
    {
        return (char)160;
    }
    else if (strcmp(buf, "iexcl") == 0)
    {
        return (char)161;
    }
    else if (strcmp(buf, "cent") == 0)
    {
        return (char)162;
    }
    else if (strcmp(buf, "pound") == 0)
    {
        return (char)163;
    }
    else if (strcmp(buf, "curren") == 0)
    {
        return (char)164;
    }
    else if (strcmp(buf, "yen") == 0)
    {
        return (char)165;
    }
    else if (strcmp(buf, "brvbar") == 0)
    {
        return (char)166;
    }
    else if (strcmp(buf, "sect") == 0)
    {
        return (char)167;
    }
    else if (strcmp(buf, "uml") == 0)
    {
        return (char)168;
    }
    else if (strcmp(buf, "copy") == 0)
    {
        return (char)169;
    }
    else if (strcmp(buf, "ordf") == 0)
    {
        return (char)170;
    }
    else if (strcmp(buf, "laquo") == 0)
    {
        return (char)171;
    }
    else if (strcmp(buf, "not") == 0)
    {
        return (char)172;
    }
    else if (strcmp(buf, "shy") == 0)
    {
        return (char)173;
    }
    else if (strcmp(buf, "reg") == 0)
    {
        return (char)174;
    }
    else if (strcmp(buf, "macr") == 0)
    {
        return (char)175;
    }
    else if (strcmp(buf, "deg") == 0)
    {
        return (char)176;
    }
    else if (strcmp(buf, "plusmn") == 0)
    {
        return (char)177;
    }
    else if (strcmp(buf, "sup2") == 0)
    {
        return (char)178;
    }
    else if (strcmp(buf, "sup3") == 0)
    {
        return (char)179;
    }
    else if (strcmp(buf, "acute") == 0)
    {
        return (char)180;
    }
    else if (strcmp(buf, "micro") == 0)
    {
        return (char)181;
    }
    else if (strcmp(buf, "para") == 0)
    {
        return (char)182;
    }
    else if (strcmp(buf, "middot") == 0)
    {
        return (char)183;
    }
    else if (strcmp(buf, "cedil") == 0)
    {
        return (char)184;
    }
    else if (strcmp(buf, "sup1") == 0)
    {
        return (char)185;
    }
    else if (strcmp(buf, "ordm") == 0)
    {
        return (char)186;
    }
    else if (strcmp(buf, "raquo") == 0)
    {
        return (char)187;
    }
    else if (strcmp(buf, "frac14") == 0)
    {
        return (char)188;
    }
    else if (strcmp(buf, "frac12") == 0)
    {
        return (char)189;
    }
    else if (strcmp(buf, "frac34") == 0)
    {
        return (char)190;
    }
    else if (strcmp(buf, "iquest") == 0)
    {
        return (char)191;
    }
    else if (strcmp(buf, "Agrave") == 0)
    {
        return (char)192;
    }
    else if (strcmp(buf, "Aacute") == 0)
    {
        return (char)193;
    }
    else if (strcmp(buf, "Acirc") == 0)
    {
        return (char)194;
    }
    else if (strcmp(buf, "Atilde") == 0)
    {
        return (char)195;
    }
    else if (strcmp(buf, "Auml") == 0)
    {
        return (char)196;
    }
    else if (strcmp(buf, "Aring") == 0)
    {
        return (char)197;
    }
    else if (strcmp(buf, "AElig") == 0)
    {
        return (char)198;
    }
    else if (strcmp(buf, "Ccedil") == 0)
    {
        return (char)199;
    }
    else if (strcmp(buf, "Egrave") == 0)
    {
        return (char)200;
    }
    else if (strcmp(buf, "Eacute") == 0)
    {
        return (char)201;
    }
    else if (strcmp(buf, "Ecirc") == 0)
    {
        return (char)202;
    }
    else if (strcmp(buf, "Euml") == 0)
    {
        return (char)203;
    }
    else if (strcmp(buf, "Igrave") == 0)
    {
        return (char)204;
    }
    else if (strcmp(buf, "Iacute") == 0)
    {
        return (char)205;
    }
    else if (strcmp(buf, "Icirc") == 0)
    {
        return (char)206;
    }
    else if (strcmp(buf, "Iuml") == 0)
    {
        return (char)207;
    }
    else if (strcmp(buf, "ETH") == 0)
    {
        return (char)208;
    }
    else if (strcmp(buf, "Ntilde") == 0)
    {
        return (char)209;
    }
    else if (strcmp(buf, "Ograve") == 0)
    {
        return (char)210;
    }
    else if (strcmp(buf, "Oacute") == 0)
    {
        return (char)211;
    }
    else if (strcmp(buf, "Ocirc") == 0)
    {
        return (char)212;
    }
    else if (strcmp(buf, "Otilde") == 0)
    {
        return (char)213;
    }
    else if (strcmp(buf, "Ouml") == 0)
    {
        return (char)214;
    }
    else if (strcmp(buf, "times") == 0)
    {
        return (char)215;
    }
    else if (strcmp(buf, "Oslash") == 0)
    {
        return (char)216;
    }
    else if (strcmp(buf, "Ugrave") == 0)
    {
        return (char)217;
    }
    else if (strcmp(buf, "Uacute") == 0)
    {
        return (char)218;
    }
    else if (strcmp(buf, "Ucirc") == 0)
    {
        return (char)219;
    }
    else if (strcmp(buf, "Uuml") == 0)
    {
        return (char)220;
    }
    else if (strcmp(buf, "Yacute") == 0)
    {
        return (char)221;
    }
    else if (strcmp(buf, "THORN") == 0)
    {
        return (char)222;
    }
    else if (strcmp(buf, "szlig") == 0)
    {
        return (char)223;
    }
    else if (strcmp(buf, "agrave") == 0)
    {
        return (char)224;
    }
    else if (strcmp(buf, "aacute") == 0)
    {
        return (char)225;
    }
    else if (strcmp(buf, "acirc") == 0)
    {
        return (char)226;
    }
    else if (strcmp(buf, "atilde") == 0)
    {
        return (char)227;
    }
    else if (strcmp(buf, "auml") == 0)
    {
        return (char)228;
    }
    else if (strcmp(buf, "aring") == 0)
    {
        return (char)229;
    }
    else if (strcmp(buf, "aelig") == 0)
    {
        return (char)230;
    }
    else if (strcmp(buf, "ccedil") == 0)
    {
        return (char)231;
    }
    else if (strcmp(buf, "egrave") == 0)
    {
        return (char)232;
    }
    else if (strcmp(buf, "eacute") == 0)
    {
        return (char)233;
    }
    else if (strcmp(buf, "ecirc") == 0)
    {
        return (char)234;
    }
    else if (strcmp(buf, "euml") == 0)
    {
        return (char)235;
    }
    else if (strcmp(buf, "igrave") == 0)
    {
        return (char)236;
    }
    else if (strcmp(buf, "iacute") == 0)
    {
        return (char)237;
    }
    else if (strcmp(buf, "icirc") == 0)
    {
        return (char)238;
    }
    else if (strcmp(buf, "iuml") == 0)
    {
        return (char)239;
    }
    else if (strcmp(buf, "eth") == 0)
    {
        return (char)240;
    }
    else if (strcmp(buf, "ntilde") == 0)
    {
        return (char)241;
    }
    else if (strcmp(buf, "ograve") == 0)
    {
        return (char)242;
    }
    else if (strcmp(buf, "oacute") == 0)
    {
        return (char)243;
    }
    else if (strcmp(buf, "ocirc") == 0)
    {
        return (char)244;
    }
    else if (strcmp(buf, "otilde") == 0)
    {
        return (char)245;
    }
    else if (strcmp(buf, "ouml") == 0)
    {
        return (char)246;
    }
    else if (strcmp(buf, "divide") == 0)
    {
        return (char)247;
    }
    else if (strcmp(buf, "oslash") == 0)
    {
        return (char)248;
    }
    else if (strcmp(buf, "ugrave") == 0)
    {
        return (char)249;
    }
    else if (strcmp(buf, "uacute") == 0)
    {
        return (char)250;
    }
    else if (strcmp(buf, "ucirc") == 0)
    {
        return (char)251;
    }
    else if (strcmp(buf, "uuml") == 0)
    {
        return (char)252;
    }
    else if (strcmp(buf, "yacute") == 0)
    {
        return (char)253;
    }
    else if (strcmp(buf, "thorn") == 0)
    {
        return (char)254;
    }
    else if (strcmp(buf, "yuml") == 0)
    {
        return (char)255;
    }
    else
    {
        return '?';
    }
}

static RString ReadText(QIStream& in, int langID)
{
    char buf[4 * 1024];
    int len = 0;
    int c = in.get();
    while (!in.eof() && !in.fail() && c != '<')
    {
        if (c == 0x0a)
        {
            // avoid spaces at line end
            while (len > 0 && buf[len - 1] == ' ')
            {
                len--;
            }
            // avoid CR LF at begin of the text
            if (len == 0)
            {
                goto ReadTextContinue;
            }
        }
        if (c != 0x0d) // avoid CR LF -> 2 spaces (CRLF or LF are handled well)
        {
            if (ISSPACE(c))
            {
                c = ' ';
            }
            else if (c == '&')
            {
                c = ReadChar(in);
            }

            if (len < sizeof(buf) - 1)
            {
                buf[len++] = c;
            }
        }
    ReadTextContinue:
        c = in.get();
    }
    in.unget();
    buf[len] = 0;
    return buf;
}

static RString ConvertURL(RString src)
{
    char buf[2048];
    const char* ptr = src;

    int j = 0;
    while (*ptr)
    {
        if (strnicmp(ptr, "%20", 3) == 0)
        {
            buf[j++] = ' ';
            ptr += 3;
        }
        else
        {
            buf[j++] = *(ptr++);
        }
    }
    buf[j] = 0;
    return buf;
}

enum HTMLContext
{
    HTMLNone,
    HTMLHTML,
    HTMLHead,
    HTMLBody,
    HTMLP,
    HTMLH1,
    HTMLH2,
    HTMLH3,
    HTMLH4,
    HTMLH5,
    HTMLH6,
    HTMLA,
    HTMLB,
    HTMLAddr,
    HTMLTable,
    HTMLTR,
    HTMLTD,
};

struct HTMLStackItem
{
    HTMLContext context;
    HTMLAlign align;
    HTMLFormat format;
    bool bold;
    float width;

    HTMLStackItem(HTMLContext c, HTMLAlign a, HTMLFormat f, bool b = false, float w = 0)
    {
        context = c;
        align = a;
        format = f;
        bold = b;
        width = w;
    }
};

class HTMLStack : public AutoArray<HTMLStackItem>
{
  public:
    void Push(HTMLStackItem item) { Add(item); }
    void Push(HTMLContext c, HTMLAlign a, HTMLFormat f) { Add(HTMLStackItem(c, a, f)); }
    HTMLStackItem Pop()
    {
        int n = Size() - 1;
        HTMLStackItem item = Get(n);
        Delete(n);
        return item;
    }
    //	HTMLFormat GetFormat();
};

/*
HTMLFormat GetFormat(HTMLContext ctx)
{
    switch (ctx)
    {
        case HTMLP:
        case HTMLAddr:
            return HFP;
        case HTMLH1:
            return HFH1;
        case HTMLH2:
            return HFH2;
        case HTMLH3:
            return HFH3;
        case HTMLH4:
            return HFH4;
        case HTMLH5:
            return HFH5;
        case HTMLH6:
            return HFH6;
        default:
            return HFError;
    }
}

HTMLFormat HTMLStack::GetFormat()
{
    int n = Size() - 1;
    for (int i=n; i>=0; i--)
    {
        HTMLFormat f = ::GetFormat(Get(i).context);
        if (f != HFError) return f;
    }
    return HFError;
}
*/

void ReadParagraphProperties(QIStream& in, HTMLStackItem& item)
{
    SkipSpaces(in);
    char c = in.get();
    in.unget();
    while (c != '>')
    {
        RString name = ReadPropertyName(in);
        if (name.GetLength() == 0)
        {
            LOG_DEBUG(UI, "SYNTAX ERROR");
            break;
        }
        RString value = ReadPropertyValue(in);
        if (stricmp(name, "align") == 0)
        {
            if (stricmp(value, "left") == 0)
            {
                item.align = HALeft;
            }
            else if (stricmp(value, "center") == 0)
            {
                item.align = HACenter;
            }
            else if (stricmp(value, "right") == 0)
            {
                item.align = HARight;
            }
        }
        SkipSpaces(in);
        c = in.get();
        in.unget();
    }
}

void ReadTableProperties(QIStream& in, HTMLStackItem& item, float pageWidth)
{
    SkipSpaces(in);
    char c = in.get();
    in.unget();
    while (c != '>')
    {
        RString name = ReadPropertyName(in);
        if (name.GetLength() == 0)
        {
            LOG_DEBUG(UI, "SYNTAX ERROR");
            break;
        }
        RString value = ReadPropertyValue(in);
        if (stricmp(name, "width") == 0)
        {
            float w = atoi(value);
            int len = value.GetLength();
            if (len > 0 && value[len - 1] == '%')
            {
                item.width = 0.01 * w * pageWidth;
            }
            else
            {
                item.width = w * (1.0 / 640.0);
            }
        }
        else if (stricmp(name, "align") == 0)
        {
            if (stricmp(value, "left") == 0)
            {
                item.align = HALeft;
            }
            else if (stricmp(value, "center") == 0)
            {
                item.align = HACenter;
            }
            else if (stricmp(value, "right") == 0)
            {
                item.align = HARight;
            }
        }
        else if (stricmp(name, "size") == 0)
        {
            switch (atoi(value))
            {
                case 0:
                    item.format = HFP;
                    break;
                case 1:
                    item.format = HFH1;
                    break;
                case 2:
                    item.format = HFH2;
                    break;
                case 3:
                    item.format = HFH3;
                    break;
                case 4:
                    item.format = HFH4;
                    break;
                case 5:
                    item.format = HFH5;
                    break;
                case 6:
                    item.format = HFH6;
                    break;
                default:
                    Fail("Size");
                    break;
            }
        }
        SkipSpaces(in);
        c = in.get();
        in.unget();
    }
}

static RString GetAlignment(HTMLAlign align)
{
    switch (align)
    {
        case HALeft:
            return "left";
        case HACenter:
            return "center";
        case HARight:
            return "right";
        default:
            return "unknown";
    }
}

void CHTMLContainer::Load(const char* filename, bool add)
{
    if (!QIFStreamB::FileExist(filename))
    {
        // try to find alternate location
        RString fullname = Poseidon::GetMissionDirectory() + _filename;
        if (QIFStreamB::FileExist(fullname))
        {
            filename = _filename = fullname;
        }
        else
        {
            fullname = Poseidon::GetBaseDirectory() + _filename;
            if (QIFStreamB::FileExist(fullname))
            {
                filename = _filename = fullname;
            }
            else
            {
                return;
            }
        }
    }

    // Slurp the whole file. Mission/overview HTML is small (a few KB) and we
    // need an in-memory copy anyway to transcode legacy codepages to UTF-8
    // before parsing — the parser appends to RString text segments and would
    // otherwise leak raw W1250/W1251 bytes into the UTF-8 string pool, causing
    // the modern font renderer to draw garbage glyphs.
    //
    // Encoding is decided as follows:
    //   - filename ends with ".utf8.html" → input is already UTF-8
    //   - otherwise → use Poseidon::CodepageForLanguage(GLanguage)
    //                 (CP1250 for Czech, CP1252 for Western, CP1251 for Russian, ...)
    std::string utf8Buf;
    {
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
    }

    // Expand `$STR_*` / `@KEY` tokens against the global stringtable
    // so any HTML can carry remaster localization markers — currently
    // used by the briefing equipment library (a single
    // `equipment.utf8.html` resolves all 8 UI languages via
    // `STRINGTABLE_EQUIPMENT.utf8.csv`).  Token-free HTML is a
    // no-op: tokens that don't resolve are left in place verbatim.
    const RString expanded = ExpandMissionHtmlStringtableTokens(RString(utf8Buf.c_str()), RString());

    LoadBuffer(filename, (const char*)expanded, add);
}

void CHTMLContainer::LoadBuffer(const char* filenameHint, const char* utf8Text, bool add)
{
    if (!add)
    {
        Init(); // clear current content
    }
    _filename = filenameHint ? filenameHint : "";

    std::string utf8Buf = utf8Text ? utf8Text : "";

    QIStream in;
    in.init(utf8Buf.data(), static_cast<int>(utf8Buf.size()));

    HTMLStackItem item(HTMLNone, HALeft, HFP);
    HTMLStack stack;
    RString href = "";
    int section = -1;
    bool bottom = false;
    int c;
    while (true)
    {
        I_AM_ALIVE();

        c = in.get();
        if (in.eof() || in.fail())
        {
            break;
        }

        if (c == '<')
        {
            // tag
            RString tag = ReadTag(in);
            switch (item.context)
            {
                case HTMLNone:
                    if (stricmp(tag, "html") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLHTML;
                    }
                    break;
                case HTMLHTML:
                    if (stricmp(tag, "/html") == 0)
                    {
                        item = stack.Pop();
                    }
                    else if (stricmp(tag, "head") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLHead;
                    }
                    else if (stricmp(tag, "body") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLBody;
                        if (section >= 0)
                        {
                            FormatSection(section);
                        }
                        section = AddSection();
                        bottom = false;
                    }
                    break;
                case HTMLHead:
                    if (stricmp(tag, "/head") == 0)
                    {
                        item = stack.Pop();
                    }
                    break;
                case HTMLBody:
                    if (stricmp(tag, "/body") == 0)
                    {
                        item = stack.Pop();
                    }
                    else if (stricmp(tag, "p") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLP;
                        item.format = HFP;
                        ReadParagraphProperties(in, item);
                    }
                    else if (stricmp(tag, "address") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLAddr;
                        bottom = true;
                        ReadParagraphProperties(in, item);
                    }
                    else if (stricmp(tag, "h1") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLH1;
                        item.format = HFH1;
                        ReadParagraphProperties(in, item);
                    }
                    else if (stricmp(tag, "h2") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLH2;
                        item.format = HFH2;
                        ReadParagraphProperties(in, item);
                    }
                    else if (stricmp(tag, "h3") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLH3;
                        item.format = HFH3;
                        ReadParagraphProperties(in, item);
                    }
                    else if (stricmp(tag, "h4") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLH4;
                        item.format = HFH4;
                        ReadParagraphProperties(in, item);
                    }
                    else if (stricmp(tag, "h5") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLH5;
                        item.format = HFH5;
                        ReadParagraphProperties(in, item);
                    }
                    else if (stricmp(tag, "h6") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLH6;
                        item.format = HFH6;
                        ReadParagraphProperties(in, item);
                    }
                    else if (stricmp(tag, "table") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLTable;
                    }
                    else if (stricmp(tag, "hr") == 0)
                    {
                        if (section >= 0)
                        {
                            FormatSection(section);
                        }
                        section = AddSection();
                        bottom = false;
                    }
                    else
                    {
                        goto General;
                    }
                    break;
                case HTMLP:
                    if (stricmp(tag, "/p") == 0)
                    {
                        item = stack.Pop();
                        AddBreak(section, bottom);
                    }
                    else
                    {
                        goto Paragraph;
                    }
                    break;
                case HTMLAddr:
                    if (stricmp(tag, "/address") == 0)
                    {
                        item = stack.Pop();
                        AddBreak(section, bottom);
                    }
                    else
                    {
                        goto Paragraph;
                    }
                    break;
                case HTMLH1:
                    if (stricmp(tag, "/h1") == 0)
                    {
                        item = stack.Pop();
                        AddBreak(section, bottom);
                    }
                    else
                    {
                        goto Paragraph;
                    }
                    break;
                case HTMLH2:
                    if (stricmp(tag, "/h2") == 0)
                    {
                        item = stack.Pop();
                        AddBreak(section, bottom);
                    }
                    else
                    {
                        goto Paragraph;
                    }
                    break;
                case HTMLH3:
                    if (stricmp(tag, "/h3") == 0)
                    {
                        item = stack.Pop();
                        AddBreak(section, bottom);
                    }
                    else
                    {
                        goto Paragraph;
                    }
                    break;
                case HTMLH4:
                    if (stricmp(tag, "/h4") == 0)
                    {
                        item = stack.Pop();
                        AddBreak(section, bottom);
                    }
                    else
                    {
                        goto Paragraph;
                    }
                    break;
                case HTMLH5:
                    if (stricmp(tag, "/h5") == 0)
                    {
                        item = stack.Pop();
                        AddBreak(section, bottom);
                    }
                    else
                    {
                        goto Paragraph;
                    }
                    break;
                case HTMLH6:
                    if (stricmp(tag, "/h6") == 0)
                    {
                        item = stack.Pop();
                        AddBreak(section, bottom);
                    }
                    else
                    {
                        goto Paragraph;
                    }
                    break;
                case HTMLTable:
                    if (stricmp(tag, "/table") == 0)
                    {
                        item = stack.Pop();
                    }
                    else if (stricmp(tag, "tr") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLTR;
                    }
                    break;
                case HTMLTR:
                    if (stricmp(tag, "/tr") == 0)
                    {
                        item = stack.Pop();
                        AddBreak(section, bottom);
                    }
                    else if (stricmp(tag, "td") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLTD;
                        ReadTableProperties(in, item, GetPageWidth());
                    }
                    break;
                case HTMLTD:
                    if (stricmp(tag, "/td") == 0)
                    {
                        item = stack.Pop();
                    }
                    else
                    {
                        goto Paragraph;
                    }
                    break;
                case HTMLA:
                    if (stricmp(tag, "/a") == 0)
                    {
                        item = stack.Pop();
                        href = "";
                    }
                    else
                    {
                        goto General;
                    }
                case HTMLB:
                    if (stricmp(tag, "/b") == 0)
                    {
                        item = stack.Pop();
                    }
                    else
                    {
                        goto General;
                    }
                    break;
                Paragraph:
                    if (stricmp(tag, "a") == 0)
                    {
                        stack.Push(item);
                        item.context = HTMLA;
                        SkipSpaces(in);
                        c = in.get();
                        in.unget();
                        while (c != '>')
                        {
                            RString name = ReadPropertyName(in);
                            if (name.GetLength() == 0)
                            {
                                LOG_DEBUG(UI, "SYNTAX ERROR");
                                break;
                            }
                            RString value = ReadPropertyValue(in);
                            if (stricmp(name, "href") == 0)
                            {
                                href = ConvertURL(value);
                            }
                            else if (stricmp(name, "name") == 0)
                            {
                                AddName(section, value);
                            }
                            SkipSpaces(in);
                            c = in.get();
                            in.unget();
                        }
                    }
                    else if (stricmp(tag, "b") == 0)
                    {
                        stack.Push(item);
                        item.bold = true;
                        item.context = HTMLB;
                    }
                    else
                    {
                        goto General;
                    }
                    break;
                General:
                    if (stricmp(tag, "br") == 0)
                    {
                        AddBreak(section, bottom);
                    }
                    else if (stricmp(tag, "img") == 0)
                    {
                        RString image = "";
                        float w = -1.0;
                        float h = -1.0;

                        SkipSpaces(in);
                        c = in.get();
                        in.unget();
                        while (c != '>')
                        {
                            RString name = ReadPropertyName(in);
                            if (name.GetLength() == 0)
                            {
                                LOG_DEBUG(UI, "SYNTAX ERROR");
                                break;
                            }
                            RString value = ReadPropertyValue(in);
                            if (stricmp(name, "src") == 0)
                            {
                                image = value;
                            }
                            else if (stricmp(name, "width") == 0)
                            {
                                w = atoi(value);
                            }
                            else if (stricmp(name, "height") == 0)
                            {
                                h = atoi(value);
                            }
                            SkipSpaces(in);
                            c = in.get();
                            in.unget();
                        }
                        AddImage(section, image, item.align, bottom, w, h, href, RString(), item.width);
                    }
                    break;
            }
            SkipTag(in);
        }
        else
        {
            // text
            in.unget();
            RString text = ReadText(in, _fontP->GetLangID());

            // Make sure newly-converted codepoints are in UTF-8 representation
            const Poseidon::Codepage cp = Poseidon::CodepageForLanguage((const char*)GLanguage);
            text = Poseidon::DecodeLegacyTextToRString(text.Data(), cp);

            switch (item.context)
            {
                case HTMLNone:
                case HTMLHTML:
                case HTMLHead:
                case HTMLBody:
                    // ignore text
                    break;
                case HTMLP:
                case HTMLAddr:
                case HTMLH1:
                case HTMLH2:
                case HTMLH3:
                case HTMLH4:
                case HTMLH5:
                case HTMLH6:
                case HTMLTD:
                case HTMLA:
                case HTMLB:
                    AddText(section, text, item.format, item.align, bottom, item.bold, href, item.width);
                    break;
            }
        }
    }
    if (section >= 0)
    {
        FormatSection(section);
    }

    // Briefing HTML can carry unbalanced tags (e.g. an unclosed <a> from a truncated
    // stringtable entry); the dangling tag absorbs trailing markup but the sections
    // parsed so far still render. Warn rather than abort.
    if (item.context != HTMLNone || stack.Size() != 0)
    {
        LOG_WARN(UI, "Malformed briefing HTML '{}': unbalanced tags (context {}, {} open)", (const char*)_filename,
                 (int)item.context, stack.Size());
    }

    for (int i = 0; i < _sections.Size(); i++)
    {
        _sections[i].fields.Compact();
        _sections[i].names.Compact();
        _sections[i].rows.Compact();
    }
    _sections.Compact();
    _bookmarks.Compact();
}

void CHTMLContainer::RemoveSection(int s)
{
    _sections.Delete(s);
}

void CHTMLContainer::CopySection(int from, int to)
{
    if (from < 0 || from >= _sections.Size())
    {
        return;
    }
    if (to < 0 || to >= _sections.Size())
    {
        return;
    }

    HTMLSection& src = _sections[from];
    HTMLSection& dest = _sections[to];

    for (int i = 0; i < src.fields.Size(); i++)
    {
        HTMLField& fld = src.fields[i];
        int index = dest.fields.Add(fld);
        dest.fields[index].indent += _indent;
    }
}

void CHTMLContainer::FormatSection(int s)
{
    float maxLineWidth = GetPageWidth();
    float minHeight = _sizeP;

    HTMLSection& section = _sections[s];
    section.rows.Clear();

    int r = section.rows.Add();
    HTMLRow* row = &section.rows[r];
    row->firstField = 0;
    row->firstPos = 0;
    row->width = 0;
    row->align = HALeft;
    row->height = minHeight;

    HTMLFormat format = HFP;
    bool bold = false;
    Font* font = _fontP;
    float size = _sizeP;

    bool bottom = false;

    int n = section.fields.Size();
    for (int f = 0; f < n; f++)
    {
        HTMLField& field = section.fields[f];
        float lineWidth = maxLineWidth - field.indent;
        if (field.bottom)
        {
            bottom = true;
        }
        if (field.nextline)
        {
            row->lastField = f + 1;
            row->lastPos = 0;
            row->bottom = bottom;
            r = section.rows.Add();
            row = &section.rows[r];
            row->firstField = f + 1;
            row->firstPos = 0;
            row->width = 0;
            row->align = HALeft;
            row->height = minHeight;
        }
        else if (field.format == HFImg)
        {
            if (row->width > 0 && row->width + field.width > lineWidth)
            {
                row->lastField = f;
                row->lastPos = 0;
                row->bottom = bottom;
                r = section.rows.Add();
                row = &section.rows[r];
                row->firstField = f;
                row->firstPos = 0;
                row->width = 0;
                row->align = HALeft;
                row->height = minHeight;
            }
            float height = field.height;
            if (field.text.GetLength() > 0)
            {
                height += _sizeP;
            }
            saturateMax(row->height, height);
            row->width += field.width;
            row->align = field.align;
            if (field.exclude)
            {
                row->lastField = f + 1;
                row->lastPos = 0;
                row->bottom = bottom;
                row->height = 0;
                r = section.rows.Add();
                row = &section.rows[r];
                row->firstField = f + 1;
                row->firstPos = 0;
                row->width = 0;
                row->align = HALeft;
                row->height = minHeight;
            }
        }
        else
        {
            if (field.format != format || field.bold != bold)
            {
                format = field.format;
                bold = field.bold;
                switch (field.format)
                {
                    case HFP:
                        if (bold)
                        {
                            font = _fontPBold;
                        }
                        else
                        {
                            font = _fontP;
                        }
                        size = _sizeP;
                        break;
                    case HFH1:
                        if (bold)
                        {
                            font = _fontH1Bold;
                        }
                        else
                        {
                            font = _fontH1;
                        }
                        size = _sizeH1;
                        break;
                    case HFH2:
                        if (bold)
                        {
                            font = _fontH2Bold;
                        }
                        else
                        {
                            font = _fontH2;
                        }
                        size = _sizeH2;
                        break;
                    case HFH3:
                        if (bold)
                        {
                            font = _fontH3Bold;
                        }
                        else
                        {
                            font = _fontH3;
                        }
                        size = _sizeH3;
                        break;
                    case HFH4:
                        if (bold)
                        {
                            font = _fontH4Bold;
                        }
                        else
                        {
                            font = _fontH4;
                        }
                        size = _sizeH4;
                        break;
                    case HFH5:
                        if (bold)
                        {
                            font = _fontH5Bold;
                        }
                        else
                        {
                            font = _fontH5;
                        }
                        size = _sizeH5;
                        break;
                    case HFH6:
                        if (bold)
                        {
                            font = _fontH6Bold;
                        }
                        else
                        {
                            font = _fontH6;
                        }
                        size = _sizeH6;
                        break;
                    default:
                        Fail("Format");
                        break;
                }
            }
            saturateMax(row->height, size);

            if (field.tableWidth > 0)
            {
                row->width += field.tableWidth;
            }
            else
            {
                float curW = row->width;
                float wordW = curW;
                std::unordered_map<std::string, float> charWidthCache;
                charWidthCache.reserve(64);

                int wordI = 0;
                int n = field.text.GetLength();
                for (int i = 0; i < n;)
                {
                    const int j = i;
                    const int charBytes = HtmlTextWrap::Utf8CharBytes(field.text + i, n - i);
                    PoseidonAssert(charBytes > 0);
                    if (HtmlTextWrap::IsWrapWhitespace(field.text + i, charBytes))
                    {
                        wordI = i + charBytes;
                        wordW = curW;
                    }
                    const RString ch = field.text.Substring(i, i + charBytes);
                    const std::string cacheKey(ch.Data(), ch.GetLength());
                    const auto it = charWidthCache.find(cacheKey);
                    const float cW = it != charWidthCache.end()
                                         ? it->second
                                         : (charWidthCache[cacheKey] = GetTextWidth(size, font, ch));

                    if (curW + cW > lineWidth)
                    {
                        if (wordW > 0)
                        {
                            row->width = wordW;
                            i = wordI;
                        }
                        else
                        {
                            if (curW <= 0)
                            {
                                curW += cW;
                                i += charBytes;
                                continue;
                            }
                            row->width = curW;
                            i = j;
                        }
                        row->align = field.align;
                        row->lastField = f;
                        row->lastPos = i;
                        row->bottom = bottom;
                        r = section.rows.Add();
                        row = &section.rows[r];
                        row->firstField = f;
                        row->firstPos = i;
                        row->width = 0;
                        row->height = floatMax(minHeight, size);
                        row->align = HALeft;

                        curW = 0;
                        wordW = 0;
                        wordI = i;
                    }
                    else
                    {
                        curW += cW;
                        i += charBytes;
                    }
                }
                row->width = curW;
                row->align = field.align;
            }
        }
    }
    row->lastField = section.fields.Size();
    row->lastPos = 0;
    row->bottom = bottom;

    // delete obsolete rows
    for (r = section.rows.Size() - 1; r >= 0; r--)
    {
        if (section.rows[r].firstField >= n)
        {
            section.rows.Delete(r);
        }
    }

    SplitSection(s);
}

void CHTMLContainer::SplitSection(int s)
{
    HTMLSection source = _sections[s];
    if (source.names.Size() < 1)
    {
        return;
    }

    // check if section doesn't fit on one page
    float pageHeight = GetPageHeight();
    int n = source.rows.Size();
    float totalHeight = 0;
    for (int i = 0; i < n; i++)
    {
        HTMLRow& row = source.rows[i];
        totalHeight += row.height;
    }
    if (totalHeight <= pageHeight)
    {
        return;
    }

    RString name = source.names[0];
    char buffer[256];

    RemoveSection(s);

    float rowHeight = _sizeP;
    float imgRowHeight = 1.5f * rowHeight;
    float imgHeight = 480.0f * imgRowHeight;
    pageHeight -= 2.0f * rowHeight + imgRowHeight;

    float curHeight = 0;
    int page = 0;

    s = _sections.Add();
    HTMLSection* dest = &_sections[s];
    dest->names.Add(name);
    snprintf(buffer, sizeof(buffer), "%s/%d", (const char*)name, page);
    dest->names.Add(buffer);

    int firstField = 0;
    for (int i = 0; i < n; i++)
    {
        HTMLRow& row = source.rows[i];
        curHeight += row.height;
        // keep line with height == 0 with the next page
        float checkHeight = curHeight;
        if (row.height == 0 && i + 1 < n)
        {
            checkHeight += source.rows[i + 1].height;
        }
        // next page?
        if (checkHeight > pageHeight)
        {
            // complete old page
            int lastField = source.rows[i - 1].lastField;
            for (int j = firstField; j <= lastField; j++)
            {
                if (j >= source.fields.Size())
                {
                    break;
                }
                dest->fields.Add(source.fields[j]);
            }
            // add link to prev / next
            int f1 = dest->fields.Size();
            {
                // empty line
                AddBreak(s, true);
                int r = dest->rows.Add();
                HTMLRow& rowDest = dest->rows[r];
                rowDest.firstField = f1;
                rowDest.firstPos = 0;
                f1 = dest->fields.Size();
                rowDest.lastField = f1;
                rowDest.lastPos = 0;
                rowDest.height = rowHeight;
                rowDest.width = 0;
                rowDest.bottom = true;
            }
            {
                // empty line
                AddBreak(s, true);
                int r = dest->rows.Add();
                HTMLRow& rowDest = dest->rows[r];
                rowDest.firstField = f1;
                rowDest.firstPos = 0;
                f1 = dest->fields.Size();
                rowDest.lastField = f1;
                rowDest.lastPos = 0;
                rowDest.height = rowHeight;
                rowDest.width = 0;
                rowDest.bottom = true;
            }
            if (page > 0)
            {
                // ref to previous page
                snprintf(buffer, sizeof(buffer), "#%s/%d", (const char*)name, page - 1);
                AddImage(s, "sipka_left.paa", HARight, true, -1.0, imgHeight, buffer);
            }
            snprintf(buffer, sizeof(buffer), "#%s/%d", (const char*)name, page + 1);
            AddImage(s, "sipka_right.paa", HARight, true, -1.0, imgHeight, buffer);
            AddBreak(s, true);
            int r = dest->rows.Add();
            HTMLRow& rowDest = dest->rows[r];
            rowDest.firstField = f1;
            rowDest.firstPos = 0;
            f1 = dest->fields.Size();
            rowDest.lastField = f1;
            rowDest.lastPos = 0;
            rowDest.height = imgRowHeight;
            rowDest.width = 0;
            rowDest.bottom = true;
            for (int f = rowDest.firstField; f < rowDest.lastField; f++)
            {
                /*
                                rowDest.width += GEngine->GetTextWidth
                                (
                                    _sizeP, _fontP, dest->fields[f].text
                                );
                */
                rowDest.width += dest->fields[f].width;
            }
            rowDest.align = HARight;
            // new section
            curHeight = row.height;
            page++;
            s = _sections.Add();
            dest = &_sections[s];
            dest->names.Add(name);
            snprintf(buffer, sizeof(buffer), "%s/%d", (const char*)name, page);
            dest->names.Add(buffer);
        }
        int r = dest->rows.Add(row);
        dest->rows[r].firstField -= firstField;
        dest->rows[r].lastField -= firstField;
    }
    // complete last page
    int lastField = source.rows[n - 1].lastField;
    for (int j = firstField; j <= lastField; j++)
    {
        if (j >= source.fields.Size())
        {
            break;
        }
        dest->fields.Add(source.fields[j]);
    }
    // add link to prev
    if (page > 0)
    {
        int f1 = dest->fields.Size();
        {
            // empty line
            AddBreak(s, true);
            int r = dest->rows.Add();
            HTMLRow& rowDest = dest->rows[r];
            rowDest.firstField = f1;
            rowDest.firstPos = 0;
            f1 = dest->fields.Size();
            rowDest.lastField = f1;
            rowDest.lastPos = 0;
            rowDest.height = rowHeight;
            rowDest.width = 0;
            rowDest.bottom = true;
        }
        {
            // empty line
            AddBreak(s, true);
            int r = dest->rows.Add();
            HTMLRow& rowDest = dest->rows[r];
            rowDest.firstField = f1;
            rowDest.firstPos = 0;
            f1 = dest->fields.Size();
            rowDest.lastField = f1;
            rowDest.lastPos = 0;
            rowDest.height = rowHeight;
            rowDest.width = 0;
            rowDest.bottom = true;
        }
        snprintf(buffer, sizeof(buffer), "#%s/%d", (const char*)name, page - 1);
        AddImage(s, "sipka_left.paa", HARight, true, -1.0, imgHeight, buffer);
        AddBreak(s, true);
        int r = dest->rows.Add();
        HTMLRow& rowDest = dest->rows[r];
        rowDest.firstField = f1;
        rowDest.firstPos = 0;
        f1 = dest->fields.Size();
        rowDest.lastField = f1;
        rowDest.lastPos = 0;
        rowDest.height = imgRowHeight;
        rowDest.width = 0;
        rowDest.bottom = true;
        for (int f = rowDest.firstField; f < rowDest.lastField; f++)
        {
            /*
                        rowDest.width += GEngine->GetTextWidth
                        (
                            _sizeP, _fontP, dest->fields[f].text
                        );
            */
            rowDest.width += dest->fields[f].width;
        }
        rowDest.align = HARight;
    }
}

int CHTMLContainer::FindField(float x, float y)
{
    if (_sections.Size() <= 0)
    {
        return -1;
    }
    HTMLSection& section = _sections[_currentSection];

    int r = -1;
    float top = 0;
    bool bottom = false;
    for (int i = 0; i < section.rows.Size(); i++)
    {
        if (!bottom && section.rows[i].bottom)
        {
            bottom = true;
            top = GetPageHeight();
            for (int rr = i; rr < section.rows.Size(); rr++)
            {
                top -= section.rows[rr].height;
            }
            if (top > y)
            {
                return -1;
            }
        }
        top += section.rows[i].height;
        if (top > y)
        {
            r = i;
            break;
        }
    }
    if (r < 0)
    {
        return -1;
    }

    HTMLRow& row = section.rows[r];
    float left;
    switch (row.align)
    {
        case HARight:
            left = GetPageWidth() - row.width;
            break;
        case HACenter:
            left = 0.5 * (GetPageWidth() - row.width);
            break;
        default:
            PoseidonAssert(row.align == HALeft);
            left = 0;
            break;
    }
    if (left > x)
    {
        return -1;
    }
    for (int f = row.firstField; f <= row.lastField; f++)
    {
        if (f >= section.fields.Size())
        {
            continue;
        }
        HTMLField& field = section.fields[f];
        if (field.nextline)
        {
            continue;
        }
        int from = 0;
        if (f == row.firstField)
        {
            from = row.firstPos;
        }
        int to = INT_MAX;
        if (f == row.lastField)
        {
            to = row.lastPos;
        }
        if (from >= to)
        {
            continue;
        }

        if (field.tableWidth > 0)
        {
            left += field.tableWidth;
        }
        else if (field.format == HFImg)
        {
            left += field.width;
        }
        else
        {
            Font* font = _fontP;
            float size = _sizeP;
            switch (field.format)
            {
                case HFP:
                    if (field.bold)
                    {
                        font = _fontPBold;
                    }
                    else
                    {
                        font = _fontP;
                    }
                    size = _sizeP;
                    break;
                case HFH1:
                    if (field.bold)
                    {
                        font = _fontH1Bold;
                    }
                    else
                    {
                        font = _fontH1;
                    }
                    size = _sizeH1;
                    break;
                case HFH2:
                    if (field.bold)
                    {
                        font = _fontH2Bold;
                    }
                    else
                    {
                        font = _fontH2;
                    }
                    size = _sizeH2;
                    break;
                case HFH3:
                    if (field.bold)
                    {
                        font = _fontH3Bold;
                    }
                    else
                    {
                        font = _fontH3;
                    }
                    size = _sizeH3;
                    break;
                case HFH4:
                    if (field.bold)
                    {
                        font = _fontH4Bold;
                    }
                    else
                    {
                        font = _fontH4;
                    }
                    size = _sizeH4;
                    break;
                case HFH5:
                    if (field.bold)
                    {
                        font = _fontH5Bold;
                    }
                    else
                    {
                        font = _fontH5;
                    }
                    size = _sizeH5;
                    break;
                case HFH6:
                    if (field.bold)
                    {
                        font = _fontH6Bold;
                    }
                    else
                    {
                        font = _fontH6;
                    }
                    size = _sizeH6;
                    break;
                default:
                    Fail("Format");
                    break;
            }
            RString text = field.text.Substring(from, to);
            left += GetTextWidth(size, font, text);
        }
        if (left + field.indent > x)
        {
            return f;
        }
    }

    return -1;
}
