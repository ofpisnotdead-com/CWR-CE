#include <Poseidon/Core/ModInstall.hpp>

#include <Poseidon/Core/ModCollection.hpp>

#include <cjson/cJSON.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace Poseidon
{
namespace
{
class Sha256
{
  public:
    void Update(const uint8_t* data, size_t size)
    {
        _bitCount += static_cast<uint64_t>(size) * 8;
        while (size > 0)
        {
            const size_t count = std::min(size, _block.size() - _blockSize);
            std::copy_n(data, count, _block.data() + _blockSize);
            data += count;
            size -= count;
            _blockSize += count;
            if (_blockSize == _block.size())
            {
                Transform(_block.data());
                _blockSize = 0;
            }
        }
    }

    std::array<uint8_t, 32> Finish()
    {
        _block[_blockSize++] = 0x80;
        if (_blockSize > 56)
        {
            std::fill(_block.begin() + _blockSize, _block.end(), 0);
            Transform(_block.data());
            _blockSize = 0;
        }
        std::fill(_block.begin() + _blockSize, _block.begin() + 56, 0);
        for (int i = 0; i < 8; ++i)
            _block[63 - i] = static_cast<uint8_t>(_bitCount >> (i * 8));
        Transform(_block.data());

        std::array<uint8_t, 32> digest{};
        for (size_t i = 0; i < _state.size(); ++i)
            for (int byte = 0; byte < 4; ++byte)
                digest[i * 4 + byte] = static_cast<uint8_t>(_state[i] >> (24 - byte * 8));
        return digest;
    }

  private:
    static uint32_t Rotate(uint32_t value, int count) { return (value >> count) | (value << (32 - count)); }
    void Transform(const uint8_t* block)
    {
        static constexpr uint32_t constants[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t words[64];
        for (int i = 0; i < 16; ++i)
            words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
        for (int i = 16; i < 64; ++i)
        {
            const uint32_t s0 = Rotate(words[i - 15], 7) ^ Rotate(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const uint32_t s1 = Rotate(words[i - 2], 17) ^ Rotate(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        uint32_t a = _state[0], b = _state[1], c = _state[2], d = _state[3];
        uint32_t e = _state[4], f = _state[5], g = _state[6], h = _state[7];
        for (int i = 0; i < 64; ++i)
        {
            const uint32_t s1 = Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choice + constants[i] + words[i];
            const uint32_t s0 = Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        _state[0] += a;
        _state[1] += b;
        _state[2] += c;
        _state[3] += d;
        _state[4] += e;
        _state[5] += f;
        _state[6] += g;
        _state[7] += h;
    }

    std::array<uint32_t, 8> _state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> _block{};
    size_t _blockSize = 0;
    uint64_t _bitCount = 0;
};
} // namespace

bool VerifyModArtifact(const std::string& path, int64_t expectedBytes, const std::string& expectedSha256,
                       std::string* error)
{
    const auto fail = [&](const std::string& message)
    {
        if (error != nullptr)
            *error = message;
        return false;
    };
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return fail("cannot open downloaded package");
    Sha256 hash;
    std::array<uint8_t, 64 * 1024> buffer{};
    int64_t size = 0;
    size_t read = 0;
    while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0)
    {
        hash.Update(buffer.data(), read);
        size += static_cast<int64_t>(read);
    }
    const bool readError = std::ferror(file) != 0;
    std::fclose(file);
    if (readError)
        return fail("cannot read downloaded package");
    if (expectedBytes > 0 && size != expectedBytes)
        return fail("downloaded package size mismatch");
    if (expectedSha256.empty())
        return true;

    static constexpr char hex[] = "0123456789abcdef";
    std::string actual;
    for (uint8_t byte : hash.Finish())
    {
        actual.push_back(hex[byte >> 4]);
        actual.push_back(hex[byte & 0x0f]);
    }
    std::string expected = expectedSha256;
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return actual == expected ? true : fail("downloaded package SHA-256 mismatch");
}

StagedModInstall MakeStagedModInstall(const std::string& destinationDir, const std::string& modId)
{
    static std::atomic<uint64_t> sequence{0};
    const std::string suffix = std::to_string(sequence.fetch_add(1));
    return {modId, destinationDir + ".staging-" + suffix, destinationDir, destinationDir + ".backup-" + suffix};
}

void RestoreStagedModInstalls(std::vector<StagedModInstall>& installs)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto it = installs.rbegin(); it != installs.rend(); ++it)
    {
        if (it->stagingMoved)
        {
            ec.clear();
            fs::remove_all(it->destinationDir, ec);
            it->stagingMoved = false;
        }
        if (it->previousMoved)
        {
            ec.clear();
            fs::rename(it->backupDir, it->destinationDir, ec);
            if (!ec)
                it->previousMoved = false;
        }
    }
}

bool SwapStagedModInstalls(std::vector<StagedModInstall>& installs, std::string* error)
{
    namespace fs = std::filesystem;
    const auto fail = [&](const std::string& message)
    {
        RestoreStagedModInstalls(installs);
        if (error != nullptr)
            *error = message;
        return false;
    };

    std::error_code ec;
    for (StagedModInstall& install : installs)
    {
        ec.clear();
        if (!fs::is_directory(install.stagingDir, ec))
            return fail("staged mod directory is missing: " + install.stagingDir);
        if (fs::exists(install.destinationDir, ec))
        {
            ec.clear();
            fs::rename(install.destinationDir, install.backupDir, ec);
            if (ec)
                return fail("cannot back up installed mod: " + ec.message());
            install.previousMoved = true;
        }
        ec.clear();
        fs::rename(install.stagingDir, install.destinationDir, ec);
        if (ec)
            return fail("cannot activate staged mod: " + ec.message());
        install.stagingMoved = true;
    }
    return true;
}

void CommitStagedModInstalls(std::vector<StagedModInstall>& installs)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (StagedModInstall& install : installs)
    {
        if (install.previousMoved)
        {
            ec.clear();
            fs::remove_all(install.backupDir, ec);
        }
        ec.clear();
        fs::remove(install.stagingDir + ".pbo.zst", ec);
    }
    installs.clear();
}

void DiscardStagedModInstalls(std::vector<StagedModInstall>& installs)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    RestoreStagedModInstalls(installs);
    for (const StagedModInstall& install : installs)
    {
        ec.clear();
        fs::remove_all(install.stagingDir, ec);
        ec.clear();
        fs::remove(install.stagingDir + ".pbo.zst", ec);
    }
    installs.clear();
}

std::string ModInstallDir(const std::string& modsRoot, const std::string& modId)
{
    return modsRoot + "/@" + modId;
}

std::string ModInstallDir(const std::string& modsRoot, const std::string& modId, const std::string& folderName)
{
    if (!folderName.empty())
        return modsRoot + "/" + folderName;
    return ModInstallDir(modsRoot, modId);
}

std::string FindInstalledModDir(const std::string& modsRoot, const std::string& modId)
{
    ModCollection mods;
    for (Mod& mod : ScanModsRoot(modsRoot, ModSource::Workshop))
        mods.Add(std::move(mod));
    const Mod* found = mods.Find(modId);
    return found != nullptr ? found->path : std::string();
}

std::string ReadInstalledModVersion(const std::string& modsRoot, const std::string& modId)
{
    namespace fs = std::filesystem;
    const std::string installDir = FindInstalledModDir(modsRoot, modId);
    if (installDir.empty())
        return {};
    const fs::path metadata = fs::path(installDir) / "mod.json";
    std::error_code ec;
    if (!fs::exists(metadata, ec))
    {
        return {};
    }

    std::ifstream in(metadata, std::ios::binary);
    if (!in)
    {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    cJSON* root = cJSON_Parse(text.c_str());
    std::string version;
    if (root != nullptr)
    {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "version");
        if (cJSON_IsString(item) && item->valuestring != nullptr)
        {
            version = item->valuestring;
        }
        cJSON_Delete(root);
    }
    return version;
}

int64_t ReadInstalledPackageRevision(const std::string& modsRoot, const std::string& modId)
{
    namespace fs = std::filesystem;
    const std::string installDir = FindInstalledModDir(modsRoot, modId);
    if (installDir.empty())
        return 0;
    std::ifstream in(fs::path(installDir) / "mod.json", std::ios::binary);
    if (!in)
        return 1;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    cJSON* root = cJSON_Parse(buffer.str().c_str());
    int64_t revision = 1;
    if (root != nullptr)
    {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "packageRevision");
        if (cJSON_IsNumber(item) && item->valuedouble >= 1)
            revision = static_cast<int64_t>(item->valuedouble);
        cJSON_Delete(root);
    }
    return revision;
}

std::string ReadInstalledArtifactHash(const std::string& modsRoot, const std::string& modId)
{
    namespace fs = std::filesystem;
    const std::string installDir = FindInstalledModDir(modsRoot, modId);
    if (installDir.empty())
        return {};
    std::ifstream in(fs::path(installDir) / "mod.json", std::ios::binary);
    if (!in)
        return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    cJSON* root = cJSON_Parse(buffer.str().c_str());
    std::string hash;
    if (root != nullptr)
    {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "sha256");
        if (cJSON_IsString(item) && item->valuestring != nullptr)
            hash = item->valuestring;
        cJSON_Delete(root);
    }
    return hash;
}

ModInstallStatus GetModInstallStatus(const std::string& modsRoot, const std::string& modId,
                                     const std::string& catalogVersion)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (FindInstalledModDir(modsRoot, modId).empty())
    {
        return ModInstallStatus::NotInstalled;
    }

    const std::string installed = ReadInstalledModVersion(modsRoot, modId);
    // Installed with no readable version → can't prove it's stale, so don't nag.
    if (installed.empty() || installed == catalogVersion)
    {
        return ModInstallStatus::Installed;
    }
    return ModInstallStatus::UpdateAvailable;
}

ModInstallStatus GetModInstallStatus(const std::string& modsRoot, const std::string& modId,
                                     const std::string& catalogVersion, int64_t catalogRevision,
                                     const std::string& catalogSha256)
{
    (void)catalogVersion;
    if (FindInstalledModDir(modsRoot, modId).empty())
        return ModInstallStatus::NotInstalled;
    const int64_t installedRevision = ReadInstalledPackageRevision(modsRoot, modId);
    if (installedRevision < catalogRevision)
        return ModInstallStatus::UpdateAvailable;
    if (installedRevision > catalogRevision)
        return ModInstallStatus::InstalledAhead;
    const std::string installedHash = ReadInstalledArtifactHash(modsRoot, modId);
    if (!installedHash.empty() && !catalogSha256.empty() && installedHash != catalogSha256)
        return ModInstallStatus::UpdateAvailable;
    return ModInstallStatus::Installed;
}

std::vector<ScannedMod> ScanLocalMods(const std::string& modsRoot)
{
    // The catalog view of a local scan: ScanModsRoot does the work (and sorts by
    // display name); ScannedMod is the slimmer install-status projection of Mod.
    std::vector<ScannedMod> mods;
    for (const Mod& m : ScanModsRoot(modsRoot, ModSource::Local))
        mods.push_back({m.catalogId.empty() ? m.id : m.catalogId, m.id, m.name, m.version, m.packageRevision, m.sha256,
                        m.sizeBytes});
    return mods;
}

bool WriteInstalledModManifest(const std::string& installDir, const std::string& modId, const std::string& name,
                               const std::string& version, const std::string& folderName,
                               const std::string& downloadUrl, int64_t sizeBytes, std::string* error,
                               int64_t packageRevision, const std::string& sha256)
{
    namespace fs = std::filesystem;
    const auto fail = [&](const std::string& message)
    {
        if (error != nullptr)
            *error = message;
        return false;
    };

    fs::create_directories(installDir);
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr)
        return fail("cannot allocate mod manifest");
    cJSON_AddStringToObject(root, "modId", modId.c_str());
    cJSON_AddStringToObject(root, "name", name.c_str());
    cJSON_AddStringToObject(root, "version", version.c_str());
    cJSON_AddNumberToObject(root, "packageRevision", static_cast<double>(std::max<int64_t>(1, packageRevision)));
    if (!sha256.empty())
        cJSON_AddStringToObject(root, "sha256", sha256.c_str());
    if (!folderName.empty())
        cJSON_AddStringToObject(root, "folderName", folderName.c_str());
    if (!downloadUrl.empty())
        cJSON_AddStringToObject(root, "downloadUrl", downloadUrl.c_str());
    if (sizeBytes > 0)
        cJSON_AddNumberToObject(root, "sizeBytes", static_cast<double>(sizeBytes));

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == nullptr)
        return fail("cannot serialize mod manifest");

    std::ofstream out(fs::path(installDir) / "mod.json", std::ios::binary);
    if (!out)
    {
        cJSON_free(text);
        return fail("cannot write mod manifest");
    }
    out << text;
    cJSON_free(text);
    return true;
}
} // namespace Poseidon
