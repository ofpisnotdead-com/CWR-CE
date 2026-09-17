#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Streams/SerializeBin.hpp>
#include <Poseidon/Foundation/Framework/LogFlags.hpp>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Poseidon
{
#include <Poseidon/IO/ParamFile/ParamFilePrivate.inc>

ParamFile::ParamFile()
{
    _vars = nullptr;
}

ParamFile::~ParamFile()
{
    Clear();
    DeleteVariables();
}

#define DIAG_OPEN 0

#if DIAG_OPEN
static int ParamFileOpen = 0;
#endif

struct ParamFileContext
{
    // string pool
    FindArray<RStringB> _strings;
    int _version; // load - backward compatibility

    // transfer name
    void TransferString(SerializeBinStream& f, RStringB& string);
    void TransferInt(SerializeBinStream& f, int& a);
    void TransferIndex(SerializeBinStream& f, int& a, int verEncode = 3);
};

void ParamFileContext::TransferIndex(SerializeBinStream& f, int& a, int verEncode)
{
    if (_version >= verEncode)
    {
        // index encoded
        // we expect for most cfg files 2B should be enough
        TransferInt(f, a);
    }
    else
    {
        // plain string index
        f.Transfer(a);
    }
}

void ParamFileContext::TransferString(SerializeBinStream& f, RStringB& string)
{
    if (f.IsSaving())
    {
        // check if name is already in table
        RStringB stringB = (const char*)string;
        int index = _strings.Find(stringB);
        if (index >= 0)
        {
            // already there - transfer only index
            TransferIndex(f, index);
        }
        else
        {
            // transfer new index and string defition
            RStringB stringB = (const char*)string;
            index = _strings.Add(stringB);
            TransferIndex(f, index);
            f.Transfer(stringB);
        }
    }
    else
    {
        // transfer index
        int index = -1;
        TransferIndex(f, index);
        if (index < 0)
        {
            f.SetError(f.EFileStructure);
            return;
        }
        if (index < _strings.Size())
        {
            // old string - use it
            string = (const char*)(_strings[index]);
        }
        else
        {
            // new string - define and use it. A new string's index must be the next
            // sequential slot; a wire index past the end of the table is malformed and
            // would grow _strings to index+1 (OOM) via Access(index). The assert that
            // documented this invariant compiles out under NDEBUG (the shipping save
            // loader), so enforce it explicitly.
            if (index != _strings.Size())
            {
                f.SetError(f.EFileStructure);
                return;
            }
            RStringB stringB;
            f.Transfer(stringB);
            _strings.Access(index);
            _strings[index] = stringB;
            string = (const char*)stringB;
        }
    }
}

void ParamFileContext::TransferInt(SerializeBinStream& f, int& a)
{
    // encoded integer (dynamic byte length)
    if (f.IsLoading())
    {
        unsigned int val = 0;
        int offset = 0;
        while (f.GetError() == f.EOK)
        {
            // A continuation-bit byte (0x80) with nothing behind it would spin forever:
            // QIStream::get() at EOF returns -1, i.e. 0xFF as a char, so the terminator
            // bit is never seen. Stop at end-of-stream, and cap the width at a 32-bit int
            // (a 5th group already reaches offset 28; past that the shift is out of range).
            // Both mark the varint malformed.
            if (f.GetRest() <= 0 || offset >= 32)
            {
                f.SetError(f.EFileStructure);
                break;
            }
            unsigned char c = f.LoadChar();
            // transfer 7 bits ber byte. Unsigned shift: (c & 0x7f) is int and at
            // offset 28 a high byte overflows int (UB) even though offset is < 32.
            val |= (unsigned)(c & 0x7f) << offset;
            // check terminator
            if ((c & 0x80) == 0)
            {
                // extend MSB?
                break;
            }
            offset += 7;
        }
        a = val;
    }
    else
    {
        unsigned int val = a;
        for (;;)
        {
            unsigned char c = val & 0x7f;
            val >>= 7;
            // check MSB?
            if (val)
            {
                f.SaveChar(c | 0x80);
            }
            else
            {
                // no more bits left
                f.SaveChar(c);
                break;
            }
        }
    }
}

void ParamFile::Clear()
{
#if DIAG_OPEN
    if (_entries.Size() > 0)
    {
        LOG_DEBUG(Core, "{}: Clear paramfile {}", ParamFileOpen, (const char*)GetName());
        --ParamFileOpen;
    }
#endif
    PoseidonAssert(!_parent);
    PoseidonAssert(!_base);
    _entries.Clear();
    _name = Foundation::RStringBEmpty;
}

#define OUTPUT_PREPROC 0

LSError ParamFile::ParsePlainText(const char* name)
{
    SetName(name);
    if (!QIFStreamB::FileExist(name))
    {
        return LSOK;
    }

    QOStream out;
    if (!Preprocess(out, name))
    {
        return LSStructure;
    }
    QIStream in;
    in.init(out.str(), out.pcount());
    ParsePlainText(in);
#if DIAG_OPEN
    if (_entries.Size() > 0)
    {
        ParamFileOpen++;
        LOG_DEBUG(Core, "{}: Parsed paramfile {}", ParamFileOpen, name);
    }
#endif
    return in.fail() ? in.error() : LSOK;
}

void ParamFile::ParsePlainText(QIStream& in)
{
    int c;
    // parse section content
    int number = 1;
    for (;;)
    {
        c = in.get();
        while (isspace(c))
        {
            c = in.get();
        }
        if (in.eof() || in.fail())
        {
            break;
        }
        in.unget();
        WordBuf word;
        static const char term[] = " \t\r\n";
        GetWord(word, sizeof(word), in, term);
        // word is entry value
        // get termination character (it should be one of term)
        c = in.get();
        PoseidonAssert(strchr(term, c));

        BString<256> nameBuf;
        // entry name is autogenerated
        sprintf(nameBuf, "Line%d", number++);
        RStringB valueName = (const char*)nameBuf;
        ParamEntry* value = nullptr;
        {
            value = new ParamValue(valueName);
            value->SetValue(word);
        }

        SRef<ParamEntry> newEntry = value;
        // check for overload
        if (newEntry)
        {
            int baseIndex = FindIndex(newEntry->GetName());
            if (baseIndex < 0)
            {
                _entries.Add(newEntry);
            }
            else
            {
                ErrorMessage("%s: Member already defined.", (const char*)GetContext(newEntry->GetName()));
            }
        }
    }

    _entries.Compact();
}

LSError ParamFile::Parse(const char* name)
{
    SetName(name);

    // Log full absolute path of file being parsed
#ifdef _WIN32
    char fullPath[512];
    ::GetFullPathNameA(name, sizeof(fullPath), fullPath, nullptr);
#else
    // realpath() may write up to PATH_MAX bytes into the buffer
    char fullPath[PATH_MAX];
    if (!realpath(name, fullPath))
    {
        strncpy(fullPath, name, sizeof(fullPath) - 1);
        fullPath[sizeof(fullPath) - 1] = '\0';
    }
#endif
    LOG_DEBUG(Core, "ParamFile::Parse() - Loading config file: {}", fullPath);

    if (!QIFStreamB::FileExist(name))
    {
        LOG_DEBUG(Core, "ParamFile::Parse() - File does not exist: {}", fullPath);
        return LSOK;
    }

#if OUTPUT_PREPROC
    QOFStream out;
#else
    QOStream out;
#endif
    if (!Preprocess(out, name))
    {
        return LSStructure;
    }
#if OUTPUT_PREPROC
    ::DeleteFile("bin/output.txt");
    out.export("bin/output.txt");
#endif
    QIStream in;
    in.init(out.str(), out.pcount());
    Parse(in);
#if DIAG_OPEN
    if (_entries.Size() > 0)
    {
        ParamFileOpen++;
        LOG_DEBUG(Core, "{}: Parsed paramfile {}", ParamFileOpen, name);
    }
#endif
    return in.fail() ? in.error() : LSOK;
}

void Indent(QOStream& f, int indent)
{
    while (--indent >= 0)
    {
        f << "\t";
    }
}

inline bool IsNumerical(const char* strValue, float* ret = nullptr)
{
    char* endptr = nullptr;
    float fValue = strtod(strValue, &endptr);
    if (ret)
    {
        *ret = fValue;
    }
    return (*strValue != 0 && *endptr == 0);
}

void ParamRawValue::Save(QOStream& f, int indent) const
{
    const char* strValue = _value;
    f << "\"";
    while (*strValue)
    {
        if (*strValue == '"')
        {
            f << "\"\"";
        }
        else
        {
            f.put(*strValue);
        }
        strValue++;
    }
    f << "\"";
}

void ParamRawValue::SerializeBin(SerializeBinStream& f)
{
    // string are very likely to be class names
    // and there is high probability they can be reused
    ParamFileContext* context = (ParamFileContext*)f.GetContext();
    context->TransferString(f, _value);
}

void ParamRawValueFloat::Save(QOStream& f, int indent) const
{
    // Keep the familiar %f form when it already round-trips the 32-bit value; otherwise fall back to
    // %.9g (FLT_DECIMAL_DIG — guaranteed exact) so debin -> bin doesn't drop precision (e.g. plain
    // "%f" turns 3.16228e-05 into "0.000032"). This keeps clean values clean and only widens the lossy ones.
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%f", _value);
    if ((float)atof(buffer) != _value)
    {
        snprintf(buffer, sizeof(buffer), "%.9g", _value);
        // A bare integer literal (e.g. "5") would re-parse as int; keep the float type.
        if (!strpbrk(buffer, ".eEnN") && strlen(buffer) + 2 < sizeof(buffer))
        {
            strcat(buffer, ".0");
        }
    }
    f << buffer;
}

RStringB ParamRawValueFloat::GetValue() const
{
    BString<256> buf;
    sprintf(buf, "%g", _value);
    return (const char*)buf;
}

void ParamRawValueFloat::SerializeBin(SerializeBinStream& f)
{
    f.Transfer(_value);
}

void ParamRawValueFloat::CalculateCheckValue(PASumCalculator& sum) const
{
    ParamFile::AddCRC(sum, &_value, sizeof(_value));
}

void ParamRawValueInt::SerializeBin(SerializeBinStream& f)
{
    // before version 3 integers were not encoded
    f.Transfer(_value);
}

void ParamRawValueInt::CalculateCheckValue(PASumCalculator& sum) const
{
    ParamFile::AddCRC(sum, &_value, sizeof(_value));
}

void ParamRawValueInt::Save(QOStream& f, int indent) const
{
    BString<256> buffer;
    sprintf(buffer, "%d", _value);
    f << buffer;
}

RStringB ParamRawValueInt::GetValue() const
{
    BString<256> buf;
    sprintf(buf, "%d", _value);
    return (const char*)buf;
}

void ParamRawArray::Parse(QIStream& in, ParamFile* file)
{
    int c = in.get();
    while (isspace(c))
    {
        c = in.get();
    }
    if (c != '{')
    {
        ErrorMessage("Config: '%c' encountered instead of '{'", c);
        return;
    }
    for (;;)
    {
        // check for sub-array
        c = in.get();
        while (isspace(c))
        {
            c = in.get();
        }
        in.unget();
        if (c == '{')
        {
            // sub-array
            Ref<ParamArrayValueArray> sub = new ParamArrayValueArray();
            IParamArrayValue* val = sub;
            sub->Parse(in, file);
            _value.Add(val);
            c = in.get();
        }
        else
        {
            WordBuf word;
            bool someWord = GetWord(word, sizeof(word), in, ",;}");
            c = in.get();
            // c may be one of ,;} or \n or \r
            if (c == ',' || c == ';' || someWord)
            {
                if (strncmp(word, "__EVAL", 6) == 0)
                {
                    ::strcpy(word, file->EvaluateStringInternal(word + 6));
                }
                else if (word[0] == '(')
                {
                    // Implicit arithmetic — array elements like
                    // `position[]={0, (-A - B), 0.3};` evaluate inline.
                    ::strcpy(word, file->EvaluateStringInternal(word));
                }

                // autodetect value type
                bool ok = false;
                int val = ScanInt(word, ok);
                if (ok)
                {
                    AddValue(val);
                }
                else
                {
                    float val = ScanFloat(word, ok);
                    if (ok)
                    {
                        AddValue(val);
                    }
                    else
                    {
                        AddValue(word);
                    }
                }
            }
        } // if subarray else
        if (c < 0)
        {
            ErrorMessage("EOF encountered.");
            return;
        }
        while (isspace(c))
        {
            c = in.get();
        }
        if (c == '}')
        {
            break; // array terminated
        }
        // , or ; should follow
        if (c != ',' && c != ';')
        {
            ErrorMessage("Config: '%c' encountered instead of ','", c);
            return;
        }
    }
    c = in.get();
    while (isspace(c))
    {
        c = in.get();
    }
    in.unget();
    Compact();
}

void ParamRawArray::Save(QOStream& f, int indent) const
{
    // check if some item is string
    // (item may be numerical or string)
    bool someString = false;
    for (int i = 0; i < _value.Size(); i++)
    {
        if (dynamic_cast<ParamArrayValueArray*>(_value[i].GetRef()) || !IsNumerical(_value[i]->GetValue()))
        {
            someString = true;
            break;
        }
    }
    if (someString)
    {
        // array of string values (at least one string)
        f << "\r\n";
        Indent(f, indent);
        f << "{\r\n";
        for (int i = 0; i < _value.Size(); i++)
        {
            Indent(f, indent + 1);
            _value[i]->Save(f, indent);
            if (i < _value.Size() - 1)
            {
                f << ",";
            }
            f << "\r\n";
        }
        Indent(f, indent);
        f << "}";
    }
    else
    {
        // array of numeric values
        f << "{";
        for (int i = 0; i < _value.Size(); i++)
        {
            _value[i]->Save(f, indent);
            if (i < _value.Size() - 1)
            {
                f << ",";
            }
        }
        f << "}";
    }
}

void ParamRawArray::SerializeBin(SerializeBinStream& f)
{
    ParamFileContext* context = (ParamFileContext*)f.GetContext();
    // make table of names
    if (f.IsSaving())
    {
        int n = _value.Size();
        context->TransferIndex(f, n, 4);
        for (int i = 0; i < n; i++)
        {
            _value[i]->SerializeBin(f);
        }
    }
    else
    {
        PoseidonAssert(f.IsLoading());
        int n = 0;
        context->TransferIndex(f, n, 4);
        // Each element consumes at least its 1-byte type tag below; reject a count the
        // remaining stream cannot back before a huge/negative Realloc (OOM).
        if (n < 0 || n > f.GetRest())
        {
            f.SetError(f.EFileStructure);
            return;
        }
        _value.Realloc(n);
        _value.Resize(n);
        for (int i = 0; i < n; i++)
        {
            // CreateValue - create value of appropriate type
            _value[i] = CreateParamArrayValue(f);
            _value[i]->SerializeBin(f);
        }
    }
}

void ParamArray::Parse(QIStream& in, ParamFile* file)
{
    ParamRawArray::Parse(in, file);
}
void ParamArray::Save(QOStream& f, int indent) const
{
    Indent(f, indent);
    f << _name << "[]=";
    ParamRawArray::Save(f, indent);
    f << ";\r\n";
}
void ParamArray::SerializeBin(SerializeBinStream& f)
{
    ParamFileContext* context = (ParamFileContext*)f.GetContext();
    // make table of names
    context->TransferString(f, _name);
    ParamRawArray::SerializeBin(f);
}

void ParamRawArray::CalculateCheckValue(PASumCalculator& sum) const
{
    for (int i = 0; i < _value.Size(); i++)
    {
        _value[i]->CalculateCheckValue(sum);
    }
}

void ParamArray::CalculateCheckValue(PASumCalculator& sum) const
{
    ParamRawArray::CalculateCheckValue(sum);
}

template <class ParamRawValueSpec>
void ParamValueSpec<ParamRawValueSpec>::Save(QOStream& f, int indent) const
{
    Indent(f, indent);
    f << _name << "=";
    ParamRawValueSpec::Save(f, indent);
    f << ";\r\n";
}

template <class ParamRawValueSpec>
void ParamValueSpec<ParamRawValueSpec>::SerializeBin(SerializeBinStream& f)
{
    if (f.IsSaving())
    {
        char type = ParamRawValueSpec::GetValueType();
        f.Transfer(type);
    }
    // Loading - type processed by CreateParamValue

    ParamFileContext* context = (ParamFileContext*)f.GetContext();
    // make table of names
    context->TransferString(f, _name);
    ParamRawValueSpec::SerializeBin(f);
}

template <class ParamRawValueSpec>
void ParamValueSpec<ParamRawValueSpec>::CalculateCheckValue(PASumCalculator& sum) const
{
    ParamRawValueSpec::CalculateCheckValue(sum);
}

void ParamClass::Save(QOStream& f, int indent) const
{
    Indent(f, indent);
    f << "class " << _name;
    // Emit the base-class link so a debinned config round-trips through the binarizer with its
    // inheritance intact (entries hold only own members; the hierarchy carries the rest).
    if (_base)
    {
        f << ": " << _base->GetName();
    }
    f << "\r\n";
    Indent(f, indent);
    f << "{\r\n";
    for (int i = 0; i < GetEntryCount(); i++)
    {
        GetEntry(i).Save(f, indent + 1);
    }
    Indent(f, indent);
    f << "};\r\n";
}

void ParamEntry::Compact() {}

void ParamEntry::ReserveArrayElements(int count) {}

// Useful when the class's entry count is known in advance: avoids reallocation
// during growth, and an accurate count also avoids resizing during Compact.
void ParamEntry::ReserveEntries(int count) {}

bool ParamEntry::HasChecksum() const
{
    // by default no entry has checksum
    // only class may be checksumed
    return false;
}

void ParamEntry::SerializeBin(SerializeBinStream& f)
{
    Fail("Should be never reached (Pure virtual function?)");
}

void ParamClass::CheckInheritedAccess()
{
    if (_access == PADefault)
    {
        // no access given - inherit it
        // prefer base class
        if (_base && _base->GetAccessMode() > PADefault)
        {
            // base must be closed - no need to check its parent
            _access = _base->GetAccessMode();
        }
        else if (_parent)
        {
            ParamClass* parent = _parent;
            do
            {
                if (parent->GetAccessMode() > PADefault)
                {
                    _access = parent->GetAccessMode();
                    break;
                }
                // before checking parent of parent check base of parent
                if (parent->_base && parent->_base->GetAccessMode() > PADefault)
                {
                    _access = parent->_base->GetAccessMode();
                    break;
                }
                parent = parent->_parent;
            } while (parent);
        }
    }
}

void ParamClass::SerializeBin(SerializeBinStream& f)
{
    const int idClass = 0;
    const int idValue = 1;
    const int idArray = 2;

    ParamFileContext* context = (ParamFileContext*)f.GetContext();
    // make table of names
    context->TransferString(f, _name);

    if (f.IsSaving())
    {
        RStringB base = "";
        if (_base)
        {
            base = _base->GetName();
        }
        f.Transfer(base);
        int n = _entries.Size();
        context->TransferIndex(f, n, 4);
        for (int i = 0; i < n; i++)
        {
            ParamEntry* entry = _entries[i];
            if (entry->IsClass())
            {
                f.SaveChar(idClass);
            }
            else if (entry->IsArray())
            {
                f.SaveChar(idArray);
            }
            else // value
            {
                f.SaveChar(idValue);
            }
            // SerializeBin is virtual function of ParamEntry
            entry->SerializeBin(f);
        }
    }
    else
    {
        PoseidonAssert(f.IsLoading());
        RStringB base;
        f.Transfer(base);
        if (base.GetLength() > 0)
        {
            // A base name resolves against the parent's scope; the root class has no
            // parent. The assert that guarded this compiles out under NDEBUG (the
            // shipping save loader), so a tampered save naming a base on the root would
            // dereference a null _parent. Reject it.
            if (!_parent)
            {
                f.SetError(f.EFileStructure);
                return;
            }
            ParamEntry* entry = _parent->Find(base, true, true, DefaultAccess); // search parents and bases of my parent
            // entry==null (base not found) is fine. But a base name resolving to a value
            // or array is malformed: the unchecked static_cast<ParamClass*> would later be
            // walked as a class (Find -> _entries.Size()) and dereference garbage.
            if (entry && !entry->IsClass())
            {
                f.SetError(f.EFileStructure);
                return;
            }
            _base = static_cast<ParamClass*>(entry);
        }

        int n;
        context->TransferIndex(f, n, 4);
        // n is read off the wire; each entry consumes at least its 1-byte id below, so a
        // count the remaining stream cannot back is a corrupt/tampered save — reject it
        // before it drives a huge/negative Realloc (OOM).
        if (n < 0 || n > f.GetRest())
        {
            f.SetError(f.EFileStructure);
            return;
        }
        _entries.Realloc(n);
        for (int i = 0; i < n; i++)
        {
            int id = f.LoadChar();
            if (id == idClass)
            {
                ParamClass* cls = new ParamClass();
                cls->_parent = this;
                cls->SerializeBin(f);
                _entries.Add(cls);
            }
            else if (id == idArray)
            {
                ParamArray* array = new ParamArray("");
                array->SerializeBin(f);
                _entries.Add(array);
            }
            else // value
            {
                ParamEntry* entry = CreateParamValue(f);
                entry->SerializeBin(f);
                if (entry->GetName() == GetAccessString())
                {
                    _access = (ParamAccessMode)entry->GetInt();
                    DoAssert(_access > PADefault);
                }
                _entries.Add(entry);
            }
        }
        // check access attribute
        CheckInheritedAccess();
    }
}

void ParamClass::Compact()
{
    _entries.Compact();
}

void ParamClass::ReserveEntries(int count)
{
    _entries.Reserve(count, count);
}

void ParamFile::Save(QOStream& f, int indent) const
{
    for (int i = 0; i < GetEntryCount(); i++)
    {
        GetEntry(i).Save(f, indent);
    }
}

LSError ParamFile::Save(const char* name) const
{
    QOFStream f;
    f.open(name);
    for (int i = 0; i < GetEntryCount(); i++)
    {
        GetEntry(i).Save(f, 0);
    }
    f.close();
    return f.error();
}

void ParamFile::Parse(QIStream& in)
{
    _entries.Clear();
    // read all class definitions
    DeleteVariables();
    _vars = CreateVariables();
    InitEvaluator();
    // variable context defined - in this context all enum values are defined
    ParamClass::Parse(in, this);
    DoneEvaluator();
    SetFile(this);
    if (in.fail() || in.eof())
    {
        return;
    }
    ErrorMessage("Config %s: some input after EndOfFile.", (const char*)_name);
}

void ParamFile::Reload()
{
    ParamFile source;
    source.Parse(_name);
    DeleteVariables();
    _vars = source._vars;
    source._vars = nullptr;
    Update(source);
    SetFile(this);
}

void ParamFile::SerializeBin(SerializeBinStream& f)
{
    ParamFileContext context;
    void* oldContext = f.GetContext();
    f.SetContext(&context);
#ifdef _WIN32
    if (!f.Version('Par\0'))
#else
    if (!f.Version(StrToInt("\0raP")))
#endif
    {
        f.SetError(f.EBadFileType);
        f.SetContext(oldContext);
        return;
    }
    context._version = 4;
    f.Transfer(context._version);
    if (f.IsLoading())
    {
        if (context._version < 2)
        {
            WarningMessage("Bad version in ParamFile");
            f.SetError(f.EBadVersion);
            f.SetContext(oldContext);
            return;
        }
    }
    ParamClass::SerializeBin(f);
    f.SetContext(oldContext);
}

bool ParamFile::ParseBin(const char* name)
{
    _entries.Clear();

    SetName(name);
    if (!QIFStreamB::FileExist(name))
    {
        return LSOK;
    }

    QIFStreamB in;
    in.AutoOpen(name);

    SerializeBinStream f(&in);

    SerializeBin(f);
    if (f.GetError() != SerializeBinStream::EOK)
    {
        return false;
    }

    SetFile(this);

    LoadVariables(f);

#if DIAG_OPEN
    if (_entries.Size() > 0)
    {
        ParamFileOpen++;
        LOG_DEBUG(Core, "{}: Parsed paramfile {}", ParamFileOpen, name);
    }
#endif

    return f.GetError() == SerializeBinStream::EOK;
}

bool ParamFile::ParseBin(QFBank& bank, const char* name)
{
    _entries.Clear();

    SetName(name);

    QIFStreamB in;
    in.open(bank, name);

    SerializeBinStream f(&in);

    SerializeBin(f);
    if (f.GetError() != SerializeBinStream::EOK)
    {
        return false;
    }

    SetFile(this);

    LoadVariables(f);

#if DIAG_OPEN
    if (_entries.Size() > 0)
    {
        ParamFileOpen++;
        LOG_DEBUG(Core, "{}: Parsed paramfile {}", ParamFileOpen, name);
    }
#endif

    return f.GetError() == SerializeBinStream::EOK;
}

bool ParamFile::SaveBin(const char* name)
{
    QOFStream out;
    out.open(name);

    SerializeBinStream f(&out);

    SerializeBin(f);

    SaveVariables(f);

    out.close();

    return f.GetError() == SerializeBinStream::EOK;
}

bool ParamFile::ParseBinOrTxt(const char* name)
{
    if (ParseBin(name))
    {
        return true;
    }
    return Parse(name) == LSOK;
}

} // namespace Poseidon
