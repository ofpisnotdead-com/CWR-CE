#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <Poseidon/Core/DownloadWorker.hpp> // DownloadTask, DownloadFileFn
#include <Poseidon/Core/ModArchive.hpp>     // ModArchive::Unpack
#include <Poseidon/Core/ModInstall.hpp>     // ModInstallDir
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Network/MasterServerServiceClient.hpp> // DownloadMasterServerServiceFile

namespace Poseidon
{

// The local + workshop mod roots, CLI-overridable, shared so every screen agrees on
// where mods live (DisplayMods builds the same paths from its own statics).
inline std::string ModsLocalRoot()
{
    const RString& cli = AppConfig::Instance().GetModsDir();
    if (cli.GetLength() > 0)
        return (const char*)cli;
    return Foundation::GamePaths::Instance().ModsDir();
}

inline std::string ModsWorkshopRoot()
{
    const RString& cli = AppConfig::Instance().GetWorkshopDir();
    if (cli.GetLength() > 0)
        return (const char*)cli;
    return Foundation::GamePaths::Instance().WorkshopDir();
}

// libcurl download bridged to the agnostic DownloadFileFn. The C progress callback
// can't carry captures, so onBytes/cancelled are reached through a void* context.
inline DownloadFileFn MakeModDownloadTransport(std::string proxy)
{
    return [proxy](const DownloadTask& task, const std::function<void(int64_t, int64_t)>& onBytes,
                   const std::function<bool()>& cancelled, std::string& error) -> bool
    {
        struct Ctx
        {
            const std::function<void(int64_t, int64_t)>* cb;
            const std::function<bool()>* cancelled;
        } ctx{&onBytes, &cancelled};
        const bool ok = DownloadMasterServerServiceFile(
            task.url.c_str(), proxy.empty() ? nullptr : proxy.c_str(), task.destPath.c_str(), &ctx,
            [](void* instance, int64_t received, int64_t total)
            {
                auto* c = static_cast<Ctx*>(instance);
                if (c != nullptr && c->cb != nullptr && *c->cb)
                    (*c->cb)(received, total);
            },
            [](void* instance) -> bool
            {
                auto* c = static_cast<Ctx*>(instance);
                return c != nullptr && c->cancelled != nullptr && *c->cancelled && (*c->cancelled)();
            });
        if (!ok)
            error = "download failed";
        return ok;
    };
}

// A DownloadTask that fetches the zstd-wrapped mod artifact to <root>/<folderName>.pbo.zst and
// unpacks it into <root>/<folderName>. ModArchive::Unpack streams the zstd decode.
// `modId` is the catalog id; `folderName` is the physical install folder.
inline DownloadTask MakeModDownloadTask(const std::string& modId, const std::string& name, const std::string& url,
                                        int64_t sizeBytes, const std::string& root,
                                        std::vector<StagedModInstall>& installs, const std::string& folderName = {},
                                        const std::string& version = {}, int64_t packageRevision = 1,
                                        const std::string& sha256 = {})
{
    const std::string destDir = ModInstallDir(root, modId, folderName);
    StagedModInstall install = MakeStagedModInstall(destDir, modId);
    const std::string stagingDir = install.stagingDir;
    DownloadTask t;
    t.label = name.empty() ? modId : name;
    t.url = url;
    t.destPath = stagingDir + ".pbo.zst";
    t.expectedBytes = sizeBytes;
    t.postStep = [stagingDir, modId, name, version, folderName, url, sizeBytes, packageRevision,
                  sha256](const DownloadTask& task, std::string& error) -> bool
    {
        if (!VerifyModArtifact(task.destPath, sizeBytes, sha256, &error))
            return false;
        std::error_code ec;
        std::filesystem::remove_all(stagingDir, ec);
        if (!ModArchive::Unpack(task.destPath.c_str(), stagingDir.c_str(), &error))
            return false;
        return WriteInstalledModManifest(stagingDir, modId, name, version, folderName, url, sizeBytes, &error,
                                         packageRevision, sha256);
    };
    installs.push_back(std::move(install));
    return t;
}

} // namespace Poseidon
