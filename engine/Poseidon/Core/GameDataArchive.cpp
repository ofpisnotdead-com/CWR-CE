#include <Poseidon/Core/GameDataArchive.hpp>

#include <zip.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Poseidon
{
namespace
{
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

// Zip-slip guard: the archive is untrusted (a URL or a file the user picked),
// unlike ModArchive's closed/trusted PBO format, so an entry that could escape
// destDir via ".." or an absolute/rooted path must be rejected outright rather
// than merely normalized.
bool IsSafeEntryName(const std::string& normalized)
{
    if (normalized.empty())
    {
        return false;
    }
    if (normalized.front() == '/' || (normalized.size() >= 2 && normalized[1] == ':')) // "/etc/..." or "C:..."
    {
        return false;
    }

    size_t start = 0;
    while (start <= normalized.size())
    {
        const size_t slash = normalized.find('/', start);
        const std::string segment = normalized.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (segment == "..")
        {
            return false;
        }
        if (slash == std::string::npos)
        {
            break;
        }
        start = slash + 1;
    }
    return true;
}

std::string ZipErrorMessage(int libzipErrorCode)
{
    zip_error_t err;
    zip_error_init_with_code(&err, libzipErrorCode);
    const std::string message = zip_error_strerror(&err);
    zip_error_fini(&err);
    return message;
}

// Junk that macOS's Finder "Compress" (and plain `zip -r` without -X) adds:
// AppleDouble resource-fork shadow files and Finder's own .DS_Store, under a
// __MACOSX/ sibling tree or alongside the real entries. Never part of the
// game-data tree; silently skipped rather than written out.
bool IsNoiseEntry(const std::string& normalized)
{
    if (normalized.rfind("__MACOSX/", 0) == 0)
    {
        return true;
    }
    const size_t lastSlash = normalized.find_last_of('/');
    const std::string basename = lastSlash == std::string::npos ? normalized : normalized.substr(lastSlash + 1);
    return basename == ".DS_Store" || basename.rfind("._", 0) == 0;
}

std::string FirstPathSegment(const std::string& normalized)
{
    const size_t slash = normalized.find('/');
    return slash == std::string::npos ? std::string() : normalized.substr(0, slash);
}

// Archives created by zipping a folder (Finder "Compress", `zip -r name/ .`)
// wrap every real entry in that folder's name -- if every non-noise entry
// shares the same single top-level directory, strip it so DTA/AddOns/BIN land
// at destDir's root as expected, matching ordinary unarchiver behavior (e.g.
// `tar --strip-components=1`). Returns "" (nothing to strip) when entries
// already live at the archive root, or span multiple top-level directories.
std::string CommonTopLevelPrefix(zip_t* za, zip_int64_t numEntries)
{
    std::string commonTop;
    bool sawAny = false;
    for (zip_int64_t i = 0; i < numEntries; ++i)
    {
        const char* rawName = zip_get_name(za, static_cast<zip_uint64_t>(i), 0);
        if (rawName == nullptr)
        {
            continue;
        }
        const std::string normalized = NormalizeEntryName(rawName);
        if (IsNoiseEntry(normalized))
        {
            continue;
        }
        const std::string top = FirstPathSegment(normalized);
        if (top.empty())
        {
            return {}; // a real entry already lives at the archive root
        }
        if (!sawAny)
        {
            commonTop = top;
            sawAny = true;
        }
        else if (top != commonTop)
        {
            return {};
        }
    }
    return commonTop;
}
} // namespace

bool GameDataArchive::Unpack(const char* archivePath, const char* destDir,
                             const std::function<void(int64_t, int64_t)>& onProgress, std::string* error)
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
    if (!fs::exists(archivePath, ec))
    {
        return fail(std::string("archive not found: ") + archivePath);
    }

    int err = 0;
    zip_t* za = zip_open(archivePath, ZIP_RDONLY, &err);
    if (za == nullptr)
    {
        return fail("cannot open archive: " + ZipErrorMessage(err));
    }

    const fs::path root(destDir);
    fs::create_directories(root, ec);

    const zip_int64_t numEntries = zip_get_num_entries(za, 0);
    const std::string topPrefix = CommonTopLevelPrefix(za, numEntries);
    const std::string stripPrefix = topPrefix.empty() ? std::string() : topPrefix + "/";

    bool ok = true;
    for (zip_int64_t i = 0; ok && i < numEntries; ++i)
    {
        const char* rawName = zip_get_name(za, static_cast<zip_uint64_t>(i), 0);
        if (rawName == nullptr)
        {
            ok = fail("cannot read entry name at index " + std::to_string(i));
            break;
        }

        const std::string normalized = NormalizeEntryName(rawName);
        if (IsNoiseEntry(normalized))
        {
            continue;
        }

        std::string relative = normalized;
        if (!stripPrefix.empty())
        {
            if (relative.rfind(stripPrefix, 0) == 0)
            {
                relative = relative.substr(stripPrefix.size());
            }
            else
            {
                // The wrapping folder's own directory entry (e.g. "Combined/")
                // -- nothing to create at destDir's root for ".".
                continue;
            }
        }
        if (relative.empty())
        {
            continue;
        }

        if (!IsSafeEntryName(relative))
        {
            ok = fail("archive entry escapes destination: " + std::string(rawName));
            break;
        }

        const fs::path outPath = root / fs::path(relative);

        // Directory entries end with '/' and carry no data to read.
        if (relative.back() == '/')
        {
            fs::create_directories(outPath, ec);
            if (onProgress)
            {
                onProgress(i + 1, numEntries);
            }
            continue;
        }

        fs::create_directories(outPath.parent_path(), ec);

        zip_file_t* zf = zip_fopen_index(za, static_cast<zip_uint64_t>(i), 0);
        if (zf == nullptr)
        {
            ok = fail("cannot open archive entry: " + std::string(rawName));
            break;
        }

        FILE* out = std::fopen(outPath.string().c_str(), "wb");
        if (out == nullptr)
        {
            zip_fclose(zf);
            ok = fail("cannot write: " + outPath.string());
            break;
        }

        std::vector<char> buf(1 << 16);
        zip_int64_t readBytes = 0;
        while ((readBytes = zip_fread(zf, buf.data(), buf.size())) > 0)
        {
            if (std::fwrite(buf.data(), 1, static_cast<size_t>(readBytes), out) != static_cast<size_t>(readBytes))
            {
                ok = fail("short write: " + outPath.string());
                break;
            }
        }
        if (readBytes < 0)
        {
            ok = fail("cannot read archive entry: " + std::string(rawName));
        }

        std::fclose(out);
        zip_fclose(zf);

        if (ok && onProgress)
        {
            onProgress(i + 1, numEntries);
        }
    }

    zip_close(za);
    return ok;
}

} // namespace Poseidon
