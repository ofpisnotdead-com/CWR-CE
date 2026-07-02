#include <Poseidon/Core/GameDataInstall.hpp>

#include <Poseidon/Foundation/Platform/GamePaths.hpp>

#include <cjson/cJSON.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace Poseidon
{
namespace fs = std::filesystem;

std::string GameDataDir()
{
    return Foundation::GamePaths::Instance().UserContentDir() + "/GameData";
}

std::vector<std::string> RequiredGameDataPaths()
{
    return {"BIN/CONFIG.BIN", "DTA/Fonts.pbo", "AddOns"};
}

namespace
{
bool RequiredPathPresent(const fs::path& dir, const std::string& relative)
{
    std::error_code ec;
    const fs::path full = dir / relative;
    if (fs::is_directory(full, ec))
        return !fs::is_empty(full, ec);
    return fs::is_regular_file(full, ec) && fs::file_size(full, ec) > 0;
}
} // namespace

GameDataStatus DetectGameDataStatus(const std::string& gameDataDir)
{
    std::error_code ec;
    const fs::path dir = gameDataDir;
    if (!fs::is_directory(dir, ec))
        return GameDataStatus::Missing;

    if (!fs::is_regular_file(dir / "manifest.json", ec))
        return GameDataStatus::Partial;

    for (const std::string& relative : RequiredGameDataPaths())
    {
        if (!RequiredPathPresent(dir, relative))
            return GameDataStatus::Partial;
    }
    return GameDataStatus::Ready;
}

bool WriteGameDataManifest(const std::string& gameDataDir, const std::string& sourceUrl, int64_t sizeBytes,
                           int64_t fileCount, std::string* error)
{
    const auto fail = [&](const std::string& message)
    {
        if (error != nullptr)
            *error = message;
        return false;
    };

    const fs::path dir = gameDataDir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
        return fail("cannot create game-data directory: " + ec.message());

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr)
        return fail("cannot allocate game-data manifest");
    cJSON_AddNumberToObject(root, "schemaVersion", 1);
    if (!sourceUrl.empty())
        cJSON_AddStringToObject(root, "sourceUrl", sourceUrl.c_str());
    const auto unpackedAt = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
    cJSON_AddNumberToObject(root, "unpackedAtUnix", static_cast<double>(unpackedAt));
    cJSON_AddNumberToObject(root, "sizeBytes", static_cast<double>(sizeBytes));
    cJSON_AddNumberToObject(root, "fileCount", static_cast<double>(fileCount));

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == nullptr)
        return fail("cannot serialize game-data manifest");

    std::ofstream out(dir / "manifest.json", std::ios::binary);
    if (!out)
    {
        cJSON_free(text);
        return fail("cannot write game-data manifest");
    }
    out << text;
    cJSON_free(text);
    return true;
}

bool DeleteGameData(const std::string& gameDataDir, std::string* error)
{
    std::error_code ec;
    fs::remove_all(gameDataDir, ec);
    if (ec)
    {
        if (error != nullptr)
            *error = "cannot delete game-data directory: " + ec.message();
        return false;
    }
    return true;
}

} // namespace Poseidon
