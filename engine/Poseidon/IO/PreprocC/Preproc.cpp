#include <Poseidon/IO/PreprocC/Preproc.h>
#include <stdio.h>
#include <Poseidon/Foundation/Framework/Log.hpp>

#define LexItem PreprocLexItem

namespace Poseidon
{
struct LexDef
{
    LexItem item;
    char text[256];
};

static LexDef lexdefs[] = {{lxInclude, "include"},
                           {lxDefine, "define"},
                           {lxIfDef, "ifdef"},
                           {lxIfNDef, "ifndef"},
                           {lxElse, "else"},
                           {lxEndIf, "endif"},
                           {lxLeft, "("},
                           {lxRight, ")"},
                           {lxComma, ","},
                           {lxHash, "#"},
                           {lxNewLine, "\n"},
                           {lxBeginLineComment, "//"},
                           {lxBeginBlockComment, "/*"},
                           {lxLineBreak, "\\\n"},
                           {lxUhozy, "\""},
                           {lxLevaZlomena, "<"},
                           {lxPravaZlomena, ">"},
                           {lx2Hash, "##"},
                           {lxUndef, "undef"}};

static LexItem FindLex(const char* name)
{
    for (unsigned int i = 0; i < sizeof(lexdefs) / sizeof(LexDef); i++)
    {
        if (strcmp(name, lexdefs[i].text) == 0)
        {
            return lexdefs[i].item;
        }
    }
    return lxUnknown;
}

static char validIdChar(char chr, bool firstchar)
{
    if (firstchar)
    {
        return ((chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z') || chr == '_');
    }
    else
    {
        return validIdChar(chr, true) || (chr >= '0' && chr <= '9');
    }
}

static char* scanName(QIStream& in, char* buffer, int size)
{
    int i = in.get();
    int p = 0;
    bool first = true;
    size--;
    while (p < size && validIdChar((char)i, first))
    {
        buffer[p++] = (char)i;
        first = false;
        i = in.get();
    }
    if (i != EOF)
    {
        in.unget();
    }
    buffer[p++] = 0;
    return buffer;
}

static char* scanString(QIStream& in, char* buffer, int size, const char* terminators)
{
    int i = in.get();
    int p = 0;
    size--;
    while (p < size && i != EOF && strchr(terminators, i) == nullptr)
    {
        buffer[p++] = (char)i;
        i = in.get();
    }
    if (i != EOF)
    {
        in.unget();
    }
    buffer[p++] = 0;
    return buffer;
}

static void SkipWhites(QIStream& in)
{
    int i;
    do
    {
        i = in.get();
    } while (i < 33 && i != EOF && i != '\n');
    if (i != EOF)
    {
        in.unget();
    }
}

static LexItem GetNext(QIStream& in, char* buffer, int size)
{
    int i = in.get();
    while (i == 0x0d)
    {
        i = in.get();
    }
    if (i == EOF)
    {
        return lxEof;
    }
    if (validIdChar((char)i, true))
    {
        in.unget();
        scanName(in, buffer, size);
        LexItem it = FindLex(buffer);
        if (it == lxUnknown)
        {
            return lxText;
        }
        else
        {
            return it;
        }
    }
    buffer[0] = i;
    buffer[1] = 0;
    if (i == '\\' || i == '/')
    {
        i = in.get();
        while (i == 0x0d)
        {
            i = in.get();
        }
        if (!validIdChar((char)i, true))
        {
            buffer[1] = i;
            buffer[2] = 0;
            if (FindLex(buffer) == lxUnknown)
            {
                in.unget();
                buffer[1] = 0;
            }
        }
        else
        {
            in.unget();
        }
    }
    else if (i == '#')
    {
        i = in.get();
        if (i != '#')
        {
            in.unget();
        }
        else
        {
            buffer[1] = i;
            buffer[2] = 0;
        }
    }
    LexItem it = FindLex(buffer);
    return it;
}

Preproc::Preproc() : filename("")
{
    out = nullptr;
    maclist = nullptr;
    recurse = 0;
    maxrecurse = 20;
}

void Preproc::MacroParam::SetValue(const char* val)
{
    if (!val)
    {
        val = "";
    }
    if (value)
    {
        free(value);
    }
    value = strDup(val);
}

void Preproc::DefineSymb::AddParam(const char* name, int poradi)
{
    char val[32];
#ifdef _WIN32
    itoa(poradi, val, 10);
#else
    snprintf(val, sizeof(val), "%d", poradi);
#endif
    MacroParam parm(name), por(val);
    por.SetValue(name);
    params.Add(parm);
    params.Add(por);
}

bool Preproc::DefineSymb::SetParam(int poradi, const char* text, MacroParams& parlist)
{
    char val[32];
#ifdef _WIN32
    itoa(poradi, val, 10);
#else
    snprintf(val, sizeof(val), "%d", poradi);
#endif
    MacroParam& parname = params[val];
    if (params.IsNull(parname))
    {
        return false;
    }
    MacroParam newpar(parname.GetValue());
    newpar.SetValue(text);
    parlist.Add(newpar);
    return true;
}

const char* Preproc::DefineSymb::GetParam(const char* name)
{
    const MacroParam& par = params[name];
    if (params.IsNull(par))
    {
        return nullptr;
    }
    return par.GetValue();
}

bool Preproc::Process(QOStream* out, const char* name)
{
    this->out = out;
    curline = 0;
    error = prNoError;
    filename = name;
    QIStream* in = OnEnterInclude(name);
    if (in == nullptr)
    {
        LOG_ERROR(Config, "Cannot include file {}", name);
        error = prStreamOpenError;
        return false;
    }
    item = lxNewLine;
    AtBeginLine(*out);
    bool ret = GlobalScan(*in, out);
    OnExitInclude(in);
    return ret;
}

static int SkipBlockComment(QIStream& in)
{
    int lines = 0;
    int last = 0;
    int i = in.get();
    while (i != EOF && (last != '*' || i != '/'))
    {
        if (i == '\n')
        {
            lines++;
        }
        last = i;
        i = in.get();
    }
    return lines;
}

static void SkipLineComment(QIStream& in)
{
    int i = in.get();
    while (i != EOF && i != '\n')
    {
        i = in.get();
    }
    if (i != EOF)
    {
        in.unget();
    }
}

bool Preproc::GlobalScan(QIStream& in, QOStream* out)
{
    bool uvozovky = false;
    bool ok = true;
    for (;;)
    {
        if (item == lxEof)
        {
            return true;
        }
        if (item == lxUhozy)
        {
            uvozovky = !uvozovky;
        }
        if (uvozovky)
        {
            if (out)
            {
                (*out) << text;
            }
            ReadNext(in);
        }
        else
        {
            if (item == lxNewLine)
            {
                SkipWhites(in);
                ReadNext(in);
                if (item == lxHash)
                {
                    ReadNext(in);
                    SkipWhites(in);
                    switch (item)
                    {
                        case lxInclude:
                            if (out)
                            {
                                ReadNext(in);
                                ok = DoIncludeBlock(in);
                            }
                            break;
                        case lxDefine:
                            if (out)
                            {
                                ReadNext(in);
                                ok = DoDefineBlock(in);
                            }
                            break;
                        case lxIfDef:
                            if (out)
                            {
                                ReadNext(in);
                                ok = DoIfDefBlock(in, true);
                            }
                            break;
                        case lxIfNDef:
                            if (out)
                            {
                                ReadNext(in);
                                ok = DoIfDefBlock(in, false);
                            }
                            break;
                        case lxEndIf:
                        case lxElse:
                            error = prParseExit;
                            ok = false;
                            break;
                        case lxUndef:
                            if (out)
                            {
                                ReadNext(in);
                                ok = DoUndefBlock(in);
                            }
                            break;
                        default:
                            error = prInvalidPreprocessorCommand;
                            ok = false;
                            break;
                    }
                    if (!ok)
                    {
                        return false;
                    }
                }
                else if (out)
                {
                    out->put('\n');
                }
            }
            else if (item == lxBeginBlockComment)
            {
                curline += SkipBlockComment(in);
                ReadNext(in);
            }
            else if (item == lxBeginLineComment)
            {
                SkipLineComment(in);
                ReadNext(in);
            }
            else if (item == lxText)
            {
                if (out)
                {
                    TryExpandMacro(in, *out);
                }
                else
                {
                    ReadNext(in);
                }
            }
            else
            {
                if (out)
                {
                    (*out) << text;
                }
                ReadNext(in);
            }
        }
    }
}

void Preproc::ReadNext(QIStream& in)
{
    item = GetNext(in, text, sizeof(text));
    if (strchr(text, '\n') != nullptr)
    {
        curline++;
        AtBeginLine(*out);
    }
}

bool Preproc::DoIncludeBlock(QIStream& in)
{
    if (item != lxUhozy && item != lxLevaZlomena)
    {
        error = prIncludeError;
        return false;
    }
    const char* del = (item == lxUhozy) ? "\"" : ">";
    scanString(in, text, sizeof(text), del);
    int cline = curline;
    RString p = filename;
    if (Process(out, text))
    {
        ReadNext(in);
        curline = cline;
        filename = p;
        ReadNext(in);
        return true;
    }
    return false;
}

bool Preproc::DoDefineBlock(QIStream& in)
{
    DefineSymb defs(text, "");
    deftable.Add(defs);
    DefineSymb& ddf = deftable[text];
    ReadNext(in);
    if (item == lxLeft)
    {
        ReadDefineParams(in, ddf);
        if (item != lxRight)
        {
            error = prDefineError;
            return false;
        }
        SkipWhites(in);
        ReadNext(in);
    }
    if (text[0] == 32)
    {
        SkipWhites(in);
        ReadNext(in);
    }
    ReadDefineText(in, ddf);
    ddf.Unblock();
    return true;
}

Preproc::DefineSymb* Preproc::CreateExpandMacro(QIStream& in, MacTableList** params)
{
    DefineSymb& smb = deftable[text];
    if (deftable.IsNull(smb) || smb.Blocked())
    {
        return nullptr;
    }
    ReadNext(in);
    *params = nullptr;
    if (item == lxLeft)
    {
        int p = 0;
        *params = new MacTableList();
        ReadNext(in);
        RString g;
        bool end;
        do
        {
            end = !LoadMacroParam(g, in);
            if (!smb.SetParam(p++, g.Data(), **params))
            {
                error = prToManyParameters;
                delete *params;
                return nullptr;
            }
        } while (!end);
        if (smb.SetParam(p, "", **params))
        {
            error = prToFewParameters;
            delete *params;
            return nullptr;
        }
    }
    return &smb;
}

bool Preproc::LoadMacroParam(RString& p, QIStream& in)
{
    error = prNoError;
    int zavorka = 0;
    bool uvozovky = false;
    QOStream out;
    for (;;)
    {
        switch (item)
        {
            case lxLeft:
                if (!uvozovky)
                {
                    zavorka++;
                }
                out << text;
                break;
            case lxRight:
                if (!uvozovky)
                {
                    zavorka--;
                }
                if (zavorka < 0)
                {
                    ReadNext(in);
                    out.put(0);
                    p = out.str();
                    return false;
                }
                else
                {
                    out << text;
                }
                break;
            case lxUhozy:
                uvozovky = !uvozovky;
                out << text;
                break;
            case lxComma:
                if (zavorka == 0 && !uvozovky)
                {
                    ReadNext(in);
                    out.put(0);
                    p = out.str();
                    return true;
                }
                break;
            case lxEof:
                error = prUnexceptedEndOfFile;
                return false;
            case lxText:
                if (!uvozovky)
                {
                    if (!TryExpandMacro(in, out))
                    {
                        return false;
                    }
                    continue;
                }
                else
                {
                    out << text;
                }
                break;
            default:
                out << text;
        }
        ReadNext(in);
    }
}

void Preproc::ReadDefineParams(QIStream& str, DefineSymb& def)
{
    SkipWhites(str);
    ReadNext(str);
    int count = 0;
    while (item == lxText)
    {
        def.AddParam(text, count);
        count++;
        SkipWhites(str);
        ReadNext(str);
        if (item == lxComma)
        {
            SkipWhites(str);
            ReadNext(str);
        }
    }
}

void Preproc::ReadDefineText(QIStream& str, DefineSymb& def)
{
    QOStream out;
    while (item != lxNewLine && item != lxEof)
    {
        if (item == lxBeginLineComment)
        {
            SkipLineComment(str);
        }
        else if (item == lxBeginBlockComment)
        {
            curline += SkipBlockComment(str);
        }
        else if (item != lxLineBreak)
        {
            out << text;
        }
        ReadNext(str);
    }
    out.put(0);
    def.SetValue(out.str());
}

bool Preproc::TryExpandMacro(QIStream& in, QOStream& out)
{
    if (maclist)
    {
        const MacroParam& par = maclist->GetFromList(text);
        if (maclist->NotNull(par))
        {
            out << par.GetValue();
            ReadNext(in);
            return true;
        }
    }
    LexItem last = lxUnknown;
    MacTableList* params; // parametry tohoto makra
    DefineSymb* fnd = CreateExpandMacro(in, &params);
    bool uvozovky = false;
    if (fnd == nullptr)
    {
        out << text;
        ReadNext(in);
        return error == prNoError;
    }
    if (params)
    {
        if (maclist)
            maclist = maclist->Add(params);
        else
        {
            params->next = nullptr;
            maclist = params;
        }
    }
    fnd->Block();
    const char* expand = fnd->GetValue();
    QIStream local(expand, strlen(expand));
    LexItem it = item;
    char tt[128];
    strncpy(tt, text, 128);
    ReadNext(local);
    while (item != lxEof)
    {
        if (item == lxUhozy)
        {
            uvozovky = !uvozovky;
        }
        if (uvozovky)
        {
            out << text;
            ReadNext(local);
        }
        else if (item == lxText)
        {
            LexItem itsave = item;
            if (last == lxHash)
            {
                out.put('"');
            }
            TryExpandMacro(local, out);
            if (last == lxHash)
            {
                out.put('"');
            }
            last = itsave;
        }
        else
        {
            if (item != lx2Hash && item != lxHash)
            {
                out << text;
            }
            last = item;
            ReadNext(local);
        }
    }
    item = it;
    strncpy(text, tt, 128);
    fnd->Unblock();
    if (params)
    {
        maclist = maclist->Remove();
    }
    return error == prNoError;
}

bool Preproc::DoIfDefBlock(QIStream& in, bool cond)
{
    if (item != lxText)
    {
        error = prUnexceptedSymbol;
        return false;
    }
    DefineSymb& def = deftable[text];
    bool skip = deftable.IsNull(def);
    if (!cond)
    {
        skip = !skip;
    }
    ReadNext(in);
    if (skip)
    {
        GlobalScan(in, nullptr);
    }
    else
    {
        GlobalScan(in, out);
    }
    if (error != prParseExit)
    {
        return false;
    }
    if (item == lxElse)
    {
        ReadNext(in);
        if (skip)
        {
            GlobalScan(in, out);
        }
        else
        {
            GlobalScan(in, nullptr);
        }
        if (error != prParseExit)
        {
            return false;
        }
    }
    if (item != lxEndIf)
    {
        error = prEndIfExcepted;
        return false;
    }
    ReadNext(in);
    return true;
}

bool Preproc::DoUndefBlock(QIStream& str)
{
    if (item != lxText)
    {
        error = prUnexceptedSymbol;
        return false;
    }
    deftable.Remove(text);
    ReadNext(str);
    return true;
}

} // namespace Poseidon
