#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Streams/SerializeBin.hpp>
#include <Poseidon/Foundation/Framework/LogFlags.hpp>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Poseidon
{
using Poseidon::Foundation::FastCAlloc;

typedef char WordBuf[2048];

static bool GetWord(char* buf, int bufSize, QIStream& in, const char* termin, bool* quot = nullptr)
{
    int len = 0;
    buf[len] = 0;
    int c = in.get();
    // LTrim the word
    while (isspace(c))
    {
        c = in.get();
    }
    if (c == '"')
    {
        if (quot)
        {
            *quot = true;
        }
        c = in.get();
        for (;;)
        {
            if (c == EOF)
            {
                // Unterminated string: stop at end of input rather than spinning here,
                // since QIStream::get keeps returning EOF without advancing the cursor.
                ErrorMessage("Config: End of file encountered inside a string");
                return len > 0;
            }
            if (c == '"')
            {
                c = in.get();
                if (c != '"')
                {
                    in.unget();
                    return true; // word parsed
                }
            }
            if (c == '\n' || c == '\r')
            {
                ErrorMessage("Config: End of line encountered after %s", buf);
            }
            if (len < bufSize - 1)
            {
                buf[len++] = c, buf[len] = 0;
            }
            c = in.get();
        }
    }
    else
    {
        if (quot)
        {
            *quot = false;
        }
        while (!strchr(termin, c) && c != EOF)
        {
            if (c == '\n' || c == '\r')
            {
                // word terminated - only white spaces or termin now
                for (;;)
                {
                    c = in.get();
                    if (!isspace(c))
                    {
                        break;
                    }
                }
                if (!strchr(termin, c))
                {
                    ErrorMessage("Config: '%c' after %s", c, buf);
                }
                else
                {
                    in.unget();
                }
                goto Return;
            }
            if (len < bufSize - 1)
            {
                buf[len++] = c, buf[len] = 0;
            }
            c = in.get();
        }
        if (c != EOF)
        {
            in.unget();
        }
    Return:
        // RTrim the word
        while (len > 0 && isspace(buf[len - 1]))
        {
            buf[--len] = 0;
        }
        return len > 0;
    }
    /*NOTREACHED*/
}

static void GetAlphaWord(char* buf, int bufSize, QIStream& in)
{
    int len = 0;
    int c = in.get();
    while (isspace(c))
    {
        c = in.get();
    }
    while (isalnum(c) || c == '_')
    {
        if (len < bufSize)
        {
            buf[len++] = c;
        }
        c = in.get();
    }
    in.unget();
    buf[len] = 0;
}

// Meyer's singleton for access string constant - no global constructor!
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
static const RStringB& GetAccessString()
{
    static RStringB instance("access");
    return instance;
}
#pragma clang diagnostic pop

ParamEntry::ParamEntry(const RStringB& name)
{
    if (name.GetLength() > 0)
    {
        _name = name;
    }
}

inline void NotClass(const char* cName, const char* eName)
{
    ErrorMessage(EMError, "'%s' is not a class ('%s' accessed)", cName, eName);
}

inline void NotClass(const char* cName)
{
    LOG_DEBUG(Core, "ParamFile error: '{}' is not a class.", cName);
}

inline void NotValue(const char* eName)
{
    LOG_DEBUG(Core, "ParamFile error: '{}' is not a value", eName);
}

inline void NotArray(const char* aName)
{
    LOG_DEBUG(Core, "ParamFile error: '{}' is not an array.", aName);
}

enum SpecValueType
{
    SVGeneric, // generic - string
    SVFloat,
    SVInt,
    SVArray,
    NSpecValueType
    // note: char is used to contain values of this type
};

// Error sentinel returned (as a global instance) when a config value is missing
// or has the wrong type.
class ParamEntryError : public ParamClass
{
  public:
    ParamEntryError() = default;

    bool IsError() const override { return true; }
};

//! Meyer's singleton for error instance - no global constructor!
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
static ParamEntryError& GetParamEntryError()
{
    static ParamEntryError instance;
    return instance;
}
#pragma clang diagnostic pop

ParamEntry* ParamEntry::FindEntry(const RStringB& name, IParamVisibleTest& visible) const
{
    NotClass(GetContext(), name);
    return nullptr;
}

ParamEntry* ParamEntry::FindEntryNoInheritance(const RStringB& name, IParamVisibleTest& visible) const
{
    NotClass(GetContext(), name);
    return nullptr;
}

const ParamEntry& ParamEntry::operator>>(const char* name) const
{
    NotClass(GetContext(), name);
    return GetParamEntryError();
}

ParamEntry::operator RStringB() const
{
    NotValue(GetContext());
    return Foundation::RStringBEmpty;
}
ParamEntry::operator float() const
{
    NotValue(GetContext());
    return 0;
}
ParamEntry::operator int() const
{
    NotValue(GetContext());
    return 0;
}
ParamEntry::operator bool() const
{
    NotValue(GetContext());
    return false;
}
int ParamEntry::GetInt() const
{
    NotValue(GetContext());
    return 0;
}
RStringB ParamEntry::GetValueRaw() const
{
    NotValue(GetContext());
    return nullptr;
}
RStringB ParamEntry::GetValue() const
{
    NotValue(GetContext());
    return nullptr;
}

void ParamEntry::Add(const RStringB& name, const RStringB& val)
{
    NotClass(GetContext(), name);
}
void ParamEntry::Add(const RStringB& name, float val)
{
    NotClass(GetContext(), name);
}
void ParamEntry::Delete(const RStringB& name)
{
    NotClass(GetContext(), name);
}

void ParamEntry::Add(const RStringB& name, int val)
{
    NotClass(GetContext(), name);
}
ParamClass* ParamEntry::AddClass(const RStringB& name, bool guaranteedUnique)
{
    NotClass(GetContext(), name);
    return nullptr;
}
ParamEntry* ParamEntry::AddArray(const RStringB& name)
{
    NotClass(GetContext(), name);
    return nullptr;
}
void ParamEntry::Clear()
{
    NotArray(GetContext());
}

void ParamEntry::AddValue(float val)
{
    NotArray(GetContext());
}
void ParamEntry::AddValue(int val)
{
    NotArray(GetContext());
}
void ParamEntry::AddValue(const RStringB& val)
{
    NotArray(GetContext());
}
IParamArrayValue* ParamEntry::AddArrayValue()
{
    NotArray(GetContext());
    return nullptr;
}

int ParamEntry::GetEntryCount() const
{
    NotClass(GetContext());
    return 0;
}
const ParamEntry& ParamEntry::GetEntry(int i) const
{
    NotClass(GetContext());
    return GetParamEntryError();
}

void ParamEntry::SetValue(const RStringB& val)
{
    NotValue(GetContext());
}
void ParamEntry::SetValue(float val)
{
    NotValue(GetContext());
}
void ParamEntry::SetValue(int val)
{
    NotValue(GetContext());
}

class ParamRawValue
{
    RStringB _value;
    ParamFile* _file;

  public:
    ParamRawValue() { _file = nullptr; }

    SpecValueType GetValueType() { return SVGeneric; }

    void SetValue(const RStringB& value);
    void SetValue(float val);
    void SetValue(int val);
    void SetFile(ParamFile* file) { _file = file; }

    //! get value, use localization if necessary
    const RStringB GetValue() const;
    //! get value - no localization
    const RStringB GetValueRaw() const;
    float GetFloat() const;
    int GetInt() const;

    operator RStringB() const { return _value; }
    operator float() const { return GetFloat(); }
    operator int() const { return GetInt(); }

    bool IsTextValue() const { return true; }
    bool IsFloatValue() const { return false; }
    bool IsIntValue() const { return false; }

    void Save(QOStream& f, int indent) const;
    void SerializeBin(SerializeBinStream& f);
    void CalculateCheckValue(PASumCalculator& sum) const;
};

class ParamRawValueFloat
{ // special case - number detected as float
    float _value;

  public:
    ParamRawValueFloat() = default;

    SpecValueType GetValueType() { return SVFloat; }

    void SetValue(const RStringB& value) { Fail("Float value set as string"); }
    void SetValue(float val) { _value = val; }
    void SetValue(int val) { _value = val; }
    void SetFile(ParamFile* file) {}

    RStringB GetValue() const;
    RStringB GetValueRaw() const { return GetValue(); }

    float GetFloat() const { return _value; }
    int GetInt() const { return toLargeInt(_value); }

    operator RStringB() const { return GetValue(); }
    operator float() const { return GetFloat(); }
    operator int() const { return GetInt(); }

    bool IsTextValue() const { return false; }
    bool IsFloatValue() const { return true; }
    bool IsIntValue() const { return false; }

    void Save(QOStream& f, int indent) const;
    void SerializeBin(SerializeBinStream& f);

    void CalculateCheckValue(PASumCalculator& sum) const;
};

class ParamRawValueInt
{ // special case - number detected as int
    int _value;

  public:
    ParamRawValueInt() = default;

    SpecValueType GetValueType() { return SVInt; }

    void SetValue(const RStringB& value) { Fail("Float value set as string"); }
    void SetValue(float val) { _value = toLargeInt(val); }
    void SetValue(int val) { _value = val; }
    void SetFile(ParamFile* file) {}

    RStringB GetValue() const;
    RStringB GetValueRaw() const { return GetValue(); }
    float GetFloat() const { return _value; }
    int GetInt() const { return _value; }

    operator RStringB() const { return GetValue(); }
    operator float() const { return GetFloat(); }
    operator int() const { return GetInt(); }

    bool IsTextValue() const { return false; }
    bool IsFloatValue() const { return false; }
    bool IsIntValue() const { return true; }

    void Save(QOStream& f, int indent) const;
    void SerializeBin(SerializeBinStream& f);

    void CalculateCheckValue(PASumCalculator& sum) const;
};

void ParamEntry::SetValue(int index, const RStringB& string)
{
    ErrorMessage(EMError, "SetValue: '%s' not an array", (const char*)GetContext());
}

void ParamEntry::SetValue(int index, float val)
{
    ErrorMessage(EMError, "SetValue: '%s' not an array", (const char*)GetContext());
}

void ParamEntry::SetValue(int index, int val)
{
    ErrorMessage(EMError, "SetValue: '%s' not an array", (const char*)GetContext());
}

int ParamEntry::GetSize() const
{
    ErrorMessage(EMError, "Size: '%s' not an array", (const char*)GetContext());
    return 0;
}

DEFINE_FAST_ALLOCATOR(ParamClass)

ParamClass::ParamClass() : ParamEntry(nullptr)
{
    _access = PADefault;
}
ParamClass::ParamClass(const RStringB& name) : ParamEntry(name)
{
    _access = PADefault;
}

ParamClass::~ParamClass() {}

template <class ParamRawValueSpec>
class ParamValueSpec : public ParamEntry, public ParamRawValueSpec
{
  public:
    ParamValueSpec();
    ParamValueSpec(const RStringB& name);

    RStringB GetValue() const override { return ParamRawValueSpec::GetValue(); }
    RStringB GetValueRaw() const override { return ParamRawValueSpec::GetValueRaw(); }
    float GetFloat() const { return ParamRawValueSpec::GetFloat(); }
    int GetInt() const override { return ParamRawValueSpec::GetInt(); }

    operator RStringB() const override { return ParamRawValueSpec::GetValue(); }
    operator float() const override { return ParamRawValueSpec::GetFloat(); }
    operator int() const override { return ParamRawValueSpec::GetInt(); }
    operator bool() const override { return ParamRawValueSpec::GetInt() != 0; }

    void SetValue(const RStringB& val) override { ParamRawValueSpec::SetValue(val); }
    void SetValue(float val) override { ParamRawValueSpec::SetValue(val); }
    void SetValue(int val) override { ParamRawValueSpec::SetValue(val); }
    void SetFile(ParamFile* file) override { ParamRawValueSpec::SetFile(file); }

    bool IsTextValue() const override { return ParamRawValueSpec::IsTextValue(); }
    bool IsFloatValue() const override { return ParamRawValueSpec::IsFloatValue(); }
    bool IsIntValue() const override { return ParamRawValueSpec::IsIntValue(); }
    bool IsArrayValue() const { return false; }

    void Save(QOStream& f, int indent) const override;

    void SerializeBin(SerializeBinStream& f) override;
    void CalculateCheckValue(PASumCalculator& sum) const override;

    USE_FAST_ALLOCATOR
};

template <class ParamRawValueSpec>
class ParamArrayValueSpec : public IParamArrayValue, public ParamRawValueSpec
{
  public:
    ParamArrayValueSpec(const RStringB& val) { ParamRawValueSpec::SetValue(val); }
    ParamArrayValueSpec(float val) { ParamRawValueSpec::SetValue(val); }
    ParamArrayValueSpec(int val) { ParamRawValueSpec::SetValue(val); }

    RStringB GetValue() const override { return ParamRawValueSpec::GetValue(); }
    RStringB GetValueRaw() const override { return ParamRawValueSpec::GetValueRaw(); }
    int GetInt() const override { return ParamRawValueSpec::GetInt(); }
    float GetFloat() const override { return ParamRawValueSpec::GetFloat(); }

    void SetValue(const RStringB& val) override { ParamRawValueSpec::SetValue(val); }
    void SetValue(float val) override { ParamRawValueSpec::SetValue(val); }
    void SetValue(int val) override { ParamRawValueSpec::SetValue(val); }
    void SetFile(ParamFile* file) override { ParamRawValueSpec::SetFile(file); }

    bool IsTextValue() const override { return ParamRawValueSpec::IsTextValue(); }
    bool IsFloatValue() const override { return ParamRawValueSpec::IsFloatValue(); }
    bool IsIntValue() const override { return ParamRawValueSpec::IsIntValue(); }
    bool IsArrayValue() const override { return false; }

    void Save(QOStream& f, int indent) const override;
    void SerializeBin(SerializeBinStream& f) override;

    // may be array of values
    const IParamArrayValue* GetItem(int i) const override { return nullptr; }
    int GetItemCount() const override
    {
        ErrorMessage(EMError, "Value not an array.");
        return 0;
    }

    void AddValue(float val) override { ErrorMessage(EMError, "Value not an array."); }
    void AddValue(int val) override { ErrorMessage(EMError, "Value not an array."); }
    void AddValue(const RStringB& val) override { ErrorMessage(EMError, "Value not an array."); }
    IParamArrayValue* AddArrayValue() override
    {
        ErrorMessage(EMError, "Value not an array.");
        return nullptr;
    }

    void CalculateCheckValue(PASumCalculator& sum) const override { ParamRawValueSpec::CalculateCheckValue(sum); }
    USE_FAST_ALLOCATOR
};

typedef ParamValueSpec<ParamRawValue> ParamValue;
typedef ParamValueSpec<ParamRawValueFloat> ParamValueFloat;
typedef ParamValueSpec<ParamRawValueInt> ParamValueInt;

typedef ParamArrayValueSpec<ParamRawValue> ParamArrayValue;
typedef ParamArrayValueSpec<ParamRawValueFloat> ParamArrayValueFloat;
typedef ParamArrayValueSpec<ParamRawValueInt> ParamArrayValueInt;

// Explicit template instantiation - this creates the allocator static members
// Define allocators for template specializations BEFORE instantiation
// Must use DEFINE_FAST_ALLOCATOR_ID syntax expanded for template types
template <>
FastCAlloc& ParamValueSpec<ParamRawValue>::_allocatorF_instance()
{
    [[clang::no_destroy]] static FastCAlloc allocator(sizeof(ParamValueSpec<ParamRawValue>),
                                                      "ParamValueSpec<ParamRawValue>");
    return allocator;
}
template <>
FastCAlloc& ParamValueSpec<ParamRawValueFloat>::_allocatorF_instance()
{
    [[clang::no_destroy]] static FastCAlloc allocator(sizeof(ParamValueSpec<ParamRawValueFloat>),
                                                      "ParamValueSpec<ParamRawValueFloat>");
    return allocator;
}
template <>
FastCAlloc& ParamValueSpec<ParamRawValueInt>::_allocatorF_instance()
{
    [[clang::no_destroy]] static FastCAlloc allocator(sizeof(ParamValueSpec<ParamRawValueInt>),
                                                      "ParamValueSpec<ParamRawValueInt>");
    return allocator;
}
template <>
FastCAlloc& ParamArrayValueSpec<ParamRawValue>::_allocatorF_instance()
{
    [[clang::no_destroy]] static FastCAlloc allocator(sizeof(ParamArrayValueSpec<ParamRawValue>),
                                                      "ParamArrayValueSpec<ParamRawValue>");
    return allocator;
}
template <>
FastCAlloc& ParamArrayValueSpec<ParamRawValueFloat>::_allocatorF_instance()
{
    [[clang::no_destroy]] static FastCAlloc allocator(sizeof(ParamArrayValueSpec<ParamRawValueFloat>),
                                                      "ParamArrayValueSpec<ParamRawValueFloat>");
    return allocator;
}
template <>
FastCAlloc& ParamArrayValueSpec<ParamRawValueInt>::_allocatorF_instance()
{
    [[clang::no_destroy]] static FastCAlloc allocator(sizeof(ParamArrayValueSpec<ParamRawValueInt>),
                                                      "ParamArrayValueSpec<ParamRawValueInt>");
    return allocator;
}

// Now explicitly instantiate the templates
template class ParamValueSpec<ParamRawValue>;
template class ParamValueSpec<ParamRawValueFloat>;
template class ParamValueSpec<ParamRawValueInt>;
template class ParamArrayValueSpec<ParamRawValue>;
template class ParamArrayValueSpec<ParamRawValueFloat>;
template class ParamArrayValueSpec<ParamRawValueInt>;

static IParamArrayValue* CreateParamArrayValue(RStringB val)
{
    return new ParamArrayValue(val);
}
static IParamArrayValue* CreateParamArrayValue(float val)
{
    return new ParamArrayValueFloat(val);
}

static ParamEntry* CreateParamValue(SerializeBinStream& f)
{
    // load type and create value
    PoseidonAssert(f.IsLoading());
    char type;
    f.Transfer(type);
    switch (type)
    {
        case SVGeneric:
            return new ParamValue();
        case SVFloat:
            return new ParamValueFloat();
        case SVInt:
            return new ParamValueInt();
        default:
            LOG_ERROR(Core, "Unknown value type {}", type);
            return new ParamValue();
    }
}

template <class ParamRawValueSpec>
ParamValueSpec<ParamRawValueSpec>::ParamValueSpec() : ParamEntry(nullptr)
{
}

template <class ParamRawValueSpec>
ParamValueSpec<ParamRawValueSpec>::ParamValueSpec(const RStringB& name) : ParamEntry(name)
{
}

template <class ParamRawValueSpec>
void ParamArrayValueSpec<ParamRawValueSpec>::Save(QOStream& f, int indent) const
{
    ParamRawValueSpec::Save(f, indent);
}

template <class ParamRawValueSpec>
void ParamArrayValueSpec<ParamRawValueSpec>::SerializeBin(SerializeBinStream& f)
{
    if (f.IsSaving())
    {
        char type = ParamRawValueSpec::GetValueType();
        f.Transfer(type);
    }
    ParamRawValueSpec::SerializeBin(f);
}

// scan some special value types

static int ScanHex(const char* val, bool& ok)
{
    ok = false;
    if (!strnicmp(val, "0x", 2))
    {
        char c;
        const char* ptr = (const char*)val + 2;
        ok = isxdigit(*ptr) != 0;
        if (!ok)
        {
            return 0;
        }
        // unsigned accumulator: an over-long hex literal overflows int (UB); the
        // unsigned wraparound reproduces the original two's-complement result.
        unsigned iValue = 0;
        while (c = *(ptr++), isxdigit(c))
        {
            iValue *= 16u;
            if (isdigit(c))
            { // 0..9
                iValue += c - '0';
            }
            else if (c <= 'F')
            { // A..F
                iValue += 10 + c - 'A';
            }
            else
            { // a..f
                iValue += 10 + c - 'a';
            }
        }
        return (int)iValue;
    }
    else
    {
        return 0;
    }
}

static float ScanDb(const char* ptr, bool& ok)
{
    ok = false;
    if (ptr[0] != 'd' || ptr[1] != 'b')
    {
        return 0;
    }
    ok = true;
    char* end;
    float db = strtod(ptr + 2, &end);
    if (*end != 0)
    {
        LOG_DEBUG(Core, "invalid db value {}", ptr);
    }
    return pow(10, db * (1.0f / 20));
}

static float ScanFloatPlain(const char* ptr, bool& ok)
{
    char* end;
    float db = strtod(ptr, &end);
    ok = (*end == 0);
    return db;
}

static int ScanIntPlain(const char* ptr, bool& ok)
{
    char* end;
    long db = strtol(ptr, &end, 10);
    ok = (*end == 0);
    return (int)db;
}

static int ScanInt(const char* ptr, bool& ok)
{
    ok = false;
    if (!*ptr)
    {
        return 0;
    }
    int val = ScanIntPlain(ptr, ok);
    if (ok)
    {
        return val;
    }
    val = ScanHex(ptr, ok);
    if (ok)
    {
        return val;
    }
    return 0;
}

static float ScanFloat(const char* ptr, bool& ok)
{
    ok = false;
    if (!*ptr)
    {
        return 0;
    }
    float val = ScanFloatPlain(ptr, ok);
    if (ok)
    {
        return val;
    }
    val = ScanDb(ptr, ok);
    if (ok)
    {
        return val;
    }
    return 0;
}

const RStringB ParamRawValue::GetValue() const
{
    const char* val = _value;
    if (strncmp(val, "$STR", 4) == 0)
    {
        return _file->LocalizeString(val + 1);
    }
    else
    {
        return _value;
    }
}

const RStringB ParamRawValue::GetValueRaw() const
{
    return _value;
}

void ParamRawValue::CalculateCheckValue(PASumCalculator& sum) const
{
    PoseidonAssert(_file);
    ParamFile::AddCRC(sum, _value, _value.GetLength());
}

float ParamRawValue::GetFloat() const
{
    bool ok;
    // check for simple cases
    float valF = ScanFloat(_value, ok);
    if (ok)
    {
        return valF;
    }
    int valI = ScanInt(_value, ok);
    if (ok)
    {
        return valI;
    }
    // if there is no file, we cannot evaluate expressions
    if (!_file)
    {
        RptF("Cannot evaluate %s - no file", (const char*)_value);
        return 0.0f;
    }
    return _file->EvaluateFloat(_value);
}
int ParamRawValue::GetInt() const
{
    bool ok;

    // check for simple cases
    int valI = ScanInt(_value, ok);
    if (ok)
    {
        return valI;
    }
    float valF = ScanFloat(_value, ok);
    if (ok)
    {
        LOG_DEBUG(Core, "Warning: rounding float value {}", valF);
        return toLargeInt(valF);
    }
    // if there is no file, we cannot evaluate expressions
    if (!_file)
    {
        LOG_DEBUG(Core, "Cannot evaluate {} - no file", (const char*)_value);
        return 0;
    }
    return toLargeInt(_file->EvaluateFloat(_value));
}

void ParamRawValue::SetValue(const RStringB& value)
{
    _value = value;
}

void ParamRawValue::SetValue(float val)
{
    BString<256> buf;
    sprintf(buf, "%f", val);
    _value = buf.cstr();

    // check if ok
    char* end;
    float dummy = strtod(buf, &end);
    (void)dummy;
    if (*end != 0)
    {
        LOG_ERROR(Core, "Setting invalid value {}", (const char*)buf);
        _value = "0";
    }
}

void ParamRawValue::SetValue(int val)
{
    BString<256> buf;
    sprintf(buf, "%d", val);
    _value = buf.cstr();
}

class ParamRawArray
{
  protected:
    RefArray<IParamArrayValue> _value;

  public:
    void AddValue(float val);
    void AddValue(int val);
    void AddValue(const RStringB& val);
    IParamArrayValue* AddArrayValue();

    void Compact() { _value.Compact(); }
    void Clear() { _value.Clear(); }
    void Reserve(int count) { _value.Reserve(count, count); }
    void Copy(const ParamRawArray& src) { _value = src._value; }

    void SetFile(ParamFile* file);

    int GetSize() const { return _value.Size(); }
    IParamArrayValue& GetValue(int i) const;
    const IParamArrayValue& operator[](int i) const { return GetValue(i); }

    void SetValue(int index, const RStringB& string);
    void SetValue(int index, float val);

    void Parse(QIStream& in, ParamFile* file);
    void Save(QOStream& f, int indent) const;
    void SerializeBin(SerializeBinStream& f);
    void CalculateCheckValue(PASumCalculator& sum) const;
};

class ParamArrayValueArray : public IParamArrayValue, public ParamRawArray
{
  public:
    ParamArrayValueArray() = default;

    RStringB GetValue() const override { return Foundation::RStringBEmpty; }
    RStringB GetValueRaw() const override { return Foundation::RStringBEmpty; }
    int GetInt() const override { return 0; }
    float GetFloat() const override { return 0; }

    void SetValue(const RStringB& val) override {}
    void SetValue(float val) override {}
    void SetValue(int val) override {}
    void SetFile(ParamFile* file) override { ParamRawArray::SetFile(file); }

    bool IsTextValue() const override { return false; }
    bool IsFloatValue() const override { return false; }
    bool IsIntValue() const override { return false; }
    bool IsArrayValue() const override { return true; }

    void Save(QOStream& f, int indent) const override;
    void SerializeBin(SerializeBinStream& f) override;

    // may be array of values
    const IParamArrayValue* GetItem(int i) const override { return &ParamRawArray::operator[](i); }
    int GetItemCount() const override { return ParamRawArray::GetSize(); }

    void AddValue(float val) override { ParamRawArray::AddValue(val); }
    void AddValue(int val) override { ParamRawArray::AddValue(val); }
    void AddValue(const RStringB& val) override { ParamRawArray::AddValue(val); }
    IParamArrayValue* AddArrayValue() override { return ParamRawArray::AddArrayValue(); }

    void CalculateCheckValue(PASumCalculator& sum) const override;

    USE_FAST_ALLOCATOR
};

class ParamArray : public ParamEntry, public ParamRawArray
{
    ParamAccessMode _access;

  public:
    ParamArray(const RStringB& name);

    bool IsArray() const override { return true; }

    void SetFile(ParamFile* file) override { ParamRawArray::SetFile(file); }

    void Compact() override;
    void Clear() override;
    void ReserveArrayElements(int count) override;

    void SetAccessMode(ParamAccessMode mode) override { _access = mode; }
    ParamAccessMode GetAccessMode() const override { return _access; }

    bool EnableModification()
    {
        if (_access >= PAReadOnly)
        {
            RptF("Attempt to modify read-only item %s", (const char*)GetName());
            return false;
        }
        if (_access >= PAReadAndCreate)
        {
            RptF("Attempt to modify add-only item %s", (const char*)GetName());
            return false;
        }
        return true;
    }
    void AddValue(float val) override
    {
        if (!EnableModification())
        {
            return;
        }
        ParamRawArray::AddValue(val);
    }
    void AddValue(int val) override
    {
        if (!EnableModification())
        {
            return;
        }
        ParamRawArray::AddValue(val);
    }
    void AddValue(const RStringB& val) override
    {
        if (!EnableModification())
        {
            return;
        }
        ParamRawArray::AddValue(val);
    }
    IParamArrayValue* AddArrayValue() override
    {
        if (!EnableModification())
        {
            return nullptr;
        }
        return ParamRawArray::AddArrayValue();
    }

    int GetSize() const override { return ParamRawArray::GetSize(); }
    using ParamEntry::GetValue; // Bring base class GetValue() into scope
    IParamArrayValue& GetValue(int i) const { return ParamRawArray::GetValue(i); }
    void SetValue(int i, const RStringB& string) override { ParamRawArray::SetValue(i, string); }
    void SetValue(int i, float val) override { ParamRawArray::SetValue(i, val); }

    const IParamArrayValue& operator[](int i) const override { return GetValue(i); }

    void Parse(QIStream& in, ParamFile* file);
    void Save(QOStream& f, int indent) const override;

    void SerializeBin(SerializeBinStream& f) override;
    void CalculateCheckValue(PASumCalculator& sum) const override;

    USE_FAST_ALLOCATOR
};

void ParamArray::Compact()
{
    ParamRawArray::Compact();
}
void ParamArray::Clear()
{
    ParamRawArray::Clear();
}
void ParamArray::ReserveArrayElements(int count)
{
    ParamRawArray::Reserve(count);
}

static IParamArrayValue* CreateParamArrayValue(SerializeBinStream& f)
{
    // load type and create value
    PoseidonAssert(f.IsLoading());
    char type;
    f.Transfer(type);
    switch (type)
    {
        case SVGeneric:
            return new ParamArrayValue("");
        case SVFloat:
            return new ParamArrayValueFloat(0.0f);
        case SVInt:
            return new ParamArrayValueInt(0);
        case SVArray:
            return new ParamArrayValueArray();
        default:
            LOG_ERROR(Core, "Unknown value type {}", type);
            return new ParamArrayValue("");
    }
}

DEFINE_FAST_ALLOCATOR(ParamArrayValueArray)

void ParamArrayValueArray::Save(QOStream& f, int indent) const
{
    ParamRawArray::Save(f, indent);
}

void ParamArrayValueArray::SerializeBin(SerializeBinStream& f)
{
    if (f.IsSaving())
    {
        char type = SVArray;
        f.Transfer(type);
    }
    ParamRawArray::SerializeBin(f);
}

void ParamArrayValueArray::CalculateCheckValue(PASumCalculator& sum) const
{
    ParamRawArray::CalculateCheckValue(sum);
}

DEFINE_FAST_ALLOCATOR(ParamArray)

ParamArray::ParamArray(const RStringB& name) : ParamEntry(name)
{
    _access = PADefault;
}

void ParamRawArray::AddValue(float val)
{
    _value.Add(new ParamArrayValueFloat(val));
}
void ParamRawArray::AddValue(int val)
{
    _value.Add(new ParamArrayValueInt(val));
}
// void ParamRawArray::AddValue(bool val){_value.Add(new ParamArrayValueInt(val));}
void ParamRawArray::AddValue(const RStringB& val)
{
    _value.Add(new ParamArrayValue(val));
}
// void ParamRawArray::AddValue(const char *val){_value.Add(new ParamArrayValue(val));}
IParamArrayValue* ParamRawArray::AddArrayValue()
{
    IParamArrayValue* value = new ParamArrayValueArray();
    _value.Add(value);
    return value;
}

const IParamArrayValue& ParamEntry::operator[](int index) const
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
    const static ParamArrayValue nil("");
#pragma clang diagnostic pop
    ErrorMessage(EMError, "[]: '%s' not an array", (const char*)GetContext());
    return nil;
}

const RStringB& ParamEntry::GetOwner() const
{
    return Foundation::RStringBEmpty;
}
void ParamEntry::SetOwner(RString owner, bool subentries)
{
    // no owner can be set for single entries, only for classes
}

bool ParamEntry::CheckVisible(IParamVisibleTest& visible) const
{
    Fail("ParamEntry does not know if it is visible");
    return true;
}

ParamEntry::~ParamEntry() = default;

void ParamRawArray::SetFile(ParamFile* file)
{
    // note: only ArrayValues of ParamArrays
    // loaded via Parse or SerializeBin have _file member set
    // SetFile is always called recursivelly after corresponding fucntions
    //_file = file;
    for (int i = 0; i < _value.Size(); i++)
    {
        IParamArrayValue* value = _value[i];
        if (value)
        {
            value->SetFile(file);
        }
    }
}

IParamArrayValue& ParamRawArray::GetValue(int i) const
{
    if (i >= _value.Size())
    {
        // ErrorMessage(EMError,"Config: '%s' does not have %d entries.",(const char *)GetName(),i+1);
        return *_value[0];
    }
    return *_value[i];
}

void ParamRawArray::SetValue(int index, const RStringB& val)
{
    while (index > _value.Size())
    {
        _value.Add(new ParamArrayValue(""));
    }
    if (index >= _value.Size())
    {
        _value.Access(index);
        _value[index] = CreateParamArrayValue(val);
    }
    else
    {
        _value[index]->SetValue(val);
    }
}

void ParamRawArray::SetValue(int index, float val)
{
    while (index > _value.Size())
    {
        _value.Add(new ParamArrayValue(""));
    }
    if (index >= _value.Size())
    {
        _value.Access(index);
        _value[index] = CreateParamArrayValue(val);
    }
    else
    {
        _value[index]->SetValue(val);
    }
}

int ParamClass::FindIndex(const RStringB& name) const
{
    for (int i = 0; i < _entries.Size(); i++)
    {
        if (!_entries[i])
        {
            continue;
        }
        if (strcmpi(_entries[i]->GetName(), name) == 0)
        {
            return i;
        }
    }
    return -1;
}

ParamEntry* ParamClass::Find(const RStringB& name, bool parent, bool base, IParamVisibleTest& visible) const
{
    int i = FindIndex(name);
    if (i >= 0)
    {
        // check if this entry can be seen
        // if yes, we are done
        if (visible(*this, *_entries[i]))
        {
            return _entries[i];
        }
        LOG_DEBUG(Core, "Entry {} in {} is not visible", (const char*)_entries[i]->GetName(), (const char*)GetName());
    }
    if (base && _base != nullptr)
    {
        return _base->Find(name, parent, base, visible);
    }
    if (parent && _parent != nullptr)
    {
        return _parent->Find(name, parent, base, visible);
    }
    return nullptr;
}

ParamEntry* ParamClass::FindEntry(const RStringB& name, IParamVisibleTest& visible) const
{
    return Find(name, false, true, visible);
}

ParamEntry* ParamClass::FindEntryNoInheritance(const RStringB& name, IParamVisibleTest& visible) const
{
    return Find(name, false, false, visible);
}

class ParamEntryAllVisible : public IParamVisibleTest
{
  public:
    bool operator()(const ParamEntry& entry) override { return true; }
    bool operator()(const ParamEntry& parent, const ParamEntry& entry) override { return true; }
};

static ParamEntryAllVisible DefAccess;

IParamVisibleTest& DefaultAccess = DefAccess;

const RStringB& ParamClass::GetOwner() const
{
    return _owner;
}

void ParamClass::SetOwner(RString owner, bool subentries)
{
    owner.Lower();
    _owner = owner;
    if (!subentries)
    {
        return;
    }
    for (int i = 0; i < _entries.Size(); i++)
    {
        _entries[i]->SetOwner(owner, subentries);
    }
}

bool ParamClass::CheckVisible(IParamVisibleTest& visible) const
{
    if (_parent)
    {
        if (!visible(*_parent, *this))
        {
            return false;
        }
        return _parent->CheckVisible(visible);
    }
    else
    {
        return visible(*this);
    }
}

const ParamEntry& ParamClass::operator>>(const char* name) const
{
    const ParamEntry* entry = FindEntry(name, DefaultAccess);
    if (entry)
    {
        return *entry;
    }
    LOG_DEBUG(Core, "ParamFile error: No entry '{}'", (const char*)GetContext(name));
    return GetParamEntryError();
}

const ParamClass* ParamClass::GetClass(const RStringB& name) const
{
    ParamEntry* entry = Find(name, false, true, DefaultAccess);
    ParamClass* section = dynamic_cast<ParamClass*>(entry);
    if (!section)
    {
        ErrorMessage(EMError, "No section '%s' in '%s'", (const char*)name, (const char*)GetName());
        return &GetParamEntryError();
    }
    return section;
}

bool ParamClass::IsDerivedFrom(const ParamClass& parent) const
{
    const ParamClass* base = this;
    while (base)
    {
        if (base == &parent)
        {
            return true;
        }
        base = base->_base;
    }
    return false;
}

void ParamClass::Add(const RStringB& name, float val)
{
    ParamEntry* entry = FindEntry(name, DefaultAccess);
    if (!entry)
    {
        entry = new ParamValueFloat(name);
        entry->SetValue(val);
        NewEntry(entry, true);
        return;
    }
    entry->SetValue(val);
}

void ParamClass::Add(const RStringB& name, int val)
{
    ParamEntry* entry = FindEntry(name, DefaultAccess);
    if (!entry)
    {
        entry = new ParamValueInt(name);
        entry->SetValue(val);
        NewEntry(entry, true);
        return;
    }
    entry->SetValue(val);
}

void ParamClass::Delete(const RStringB& name)
{
    int index = FindIndex(name);
    if (index >= 0)
    {
        _entries.DeleteAt(index);
    }
}

void ParamClass::SetAccessModeForAll(ParamAccessMode mode)
{
    _access = mode;
    // traverse all entries
    for (int i = 0; i < GetEntryCount(); i++)
    {
        _entries[i]->SetAccessModeForAll(mode);
    }
}

void ParamClass::AccessDenied(const char* name)
{
    if (_access >= PAReadOnly)
    {
        RptF("Trying to modify read-only entry %s", (const char*)GetContext(name));
    }
    else if (_access >= PAReadAndCreate)
    {
        RptF("Trying to modify add-only entry %s", (const char*)GetContext(name));
    }
}

void ParamClass::Add(const RStringB& name, const RStringB& val)
{
    if (_access >= PAReadOnly)
    {
        AccessDenied(name);
        return;
    }
    ParamEntry* entry = FindEntry(name, DefaultAccess);
    if (!entry)
    {
        entry = new ParamValue(name);
        entry->SetValue(val);
        NewEntry(entry, true);
        return;
    }
    else if (_access >= PAReadAndCreate)
    {
        AccessDenied(name);
        return;
    }
    entry->SetValue(val);
}

ParamClass* ParamClass::AddClass(const RStringB& name, bool guaranteedUnique)
{
    if (_access >= PAReadOnly)
    {
        AccessDenied(name);
        return nullptr;
    }
    ParamEntry* entry = nullptr;
    if (guaranteedUnique)
    {
        entry = FindEntry(name, DefaultAccess);
        if (entry)
        {
            Fail("Guaranteed unique entry already present");
            RptF("  entry %s", (const char*)name);
        }
    }
    else
    {
        entry = FindEntry(name, DefaultAccess);
    }
    if (!entry)
    {
        ParamClass* entry = new ParamClass(name);
        entry->_parent = this;
        NewEntry(entry, true);
        return entry;
    }
    return entry->GetClassInterface();
}

ParamEntry* ParamClass::AddArray(const RStringB& name)
{
    if (_access >= PAReadOnly)
    {
        AccessDenied(name);
        return nullptr;
    }
    ParamEntry* entry = FindEntry(name, DefaultAccess);
    if (!entry)
    {
        entry = new ParamArray(name);
        NewEntry(entry, true);
    }
    else
    {
        DoAssert(entry->IsArray());
        entry->Clear();
    }
    return entry;
}

void ParamClass::SetFile(ParamFile* file)
{
    for (int i = 0; i < _entries.Size(); i++)
    {
        ParamEntry* entry = _entries[i];
        if (entry)
        {
            entry->SetFile(file);
        }
    }
}

const ParamClass* ParamClass::GetFile() const
{
    if (_parent)
    {
        return _parent->GetFile();
    }
    return this;
}

void ParamClass::NewEntry(ParamEntry* entry, bool guaranteedUnique)
{
    int index = -1;
    if (guaranteedUnique)
    {
        index = FindIndex(entry->GetName());
        if (index >= 0)
        {
            Fail("Guaranteed unique Entry already exists");
            ParamEntry* entry = _entries[index];
            RptF("Config: '%s' already defined in '%s'.", (const char*)entry->GetName(), (const char*)GetName());
        }
    }
    else
    {
        index = FindIndex(entry->GetName());
    }
    if (index >= 0)
    {
        Fail("Entry already exists");
        ParamEntry* entry = _entries[index];
        ErrorMessage("Config: '%s' already defined in '%s'.", (const char*)entry->GetName(), (const char*)GetName());
        _entries[index] = entry;
    }
    _entries.Add(entry);
}

RString ParamEntry::GetContext(const char* member) const
{
    char buf1[512];
    bool first = false;
    if (member)
    {
        ::strcpy(buf1, member), first = true;
    }
    else
    {
        *buf1 = 0;
    }
    const ParamEntry* src = this;
    while (src)
    {
        char buf2[512];
        ::strncpy(buf2, buf1, sizeof(buf1));
        if (src->GetName())
        {
            ::strncpy(buf1, src->GetName(), sizeof(buf1));
        }
        else
        {
            snprintf(buf1, sizeof(buf1), "%s", (const char*)"");
            Fail("Bad context");
        }
        if (first)
        {
            strncat(buf1, ".", sizeof(buf1) - strlen(buf1) - 1), first = false;
        }
        else
        {
            strncat(buf1, "/", sizeof(buf1) - strlen(buf1) - 1);
        }
        strncat(buf1, buf2, sizeof(buf1) - strlen(buf1) - 1);
        const ParamClass* cls = dynamic_cast<const ParamClass*>(src);
        if (!cls)
        {
            break;
        }
        src = cls->_parent;
    }
    return buf1;
}

void ParamClass::Parse(QIStream& in, ParamFile* file)
{
    int c;
    // parse section content
    for (;;)
    {
        c = in.get();
        while (isspace(c))
        {
            c = in.get();
        }
        if (in.eof() || in.fail())
        {
            return;
        }
        if (c == '}')
        {
            c = in.get();
            while (isspace(c) || c == ';')
            {
                c = in.get();
            }
            in.unget();
            break; // section end reached
        }
        in.unget();
        WordBuf word;
        GetAlphaWord(word, sizeof(word), in);
        // word is entry name
        SRef<ParamEntry> newEntry;
        if (!strcmp(word, "class"))
        {
            // "class" may be forgotten now
            GetAlphaWord(word, sizeof(word), in);
            ParamClass* section = new ParamClass(word);
            section->_parent = this;
            // section header
            int c = in.get();
            while (isspace(c))
            {
                c = in.get();
            }
            if (c == ':')
            {
                // base class
                WordBuf base;
                GetAlphaWord(base, sizeof(base), in);
                ParamEntry* entry = Find(base, true, true, DefaultAccess); // search parents and bases of my parent
                if (!entry)
                {
                    ErrorMessage("%s: Undefined base class '%s'", (const char*)GetContext(word), (const char*)base);
                    return;
                }
                section->_base = dynamic_cast<ParamClass*>(entry);
                // Name lookup ignores case, so `class Burst: Burst` can resolve to an inherited `burst=1`
                // value; the class is then kept without a base.
                if (!section->_base)
                {
                    LOG_WARN(Core, "{}: '{}' is not class, ignoring the base", (const char*)GetContext(word),
                             (const char*)base);
                }
                c = in.get();
            }
            // find opening brace
            while (c != '{')
            {
                if (!isspace(c))
                {
                    ErrorMessage("%s: '%c' encountered instead of '{'", (const char*)GetContext(), c);
                    return;
                }
                c = in.get();
            }
            // parse section content
            section->Parse(in, file);
            newEntry = section;
        }
        else if (!strcmp(word, "enum"))
        {
            // "enum" may be forgotten now
            GetAlphaWord(word, sizeof(word), in);
            // enum name not used

            // find opening brace
            int c = in.get();
            while (c != '{')
            {
                if (!isspace(c))
                {
                    ErrorMessage("%s: '%c' encountered instead of '{'", (const char*)GetContext(), c);
                    return;
                }
                c = in.get();
            }
            int enumValue = 0;
            do
            {
                GetAlphaWord(word, sizeof(word), in);
                RString name = word;
                c = in.get();
                while (isspace(c))
                {
                    c = in.get();
                }
                if (c == '=')
                {
                    c = in.get();
                    while (isspace(c))
                    {
                        c = in.get();
                    }
                    in.unget();
                    GetWord(word, sizeof(word), in, ",}");
                    enumValue = toLargeInt(file->EvaluateFloatInternal(word));
                }
                file->VarSetFloatInternal(name, enumValue, true, true);
                // wrap rather than overflow int (UB) on a pathological enum{X=INT_MAX,Y}
                enumValue = (int)((unsigned)enumValue + 1u);
            } while (c == ',');
            if (c == '}')
            {
                c = in.get();
                while (isspace(c) || c == ';')
                {
                    c = in.get();
                }
                in.unget();
            }
            else
            {
                ErrorMessage("%s: '%c' encountered instead of '}'", (const char*)GetContext(), c);
                return;
            }
        }
        else if (!strcmp(word, "__EXEC"))
        {
            // find opening brace
            int c = in.get();
            while (c != '(')
            {
                if (!isspace(c))
                {
                    ErrorMessage("%s: '%c' encountered instead of '('", (const char*)GetContext(), c);
                    return;
                }
                c = in.get();
            }
            GetWord(word, sizeof(word), in, ")");
            c = in.get();
            if (c == ')')
            {
                c = in.get();
                while (isspace(c) || c == ';')
                {
                    c = in.get();
                }
                in.unget();
            }
            else
            {
                ErrorMessage("%s: '%c' encountered instead of ')'", (const char*)GetContext(), c);
                return;
            }
            file->ExecuteInternal(word);
        }
        else
        {
            // word should be value or array
            c = in.get();
            if (c == '[')
            {
                // word is array name
                ParamArray* array = new ParamArray(word);
                c = in.get();
                while (isspace(c))
                {
                    c = in.get();
                }
                if (c != ']')
                {
                    ErrorMessage("Config: %s: '%c' encountered instead of ']'", (const char*)GetContext(word), c);
                    return;
                }
                c = in.get();
                while (isspace(c))
                {
                    c = in.get();
                }
                if (c != '=')
                {
                    ErrorMessage("Config: %s: '%c' encountered instead of '='", (const char*)GetContext(word), c);
                    return;
                }
                array->Parse(in, file);
                c = in.get();
                while (isspace(c))
                {
                    c = in.get();
                }
                if (c != ';')
                {
                    ErrorMessage("%s: '%c' encountered instead of ';'", (const char*)GetContext(array->GetName()), c);
                    return;
                }
                newEntry = array;
            }
            else
            {
                while (isspace(c))
                {
                    c = in.get();
                }
                if (c != '=')
                {
                    char errorContext[1024];
                    GetWord(errorContext, sizeof(errorContext), in, "\n");
                    RptF("Error context %s", errorContext);
                    ErrorMessage("'%s': '%c' encountered instead of '='", (const char*)GetContext(word), c);
                    return;
                }
                RStringB valueName = word;
                c = in.get();
                while (isspace(c))
                {
                    c = in.get();
                }
                in.unget();
                bool quot;
                GetWord(word, sizeof(word), in, ";\n\r", &quot);
                c = in.get();
                if (c != ';' && c != '\n' && c != '\r')
                {
                    ErrorMessage("'%s': '%c' encountered instead of ';'", (const char*)GetContext(valueName), c);
                    return;
                }

                if (strncmp(word, "__EVAL", 6) == 0)
                {
                    ::strcpy(word, file->EvaluateStringInternal(word + 6));
                }
                else if (!quot && word[0] == '(')
                {
                    // Implicit arithmetic — `w = (1.0 - 2 * 0.02);` evaluates
                    // to 0.96.  Lets resource headers compose numeric layout
                    // constants from named macros without requiring the
                    // BIS-style __EVAL prefix on every site.
                    ::strcpy(word, file->EvaluateStringInternal(word));
                }

                // check if value is integer or float
                ParamEntry* value = nullptr;
                if (!quot)
                {
                    bool ok = false;
                    int val = ScanInt(word, ok);
                    if (ok)
                    {
                        value = new ParamValueInt(valueName);
                        value->SetValue(val);
                    }
                    else
                    {
                        float val = ScanFloat(word, ok);
                        if (ok)
                        {
                            value = new ParamValueFloat(valueName);
                            value->SetValue(val);
                        }
                    }
                }
                if (!value)
                {
                    value = new ParamValue(valueName);
                    value->SetValue(word);
                }

                // SetFile is done recursively at the end of parsing
                newEntry = value;
            }
        }
        // check for overload
        if (newEntry)
        {
            int baseIndex = FindIndex(newEntry->GetName());
            if (baseIndex < 0)
            {
                if (newEntry->GetName() == GetAccessString())
                {
                    newEntry->SetFile(file);
                    _access = (ParamAccessMode)newEntry->GetInt();
                }
                _entries.Add(newEntry);
            }
            else
            {
                ErrorMessage("%s: Member already defined.", (const char*)GetContext(newEntry->GetName()));
            }
        }
    }

    _entries.Compact();
    // check access protection mode
    CheckInheritedAccess();
}

void ParamClass::Update(const ParamClass& source)
{
    if (_access >= PAReadOnly)
    {
        if (source.GetEntryCount() > 0)
        {
            BString<256> buf;
            sprintf(buf, "** Update **, by %s", (const char*)source.GetContext());
            AccessDenied(buf);
        }
        return;
    }
    for (int i = 0; i < source.GetEntryCount(); i++)
    {
        const ParamEntry& srcEntry = source.GetEntry(i);
        // do not update base class, instead add new class
        ParamEntry* dstEntry = Find(srcEntry.GetName(), false, false, DefaultAccess);
        if (srcEntry.IsClass())
        {
            if (!dstEntry)
            {
                dstEntry = new ParamClass(srcEntry.GetName());
                dstEntry->SetOwner(srcEntry.GetOwner());
                NewEntry(dstEntry, true);
            }
            else if (!dstEntry->IsClass())
            {
                Fail("Cannot update non class from class");
                return;
            }

            const ParamClass* srcCls = static_cast<const ParamClass*>(&srcEntry);
            ParamClass* dstCls = static_cast<ParamClass*>(dstEntry);
            dstCls->_parent = this;
            if (srcCls->_base)
            {
                ParamEntry* baseEntry = Find(srcCls->_base->GetName(), true, true, DefaultAccess);
                if (!baseEntry || !baseEntry->IsClass())
                {
                    ErrorMessage("%s: Cannot find base class '%s'", (const char*)GetContext(),
                                 (const char*)srcCls->_base->GetName());
                    return;
                }
                // check if changing base is enabled
                if (baseEntry != dstCls->_base)
                {
                    if (dstCls->GetAccessMode() < PAReadOnly)
                    {
                        dstCls->_base = static_cast<ParamClass*>(baseEntry);
                    }
                    else
                    {
                        BString<256> buf;
                        sprintf(buf, "** Update base %s->%s **, by %s",
                                dstCls->_base ? (const char*)dstCls->_base->GetName() : "",
                                (const char*)baseEntry->GetName(), (const char*)source.GetContext());
                        dstCls->AccessDenied(buf);
                    }
                }
            }
            else
            {
                if (dstCls->_base)
                {
                    if (dstCls->GetAccessMode() < PAReadOnly)
                    {
                        dstCls->_base = nullptr;
                    }
                    else
                    {
                        BString<256> buf;
                        sprintf(buf, "** Update base %s-><null> **, by %s",
                                dstCls->_base ? (const char*)dstCls->_base->GetName() : "",
                                (const char*)source.GetContext());
                        dstCls->AccessDenied(buf);
                    }
                }
            }
            dstCls->Update(*srcCls);
        }
        else if (srcEntry.IsArray())
        {
            if (!dstEntry)
            {
                dstEntry = new ParamArray(srcEntry.GetName());
                NewEntry(dstEntry, true);
            }
            else if (!dstEntry->IsArray())
            {
                Fail("Cannot update non array from array");
                return;
            }
            else if (_access >= PAReadAndCreate)
            {
                // Existing array property is locked (PAReadAndCreate = add-only): skip
                // it and keep merging — do NOT abandon the rest of the update, or a
                // mod whose class begins with a locked array (e.g. CSLA's CfgVehicles
                // leads with access/vehicleClass) would lose every class that follows.
                AccessDenied("**Update**");
                continue;
            }

            const ParamArray* srcArr = static_cast<const ParamArray*>(&srcEntry);
            ParamArray* dstArr = static_cast<ParamArray*>(dstEntry);
            dstArr->Copy(*srcArr);
        }
        else if (_access >= PAReadAndCreate)
        {
            AccessDenied("**Update**");
        }
        else
        {
            // create dst entry of corresponding type
            if (dynamic_cast<const ParamValueFloat*>(&srcEntry))
            {
                if (!dstEntry)
                {
                    dstEntry = new ParamValueFloat(srcEntry.GetName());
                    NewEntry(dstEntry, true);
                }
                else if (_access >= PAReadAndCreate)
                {
                    AccessDenied("**Update**");
                    return;
                }
                dstEntry->SetValue((float)srcEntry);
            }
            else if (dynamic_cast<const ParamValueInt*>(&srcEntry))
            {
                if (!dstEntry)
                {
                    dstEntry = new ParamValueInt(srcEntry.GetName());
                    NewEntry(dstEntry, true);
                }
                else if (_access >= PAReadAndCreate)
                {
                    AccessDenied("**Update**");
                    return;
                }
                dstEntry->SetValue((int)srcEntry);
            }
            else if (dynamic_cast<const ParamValue*>(&srcEntry))
            {
                if (!dstEntry)
                {
                    dstEntry = new ParamValue(srcEntry.GetName());
                    NewEntry(dstEntry, true);
                }
                else if (_access >= PAReadAndCreate)
                {
                    AccessDenied("**Update**");
                    return;
                }
                dstEntry->SetValue(srcEntry.GetValueRaw());
            }
        }
    }
}

#define LOG_CHECKSUM 0

#if LOG_CHECKSUM
#include <Poseidon/Foundation/Algorithms/Crc.hpp>

class PASumCalculator : public CRCCalculator
{
};
#endif

void ParamClass::CalculateCheckValue(PASumCalculator& sum) const
{
#if LOG_CHECKSUM
    LOG_DEBUG(Core, "** Calculate CRC of '{}'", (const char*)GetName());
#endif
    // recursive get crc
    ParamAccessMode mode = GetAccessMode();
    for (int i = 0; i < _entries.Size(); i++)
    {
        const ParamEntry* entry = _entries[i];
        // scan all classes
        // other entries scan depending on class access mode
        if (mode >= PAReadOnlyVerified || entry->IsClass())
        {
            entry->CalculateCheckValue(sum);
#if LOG_CHECKSUM
            LOG_DEBUG(Core, "CRC after {} = {:x}", (const char*)entry->GetName(), sum.GetResult());
#endif
        }
    }
    // class name is added to the CRC only once
    if (mode >= PAReadOnlyVerified)
    {
        ParamFile::AddCRC(sum, GetName(), GetName().GetLength());
#if LOG_CHECKSUM
        LOG_DEBUG(Core, "add CRC of '{}'", (const char*)GetName());
#endif
    }
    if (_base && mode >= PAReadOnlyVerified)
    {
        ParamFile::AddCRC(sum, _base->GetName(), _base->GetName().GetLength());
#if LOG_CHECKSUM
        LOG_DEBUG(Core, "add CRC of base '{}'", (const char*)_base->GetName());
#endif
    }
}

bool ParamClass::HasChecksum() const
{
    // recursive get crc
    ParamAccessMode mode = GetAccessMode();
    if (mode >= PAReadOnlyVerified)
    {
        return true;
    }
    for (int i = 0; i < _entries.Size(); i++)
    {
        const ParamEntry* entry = _entries[i];
        // scan all classes and check if some of them is verified
        if (entry->IsClass())
        {
            bool ret = entry->HasChecksum();
            if (ret)
            {
                return true;
            }
        }
    }
    return false;
}

// Count classes that may be checked using CalculateCheckValue.
int ParamClass::GetNumberOfClassesForChecking() const
{
    int count = 0;
    for (int i = 0; i < _entries.Size(); i++)
    {
        const ParamEntry* entry = _entries[i];
        // scan all classes
        // other entries scan depending on class access mode
        if (!entry->IsClass())
        {
            continue;
        }
        PoseidonAssert(dynamic_cast<const ParamClass*>(entry));
        const ParamClass* cEntry = static_cast<const ParamClass*>(entry);
        count += cEntry->GetNumberOfClassesForChecking();
    }
    // check if this class is suitable for checking
    if (GetAccessMode() >= PAReadOnlyVerified || count > 0)
    {
        count++;
    }
    return count;
}

// Select a class that may be checked using CalculateCheckValue; index should be in
// [0, GetNumberOfClassesForChecking).
const ParamClass* ParamClass::SelectClassForChecking(int index) const
{
    if (index < 0)
    {
        return nullptr;
    }
    PoseidonAssert(index < GetNumberOfClassesForChecking());
    for (int i = 0; i < _entries.Size(); i++)
    {
        const ParamEntry* entry = _entries[i];
        // scan all classes
        // other entries scan depending on class access mode
        if (!entry->IsClass())
        {
            continue;
        }
        PoseidonAssert(dynamic_cast<const ParamClass*>(entry));
        const ParamClass* cEntry = static_cast<const ParamClass*>(entry);
        if (!cEntry)
        {
            continue;
        }
        int nCheckInCEntry = cEntry->GetNumberOfClassesForChecking();
        if (index < nCheckInCEntry)
        {
            const ParamClass* select = cEntry->SelectClassForChecking(index);
            if (select)
            {
                return select;
            }
        }
        index -= nCheckInCEntry;
    }
    if (index == 0)
    {
        return this;
    }
    return nullptr;
}

void ParamClass::Diagnostics(int indent) {}

DEFINE_FAST_ALLOCATOR(ParamFile)

// Meyer's singleton accessors for default function implementations - no global constructors!
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"

static EvaluatorFunctions& GetDefaultEvaluatorFunctions()
{
    static EvaluatorFunctions instance;
    return instance;
}

static LocalizeStringFunctions& GetDefaultLocalizeStringFunctions()
{
    static LocalizeStringFunctions instance;
    return instance;
}

static CRCFunctions& GetDefaultCRCFunctions()
{
    static CRCFunctions instance;
    return instance;
}

#pragma clang diagnostic pop

// Constant-init nullptr: no dynamic initializer, no SIOF.  ParamFile's
// inline DefaultXFunctions() accessors (paramFile.hpp) lazy-fall back
// to a function-local Meyer's singleton of the base callback class
// when nothing has registered yet.  Real impls register via the
// explicit Init*Defaults() functions called from Poseidon::InitDefaults().
EvaluatorFunctions* ParamFile::_defaultEvalFunctions = nullptr;
LocalizeStringFunctions* ParamFile::_defaultLocalizeStringFunctions = nullptr;
CRCFunctions* ParamFile::_defaultCRCFunctions = nullptr;

GameVarSpace* EvaluatorFunctions::LoadVariables(SerializeBinStream& f)
{
    int n;
    f.Transfer(n);
    PoseidonAssert(n == 0);
    return nullptr;
}

void EvaluatorFunctions::SaveVariables(SerializeBinStream& f, GameVarSpace* vars)
{
    int n = 0;
    f.Transfer(n);
}

} // namespace Poseidon
