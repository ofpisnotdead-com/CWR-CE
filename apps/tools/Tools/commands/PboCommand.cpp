#include "PboCommand.hpp"
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PackFiles.hpp>
#include <Poseidon/Asset/Probes/AssetInfo.hpp>
#include <iostream>
#include <fstream>
#include <array>
#include <iomanip>
#include <sstream>
#include <vector>
#include <ctime>
#include <cstring>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <CLI/App.hpp>
#include <CLI/Option.hpp>
#include <cstdlib>
#include <functional>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <sys/utime.h>
#else
#include <sys/types.h>
#include <utime.h>
#endif

namespace PoseidonTools
{
namespace
{

uint32_t RotateLeft(uint32_t value, uint32_t bits)
{
    return (value << bits) | (value >> (32U - bits));
}

std::string Md5Hex(const void* data, size_t size)
{
    static constexpr std::array<uint32_t, 64> shifts = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                                        5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                                        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                                        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    static constexpr std::array<uint32_t, 64> constants = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

    const auto* input = static_cast<const uint8_t*>(data);
    std::vector<uint8_t> message(input, input + size);
    const uint64_t bitLength = static_cast<uint64_t>(size) * 8U;

    message.push_back(0x80);
    while ((message.size() % 64) != 56)
        message.push_back(0);

    for (int i = 0; i < 8; ++i)
        message.push_back(static_cast<uint8_t>((bitLength >> (8 * i)) & 0xffU));

    uint32_t a0 = 0x67452301;
    uint32_t b0 = 0xefcdab89;
    uint32_t c0 = 0x98badcfe;
    uint32_t d0 = 0x10325476;

    for (size_t offset = 0; offset < message.size(); offset += 64)
    {
        uint32_t words[16];
        for (int i = 0; i < 16; ++i)
        {
            const size_t j = offset + static_cast<size_t>(i) * 4;
            words[i] = static_cast<uint32_t>(message[j]) | (static_cast<uint32_t>(message[j + 1]) << 8) |
                       (static_cast<uint32_t>(message[j + 2]) << 16) | (static_cast<uint32_t>(message[j + 3]) << 24);
        }

        uint32_t a = a0;
        uint32_t b = b0;
        uint32_t c = c0;
        uint32_t d = d0;

        for (uint32_t i = 0; i < 64; ++i)
        {
            uint32_t f = 0;
            uint32_t g = 0;
            if (i < 16)
            {
                f = (b & c) | (~b & d);
                g = i;
            }
            else if (i < 32)
            {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            }
            else if (i < 48)
            {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            }
            else
            {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }

            const uint32_t next = d;
            d = c;
            c = b;
            b += RotateLeft(a + f + constants[i] + words[g], shifts[i]);
            a = next;
        }

        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::nouppercase;
    for (uint32_t word : {a0, b0, c0, d0})
    {
        for (int i = 0; i < 4; ++i)
            out << std::setw(2) << ((word >> (8 * i)) & 0xffU);
    }
    return out.str();
}

std::string Md5Hex(const IFileBuffer& buffer)
{
    return Md5Hex(buffer.GetData(), static_cast<size_t>(buffer.GetSize()));
}

bool SetFileTimestamp(const std::string& path, long timestamp)
{
#ifdef _WIN32
    _utimbuf times{};
    times.actime = static_cast<time_t>(timestamp);
    times.modtime = static_cast<time_t>(timestamp);
    return _utime(path.c_str(), &times) == 0;
#else
    utimbuf times{};
    times.actime = static_cast<time_t>(timestamp);
    times.modtime = static_cast<time_t>(timestamp);
    return utime(path.c_str(), &times) == 0;
#endif
}

} // namespace

std::string PboCommand::StripPboExtension(const std::string& path)
{
    // QFBank::open() appends ".pbo" automatically
    if (path.size() >= 4)
    {
        std::string ext = path.substr(path.size() - 4);
        for (auto& c : ext)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (ext == ".pbo")
            return path.substr(0, path.size() - 4);
    }
    return path;
}

std::string PboCommand::GetDefaultOutputDir(const std::string& pboPath)
{
    std::string base = StripPboExtension(pboPath);
    auto pos = base.find_last_of("/\\");
    if (pos != std::string::npos)
        return base.substr(pos + 1);
    return base;
}

bool PboCommand::CreateDirectories(const std::string& path)
{
    if (path.empty())
        return true;
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return true;
    auto pos = path.find_last_of("/\\");
    if (pos != std::string::npos && pos > 0)
    {
        if (!CreateDirectories(path.substr(0, pos)))
            return false;
    }
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

std::string PboCommand::NormalizePath(const std::string& path)
{
#ifdef _WIN32
    std::string result = path;
    for (auto& c : result)
        if (c == '/')
            c = '\\';
    return result;
#else
    std::string result = path;
    for (auto& c : result)
        if (c == '\\')
            c = '/';
    return result;
#endif
}

std::string PboCommand::NormalizeBankPath(const std::string& path)
{
    std::string result = path;
    for (auto& c : result)
        if (c == '/')
            c = '\\';
    return result;
}

void PboCommand::Setup(CLI::App& app)
{
    auto* pbo = app.add_subcommand("pbo", "PBO archive management");
    pbo->require_subcommand(1);
    auto* listCmd = pbo->add_subcommand("list", "List files in a PBO archive");
    static std::string listPath;
    static bool verbose = false;
    listCmd->add_option("file", listPath, "Path to PBO file")->required();
    listCmd->add_flag("--verbose,-V", verbose, "Show detailed information");
    listCmd->callback([]() { std::exit(List(listPath, verbose)); });
    auto* showCmd = pbo->add_subcommand("show", "Print file contents from a PBO to stdout");
    static std::string showPbo;
    static std::string showFile;
    showCmd->add_option("pbo", showPbo, "Path to PBO file")->required();
    showCmd->add_option("file", showFile, "File path inside the PBO")->required();
    showCmd->callback([]() { std::exit(Show(showPbo, showFile)); });
    auto* extractCmd = pbo->add_subcommand("extract", "Extract files from a PBO archive");
    static std::string extractPath;
    static std::string outputDir;
    static std::string fileFilter;
    extractCmd->add_option("file", extractPath, "Path to PBO file")->required();
    extractCmd->add_option("output", outputDir, "Output directory (default: PBO name without extension)");
    extractCmd->add_option("--filter,-f", fileFilter, "Extract only files matching this substring");
    extractCmd->callback([]() { std::exit(Extract(extractPath, outputDir, fileFilter)); });
    auto* packCmd = pbo->add_subcommand("pack", "Create a PBO archive from a directory");
    static std::string packSrc;
    static std::string packOut;
    static bool compress = false;
    packCmd->add_option("source", packSrc, "Source directory")->required();
    packCmd->add_option("output", packOut, "Output PBO file path")->required();
    packCmd->add_flag("--compress,-c", compress, "Enable LZSS compression");
    packCmd->callback([]() { std::exit(Pack(packSrc, packOut, compress)); });
}

int PboCommand::List(const std::string& pboPath, bool verbose)
{
    auto info = Poseidon::InspectPbo(pboPath);
    if (!info.valid)
    {
        std::cerr << "Error: Cannot open PBO: " << pboPath << std::endl;
        return 1;
    }

    QFBank bank;
    bool bankReady = false;
    if (verbose)
    {
        std::string bankName = StripPboExtension(pboPath);
        if (bank.open(RString(bankName.c_str())))
        {
            bank.Lock();
            bankReady = !bank.error();
        }
    }

    for (const auto& e : info.entries)
    {
        long displaySize = e.compressed ? e.uncompressedSize : e.length;

        std::cout << e.name;
        int pad = 40 - static_cast<int>(e.name.size());
        if (pad > 0)
            std::cout << std::string(pad, ' ');
        else
            std::cout << "  ";

        std::cout << Poseidon::FormatSize(displaySize);
        std::cout << "  " << Poseidon::FormatTime(e.time);

        if (e.compressed)
            std::cout << "  [compressed]";

        if (verbose && e.compressed)
            std::cout << " (" << Poseidon::FormatSize(e.length) << " stored)";

        if (verbose && bankReady)
        {
            Ref<IFileBuffer> data = bank.Read(e.name.c_str());
            if (data)
                std::cout << "  md5: " << Md5Hex(*data);
        }

        std::cout << std::endl;
    }

    if (verbose && bankReady)
        bank.Unlock();

    std::cout << "---" << std::endl;
    std::cout << info.entries.size() << " files, " << Poseidon::FormatSize(info.totalSize) << " total";
    if (verbose && info.totalStored != info.totalSize)
        std::cout << " (" << Poseidon::FormatSize(info.totalStored) << " stored)";
    std::cout << std::endl;

    return 0;
}

int PboCommand::Show(const std::string& pboPath, const std::string& filePath)
{
    std::string bankName = StripPboExtension(pboPath);
    QFBank bank;
    if (!bank.open(RString(bankName.c_str())))
    {
        std::cerr << "Error: Cannot open PBO: " << pboPath << std::endl;
        return 1;
    }
    bank.Lock();

    if (bank.error())
    {
        std::cerr << "Error: Failed to load PBO: " << pboPath << std::endl;
        bank.Unlock();
        return 1;
    }

    Ref<IFileBuffer> data = bank.Read(filePath.c_str());
    if (!data || data->GetSize() == 0)
    {
        std::cerr << "Error: File not found in PBO: " << filePath << std::endl;
        bank.Unlock();
        return 1;
    }

    // Keep redirected binary payloads byte-exact on Windows.
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::cout.write(static_cast<const char*>(data->GetData()), static_cast<std::streamsize>(data->GetSize()));
    std::cout.flush();

    bank.Unlock();
    return 0;
}

int PboCommand::Extract(const std::string& pboPath, const std::string& outputDir, const std::string& fileFilter)
{
    struct stat st;
    if (stat(pboPath.c_str(), &st) != 0)
    {
        std::cerr << "Error: File not found: " << pboPath << std::endl;
        return 1;
    }

    std::string bankName = StripPboExtension(pboPath);
    QFBank bank;
    if (!bank.open(RString(bankName.c_str())))
    {
        std::cerr << "Error: Cannot open PBO: " << pboPath << std::endl;
        return 1;
    }
    bank.Lock();

    if (bank.error())
    {
        std::cerr << "Error: Failed to load PBO: " << pboPath << std::endl;
        bank.Unlock();
        return 1;
    }
    auto info = Poseidon::InspectPbo(pboPath);
    if (!info.valid)
    {
        bank.Unlock();
        std::cerr << "Error: Failed to inspect PBO: " << pboPath << std::endl;
        return 1;
    }
    std::string outDir = outputDir.empty() ? GetDefaultOutputDir(pboPath) : outputDir;
    outDir = NormalizePath(outDir);
    std::string normalizedFilter = NormalizeBankPath(fileFilter);

    int extracted = 0;
    int skipped = 0;
    for (const auto& e : info.entries)
    {
        if (!normalizedFilter.empty() && NormalizeBankPath(e.name).find(normalizedFilter) == std::string::npos)
        {
            skipped++;
            continue;
        }

        Ref<IFileBuffer> data = bank.Read(e.name.c_str());
        if (!data)
        {
            std::cerr << "Warning: Cannot read: " << e.name << std::endl;
            continue;
        }

        std::string entryPath = NormalizePath(e.name);
        std::string fullPath = outDir + "/" + entryPath;

        auto lastSep = fullPath.find_last_of("/\\");
        if (lastSep != std::string::npos)
        {
            std::string parentDir = fullPath.substr(0, lastSep);
            if (!CreateDirectories(parentDir))
            {
                std::cerr << "Error: Cannot create directory: " << parentDir << std::endl;
                continue;
            }
        }

        FILE* outFile = fopen(fullPath.c_str(), "wb");
        if (!outFile)
        {
            std::cerr << "Error: Cannot write: " << fullPath << std::endl;
            continue;
        }
        if (data->GetSize() > 0)
            fwrite(data->GetData(), data->GetSize(), 1, outFile);
        fclose(outFile);
        if (!SetFileTimestamp(fullPath, e.time))
            std::cerr << "Warning: Cannot set timestamp: " << fullPath << std::endl;

        long displaySize = e.compressed ? e.uncompressedSize : e.length;
        std::cout << "  " << e.name << " (" << Poseidon::FormatSize(displaySize);
        if (e.compressed)
            std::cout << ", compressed";
        std::cout << ")" << std::endl;
        extracted++;
    }

    std::cout << "Extracted " << extracted << " files to " << outDir << std::endl;
    if (skipped > 0)
        std::cout << "Skipped " << skipped << " files (filter: " << fileFilter << ")" << std::endl;

    bank.Unlock();
    return 0;
}

int PboCommand::Pack(const std::string& srcDir, const std::string& outputPbo, bool compress)
{
    struct stat st;
    if (stat(srcDir.c_str(), &st) != 0 || !(st.st_mode & S_IFDIR))
    {
        std::cerr << "Error: Source directory not found: " << srcDir << std::endl;
        return 1;
    }

    remove(outputPbo.c_str());

    std::cout << "Packing " << srcDir << " -> " << outputPbo;
    if (compress)
        std::cout << " (compressed)";
    std::cout << std::endl;

    FileBankManager mgr;
    LSError result = mgr.Create(outputPbo.c_str(), srcDir.c_str(), compress);
    if (result != LSOK)
    {
        std::cerr << "Error: Failed to create PBO" << std::endl;
        return 1;
    }

    if (stat(outputPbo.c_str(), &st) != 0)
    {
        std::cerr << "Error: Output file was not created" << std::endl;
        return 1;
    }

    std::cout << "Created " << outputPbo << " (" << Poseidon::FormatSize(st.st_size) << ")" << std::endl;
    return 0;
}

} // namespace PoseidonTools
