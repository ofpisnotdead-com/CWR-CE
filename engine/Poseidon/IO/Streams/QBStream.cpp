#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#ifndef _WIN32
#include <climits>
#include <dirent.h>
#endif
#include <cctype>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/Foundation/platform.hpp>

#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/IO/Filesystem/FileOps.hpp>
#include <Poseidon/IO/Filesystem/DirScanner.hpp>
#include <Poseidon/IO/Streams/FileAccessPolicy.hpp>

#if MT_SAFE
#define EXCLUSIVE() ScopeLockSection lock(_lock)
#else
#define EXCLUSIVE()
#endif

namespace Poseidon
{
namespace
{
bool EqualPathComponent(const std::string& a, const char* b, size_t bLen)
{
    if (a.size() != bLen)
        return false;
    for (size_t i = 0; i < bLen; ++i)
    {
        if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

const char* LastPathComponent(const char* path)
{
    const char* last = path;
    for (const char* p = path; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
            last = p + 1;
    }
    return last;
}

struct ModRootAliasContext
{
    const char* prefix;
    size_t prefixLen;
    const char* rest;
    std::string resolved;
};

bool ResolveModRootAliasCallback(RStringB dir, void* opaque)
{
    if (dir.GetLength() == 0)
        return false;

    auto* context = static_cast<ModRootAliasContext*>(opaque);
    if (!EqualPathComponent(LastPathComponent(dir), context->prefix, context->prefixLen))
        return false;

    std::string candidate = (const char*)dir;
    if (!candidate.empty() && candidate.back() != '/' && candidate.back() != '\\')
        candidate += "/";
    candidate += context->rest;
    if (!QIFStream::FileExists(candidate.c_str()))
        return false;

    context->resolved = std::move(candidate);
    return true;
}

std::string ResolveModRootAlias(const char* name)
{
    if (!name || !*name || name[1] == ':' || *name == '/' || *name == '\\')
        return {};

    const char* separator = strpbrk(name, "/\\");
    if (!separator)
        return {};

    ModRootAliasContext context;
    context.prefix = name;
    context.prefixLen = separator - name;
    context.rest = separator + 1;
    if (context.prefixLen == 0 || !*context.rest)
        return {};

    ModSystem::EnumDirectories(ResolveModRootAliasCallback, &context);
    return context.resolved;
}
} // namespace

QFBank::QFBank()
{
    EXCLUSIVE();
    _handle = nullptr;
    _handleOverlapped = nullptr;
    _serialize = false;
    _error = true; // no open called yet
    _locked = true;
    _lockable = false;
}

#define WIN_DIR '\\'
#define UNIX_DIR '/'

#define BEG_SERIALIZE                \
    {                                \
        PoseidonAssert(!_serialize); \
        _serialize = true;           \
    }
#define END_SERIALIZE               \
    {                               \
        PoseidonAssert(_serialize); \
        _serialize = false;         \
    }

#ifdef NO_MMAP
#define USE_MAPPING 0
#else
#define USE_MAPPING 1
#endif

} // namespace Poseidon

#if USE_MAPPING
#include <Poseidon/IO/Streams/FileMapping.hpp>
#endif

#ifdef _WIN32
#include <Poseidon/IO/Streams/FileOverlapped.hpp>
#endif

namespace Poseidon
{

static int LoadInt(HANDLE f)
{
    int i = 0;
    DWORD rd = 0;
    ReadFile(f, &i, sizeof(i), &rd, nullptr);
    if (rd != sizeof(i))
    {
        i = 0;
    }
    return i;
}

static int LoadInt(QIStream& f)
{
    int i = 0;
    f.read(&i, sizeof(i));
    if (f.fail())
    {
        i = 0;
    }
    return i;
}

static void LoadFileInfo(FileInfoO& i, HANDLE f)
{
    // read zero terminated name
    char name[512];
    char* n = name;
    int maxLen = sizeof(name) - 1;
    DWORD rd;
    for (int l = 0; l < maxLen; l++)
    {
        char c;
        ReadFile(f, &c, sizeof(c), &rd, nullptr);
        if (rd != 1)
        {
            // error during file reading
            i.name = "";
            i.startOffset = 0;
            i.length = 0;
            return;
        }
        if (!c)
        {
            break;
        }
        *n++ = c;
    }
    *n = 0; // zero terminate in any case
    strlwr(name);
    i.name = name;
    i.compressedMagic = LoadInt(f);
    i.uncompressedSize = LoadInt(f);
    i.startOffset = LoadInt(f);
    i.time = LoadInt(f);
    i.length = LoadInt(f);
}

static void LoadFileInfo(FileInfoO& i, QIStream& f)
{
    // read zero terminated name
    char name[512];
    char* n = name;
    int maxLen = sizeof(name) - 1;
    for (int l = 0; l < maxLen; l++)
    {
        int c = f.get();
        if (c < 0)
        {
            // error during file reading
            i.name = "";
            i.startOffset = 0;
            i.length = 0;
            return;
        }
        if (!c)
        {
            break;
        }
        *n++ = c;
    }
    *n = 0; // zero terminate in any case
    strlwr(name);
    i.name = name;
    i.compressedMagic = LoadInt(f);
    i.uncompressedSize = LoadInt(f);
    i.startOffset = LoadInt(f);
    i.time = LoadInt(f);
    i.length = LoadInt(f);
}

} // namespace Poseidon

bool GLogFileOps = false;

namespace Poseidon
{

// simple logging - log what is used, do not check for double files

class QFBankLog : public IBankLog
{
    FILE* _file;
    FindArray<RString> _files; // files aready there

  public:
    void Init(const char* bankName) override;
    void LogFileOp(const char* name) override;
    void Flush(const char* bankName) override;

    QFBankLog();
    ~QFBankLog() override;

  private:
    void Close();
};

void QFBankLog::Init(const char* bankName)
{
    Close();

    char logName[256];
    snprintf(logName, sizeof(logName), "%s", (const char*)"FilesUsed");
    ::CreateDirectory(logName, nullptr);
    strncat(logName, PATH_SEP_STR, sizeof(logName) - strlen(logName) - 1);
    strncat(logName, bankName, sizeof(logName) - strlen(logName) - 1);
    size_t len = strlen(logName);
    if (len > 0 && (logName[len - 1] == '\\' || logName[len - 1] == '/'))
    {
        logName[len - 1] = 0;
    }
    strncat(logName, ".log", sizeof(logName) - strlen(logName) - 1);

#ifndef _WIN32
    unixPath(logName);
#endif
    FILE* file = fopen(logName, "r");
    if (file)
    {
        // read filenames already there
        for (;;)
        {
            char buf[1024];
            *buf = 0;
            fgets(buf, sizeof(buf), file);
            if (!*buf)
            {
                break;
            }
            if (buf[strlen(buf) - 1] == '\n')
            {
                buf[strlen(buf) - 1] = 0;
            }
            _files.Add(buf);
        }
        fclose(file), file = nullptr;
    }
    _file = fopen(logName, "a+");
}

void QFBankLog::LogFileOp(const char* name)
{
    if (!_file)
    {
        return;
    }
    // check if file is already there
    if (_files.Find(name) >= 0)
    {
        return;
    }
    fprintf(_file, "%s\n", name);
}

void QFBankLog::Flush(const char* bankName)
{
    if (_file)
    {
        fflush(_file);
    }
}

QFBankLog::QFBankLog()
{
    _file = nullptr;
}

void QFBankLog::Close()
{
    if (_file)
    {
        fclose(_file);
        _file = nullptr;
    }
}

QFBankLog::~QFBankLog()
{
    Close();
}

RString LoadFromFile(HANDLE file)
{
    BString<1024> buf;
    for (;;)
    {
        char c[2] = {0, 0}; // c[1] must stay NUL: ReadFile writes only c[0], and
                            // buf += c treats c as a C-string — an uninitialized
                            // c[1] would append stack garbage and overrun buf.
        DWORD size = sizeof(char);
        BOOL ok = ReadFile(file, c, size, &size, nullptr);
        if (!ok || size != 1)
        {
            break;
        }
        if (c[0] == 0)
        {
            break;
        }
        buf += c;
    }
    return RString(buf);
}

bool SaveToFile(HANDLE file, RString value)
{
    int len = value.GetLength();
    DWORD size = sizeof(len);
    BOOL ok = WriteFile(file, &len, size, &size, nullptr);
    return ok != FALSE;
}
// Opening may be deferred: this may only remember the parameters and perform the
// real open later. beforeOpen is called once the header is loaded, to decide
// whether the bank should be loaded.
bool QFBank::open(RString name, OpenCallback beforeOpen, BankContextBase* context)
{
    char fullName[1024];
    snprintf(fullName, sizeof(fullName), "%s", (const char*)name);
    strncat(fullName, ".pbo", sizeof(fullName) - strlen(fullName) - 1);

    HANDLE handle = OpenFileForRead(fullName);
    if (handle == INVALID_HANDLE_VALUE)
    {
        Foundation::ErrorMessage("Cannot open file '%s'", fullName);
        _openName = "";
        return false;
    }
    CloseHandle(handle);
    _openName = fullName;

    _openBeforeOpenCallback = beforeOpen;
    _openContext = context;
    _files.Clear();
    _error = false;
    return true;
}

// All banks are locked by default; an app that wants a bank unloadable must unlock it.
void QFBank::Lock() const
{
    _locked = true;
    Load();
}

void QFBank::Unlock() const
{
    _locked = false;
    if (_fileAccess && _fileAccess->RefCounter() == 1)
    {
        Unload();
    }
}

bool QFBank::CanBeUnloaded() const
{
    return _fileAccess && _fileAccess->RefCounter() == 1;
}

// Meyer's singleton for bank functions - no global constructor!
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
static QFBankFunctions& GetDefaultQFBankFunctions()
{
    static QFBankFunctions instance;
    return instance;
}
#pragma clang diagnostic pop

// Constant-init nullptr (SIOF-safe); DefaultFunctions() falls back to a no-op base
// instance until a real implementation registers.
QFBankFunctions* QFBank::_defaultFunctions = nullptr;

// Platform directory separator: entries inside PBO banks are stored
// with the native separator so that lookups work without conversion.
#ifdef __GNUC__
static constexpr char NATIVE_DIR = UNIX_DIR;
static constexpr char FOREIGN_DIR = WIN_DIR;
static const RString NATIVE_DIR_STR("/");
#else
static constexpr char NATIVE_DIR = WIN_DIR;
static constexpr char FOREIGN_DIR = UNIX_DIR;
static const RString NATIVE_DIR_STR("\\");
#endif

static inline RStringB ConvertDirSlash(RStringB name)
{
    const char* change = strchr(name, FOREIGN_DIR);
    if (!change)
    {
        return name;
    }
    // make sure name is mutable
    RString mutableName = name;
    char* mutName = mutableName.MutableData();
    for (;;)
    {
        char* change = strchr(mutName, FOREIGN_DIR);
        if (!change)
        {
            break;
        }
        *change = NATIVE_DIR;
    }
    return mutableName;
}

bool QFBank::Load()
{
    if (!_locked)
    {
        RptF("Cannot open bank %s that is not locked", (const char*)_prefix);
        return false;
    }
    if (_error)
    {
        return false;
    }
    if (_handle)
    {
        return true;
    }
    _files.Clear();
    EXCLUSIVE();
    // note: name should not contain extension
    // automatic optimal bank type is performed with different extensions
    // like .pbf and .pbo
    if (GLogFileOps)
        LOG_DEBUG(Core, "Load bank {}", (const char*)_openName);
    PoseidonAssert(!_fileAccess);

    HANDLE handle = OpenFileForRead((const char*)_openName);
    if (handle == INVALID_HANDLE_VALUE)
    {
        Foundation::ErrorMessage("Cannot open file '%s'", (const char*)_openName);
        _handle = nullptr;
    }
    else
    {
        _handle = (WINHANDLE)(intptr_t)handle;
#ifdef _WIN32
        _handleOverlapped =
            CreateFile(_openName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (!_handleOverlapped || _handleOverlapped == INVALID_HANDLE_VALUE)
        {
            _handleOverlapped = nullptr;
            LOG_DEBUG(Core, "Cannot open overlapped file access on {}", (const char*)_openName);
        }
#endif
    }

    _pos = _wantPos = 0;
    // int64: a tampered bank with huge per-file lengths must not overflow the
    // running offset (UB); a sum past int range means a corrupt bank — stop.
    int64_t startOffset = 0;
    FileInfoO info;
    for (;;)
    {
        LoadFileInfo(info, (HANDLE)(intptr_t)_handle);

        info.startOffset = static_cast<int>(startOffset);
        startOffset += info.length;
        if (info.name.GetLength() <= 0 || startOffset < 0 || startOffset > INT_MAX)
        {
            break;
        }

        info.name = ConvertDirSlash(info.name);
        _files.Add(info);
    }
    // if bank is empty, it may be "new bank" with product identification
    // check if there is normal terminator, or special
    if (_files.NItems() == 0 && info.compressedMagic == VersionMagic && info.length == 0 && info.time == 0)
    {
        // read properties
        for (;;)
        {
            RString name = LoadFromFile((HANDLE)(intptr_t)_handle);
            if (name.GetLength() == 0)
            {
                break;
            }
            RString value = LoadFromFile((HANDLE)(intptr_t)_handle);
            QFProperty& prop = _properties.Append();
            prop.name = name;
            prop.value = value;
        }

        if (_openBeforeOpenCallback)
        {
            bool ok = _openBeforeOpenCallback(this, _openContext);
            if (!ok)
            {
                CloseHandle(_handle), _handle = nullptr;
                _error = true;
                return false;
            }
        }

        // read normal file headers
        RString encryption = GetProperty("encryption");
        if (encryption.GetLength() > 0)
        {
            // we need to load encrypted headers
            // for this we need to know headers encrypted size
            // load decoded size
            int headersSize = LoadInt((HANDLE)(intptr_t)_handle);
            int headersEncodedSize = LoadInt((HANDLE)(intptr_t)_handle);
            // Both sizes come straight off the wire; reject negative or absurd values
            // before they drive a Temp<char> allocation (a negative int becomes a
            // near-2^64 size_t). A PBO header block far above this is malformed.
            constexpr int kMaxHeaderBytes = 64 * 1024 * 1024;
            if (headersSize < 0 || headersSize > kMaxHeaderBytes || headersEncodedSize < 0 ||
                headersEncodedSize > kMaxHeaderBytes)
            {
                CloseHandle(_handle), _handle = nullptr;
                _error = true;
                return false;
            }
            // read encoded headers into memory
            Temp<char> headers(headersEncodedSize);
            DWORD rd = 0;
            ReadFile(_handle, headers.Data(), headers.Size(), &rd, nullptr);
            if (rd == static_cast<DWORD>(headers.Size()))
            {
                Ref<IFilebankEncryption> ss = CreateFilebankEncryption(encryption, nullptr);
                if (ss)
                {
                    QIStream headersEncoded(headers.Data(), headers.Size());
                    Temp<char> headerDecodedData(headersSize);
                    ss->Decode(headerDecodedData.Data(), headerDecodedData.Size(), headersEncoded);
                    QIStream headersDecoded(headerDecodedData.Data(), headerDecodedData.Size());
                    for (;;)
                    {
                        LoadFileInfo(info, headersDecoded);

                        info.startOffset = startOffset;
                        startOffset += info.length;
                        if (info.name.GetLength() <= 0)
                        {
                            break;
                        }

                        info.name = ConvertDirSlash(info.name);
                        _files.Add(info);
                    }
                }
            }
        }
        else
        {
            for (;;)
            {
                LoadFileInfo(info, (HANDLE)(intptr_t)_handle);

                info.startOffset = startOffset;
                startOffset += info.length;
                if (info.name.GetLength() <= 0)
                {
                    break;
                }

                info.name = ConvertDirSlash(info.name);
                _files.Add(info);
            }
        }
    }
    else
    {
        if (_openBeforeOpenCallback)
        {
            bool ok = _openBeforeOpenCallback(this, _openContext);
            if (!ok)
            {
                _files.Clear();
                CloseHandle(_handle), _handle = nullptr;
                _error = true;
                return false;
            }
        }
    }

    int headerSize = SetFilePointer(_handle, 0, nullptr, FILE_CURRENT);
    BEG_SERIALIZE
    // filemapping uses offset from end of header; direct access uses file offset
    if (_files.NItems() > 0)
    {
        // !!! avoid GetTable when NItems == 0
        for (int i = 0; i < _files.NTables(); i++)
        {
            AutoArray<FileInfoO>& container = _files.GetTable(i);
            for (int j = 0; j < container.Size(); j++)
            {
                FileInfoO& info = container[j];
                // int64 add then truncate: a corrupt bank's offset + headerSize
                // could overflow int (UB); a bogus result fails later on read.
                info.startOffset = static_cast<int>(static_cast<int64_t>(info.startOffset) + headerSize);
            }
        }
    }
    startOffset += headerSize;
    // check integrity - try to seek end of file
    DWORD checkEof = SetFilePointer(_handle, startOffset, nullptr, FILE_BEGIN);
    if ((int)checkEof != startOffset)
    {
        Foundation::ErrorMessage("Data file too short '%s'.", (const char*)_openName);
    }
    else
    {
        _pos = checkEof;
    }

    // do not map headers, only file content
    _fileAccess =
        QFileAccess::TryOpenMappedBankAccess((HANDLE)(intptr_t)_handle, checkEof - headerSize, (const char*)_openName);

    ScanPatchFiles(_prefix, RString());

    END_SERIALIZE
    return true;
}

void QFBank::Unload()
{
    // if error, there is nothing to undo
    if (_error)
    {
        return;
    }
    // if there is no handle, there is nothing to undo
    if (!_handle)
    {
        return;
    }
    if (_fileAccess && _fileAccess->RefCounter() > 1)
    {
        // if some file from bank is still used, we cannot unload it
        LOG_DEBUG(Core, "Cannot unload bank {}, {} files are still open", (const char*)_openName,
                  _fileAccess->RefCounter() - 1);
        return;
    }
    if (GLogFileOps)
        LOG_DEBUG(Core, "Unload bank {}", (const char*)_openName);
    Clear();
}

bool QFBank::error() const
{
    // if bank was not opened yet, it cannot have any fatal errors
    if (!_error)
    {
        return false;
    }
    // if there is no handle, there is some fatal error
    if (!_handle || (HANDLE)(intptr_t)_handle == INVALID_HANDLE_VALUE)
    {
        return true;
    }
    return false;
}

const RString& QFBank::GetProperty(const RString& name) const
{
    for (int i = 0; i < _properties.Size(); i++)
    {
        if (!strcmpi(_properties[i].name, name))
        {
            return _properties[i].value;
        }
    }
// Empty string constant for error return
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
    const static RString empty;
#pragma clang diagnostic pop
    return empty;
}

void QFBank::ScanPatchFiles(RString prefix, RString subdir)
{
#if _ENABLE_PATCHING
    if (prefix.GetLength() <= 0)
    {
        // patching bank with no prefix is nonsense
        // most likely cause is temporary bank (like in single missions)
        return;
    }

    // check if there is folder containing patch files
    BString<1024> wildname;
    strcpy(wildname, prefix);
    strcat(wildname, subdir);

#ifdef _WIN32

    strcat(wildname, "*.*");
    _finddata_t find;
    intptr_t hf = _findfirst(wildname, &find); // _findfirst returns intptr_t (x64)
    if (hf >= 0)
    {
        do
        {
            char lowName[256];
            snprintf(lowName, sizeof(lowName), "%s", (const char*)find.name);
            strlwr(lowName);
            RString name = lowName;
            if (find.attrib & _A_SUBDIR)
            {
                if (!strcmp(find.name, ".") || !strcmp(find.name, ".."))
                {
                    continue;
                }
                ScanPatchFiles(prefix, subdir + name + RString("\\"));
            }
            else
            {
                RString subname = subdir + name;
                const FileInfoO& file = _files[subname];
                if (_files.NotNull(file))
                {
                    LOG_DEBUG(Core, "Plain file version of {} used", (const char*)(subname));
                    FileInfoO fileSet = file;
                    fileSet.loadFromFile = true;
                    _files.Add(fileSet);
                }
            }

        } while (_findnext(hf, &find) == 0);

        _findclose(hf);
    }

#else

    unixPath((char*)(const char*)wildname);
    int len = strlen(wildname);
    if (len > 0 && wildname[len - 1] == UNIX_DIR)
        wildname[--len] = (char)0;
    DIR* dir = opendir(wildname);
    if (!dir)
        return;
    struct dirent* entry;
    while ((entry = readdir(dir)))
    {
        if (entry->d_name[0] == '.' && (!entry->d_name[1] || entry->d_name[1] == '.' && !entry->d_name[2]))
            continue;
        // stat the entry <= subdirectories must be handled differently
        wildname += "/";
        wildname += entry->d_name;
        struct stat st;
        if (!stat(wildname, &st))
        {
            if (S_ISDIR(st.st_mode))
            { // directory
                ScanPatchFiles(prefix, subdir + entry->d_name);
            }
            else
            { // regular file
                RString subname = subdir + entry->d_name;
                const FileInfoO& file = _files[subname];
                if (_files.NotNull(file))
                {
                    LOG_DEBUG(Core, "Plain file version of {} used", (const char*)(subname));
                    FileInfoO fileSet = file;
                    fileSet.loadFromFile = true;
                    _files.Add(fileSet);
                }
            }
        }
        wildname[len] = (char)0;
    }
    closedir(dir);

#endif

#endif
}

void QFBank::SetPrefix(RString prefix)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", (const char*)prefix);
    platformPath(buf);
    _prefix = buf;
    // create log
    if (GLogFileOps)
    {
        _log = new QFBankLog();
        _log->Init(_prefix);
    }
}

const FileInfoO& QFBank::FindFileInfo(const char* name) const
{
    EXCLUSIVE();
    char lowName[128];
    snprintf(lowName, sizeof(lowName), "%s", (const char*)name);
    strlwr(lowName);
    platformPath(lowName);
    return _files[lowName];
}

bool QFBank::FileExists(const char* name) const
{
    if (!Load())
    {
        return false;
    }
    const FileInfoO& info = FindFileInfo(name);
    return NotNull(info);
}

void QFBank::Seek(long pos) const
{
    EXCLUSIVE();
    _wantPos = pos;
}

void QFBank::Read(char* buf, long size, const char* name) const
{
    PoseidonAssert(!_error);
    BEG_SERIALIZE
    EXCLUSIVE(); // seek to wanted position
    if (_pos != _wantPos)
    {
        DWORD nPos = SetFilePointer(_handle, _wantPos, nullptr, FILE_BEGIN);
        if ((int)nPos != _wantPos)
        {
            Foundation::ErrorMessage("Read: Data file seek error (%s: %d,%d).", name, nPos, _wantPos);
        }
        else
        {
            _pos = nPos;
        }
    }
    // read into the temporary buffer
    DWORD bytes;
    if (!ReadFile(_handle, buf, size, &bytes, nullptr) || (int)bytes != size)
    {
        Foundation::ErrorMessage("Data file read error (%s).", name);
    }
    _pos += bytes;
    _wantPos += size;
    END_SERIALIZE
}

} // namespace Poseidon
#include <Poseidon/IO/Streams/FileCompress.hpp>

namespace Poseidon
{

// Orders files within the bank: a smaller result is nearer the start. Equal
// results mean unknown relative order, and the values are not contiguous. Lets a
// caller loading many files schedule reads for near-sequential access.
int QFBank::GetFileOrder(const char* file)
{
    const FileInfoO& info = FindFileInfo(file);
    if (IsNull(info))
    {
        return 0;
    }
    return info.startOffset;
}

Ref<IFileBuffer> QFBank::Read(const char* name) const
{
    if (!Load())
    {
        return nullptr;
    }
    // log every file opened, even when the open later fails
    if (_log)
    {
        _log->LogFileOp(name);
    }

    const FileInfoO& info = FindFileInfo(name);
    if (IsNull(info))
    {
        return nullptr;
    }
#if _ENABLE_PATCHING
    if (info.loadFromFile)
    {
        // patch file provided - use it
        char fullName[512];
        snprintf(fullName, sizeof(fullName), "%s", (const char*)GetPrefix());
        strncat(fullName, name, sizeof(fullName) - strlen(fullName) - 1);
        Ref<IFileBuffer> data = QFileAccess::OpenFileBufferAuto(fullName);
        if (!data->GetError())
        {
            return (IFileBuffer*)data;
        }
    }
#endif

    if (info.compressedMagic == CompMagic) // some compression
    {
        Seek(info.startOffset);
        QIStream inBuf;
        // read compressed data into temporary buffer
        Temp<char> cData(info.length);
        inBuf.init(cData, info.length);
        Read(cData.Data(), info.length, name);
        // uncompress
        Ref<FileBufferMemory> data = new FileBufferMemory(info.uncompressedSize);
        SSCompress ss;
        if (!ss.Decode(data->GetWritableData(), info.uncompressedSize, inBuf))
        {
            RptF("Error decoding %s from %s", name, (const char*)_prefix);
            return nullptr;
        }
        return (IFileBuffer*)data;
    }
    else if (info.compressedMagic == EncrMagic)
    {
        Seek(info.startOffset);
        QIStream inBuf;
        // read compressed data into temporary buffer
        Temp<char> cData(info.length);
        inBuf.init(cData, info.length);
        Read(cData.Data(), info.length, name);
        // ask compression manager to create encryptor/decryptor
        // uncompress
        Ref<FileBufferMemory> data = new FileBufferMemory(info.uncompressedSize);
        Ref<IFilebankEncryption> ss = CreateFilebankEncryption(GetProperty("encryption"), nullptr);
        if (!ss || !ss->Decode(data->GetWritableData(), info.uncompressedSize, inBuf))
        {
            RptF("Error decoding %s from %s", name, (const char*)_prefix);
            return nullptr;
        }
        return (IFileBuffer*)data;
    }
    else if (info.compressedMagic == 0)
    {
        // no compression

#if USE_MAPPING
        if (_fileAccess)
        {
            // use memory mapped file subsection
            Ref<IFileBuffer> data = new FileBufferSub(_fileAccess, info.startOffset, info.length);
            return data;
        }
#endif
        Seek(info.startOffset);
        // read directly from file
        Ref<FileBufferMemory> data = new FileBufferMemory(info.length);
        Read(data->GetWritableData(), info.length, name);
        return data.GetRef();
    }
    else
    {
        // some unknown compression manager
        Fail("Unknown compression manager");
        return nullptr;
    }
}

Ref<IFileBuffer> QFBank::ReadOverlapped(const char* file) const
{
    if (!Load())
    {
        return nullptr;
    }
    if (!_handleOverlapped)
    {
        // overlapped IO not supported - fall back to normal case
        return Read(file);
    }
#ifdef _WIN32
    const FileInfoO& info = FindFileInfo(file);
    if (IsNull(info))
    {
        return nullptr;
    }
#if _ENABLE_PATCHING
    if (info.loadFromFile)
    {
        return Read(file);
    }
#endif
    if (info.compressedMagic == CompMagic)
    {
        Ref<IFileBuffer> data =
            new FileBufferOverlapped(_handleOverlapped, info.uncompressedSize, info.startOffset, info.length);
        return data;
    }
    Ref<IFileBuffer> data = new FileBufferOverlapped(_handleOverlapped, info.startOffset, info.length);
    return data;
#else
    return Read(file);
#endif
}

#if USE_MAPPING
bool QFBank::BufferOwned(const FileBufferMapped* buffer) const
{
    return buffer->GetFileHandle() == (HANDLE)(intptr_t)_handle;
}
#endif

bool QFBank::BufferOwned(const FileBufferOverlapped* buffer) const
{
#ifdef _WIN32
    return buffer->GetFileHandle() == _handleOverlapped;
#else
    return false;
#endif
}

void QFBank::ForEach(void (*Func)(const FileInfoO& fi, const FileBankType* files, void* context), void* context) const
{
    if (!Load())
    {
        return;
    }
    EXCLUSIVE();
    _files.ForEach(Func, context);
}

void QFBank::Clear()
{
    EXCLUSIVE();
    // clear variables that are not part of Global structure
    _files.Clear();
    _fileAccess.Free();
    if (_handle)
    {
        CloseHandle(_handle), _handle = nullptr;
    }
    if (_handleOverlapped)
    {
        CloseHandle(_handleOverlapped), _handleOverlapped = nullptr;
    }
}

QFBank::~QFBank()
{
    EXCLUSIVE();
    Clear();
}

// AutoArray<QFBank> (BankList) relocates on grow via ModernTraits::MoveData,
// which move-constructs each element into the new storage and then destructs the
// source.  All members except the raw descriptors are reference-counted or value
// types that survive the copy + source destruction; the descriptors must be
// detached from the source so its ~QFBank()->Clear() does not CloseHandle() the
// fd the relocated bank still uses (otherwise a later compressed read — e.g.
// stringtable.csv from O.pbo — seeks a closed fd: "Data file seek error … -1").
QFBank::QFBank(QFBank&& other) : QFBank(static_cast<const QFBank&>(other))
{
    other._handle = nullptr;
    other._handleOverlapped = nullptr;
}

void QIFStreamB::open(const QFBank& bank, const char* name)
{
    _error = LSUnknownError;
    _sharedData = bank.Read(name);
    if (!_sharedData)
    {
        return;
    }
    init(_sharedData->GetData(), _sharedData->GetSize());
    _bank = &bank;
}

// Global file bank state
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"
BankList GFileBanks;
RString GFileBankPrefix; // HW config dependent banks
#pragma clang diagnostic pop

} // namespace Poseidon

bool GUseFileBanks;

namespace Poseidon
{

bool QFBankFunctions::FreeUnusedBanks(size_t sizeNeeded) const
{
    return GFileBanks.UnloadUnused();
}

#ifdef _WIN32

FindBank::FindBank()
{
    _info = nullptr; // Initialize to prevent invalid delete
    _handle = -1;    // -1 is the error value for _findfirst
}

FindBank::~FindBank()
{
    Close();
}

bool FindBank::First(const char* path)
{
    strcpy(_wild, path);
    strcat(_wild, "\\*.pbo");
    _info = new _finddata_t;
    _handle = _findfirst(_wild, (_finddata_t*)_info);
    return _handle != -1;
}

bool FindBank::Next()
{
    if (!_info || _handle == -1)
    {
        return false;
    }
    return _findnext(_handle, (_finddata_t*)_info) == 0;
}

void FindBank::Close()
{
    if (_info)
    {
        delete (_finddata_t*)_info;
        _info = nullptr;
    }
    if (_handle != -1)
    {
        _findclose(_handle);
        _handle = -1;
    }
}

const char* FindBank::GetName() const
{
    if (!_info)
    {
        return "";
    }
    return ((_finddata_t*)_info)->name;
}

#else

FindBank::FindBank() {}

FindBank::~FindBank()
{
    Close();
}

bool FindBank::First(const char* path)
{
    Close();
    LocalPath(dirName, path);
    return _scanner.First(dirName, ".pbo");
}

bool FindBank::Next()
{
    return _scanner.Next();
}

void FindBank::Close()
{
    _scanner.Close();
}

const char* FindBank::GetName() const
{
    return _scanner.GetName();
}

#endif

// The callback may be invoked even after Load returns; the context is stored in
// the bank meanwhile.
void BankList::Load(const RString& path, const RString& bankPrefix, const RString& bName, bool emptyPrefix,
                    OpenCallback beforeOpen, OpenCallback afterOpen, BankContextBase* context)
{
    int oldSize = Size();
    int index = Add();
    QFBank& bank = Set(index);
    // path can differ from bankPrefix (for example if Mod is used)
    if (!bank.open(path + bName, beforeOpen, context))
    {
        Resize(oldSize);
        return;
    }
    Log("Open bank %s", (const char*)(bankPrefix + bName));

    if (bank.error())
    {
        Resize(oldSize);
        return;
    }

    RString prefix;
    if (emptyPrefix)
    {
        prefix = bName;
    }
    else
    {
        prefix = bankPrefix + bName;
    }

    RString prefixPath = prefix + NATIVE_DIR_STR;
    bank.SetPrefix(prefixPath);

    if (afterOpen && !afterOpen(&bank, context))
    {
        Resize(oldSize);
        return;
    }
}

void BankList::Unload(const RString& bankPrefix, const RString& bName, bool emptyPrefix)
{
    // find corresponding bank
    RString prefix;
    if (emptyPrefix)
    {
        prefix = bName;
    }
    else
    {
        prefix = bankPrefix + bName;
    }

    RString prefixPath = prefix + NATIVE_DIR_STR;

    for (int i = 0; i < Size(); i++)
    {
        const QFBank& b = Get(i);
        if (b.GetPrefix() == prefixPath)
        {
            Delete(i);
            return;
        }
    }
}

void BankList::Lock(const RString& prefix)
{
    for (int i = 0; i < Size(); i++)
    {
        const QFBank& b = Get(i);
        if (b.GetPrefix() == prefix)
        {
            b.Lock();
            return;
        }
    }
    LOG_DEBUG(Core, "Lock: Bank {} not found", (const char*)prefix);
}
void BankList::Unlock(const RString& prefix)
{
    for (int i = 0; i < Size(); i++)
    {
        const QFBank& b = Get(i);
        if (b.GetPrefix() == prefix)
        {
            b.Unlock();
            return;
        }
    }
    LOG_DEBUG(Core, "Unlock: Bank {} not found", (const char*)prefix);
}

void BankList::SetLockable(const RString& prefix, bool lockable)
{
    for (int i = 0; i < Size(); i++)
    {
        QFBank& b = Set(i);
        if (b.GetPrefix() == prefix)
        {
            b.SetLockable(lockable);
            return;
        }
    }
    LOG_DEBUG(Core, "MakeLockable: Bank {} not found", (const char*)prefix);
}

bool BankList::UnloadUnused()
{
    // if it is not locked, is is probably already unloaded
    // but we still may want to try it first
    for (int i = 0; i < GFileBanks.Size(); i++)
    {
        QFBank& bank = GFileBanks[i];
        if (bank.IsLocked())
        {
            continue;
        }
        if (!bank.CanBeUnloaded())
        {
            continue;
        }
        LOG_DEBUG(Core, "Unloading bank {}", (const char*)bank.GetPrefix());
        bank.Unload();
        return true;
    }
    // if no unlocked bank is available for unloading, try locked banks
    for (int i = 0; i < GFileBanks.Size(); i++)
    {
        QFBank& bank = GFileBanks[i];
        if (!bank.CanBeUnloaded())
        {
            continue;
        }
        LOG_DEBUG(Core, "Unloading locked bank {}", (const char*)bank.GetPrefix());
        bank.Unload();
        return true;
    }
    return false;
}

void QIFStreamB::ClearBanks()
{
    GFileBanks.Clear();
}

QFBank* QIFStreamB::AutoBank(const char* name)
{
    if (!*name)
    {
        return nullptr;
    }
    if (name[1] == ':')
    {
        return nullptr;
    }
    for (int i = 0; i < GFileBanks.Size(); i++)
    {
        QFBank& bank = GFileBanks[i];
        if (!CmpStartStr(name, bank.GetPrefix()))
        {
            return &bank;
        }
    }
    return nullptr;
}

QIFStreamB::QIFStreamB() : _bank(nullptr) {}

void QIFStreamB::AutoOpen(const char* name, IQFBankContext* context)
{
    const char* name0 = name;
    if (GUseFileBanks)
    {
        QFBank* bank = AutoBank(name);
        if (bank)
        {
            if (context && !context->IsAccessible(bank))
            {
                RptF("AutoOpen %s: access denied", name0);
                return;
            }
            // check if we should use bank version of the file
            // skip bank name
            name += bank->GetPrefix().GetLength();
            open(*bank, name);
            if (_sharedData)
            {
                _bank = bank;
                return;
            }
            // if file does not exist in bank, try to open it from file
            LOG_DEBUG(Core, "File {} not in bank", name0);
        }
    }
    QIFStream::open(name0);
    if (_sharedData && !_sharedData->GetError())
    {
        return;
    }

    std::string modAlias = ResolveModRootAlias(name0);
    if (!modAlias.empty())
    {
        QIFStream::open(modAlias.c_str());
    }
}

bool QIFStreamB::IsFromBank(const QFBank* bank) const
{
    // check request to flush all banks
    if (!bank)
    {
        return true;
    }
    return bank == _bank;
}

bool QIFStreamB::FileExist(const char* name, IQFBankContext* context)
{
    if (GUseFileBanks)
    {
        QFBank* bank = AutoBank(name);
        if (bank)
        {
            if (context && !context->IsAccessible(bank))
            {
                const char* rName = name + bank->GetPrefix().GetLength();
                if (bank->FileExists(rName))
                {
                    return false;
                }
                LOG_DEBUG(Core, "FileExist {}: access denied", name);
                return false;
            }
            const char* rName = name + bank->GetPrefix().GetLength();
            if (bank->FileExists(rName))
            {
                return true;
            }
        }
    }
    if (QIFStream::FileExists(name))
    {
        return true;
    }

    std::string modAlias = ResolveModRootAlias(name);
    return !modAlias.empty();
}

struct EncryptorInformation
{
    RString name;
    IFilebankEncryption* (*createFunction)(const void* context);
};

template <>
struct FindArrayKeyTraits<EncryptorInformation>
{
    typedef const char* KeyType;
    static bool IsEqual(const char* a, const char* b) { return !strcmpi(a, b); }
    static const char* GetKey(const EncryptorInformation& a) { return a.name; }
};

// Global encryption registry
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"
static FindArrayKey<EncryptorInformation> GEncryptors;
#pragma clang diagnostic pop

void RegisterFilebankEncryption(const char* name, IFilebankEncryption* (*createFunction)(const void* context))
{
    // check if given encyption already exists
    int index = GEncryptors.FindKey(name);
    if (index >= 0)
    {
        LOG_ERROR(Core, "Ecryption {} already registered", name);
        return;
    }
    EncryptorInformation& ei = GEncryptors.Append();
    ei.name = name;
    ei.createFunction = createFunction;
}

Ref<IFilebankEncryption> CreateFilebankEncryption(const char* name, const void* context)
{
    int index = GEncryptors.FindKey(name);
    if (index < 0)
    {
        LOG_ERROR(Core, "Unknown encryption {}", name);
        return nullptr;
    }
    return GEncryptors[index].createFunction(context);
}

} // namespace Poseidon
