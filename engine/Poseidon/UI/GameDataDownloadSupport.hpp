#pragma once

#include <cstdint>
#include <string>
#include <system_error>

#include <Poseidon/Core/DownloadWorker.hpp>    // DownloadTask, DownloadFileFn
#include <Poseidon/Core/GameDataArchive.hpp>   // GameDataArchive::Unpack
#include <Poseidon/Core/GameDataInstall.hpp>   // WriteGameDataManifest

#include <filesystem>

namespace Poseidon
{

// A DownloadTask that fetches a zip archive to <gameDataDir>.zip.download and
// unpacks it into gameDataDir via GameDataArchive::Unpack, writing the
// manifest only on full success (WriteGameDataManifest) -- mirrors
// MakeModDownloadTask's shape (UI/ModDownloadSupport.hpp) for the game-data
// package instead of a mod. The transport is unrelated to mods vs game data,
// so the same Poseidon::MakeModDownloadTransport (ModDownloadSupport.hpp) is
// reused unchanged for the actual fetch.
inline DownloadTask MakeGameDataDownloadTask(const std::string& url, const std::string& gameDataDir)
{
    DownloadTask t;
    t.label = "Game Data";
    t.url = url;
    t.destPath = gameDataDir + ".zip.download";
    t.postStep = [gameDataDir, url](const DownloadTask& task, std::string& error) -> bool
    {
        int64_t entryCount = 0;
        if (!GameDataArchive::Unpack(
                task.destPath.c_str(), gameDataDir.c_str(),
                [&entryCount](int64_t /*done*/, int64_t total) { entryCount = total; }, &error))
        {
            return false;
        }

        std::error_code ec;
        const auto archiveSize = std::filesystem::file_size(task.destPath, ec);
        const int64_t sizeBytes = ec ? 0 : static_cast<int64_t>(archiveSize);
        std::filesystem::remove(task.destPath, ec);

        return WriteGameDataManifest(gameDataDir, url, sizeBytes, entryCount, &error);
    };
    return t;
}

} // namespace Poseidon
