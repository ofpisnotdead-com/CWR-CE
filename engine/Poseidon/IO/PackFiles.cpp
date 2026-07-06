#include <Poseidon/Core/Application.hpp>

#include <string.h>
#include <string>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>

#include <sys/stat.h>
#include <Poseidon/Foundation/Common/Filenames.hpp>
#include <stdio.h>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifndef _WIN32
#define __cdecl
#include <unistd.h>
#include <dirent.h>
#else
#include <io.h>
#endif

#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PackFiles.hpp>

#include <stdarg.h>

namespace Poseidon
{
using Poseidon::Foundation::QSort;

static int FileTime(const char* name)
{
    struct stat buf;
    memset(&buf, 0, sizeof(buf));
    LocalPath(fn, name);
    if (stat(fn, &buf) < 0)
    {
        return -1;
    }
    return buf.st_mtime;
}

static RString CanonicalBankEntryName(const char* name)
{
    std::string normalized = name ? name : "";
    for (char& c : normalized)
    {
        if (c == '/')
        {
            c = '\\';
        }
    }
    return RString(normalized.c_str());
}

static RString HostPathFromBankEntryName(RString name)
{
#ifndef _WIN32
    std::string local = (const char*)name;
    for (char& c : local)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    return RString(local.c_str());
#else
    return name;
#endif
}

// use LZW compression on files of some types (e.g. .p3d)

#ifndef _WIN32
#define DIR_STR "/"
#define DIR_CHR '/'
#else
#define DIR_STR "\\"
#define DIR_CHR '\\'
#endif

class SOFStream
{
    // serial output file stream
    // simple wrapper around C buffered output
    FILE* _file;
    bool _fail;

  public:
    void open(const char* name);
    void close();
    void write(const void* data, int size);
    bool fail() const { return _fail; }
    void setbuffer(int size);

    SOFStream();
    ~SOFStream();
};

SOFStream::SOFStream()
{
    _file = nullptr;
    _fail = true;
}

SOFStream::~SOFStream()
{
    close();
}

void SOFStream::open(const char* name)
{
    LocalPath(fn, name);
    _file = fopen(fn, "wb");
    if (_file)
    {
        _fail = false;
    }
}
void SOFStream::setbuffer(int size)
{
    if (_file)
    {
        setvbuf(_file, nullptr, _IOFBF, size);
    }
}
void SOFStream::write(const void* data, int size)
{
    if (fwrite(data, size, 1, _file) < 1)
    {
        _fail = true;
    }
}
void SOFStream::close()
{
    if (_file)
    {
        if (fclose(_file))
        {
            _fail = true;
        }
        _file = nullptr;
    }
}

void LoadFileInfo(FileInfoExt& i, QIStream& f)
{
    // read zero terminated name
    char name[256];
    char* n = name;
    int maxLen = sizeof(name) - 1;
    for (int l = 0; l < maxLen; l++)
    {
        char c = f.get();
        if (!c)
        {
            break;
        }
        *n++ = c;
    }
    *n = 0; // zero terminate in any case
    i.name = name;
    f.read(&i.compressedMagic, sizeof(i.compressedMagic));
    f.read(&i.uncompressedSize, sizeof(i.uncompressedSize));
    f.read(&i.startOffset, sizeof(i.startOffset));
    f.read(&i.time, sizeof(i.time));
    f.read(&i.length, sizeof(i.length));
}

void SaveFileInfo(const FileInfoExt& i, QOStream& f)
{
    // save zero terminate name
    f.write((const char*)i.name, i.name.GetLength() + 1);
    f.write(&i.compressedMagic, sizeof(i.compressedMagic));
    f.write(&i.uncompressedSize, sizeof(i.uncompressedSize));
    f.write(&i.startOffset, sizeof(i.startOffset));
    f.write(&i.time, sizeof(i.time));
    f.write(&i.length, sizeof(i.length));
}

void SaveFileInfo(const FileInfoExt& i, SOFStream& f)
{
    // save zero terminate name
    if (i.name.GetLength() > 0)
    {
        f.write((const char*)i.name, i.name.GetLength() + 1);
    }
    else
    {
        f.write("", 1);
    }
    f.write(&i.compressedMagic, sizeof(i.compressedMagic));
    f.write(&i.uncompressedSize, sizeof(i.uncompressedSize));
    f.write(&i.startOffset, sizeof(i.startOffset));
    f.write(&i.time, sizeof(i.time));
    f.write(&i.length, sizeof(i.length));
}

static int MatchMask(const char* mask, const char* string)
{
    bool exclude = false;
    if (*mask == '~')
    {
        exclude = true;
        mask++;
    }
    int ret = 0;
    if (!strcmp(mask, "*"))
    {
        ret = 1;
    }
    else if (!strcmpi(mask, string))
    {
        ret = 1;
    }
    return exclude ? -ret : ret;
}

void FileBankManager::ParseMasks(const char* logfile)
{
    // set priority to all files
    FILE* f = fopen(logfile, "r");
    if (!f)
    {
        return;
    }

    int line = 1;
    for (;;)
    {
        // scan rule
        char buf[1024];
        *buf = 0;
        fgets(buf, sizeof(buf), f);
        if (!*buf)
        {
            break;
        }
        if (strlen(buf) > 0)
        {
            if (buf[strlen(buf) - 1] == '\n')
            {
                buf[strlen(buf) - 1] = 0;
            }
        }
        // buf is now wildcard mask
        // try to match this mask with all files
        for (int i = 0; i < files.Size(); i++)
        {
            FileInfoExt& fi = files[i];
            int match = MatchMask(buf, fi.name);
            if (match == 0)
            {
                continue;
            }
            if (match > 0)
            {
                fi.priority = line;
            }
            break;
        }
        line++;
    }
    fclose(f);
}

static int CompareFileInfo(const FileInfoExt* f1, const FileInfoExt* f2)
{
    int d = f1->priority - f2->priority;
    if (d)
    {
        return d;
    }
    return strcmpi(f1->name, f2->name);
}

void FileBankManager::SortAndRemove(bool remove)
{
    // sort files by priority - then name
    QSort(files.Data(), files.Size(), CompareFileInfo);

    if (remove)
    {
        // remove files with negative priority
        int firstIPositive = files.Size();
        for (int i = 0; i < files.Size(); i++)
        {
            if (files[i].priority <= 0)
            {
                continue;
            }
            firstIPositive = i;
            break;
        }
        if (firstIPositive > 0)
        {
            for (int i = firstIPositive; i < files.Size(); i++)
            {
                files[i - firstIPositive] = files[i];
            }
            files.Resize(files.Size() - firstIPositive);
        }
    }

    for (int i = 0; i < files.Size(); i++)
    {
        size += files[i].length;
    }

    // add terminator
    FileInfoExt f;
    f.Zero();
    f.priority = 0;
    files.Add(f);
}

void FileBankManager::ScanDir(RString dir, RString rel)
{
#ifndef _WIN32
    LocalPath(dirPath, dir);
    DIR* scan = opendir(dirPath);
    if (scan)
    {
        struct dirent* entry;
        while ((entry = readdir(scan)) != nullptr)
        {
            const char* name = entry->d_name;
            RString subdir = RString(dirPath) + RString(DIR_STR) + RString(name);
            RString subrel = rel + RString(DIR_STR) + RString(name);
            struct stat buf;
            if (stat(subdir, &buf) < 0)
            {
                LOG_DEBUG(Core, "Error {}\n", (const char*)subdir);
                continue;
            }
            if (buf.st_mode & S_IFDIR)
            {
                if (*name == '.')
                    continue;
                ScanDir(subdir, subrel);
                continue;
            }
            FileInfoExt f;
            const char* rc = subrel;
            if (*rc == DIR_CHR)
                rc++;
            f.compressedMagic = 0;
            f.uncompressedSize = 0;
            f.startOffset = 0;
            f.name = CanonicalBankEntryName(rc);
            f.length = buf.st_size;
            f.time = buf.st_mtime;
            if (newestFile < f.time)
                newestFile = f.time;
            files.Add(f);
        }
    }
#else
    _finddata_t fInfo;
    RString wild = RString(dir) + RString("\\*");
    intptr_t hFile = _findfirst(wild, &fInfo);
    if (hFile != -1)
    {
        do
        {
            RString subrel = rel + RString(DIR_STR) + RString(fInfo.name);
            if (fInfo.attrib & _A_SUBDIR)
            {
                if (fInfo.name[0] == '.')
                {
                    continue;
                }
                RString subdir = dir + RString(DIR_STR) + RString(fInfo.name);
                ScanDir(subdir, subrel);
                continue;
            }
            FileInfoExt f;
            const char* rc = subrel;
            if (*rc == DIR_CHR)
            {
                rc++;
            }
            memset(&f, 0, sizeof(f));
            f.name = CanonicalBankEntryName(rc);
            f.length = fInfo.size;
            f.time = fInfo.time_write;
            f.compressedMagic = 0;
            f.uncompressedSize = 0;
            if (newestFile < f.time)
            {
                newestFile = f.time;
            }
            files.Add(f);
        } while (_findnext(hFile, &fInfo) == 0);
        _findclose(hFile);
    }
#endif
}

void FileBankManager::SaveHeadersOpt(QOStream& out)
{
    for (int i = 0; i < files.Size(); i++)
    {
        const FileInfoExt& file = files[i];
        SaveFileInfo(file, out);
    }
}

void FileBankManager::SaveHeadersOpt(SOFStream& out)
{
    for (int i = 0; i < files.Size(); i++)
    {
        const FileInfoExt& file = files[i];
        SaveFileInfo(file, out);
    }
}

void FileBankManager::SaveProperties(QOStream& out, const QFProperty* prop, int nProp)
{
    if (prop && nProp > 0)
    {
        // save terminator
        FileInfoExt terminator;
        terminator.Zero();
        terminator.compressedMagic = VersionMagic;
        SaveFileInfo(terminator, out);
        for (int i = 0; i < nProp; i++)
        {
            out.write((const char*)prop[i].name, prop[i].name.GetLength() + 1);
            out.write((const char*)prop[i].value, prop[i].value.GetLength() + 1);
        }
        out.write("", 1);
    }
}

void FileBankManager::SaveProperties(SOFStream& out, const QFProperty* prop, int nProp)
{
    if (prop && nProp > 0)
    {
        // save terminator
        FileInfoExt terminator;
        terminator.Zero();
        terminator.compressedMagic = VersionMagic;
        SaveFileInfo(terminator, out);
        for (int i = 0; i < nProp; i++)
        {
            out.write((const char*)prop[i].name, prop[i].name.GetLength() + 1);
            out.write((const char*)prop[i].value, prop[i].value.GetLength() + 1);
        }
        out.write("", 1);
    }
}

FileBankManager::FileBankManager()
{
    newestFile = 0;
    size = 0;
}

FileBankManager::~FileBankManager() = default;

const char* DefFileBankNoCompress[] = {".pbo", ".ogg", ".wss", ".jpg", nullptr};

const char* DefFileBankEncrypt[] = {".p3d", ".bin", ".cpp", ".sqm", ".sqs", ".ext", ".csv", nullptr};

static bool StringInList(const char* str, const char** list)
{
    while (*list)
    {
        if (!strcmpi(*list, str))
        {
            return true;
        }
        list++;
    }
    return false;
}

LSError FileBankManager::Create(const char* tgt, const char* src, bool compress, bool optimize, const char* logFile,
                                const char** doNotCompress, const QFProperty* properties, int nProperties)
{
    newestFile = 0;
    size = 0;

    files.Resize(0);
    files.Realloc(1024);
    RString folder = src;
    RString target = tgt;
    ScanDir(folder, "");

    if (logFile)
    {
        ParseMasks(logFile);
    }
    SortAndRemove(logFile != nullptr);
    // repack only if destination is older than all sources

    if (!optimize)
    {
        Fail("Obsolete: Optimized format should be used");
    }

    int destTime = FileTime(target);
    if (destTime >= newestFile)
    {
        LOG_DEBUG(Core, "{} skipped - no changes detected.\n", (const char*)folder);
        return LSOK;
    }

    const char* action = "Repacking";
    if (compress)
    {
        action = "Compressing";
    }
    else if (optimize)
    {
        action = "Optimizing";
    }
    LOG_DEBUG(Core, "{}: {} {} files ({} KB).\n", (const char*)folder, action, files.Size(), (size + 1023) / 1024);

    if (!compress)
    {
        // save headers
        SOFStream out;
        out.open(target);
        out.setbuffer(1024 * 1024);
        SaveProperties(out, properties, nProperties);
        SaveHeadersOpt(out);
        for (int i = 0; i < files.Size(); i++)
        {
            FileInfoExt& file = files[i];
            RString inFilename = folder + RString(DIR_STR) + HostPathFromBankEntryName(file.name);
            QIFStream in;
            in.open(inFilename);
            out.write(in.act(), in.rest());
        }
        out.close();
    } // if( !compress )
    else
    {
        QOFStream out;
        out.open(target);
        out.setbuffer(size * 3 / 4 + 2 * 1024 * 1024);
        SaveProperties(out, properties, nProperties);
        SaveHeadersOpt(out);
        for (int i = 0; i < files.Size(); i++)
        {
            FileInfoExt& file = files[i];
            RString inFilename = folder + RString(DIR_STR) + HostPathFromBankEntryName(file.name);
            QIFStream in;
            in.open(inFilename);
            // check if it should be compressed
            const char* ext = GetFileExt(file.name);
            if (doNotCompress && StringInList(ext, doNotCompress))
            {
                out.write(in.act(), in.rest());
            }
            else
            {
                SSCompress ss;
                int offset = out.tellp();
                ss.Encode(out, in.act(), in.rest());
                file.uncompressedSize = file.length;
                file.length = out.tellp() - offset;
                file.compressedMagic = CompMagic;
            }
        }

        // rewind and save headers again
        out.seekp(0, QIOS::beg);
        SaveProperties(out, properties, nProperties);
        SaveHeadersOpt(out);
        out.close();
    } // if( !compress ) else
    {
        // Verify the created PBO
        struct stat verifyBuf;
        LocalPath(verifyFn, target);
        int verifySize = (stat(verifyFn, &verifyBuf) == 0) ? (int)verifyBuf.st_size : -1;
        (void)verifySize;
    }
    files.Clear();
    return LSOK;
}

void FileBankManager::Create(QOStream& out, const char* src, bool compress, bool optimize, const char* logFile,
                             const char** doNotCompress, const QFProperty* properties, int nProperties)
{
    size = 0;

    files.Resize(0);
    files.Realloc(1024);
    RString folder = src;
    ScanDir(folder, "");
    if (logFile)
    {
        ParseMasks(logFile);
    }
    // sort by priority
    SortAndRemove(logFile != nullptr);

    if (!optimize)
    {
        Fail("Obsolete: Optimized format should be used");
    }

    if (!compress)
    {
        // save headers
        out.setbuffer(size);
        SaveProperties(out, properties, nProperties);
        SaveHeadersOpt(out);
        for (int i = 0; i < files.Size(); i++)
        {
            FileInfoExt& file = files[i];
            RString inFilename = folder + RString(DIR_STR) + HostPathFromBankEntryName(file.name);
            QIFStream in;
            in.open(inFilename);
            out.write(in.act(), in.rest());
        }
    } // if( !compress )
    else
    {
        out.setbuffer(size * 3 / 4 + 2 * 1024 * 1024);
        SaveProperties(out, properties, nProperties);
        SaveHeadersOpt(out);
        for (int i = 0; i < files.Size(); i++)
        {
            FileInfoExt& file = files[i];
            RString inFilename = folder + RString(DIR_STR) + HostPathFromBankEntryName(file.name);
            QIFStream in;
            in.open(inFilename);
            // check if it should be compressed
            const char* ext = GetFileExt(file.name);
            if (doNotCompress && StringInList(ext, doNotCompress))
            {
                out.write(in.act(), in.rest());
            }
            else
            {
                SSCompress ss;
                int offset = out.tellp();
                ss.Encode(out, in.act(), in.rest());
                file.uncompressedSize = file.length;
                file.length = out.tellp() - offset;
                file.compressedMagic = CompMagic;
            }
        }

        // rewind and save headers again
        out.seekp(0, QIOS::beg);
        SaveProperties(out, properties, nProperties);
        SaveHeadersOpt(out);
    } // if( !compress ) else
    files.Clear();
}

void FileBankManager::Create(QOStream& out, const char* src, IFilebankEncryption* encrypt, const QFProperty* properties,
                             int nProperties, const char** encryptExts)
{
    size = 0;

    files.Resize(0);
    files.Realloc(1024);
    RString folder = src;
    ScanDir(folder, "");
    // sort by priority
    SortAndRemove(false);

    QOStream temp;
    temp.setbuffer(size * 18 / 16);

    for (int i = 0; i < files.Size(); i++)
    {
        FileInfoExt& file = files[i];
        RString inFilename = folder + RString(DIR_STR) + HostPathFromBankEntryName(file.name);
        QIFStream in;
        in.open(inFilename);
        // check if it should be compressed
        const char* ext = GetFileExt(file.name);
        if (encryptExts && !StringInList(ext, encryptExts))
        {
            temp.write(in.act(), in.rest());
        }
        else
        {
            LOG_DEBUG(Core, "Encrypting {}", (const char*)file.name);

            int offset = temp.tellp();
            encrypt->Encode(temp, in.act(), in.rest());
            file.uncompressedSize = file.length;
            file.length = temp.tellp() - offset;
            file.compressedMagic = EncrMagic;
        }
    }

    // save properties
    SaveProperties(out, properties, nProperties);
    // save headers
    QOStream headers;
    SaveHeadersOpt(headers);
    QOStream headersEncoded;
    // encrypte headers
    encrypt->Encode(headersEncoded, headers.str(), headers.pcount());
    int headersSize = headers.pcount();
    int headersEncodedSize = headersEncoded.pcount();
    // save headers decrypted size
    out.write(&headersSize, sizeof(headersSize));
    // save headers encrypted size
    out.write(&headersEncodedSize, sizeof(headersEncodedSize));
    // save encrypted headers
    out.write(headersEncoded.str(), headersEncoded.pcount());
    // save all encypted content
    out.write(temp.str(), temp.pcount());
    files.Clear();
}

} // namespace Poseidon
