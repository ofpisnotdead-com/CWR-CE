#ifdef _MSC_VER
#pragma once
#endif

#ifndef _QBSTREAM_HPP
#define _QBSTREAM_HPP

#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/IO/Streams/FileInfo.h>
#include <cstdint>

#ifndef _WIN32
#include <Poseidon/IO/Filesystem/DirScanner.hpp>
#endif


namespace Poseidon
{
class QFBank;

//! tests whether a bank/file is accessible in a given context
class IQFBankContext
{
  public:
    virtual bool IsAccessible(QFBank* bank) const = 0;
};

//! stream that is opened from some file bank
class QIFStreamB : public QIFStream
{
    const QFBank* _bank;

  public:
    static QFBank* AutoBank(const char* name);
    static bool FileExist(const char* name, IQFBankContext* context = nullptr);

    static void ClearBanks();

    QIFStreamB();

    void AutoOpen(const char* name, IQFBankContext* context = nullptr); // autoselect bank/file

    void open(const QFBank& bank, const char* name); // open and preload file
    bool IsFromBank(const QFBank* bank) const;
};

class FindBank
{
#ifdef _WIN32
    char _wild[1024];
    // check all "pb?" file banks
    void* _info;
    intptr_t _handle; // Must be intptr_t for 64-bit compatibility (_findfirst returns intptr_t)

#else
    DirScanner _scanner;
#endif
  public:
    FindBank();
    ~FindBank();

    bool First(const char* path);
    bool Next();
    void Close();

    const char* GetName() const;
};

class BankContextBase : public RefCount
{
};

typedef bool (*OpenCallback)(QFBank* bank, BankContextBase* context);

class BankList : public AutoArray<QFBank>
{
  public:
    void Load(const RString& path, const RString& bankPrefix, const RString& bName, bool emptyPrefix,
              OpenCallback beforeOpen = nullptr, OpenCallback afterOpen = nullptr, BankContextBase* context = nullptr);
    void Unload(const RString& bankPrefix, const RString& bName, bool emptyPrefix);
    void Lock(const RString& prefix);
    void Unlock(const RString& prefix);
    void SetLockable(const RString& prefix, bool lockable);

    bool UnloadUnused();
};

extern BankList GFileBanks;

int CmpStartStr(const char* str, const char* start);

struct FileInfoO
{
    RStringB name;

    int compressedMagic;
    int uncompressedSize;
    int32_t startOffset;
    int32_t time;
    int32_t length;
#if _ENABLE_PATCHING
    bool loadFromFile; // fast patching enabled
#endif

    FileInfoO()
    {
#if _ENABLE_PATCHING
        loadFromFile = false;
#endif
    }

    const char* GetKey() const { return name; }
};

#include <Poseidon/Foundation/Containers/Array.hpp>


typedef MapStringToClass<FileInfoO, AutoArray<FileInfoO>> FileBankType;

typedef void* WINHANDLE;

// interface for creating file log

class IBankLog : public RefCount
{
  public:
    virtual void Init(const char* bankName) = 0;
    virtual void LogFileOp(const char* name) = 0;
    virtual void Flush(const char* bankName) = 0;
};

class FileBufferMapped;
class FileBufferOverlapped;

//! any bank may have several properties attached
struct QFProperty
{
    RString name;
    RString value;
};

// Virtual read-only file system: access is faster and storage more compact than
// going through OS file services.
class QFBank;

class QFBankFunctions
{
  public:
    QFBankFunctions() = default;
    virtual ~QFBankFunctions() = default;

    virtual bool FreeUnusedBanks(size_t sizeNeeded) const;
};

// Filebank compression/encryption interface. An instance already holds whatever
// key material it needs to encrypt or decrypt.
class IFilebankEncryption : public RefCount
{
  public:
    virtual bool Decode(char* dst, long lensb, QIStream& in) = 0;
    virtual void Encode(QOStream& out, const char* dst, long lensb) = 0;
};

Ref<IFilebankEncryption> CreateFilebankEncryption(const char* name, const void* context);
void RegisterFilebankEncryption(const char* name, IFilebankEncryption* (*createFunction)(const void* context));

#define MT_SAFE 0
class QFBank
{
#if MT_SAFE
    mutable CriticalSection _lock;
#endif
#if !_RELASE
    mutable bool _serialize; // help finding bugs
#endif
    Ref<IBankLog> _log;

    friend class QIFStream;

  private:
    // remember parameters necessary for opening
    //! name provided by open call
    RString _openName;
    //! callback provided by open call
    OpenCallback _openBeforeOpenCallback;
    //! context provided by open call
    Ref<BankContextBase> _openContext;

    //! locked banks cannot be unloaded
    mutable bool _locked;
    //! only lockable banks can be locked/unlocked
    bool _lockable;

    RString _prefix;
    FileBankType _files;
    AutoArray<QFProperty> _properties;

    Ref<IFileBuffer> _fileAccess;
    //! handle used for normal file access
    WINHANDLE _handle;
    //! handle used for overlapped file access
    WINHANDLE _handleOverlapped;
    mutable long _pos;
    mutable long _wantPos;

    //! set true when an attempt to open (DoOpen) failed
    bool _error;

    static QFBankFunctions* _defaultFunctions;
    static void SetDefaultFunctions(QFBankFunctions* f) { _defaultFunctions = f; }

  public:
    // If nothing registered the slot, fall back to a no-op base QFBankFunctions
    // (SIOF-safe lazy init).
    static QFBankFunctions* DefaultFunctions()
    {
        if (_defaultFunctions)
            return _defaultFunctions;
        static QFBankFunctions fallback;
        return &fallback;
    }

    QFBank();
    ~QFBank();
    // QFBank owns raw descriptors (_handle/_handleOverlapped) that Clear() closes.
    // AutoArray<QFBank> relocates elements by move-construct + destruct-source
    // (ModernTraits::MoveData); the implicit move falls back to copy (the declared
    // destructor suppresses it), which would copy the descriptor and then let the
    // source's destructor close it — invalidating the relocated bank. Provide an
    // ownership-transferring move ctor; keep copy defaulted for the container's
    // copy/insert paths.
    QFBank(const QFBank&) = default;
    QFBank& operator=(const QFBank&) = default;
    QFBank(QFBank&& other);
    RString GetPrefix() const { return _prefix; }
    RString GetOpenName() const { return _openName; }
    void ScanPatchFiles(RString prefix, RString subdir);
    void SetPrefix(RString prefix);
    bool open(RString name, OpenCallback beforeOpen = nullptr, BankContextBase* context = nullptr);
    //! load bank - physically performs the action
    bool Load();
    //! open bank and mark it as locked (cannot be un-opened)
    void Lock() const;
    void SetLockable(bool lockable) { _lockable = lockable; }
    bool GetLockable() const { return _lockable; }
    //! mark bank as unlocked and unload if possible
    void Unlock() const;
    bool IsLocked() const { return _locked; }
    //! a bank in use cannot be unloaded
    bool CanBeUnloaded() const;
    //! const overload - modifies state via const_cast
    bool Load() const { return const_cast<QFBank*>(this)->Load(); }
    void Unload();
    //! const overload - modifies state via const_cast
    void Unload() const
    {
        if (!_handle)
        {
            return;
        }
        const_cast<QFBank*>(this)->Unload();
    }
    void Clear(); // release all files
    void close() { Clear(); }

    bool error() const;

    const RString& GetProperty(const RString& name) const;

    const FileInfoO& FindFileInfo(const char* name) const;
    bool FileExists(const char* name) const;
    // low level access to bank
    void Read(char* buf, long size, const char* name) const; // read raw bytes from bank
    void Seek(long pos) const;

    //! read file - uncompress if necessary
    Ref<IFileBuffer> Read(const char* file) const;
    //! read file using overlapped I/O - uncompress if necessary
    Ref<IFileBuffer> ReadOverlapped(const char* file) const;
    int GetFileOrder(const char* file);

    void ForEach(void (*Func)(const FileInfoO& fi, const FileBankType* files, void* context),
                 void* context) const; // call Func for all files
    static bool IsNull(const FileInfoO& value) { return FileBankType::IsNull(value); }
    static bool NotNull(const FileInfoO& value) { return FileBankType::NotNull(value); }
    static FileInfoO& Null() { return FileBankType::Null(); }

    bool BufferOwned(const FileBufferMapped* buffer) const;
    bool BufferOwned(const FileBufferOverlapped* buffer) const;

    static bool FreeUnusedBanks(size_t sizeNeeded) { return DefaultFunctions()->FreeUnusedBanks(sizeNeeded); }
};

extern RString GFileBankPrefix;

// Resolve a base-relative path to an enabled mod's copy of the same file, "" if none.
std::string ResolveModOverride(const char* relPath);

} // namespace Poseidon

using ::Poseidon::QIFStreamB;
using ::Poseidon::FileInfoO;
using ::Poseidon::FileBankType;
using ::Poseidon::BankList;
using ::Poseidon::QFBank;
using ::Poseidon::GFileBanks;
using ::Poseidon::GFileBankPrefix;
using ::Poseidon::CmpStartStr;
using ::Poseidon::FindBank;

extern bool GLogFileOps;
extern bool GUseFileBanks;

#endif
