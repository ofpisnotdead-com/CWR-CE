#ifdef _MSC_VER
#pragma once
#endif

#ifndef _QSTREAM_HPP
#define _QSTREAM_HPP

#include <Poseidon/Foundation/Strings/RString.hpp>

#include <Poseidon/Foundation/Types/EnumDecl.hpp>

namespace Poseidon
{
using ::Poseidon::Foundation::AutoArray;
using ::Poseidon::Foundation::Buffer;
using ::Poseidon::Foundation::Ref;
using ::Poseidon::Foundation::RefCount;
using ::Poseidon::Foundation::RString;

#ifndef _DECL_ENUM_LSError
#define _DECL_ENUM_LSError
DECL_ENUM(LSError)
#endif

DEFINE_ENUM_BEG(LSError)
LSOK,
    LSFileNotFound,      // no such file
    LSBadFile,           // error in loaded file (CRC error...)
    LSStructure,         // fire structure error - caused by programm bug
    LSUnsupportedFormat, // attempt to load other file format
    LSVersionTooNew,     // attempt to load unknown version
    LSVersionTooOld,     // attempt to load version that is no longer supported
    LSDiskFull,          // cannot save - disk full
    LSAccessDenied,      // read only, directory permiss...
    LSDiskError,         // some disk error
    LSNoEntry,           // entry in ParamArchive not found
    LSNoAddOn,           // AddOns check
    LSUnknownError,
    DEFINE_ENUM_END(LSError)

        class QIOS
{
  public:
    enum
    {
        beg,
        cur,
        end
    };
    enum
    {
        binary = 1,
        text = 2,
        in = 4
    };
    typedef int seekdir;
    typedef int openmode;
};

// istream like simple and fast implementaion of file access
class QIStream
{
  protected:
    char* _buf;
    int _len;
    int _readFrom;
    bool _fail, _eof;
    LSError _error;

  protected:
    void Assign(const QIStream& src); // this points to same data as from
    void Close();

  private: // disable copying
    QIStream& operator=(const QIStream& src);
    QIStream(const QIStream& src);

  public:
    QIStream() : _buf(nullptr), _len(0), _readFrom(0), _fail(true), _eof(false) {}
    QIStream(const void* buf, int len) : _buf((char*)buf), _len(len), _readFrom(0), _fail(false), _eof(false) {}
    void init(const void* buf, int len)
    {
        _buf = (char*)buf;
        _len = len;
        _readFrom = 0;
        _fail = false;
        _eof = false;
    }
    int get()
    {
        if (_readFrom >= _len)
        {
            _eof = true;
            return EOF;
        }
        _eof = false;
        return (unsigned char)_buf[_readFrom++];
    }
    void unget()
    {
        // A get() that returned EOF did not advance the cursor, so there is nothing to
        // put back; ungetting anyway would resurrect the last real character and let a
        // parser loop re-process it forever (e.g. a config array element loop OOM).
        if (_eof)
        {
            return;
        }
        if (_readFrom > 0)
        {
            _readFrom--;
        }
    }
    void read(void* buf, int n)
    {
        int left = _len - _readFrom;
        if (n > left)
        {
            if (left == 0)
            {
                _eof = true;
            }
            _fail = true;
            return;
        }
        // note: buf and _buf may be unalligned - we cannot avoid this
        if (n > 0)
            memcpy(buf, _buf + _readFrom, n); // memcpy is nonnull: skip the n==0 / null-buf case
        _readFrom += n;
    }
    void seekg(int pos, QIOS::seekdir dir)
    {
        // 64-bit math so a hostile pos (e.g. INT_MAX) can't overflow the add
        // before the range check below rejects it.
        int64_t nPos;
        switch (dir)
        {
            case QIOS::beg:
                nPos = pos;
                break;
            case QIOS::end:
                nPos = static_cast<int64_t>(_len) + pos;
                break;
            default:
                nPos = static_cast<int64_t>(_readFrom) + pos;
                break;
        }
        if (nPos < 0 || nPos > _len)
        {
            _fail = true;
        }
        else
        {
            _readFrom = static_cast<int>(nPos);
            _fail = false;
            _eof = false;
        }
    }
    bool fail() const { return _fail; }
    bool eof() const { return _eof; }
    LSError error() const { return _error; }

    //! Reads the stream until EOLN (returns true) or EOF (returns false) is reached.
    //! Stops AFTER the EOLN.
    bool nextLine();

    // Read the current line into buf (bufLen includes the \0 terminator; 0 means
    // unrestricted). Lines too long for the buffer are truncated. Returns true on success.
    bool readLine(char* buf, int bufLen = 0);

    // caution: following functions are not available in istream
    // use them with care
    int tellg() const { return _readFrom; }
    const char* act() const { return _buf + _readFrom; }
    int rest() const { return _len - _readFrom; }
};

class QOStream
{
  protected:
    AutoArray<char> _buffer;
    int _writeTo{0};

  private: // disable copying
    QOStream& operator=(const QOStream& src) = delete;
    QOStream(const QOStream& src) = delete;

  public:
    QOStream() { _buffer.Realloc(64 * 1024); }

    void rewind() { _buffer.Resize(0), _writeTo = 0; }
    void setbuffer(int size) { _buffer.Reserve(size, size); }

    void put(char c)
    {
        if (_writeTo >= _buffer.Size())
        {
            _buffer.Add(c), _writeTo = _buffer.Size();
        }
        else
        {
            _buffer[_writeTo++] = c;
        }
    }
    void write(const void* buf, int n)
    {
        int minSize = _writeTo + n;
        if (_buffer.Size() < minSize)
        {
            _buffer.Resize(minSize);
        }
        memcpy(_buffer.Data() + _writeTo, buf, n);
        _writeTo += n;
    }

    int tellp() const { return _writeTo; }

    void seekp(int pos, QIOS::seekdir dir)
    {
        int nPos;
        switch (dir)
        {
            case QIOS::beg:
                nPos = pos;
                break;
            case QIOS::end:
                nPos = _buffer.Size() + pos;
                break;
            default:
                nPos = _writeTo + pos;
                break;
        }
        if (nPos < 0 || nPos > _buffer.Size())
        {
        }
        else
        {
            _writeTo = nPos;
        }
    }
    // str and pcount: see ostrstream
    const char* str() const { return _buffer.Data(); }
    int pcount() const { return _buffer.Size(); }

    QOStream& operator<<(const char* buf)
    {
        write(buf, strlen(buf));
        return *this;
    }
};

class ClipboardFunctions
{
  public:
    ClipboardFunctions() = default;
    virtual ~ClipboardFunctions() = default;

    // Copy the buffer into the platform clipboard.
    virtual void Copy(const char* /*buffer*/, int /*size*/) {}
};

class QOFStream : public QOStream
{
  protected:
    RString _file;
    bool _fail;
    LSError _error;

    static ClipboardFunctions* _defaultClipboardFunctions;

  public:
    QOFStream() : _file(nullptr), _fail(false), _error(LSOK) {}

    QOFStream(const char* file) : _file(file), _fail(false), _error(LSOK) {}
    void open(const char* file);
    void close();
    void export_clip(const char* name); // export to file or clipboard
    void close(const void* header, int headerSize);
    bool fail() const { return _fail; }
    LSError error() const { return _error; }

    ~QOFStream(); // perform actual save

    static void SetDefaultClipboardFunctions(ClipboardFunctions* f) { _defaultClipboardFunctions = f; }

    // If nothing registered the slot by first access, fall back to a no-op base
    // ClipboardFunctions singleton rather than returning null.
    static ClipboardFunctions* DefaultClipboardFunctions()
    {
        if (_defaultClipboardFunctions)
            return _defaultClipboardFunctions;
        static ClipboardFunctions fallback;
        return &fallback;
    }
};

class QFBank;

class IFileBuffer : public RefCount
{
  private:
    IFileBuffer(const IFileBuffer&) = delete;
    IFileBuffer& operator=(const IFileBuffer&) = delete;

  public:
    IFileBuffer() = default;

    virtual bool GetError() const = 0;
    virtual const char* GetData() const = 0;
    virtual int GetSize() const = 0;
    virtual bool IsFromBank(QFBank* bank) const = 0;
    // Some sources (overlapped IO) need time before the data is available.
    virtual bool IsReady() const = 0;
};

class FileBufferMemory : public IFileBuffer
{
  protected:
    Buffer<char> _data;

  public:
    FileBufferMemory() = default;
    FileBufferMemory(int size) { _data.Init(size); }

    void Realloc(int size) { _data.Init(size); }
    const char* GetData() const override { return _data.Data(); }
    // non-virtual writable access
    char* GetWritableData() { return _data.Data(); }

    int GetSize() const override { return _data.Size(); }
    bool GetError() const override { return false; }
    bool IsFromBank(QFBank* /*bank*/) const override { return false; }
    bool IsReady() const override { return true; }
};

class QIFStream : public QIStream
{
  private:
    Ref<IFileBuffer> _sharedData;

    friend class QIFStreamB;

  public:
    QIFStream() = default;
    QIFStream(const QIFStream& src) { DoConstruct(src); }
    void operator=(const QIFStream& src)
    {
        DoDestruct();
        DoConstruct(src);
    }

    void open(Ref<IFileBuffer> buffer) { OpenBuffer(buffer); }
    void OpenBuffer(Ref<IFileBuffer> buffer);

    IFileBuffer* GetBuffer() const { return _sharedData; }

    void import(const char* name); // import from file or clipboard
    void open(const char* name);   // open and preload file

    void DoDestruct(); // close file and destroy buffer
    ~QIFStream();

    void DoConstruct(const QIFStream& from); // close from, open this
    static bool FileExists(const char* name);
    static bool FileReadOnly(const char* name);
};

// Most compatible file buffer: needs no memory-mapping or overlapped IO, so it
// works everywhere, but throughput is modest.
class FileBufferLoaded : public FileBufferMemory
{
  public:
    FileBufferLoaded(const char* name);
    ~FileBufferLoaded() override;

    const char* GetData() const override { return (char*)_data.Data(); }
    int GetSize() const override { return _data.Size(); }
};

class SSCompress
{
    enum
    {
        N = 4096,
        F = 18,
        THRESHOLD = 2
    };

    unsigned char text_buf[N + F - 1];
    int match_position, match_len;
    int lsons[N + 1], rsons[(N) + 257], dads[N + 1];

    void InitTree();
    void InsertNode(int p);
    void DeleteNode(int p);

  public:
    bool Decode(char* dst, long lensb, QIStream& in);
    void Encode(QOStream& out, const char* dst, long lensb);
};

#ifndef _WIN32
int FileSize(int handle);
#endif

} // namespace Poseidon

using ::Poseidon::LSError;
using ::Poseidon::LSOK;
using ::Poseidon::LSNoAddOn;
using ::Poseidon::LSNoEntry;
using ::Poseidon::LSFileNotFound;
using ::Poseidon::LSBadFile;
using ::Poseidon::LSStructure;
using ::Poseidon::LSUnsupportedFormat;
using ::Poseidon::LSVersionTooNew;
using ::Poseidon::LSVersionTooOld;
using ::Poseidon::LSDiskFull;
using ::Poseidon::LSAccessDenied;
using ::Poseidon::LSDiskError;
using ::Poseidon::LSUnknownError;
using ::Poseidon::QIOS;
using ::Poseidon::QIStream;
using ::Poseidon::QOStream;
using ::Poseidon::ClipboardFunctions;
using ::Poseidon::QOFStream;
using ::Poseidon::IFileBuffer;
using ::Poseidon::FileBufferMemory;
using ::Poseidon::QIFStream;
using ::Poseidon::FileBufferLoaded;
using ::Poseidon::SSCompress;

#endif
