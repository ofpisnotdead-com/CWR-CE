#include <Poseidon/Core/CloudSync/CloudSync.hpp>

#if defined(__APPLE__)

#import <Foundation/Foundation.h>

#include <chrono>
#include <filesystem>
#include <system_error>

// The iCloud ubiquity-container identifier for this app. Matches the
// `com.apple.developer.ubiquity-container-identifiers` entry in
// apps/cwr/Game/PoseidonGame.entitlements, which must be granted via the
// "iCloud" capability on App ID io.mcardle.cwr in the Apple Developer portal
// before ContainerPath() will ever resolve to non-nil.
static NSString* const kCloudSyncContainerID = @"iCloud.io.mcardle.cwr";

namespace
{
NSString* FromStdString(const std::string& s)
{
    return [NSString stringWithUTF8String:s.c_str()];
}

std::string ToStdString(NSString* s)
{
    return s != nil ? std::string(s.UTF8String) : std::string();
}

// nil (not just non-existent) if the device isn't signed into iCloud or this
// container isn't provisioned yet -- NSFileManager returns nil rather than
// throwing.
NSURL* ContainerRootURL()
{
    return [[NSFileManager defaultManager] URLForUbiquityContainerIdentifier:kCloudSyncContainerID];
}

// Recursively lists regular files under `root`, skipping iCloud's
// not-yet-downloaded ".icloud" placeholder markers (their real content isn't
// local yet; NSFileCoordinator materializes them on demand inside
// PullOneFile instead). Returns (path relative to `root`, mtime as seconds
// since epoch).
std::vector<std::pair<std::string, double>> ListDirectory(NSURL* root)
{
    std::vector<std::pair<std::string, double>> entries;
    if (root == nil)
        return entries;

    NSFileManager* fm = [NSFileManager defaultManager];
    NSArray<NSURLResourceKey>* keys = @[ NSURLIsRegularFileKey, NSURLContentModificationDateKey ];
    NSDirectoryEnumerator<NSURL*>* enumerator =
        [fm enumeratorAtURL:root
            includingPropertiesForKeys:keys
                               options:NSDirectoryEnumerationSkipsHiddenFiles
                          errorHandler:nil];

    for (NSURL* fileURL in enumerator)
    {
        NSNumber* isRegular = nil;
        [fileURL getResourceValue:&isRegular forKey:NSURLIsRegularFileKey error:nil];
        if (!isRegular.boolValue)
            continue;
        if ([fileURL.lastPathComponent hasPrefix:@"."] && [fileURL.pathExtension isEqualToString:@"icloud"])
            continue; // not-yet-downloaded placeholder; skip, don't sync a stub

        NSDate* mtime = nil;
        [fileURL getResourceValue:&mtime forKey:NSURLContentModificationDateKey error:nil];

        NSString* relPath = [fileURL.path substringFromIndex:root.path.length + 1];
        entries.emplace_back(ToStdString(relPath), mtime != nil ? mtime.timeIntervalSince1970 : 0.0);
    }
    return entries;
}

bool CopyThroughCoordinator(NSURL* srcURL, NSURL* dstURL, bool coordinateRead, std::string& error)
{
    NSFileManager* fm = [NSFileManager defaultManager];
    [fm createDirectoryAtURL:dstURL.URLByDeletingLastPathComponent
        withIntermediateDirectories:YES
                         attributes:nil
                              error:nil];

    __block BOOL ok = NO;
    __block NSError* copyError = nil;
    NSError* coordError = nil;
    NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];

    // Coordinating the *remote* side of the copy (read when pulling, write
    // when pushing) is what materializes a not-yet-downloaded iCloud file and
    // serializes against the sync daemon -- same pattern already used for
    // document-picker URLs in IosGameDataGateScreen.mm.
    if (coordinateRead)
    {
        [coordinator coordinateReadingItemAtURL:srcURL
                                         options:NSFileCoordinatorReadingWithoutChanges
                                           error:&coordError
                                      byAccessor:^(NSURL* newURL) {
                                          [fm removeItemAtURL:dstURL error:nil];
                                          ok = [fm copyItemAtURL:newURL toURL:dstURL error:&copyError];
                                      }];
    }
    else
    {
        [coordinator coordinateWritingItemAtURL:dstURL
                                         options:NSFileCoordinatorWritingForReplacing
                                           error:&coordError
                                      byAccessor:^(NSURL* newURL) {
                                          [fm removeItemAtURL:newURL error:nil];
                                          ok = [fm copyItemAtURL:srcURL toURL:newURL error:&copyError];
                                      }];
    }

    if (!ok)
    {
        NSError* reported = copyError != nil ? copyError : coordError;
        error = reported != nil ? ToStdString(reported.localizedDescription) : "copy failed";
    }
    return ok;
}
} // namespace

namespace Poseidon
{
namespace CloudSync
{

bool IsAvailable()
{
    @autoreleasepool
    {
        return ContainerRootURL() != nil;
    }
}

std::string ContainerPath()
{
    @autoreleasepool
    {
        NSURL* root = ContainerRootURL();
        return root != nil ? ToStdString(root.path) : std::string();
    }
}

SyncOpsEnv MakeAppleSyncOpsEnv()
{
    SyncOpsEnv env;

    env.listLocal = [](const std::string& dir) -> std::vector<std::pair<std::string, double>>
    {
        std::vector<std::pair<std::string, double>> entries;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec))
            return entries;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 dir, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (ec || !entry.is_regular_file(ec))
                continue;
            const std::string relPath = std::filesystem::relative(entry.path(), dir, ec).string();
            const auto ftime = entry.last_write_time(ec);
            if (ec)
                continue;
            // No std::chrono::clock_cast pre-C++20: the standard workaround
            // (cppreference's file_time_type example) rebases the file-clock
            // time_point onto system_clock via each clock's current instant.
            const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            const double mtime = std::chrono::duration<double>(sctp.time_since_epoch()).count();
            entries.emplace_back(relPath, mtime);
        }
        return entries;
    };

    env.listContainer = [](const std::string& containerRelDir) -> std::vector<std::pair<std::string, double>>
    {
        @autoreleasepool
        {
            NSURL* root = ContainerRootURL();
            if (root == nil)
                return {};
            NSURL* dir = [root URLByAppendingPathComponent:FromStdString(containerRelDir) isDirectory:YES];
            return ListDirectory(dir);
        }
    };

    env.pullFile = [](const std::string& localDir, const std::string& containerRelPath, const std::string& relPath,
                      std::string& error) -> bool
    {
        @autoreleasepool
        {
            NSURL* root = ContainerRootURL();
            if (root == nil)
            {
                error = "iCloud container unavailable";
                return false;
            }
            NSURL* src = [[root URLByAppendingPathComponent:FromStdString(containerRelPath) isDirectory:YES]
                URLByAppendingPathComponent:FromStdString(relPath)];
            NSURL* dst = [NSURL fileURLWithPath:FromStdString(localDir + "/" + relPath)];
            return CopyThroughCoordinator(src, dst, /*coordinateRead=*/true, error);
        }
    };

    env.pushFile = [](const std::string& localDir, const std::string& containerRelPath, const std::string& relPath,
                      std::string& error) -> bool
    {
        @autoreleasepool
        {
            NSURL* root = ContainerRootURL();
            if (root == nil)
            {
                error = "iCloud container unavailable";
                return false;
            }
            NSURL* src = [NSURL fileURLWithPath:FromStdString(localDir + "/" + relPath)];
            NSURL* dst = [[root URLByAppendingPathComponent:FromStdString(containerRelPath) isDirectory:YES]
                URLByAppendingPathComponent:FromStdString(relPath)];
            return CopyThroughCoordinator(src, dst, /*coordinateRead=*/false, error);
        }
    };

    return env;
}

} // namespace CloudSync
} // namespace Poseidon

#endif // defined(__APPLE__)
