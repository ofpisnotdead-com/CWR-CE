#include <Poseidon/Core/CloudSync/CloudSync.hpp>

#if defined(__APPLE__)

#import <Foundation/Foundation.h>

#include <Poseidon/Foundation/Framework/Log.hpp>

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

// Recursively lists regular files under `root`, INCLUDING iCloud's
// not-yet-downloaded ".icloud" placeholder markers -- their metadata (name,
// mtime) is known locally even before the content is; only the bytes are
// missing. A placeholder for "1985.sqc" is named ".1985.sqc.icloud" on disk;
// this maps it back to the real relPath so the diff in RunSyncJobs can match
// it against the same file's entry on the other side. Actual content
// materialization still happens on demand via NSFileCoordinator inside
// pullFile, same as before -- this only fixes *discovery*, i.e. whether a
// remote item too new for this device to have downloaded yet is even seen
// as existing. (Previously placeholders were skipped outright, which meant
// a pull could never even attempt an item this device hadn't materialized
// at all -- see RefreshUbiquitousMetadata below for the other half of that.)
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

        NSString* lastComponent = fileURL.lastPathComponent;
        const bool isPlaceholder =
            [lastComponent hasPrefix:@"."] && [fileURL.pathExtension isEqualToString:@"icloud"];

        NSDate* mtime = nil;
        [fileURL getResourceValue:&mtime forKey:NSURLContentModificationDateKey error:nil];

        NSString* relPath = [fileURL.path substringFromIndex:root.path.length + 1];
        if (isPlaceholder)
        {
            // ".<name>.icloud" -> "<name>": strip the leading dot, then the
            // trailing ".icloud" extension, and swap it in for the mangled
            // last path component.
            NSString* realName = [[lastComponent substringFromIndex:1] stringByDeletingPathExtension];
            NSString* dir = relPath.stringByDeletingLastPathComponent;
            relPath = dir.length > 0 ? [dir stringByAppendingPathComponent:realName] : realName;
        }
        entries.emplace_back(ToStdString(relPath), mtime != nil ? mtime.timeIntervalSince1970 : 0.0);
    }
    return entries;
}

// Actively asks iCloud for this container's real server-side state instead
// of passively trusting whatever this device's OS daemon has already
// decided to mirror locally. ListDirectory() above only ever sees local
// state -- for an item this device has NEVER seen before (no placeholder,
// nothing), that local state can lag the server by an unbounded amount of
// time, since background materialization is scheduled entirely at the OS's
// discretion. Confirmed directly: a file pushed successfully from another
// device sat completely invisible on a second device for 30+ minutes with
// no placeholder ever appearing, while sibling files updated within
// seconds -- new-to-this-device paths appear to get deprioritized
// separately from already-tracked ones. NSMetadataQuery bypasses that by
// querying the actual iCloud metadata index, and startDownloadingUbiquitousItemAtURL:
// requests immediate materialization instead of waiting on the background
// scheduler. Best-effort: swallows failures (offline, query timeout) since
// the existing pull/push logic already tolerates a stale/incomplete remote
// listing -- this only ever helps freshness, never required for correctness.
void RefreshUbiquitousMetadata(double timeoutSeconds)
{
    @autoreleasepool
    {
        NSMetadataQuery* query = [[NSMetadataQuery alloc] init];
        query.searchScopes = @[ NSMetadataQueryUbiquitousDataScope ];
        query.predicate = [NSPredicate predicateWithValue:YES];
        NSOperationQueue* queryQueue = [[NSOperationQueue alloc] init];
        // NSMetadataQuery requires a serial queue -- it delivers notifications
        // in order and isn't safe with concurrent delivery. A freshly-created
        // NSOperationQueue defaults to system-determined concurrency, which
        // trips "API MISUSE: running a NSMetadataQuery with
        // maxConcurrentOperationCount != 1 is not supported" at runtime.
        queryQueue.maxConcurrentOperationCount = 1;
        query.operationQueue = queryQueue;

        dispatch_semaphore_t sema = dispatch_semaphore_create(0);
        __block id observer = nil;
        observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSMetadataQueryDidFinishGatheringNotification
                        object:query
                         queue:query.operationQueue
                    usingBlock:^(NSNotification* note) {
                        dispatch_semaphore_signal(sema);
                    }];

        [query.operationQueue addOperationWithBlock:^{
            [query startQuery];
        }];

        const int64_t timeoutNanos = (int64_t)(timeoutSeconds * NSEC_PER_SEC);
        const bool finishedGathering =
            dispatch_semaphore_wait(sema, dispatch_time(DISPATCH_TIME_NOW, timeoutNanos)) == 0;

        [query disableUpdates];
        NSFileManager* fm = [NSFileManager defaultManager];
        NSUInteger downloadRequested = 0;
        for (NSMetadataItem* item in query.results)
        {
            NSURL* url = [item valueForAttribute:NSMetadataItemURLKey];
            if (url != nil && [fm startDownloadingUbiquitousItemAtURL:url error:nil])
                downloadRequested++;
        }
        LOG_INFO(Core, "CloudSync: RefreshUbiquitousMetadata finishedGathering={} results={} downloadRequested={}",
                 finishedGathering, (unsigned long)query.results.count, (unsigned long)downloadRequested);
        [query stopQuery];
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
    }
}

bool CopyThroughCoordinator(NSURL* srcURL, NSURL* dstURL, bool coordinateRead, std::string& error)
{
    NSFileManager* fm = [NSFileManager defaultManager];
    NSError* mkdirError = nil;
    BOOL mkdirOk = [fm createDirectoryAtURL:dstURL.URLByDeletingLastPathComponent
                 withIntermediateDirectories:YES
                                  attributes:nil
                                       error:&mkdirError];
    if (!mkdirOk && mkdirError != nil)
    {
        LOG_WARN(Core, "CloudSync: createDirectoryAtURL failed for {} ({}): {}", ToStdString(dstURL.path),
                 coordinateRead ? "pull" : "push", ToStdString(mkdirError.localizedDescription));
    }

    // Land the copy in a same-directory temp file, then atomically replace
    // dstURL only once the full copy is on disk. A process kill (or any other
    // interruption) mid-copy must never leave a truncated file sitting at
    // dstURL: on push that truncated file would get a fresh mtime and look
    // "newer" than the good local copy, so a later pull would clobber it; on
    // pull it would corrupt the (supposedly authoritative) local file
    // directly. replaceItemAtURL: below does the actual swap atomically.
    NSString* tmpName = [NSString stringWithFormat:@".%@.cloudsync-tmp-%08x", dstURL.lastPathComponent, arc4random()];
    NSURL* tmpURL = [dstURL.URLByDeletingLastPathComponent URLByAppendingPathComponent:tmpName];

    __block BOOL ok = NO;
    __block NSError* opError = nil;
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
                                          [fm removeItemAtURL:tmpURL error:nil];
                                          ok = [fm copyItemAtURL:newURL toURL:tmpURL error:&opError];
                                      }];
        if (ok)
        {
            ok = [fm replaceItemAtURL:dstURL
                         withItemAtURL:tmpURL
                        backupItemName:nil
                               options:NSFileManagerItemReplacementUsingNewMetadataOnly
                      resultingItemURL:nil
                                 error:&opError];
        }
    }
    else
    {
        [fm removeItemAtURL:tmpURL error:nil];
        ok = [fm copyItemAtURL:srcURL toURL:tmpURL error:&opError];
        if (ok)
        {
            [coordinator coordinateWritingItemAtURL:dstURL
                                             options:NSFileCoordinatorWritingForReplacing
                                               error:&coordError
                                          byAccessor:^(NSURL* newURL) {
                                              ok = [fm replaceItemAtURL:newURL
                                                           withItemAtURL:tmpURL
                                                          backupItemName:nil
                                                                 options:NSFileManagerItemReplacementUsingNewMetadataOnly
                                                        resultingItemURL:nil
                                                                   error:&opError];
                                          }];
        }
    }
    [fm removeItemAtURL:tmpURL error:nil]; // best-effort: no-op on success, cleans up a stray temp on failure

    if (!ok)
    {
        NSError* reported = opError != nil ? opError : coordError;
        error = reported != nil ? ToStdString(reported.localizedDescription) : "copy failed";
        LOG_ERROR(Core, "CloudSync: {} FAILED {} -> {}: opError=[{}] coordError=[{}]", coordinateRead ? "pull" : "push",
                  ToStdString(srcURL.path), ToStdString(dstURL.path),
                  opError != nil ? ToStdString(opError.localizedDescription) : "none",
                  coordError != nil ? ToStdString(coordError.localizedDescription) : "none");
    }
    else
    {
        LOG_INFO(Core, "CloudSync: {} ok {} -> {}", coordinateRead ? "pull" : "push", ToStdString(srcURL.path),
                 ToStdString(dstURL.path));
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
        // A plain range-for over recursive_directory_iterator calls the throwing
        // operator++() during traversal -- constructing with an `ec` out-param only
        // suppresses errors at construction, not on each increment.
        // skip_permission_denied covers permission errors specifically, but any
        // other error mid-walk would throw std::filesystem::filesystem_error here,
        // uncaught -- crashing the whole sync (and possibly the app, depending on
        // caller) instead of just failing this one directory's listing.
        try
        {
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
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            LOG_ERROR(Core, "CloudSync: listLocal({}) threw mid-walk after {} entries: {}", dir, entries.size(),
                      e.what());
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

    // 3 second cap: this runs synchronously on the sync worker thread before
    // every push/pull pass, and must never turn an offline device into an
    // indefinite hang. Best-effort only -- see RefreshUbiquitousMetadata's
    // own comment for why a timeout here doesn't compromise correctness.
    env.refreshRemote = []() { RefreshUbiquitousMetadata(3.0); };

    return env;
}

} // namespace CloudSync
} // namespace Poseidon

#endif // defined(__APPLE__)
