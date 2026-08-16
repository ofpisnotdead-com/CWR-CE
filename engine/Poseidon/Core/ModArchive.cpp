#include <Poseidon/Core/ModArchive.hpp>

#include <Poseidon/Asset/Probes/AssetInfo.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>

#include <zstd.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <vector>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{
namespace
{
// Streams a zstd frame from srcPath into dstPath, chunk by chunk, so a multi-GB
// mod never has to fit in memory. Returns false with *error set on any failure,
// including a truncated frame (the decoder still expecting input at EOF).
bool DecompressZstdToFile(const std::string& srcPath, const std::string& dstPath, std::string* error)
{
    const auto fail = [&](const std::string& message)
    {
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    };

    FILE* in = std::fopen(srcPath.c_str(), "rb");
    if (in == nullptr)
    {
        return fail(std::string("cannot open archive: ") + srcPath);
    }
    FILE* out = std::fopen(dstPath.c_str(), "wb");
    if (out == nullptr)
    {
        std::fclose(in);
        return fail(std::string("cannot write decoded pbo: ") + dstPath);
    }

    ZSTD_DStream* ds = ZSTD_createDStream();
    if (ds == nullptr)
    {
        std::fclose(in);
        std::fclose(out);
        return fail("zstd: out of memory");
    }
    ZSTD_initDStream(ds);

    const size_t inCap = ZSTD_DStreamInSize();
    const size_t outCap = ZSTD_DStreamOutSize();
    std::vector<char> inBuf(inCap);
    std::vector<char> outBuf(outCap);

    bool ok = true;
    size_t lastRet = 1; // non-zero until the decoder reports a clean frame end
    size_t read = 0;
    while (ok && (read = std::fread(inBuf.data(), 1, inCap, in)) > 0)
    {
        ZSTD_inBuffer input = {inBuf.data(), read, 0};
        while (input.pos < input.size)
        {
            ZSTD_outBuffer output = {outBuf.data(), outCap, 0};
            lastRet = ZSTD_decompressStream(ds, &output, &input);
            if (ZSTD_isError(lastRet))
            {
                ok = fail(std::string("zstd decode failed: ") + ZSTD_getErrorName(lastRet));
                break;
            }
            if (output.pos > 0 && std::fwrite(outBuf.data(), 1, output.pos, out) != output.pos)
            {
                ok = fail(std::string("short write: ") + dstPath);
                break;
            }
        }
    }

    ZSTD_freeDStream(ds);
    std::fclose(in);
    std::fclose(out);

    if (ok && lastRet != 0)
    {
        return fail("zstd: truncated frame");
    }
    return ok;
}

std::string StripPboExtension(const std::string& path)
{
    if (path.size() >= 4)
    {
        std::string tail;
        for (size_t i = path.size() - 4; i < path.size(); ++i)
        {
            tail.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(path[i]))));
        }
        if (tail == ".pbo")
        {
            return path.substr(0, path.size() - 4);
        }
    }
    return path;
}

std::string NormalizeEntryName(const std::string& name)
{
    std::string out = name;
    for (char& c : out)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    return out;
}
} // namespace

bool ModArchive::IsSafeEntryPath(const std::string& entryName)
{
    namespace fs = std::filesystem;
    const std::string normalized = NormalizeEntryName(entryName);
    if (normalized.empty() || normalized[0] == '/' ||
        (normalized.size() >= 2 && std::isalpha(static_cast<unsigned char>(normalized[0])) && normalized[1] == ':'))
        return false;
    const fs::path path(normalized);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return false;
    for (const fs::path& component : path)
    {
        if (component == "..")
            return false;
    }
    return true;
}

bool ModArchive::Unpack(const char* archivePath, const char* destDir, std::string* error)
{
    const auto fail = [&](const std::string& message)
    {
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    };

    if (archivePath == nullptr || destDir == nullptr)
    {
        return fail("null path");
    }

    namespace fs = std::filesystem;
    std::error_code ec;

    // Mods ship as zstd-wrapped PBOs. Stream-decode into a private temp PBO that
    // the bank reader (which needs a seekable file) can then open.
    static std::atomic<uint64_t> seq{0};
    std::random_device rd;
    const fs::path tmpDir =
        fs::temp_directory_path() / ("cwr_modunpack_" + std::to_string(rd()) + "_" + std::to_string(seq.fetch_add(1)));
    fs::create_directories(tmpDir, ec);
    const fs::path tmpPbo = tmpDir / "mod.pbo";

    const auto cleanup = [&]() { fs::remove_all(tmpDir, ec); };

    if (!DecompressArchive(archivePath, tmpPbo.string().c_str(), error))
    {
        cleanup();
        return false;
    }

    const std::string pboPath = tmpPbo.string();
    const PboInfo info = InspectPbo(pboPath);
    if (!info.valid)
    {
        cleanup();
        return fail(std::string("cannot inspect pbo: ") + archivePath);
    }

    QFBank bank;
    if (!bank.open(RString(StripPboExtension(pboPath).c_str())))
    {
        cleanup();
        return fail(std::string("cannot open pbo: ") + archivePath);
    }
    bank.Lock();
    if (bank.error())
    {
        bank.Unlock();
        cleanup();
        return fail(std::string("failed to load pbo: ") + archivePath);
    }

    const fs::path root(destDir);
    for (const auto& entry : info.entries)
    {
        if (!IsSafeEntryPath(entry.name))
        {
            bank.Unlock();
            cleanup();
            return fail(std::string("unsafe archive entry: ") + entry.name);
        }
    }
    fs::create_directories(root, ec);

    bool ok = true;
    for (const auto& entry : info.entries)
    {
        Ref<IFileBuffer> data = bank.Read(entry.name.c_str());
        if (!data)
        {
            ok = fail(std::string("cannot read entry: ") + entry.name);
            break;
        }

        const fs::path outPath = root / fs::path(NormalizeEntryName(entry.name));
        fs::create_directories(outPath.parent_path(), ec);

        FILE* out = std::fopen(outPath.string().c_str(), "wb");
        if (out == nullptr)
        {
            ok = fail(std::string("cannot write: ") + outPath.string());
            break;
        }

        const int size = data->GetSize();
        if (size > 0)
        {
            const size_t written = std::fwrite(data->GetData(), 1, static_cast<size_t>(size), out);
            if (written != static_cast<size_t>(size))
            {
                std::fclose(out);
                ok = fail(std::string("short write: ") + outPath.string());
                break;
            }
        }
        std::fclose(out);
    }

    bank.Unlock();
    cleanup();
    return ok;
}

bool ModArchive::DecompressArchive(const char* archivePath, const char* pboPath, std::string* error)
{
    const auto fail = [&](const std::string& message)
    {
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    };

    if (archivePath == nullptr || pboPath == nullptr)
    {
        return fail("null path");
    }
    std::error_code ec;
    if (!std::filesystem::exists(archivePath, ec))
    {
        return fail(std::string("archive not found: ") + archivePath);
    }
    return DecompressZstdToFile(archivePath, pboPath, error);
}
} // namespace Poseidon
