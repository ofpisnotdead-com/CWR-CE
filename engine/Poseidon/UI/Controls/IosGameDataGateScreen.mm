#include <Poseidon/UI/Controls/IosGameDataGateScreen.hpp>

#if defined(POSEIDON_TARGET_IOS)

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <Poseidon/Core/GameDataInstall.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/UI/GameDataDownloadSupport.hpp> // MakeGameDataDownloadTask
#include <Poseidon/UI/ModDownloadSupport.hpp>       // MakeModDownloadTransport

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace
{
NSString* FromStdString(const std::string& s)
{
    return [NSString stringWithUTF8String:s.c_str()];
}
} // namespace

@interface PoseidonGameDataGateViewController : UIViewController <UIDocumentPickerDelegate>
@property(nonatomic, copy) void (^onDataReady)(void);
@end

@implementation PoseidonGameDataGateViewController
{
    UILabel* _titleLabel;
    UILabel* _messageLabel;
    UITextField* _urlField;
    UIButton* _downloadButton;
    UIButton* _importButton;
    UILabel* _statusLabel;
    UIProgressView* _progressView;
    NSTimer* _progressTimer;
    std::unique_ptr<Poseidon::DownloadWorker> _worker;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor blackColor];

    const CGRect bounds = self.view.bounds;
    const CGFloat columnWidth = MIN(bounds.size.width - 80.0, 520.0);
    const CGFloat columnX = (bounds.size.width - columnWidth) / 2.0;
    CGFloat y = bounds.size.height * 0.18;

    _titleLabel = [[UILabel alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 32)];
    _titleLabel.textColor = [UIColor whiteColor];
    _titleLabel.font = [UIFont boldSystemFontOfSize:22];
    _titleLabel.text = @"Game Data Required";
    [self.view addSubview:_titleLabel];
    y += 44;

    _messageLabel = [[UILabel alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 60)];
    _messageLabel.textColor = [UIColor lightGrayColor];
    _messageLabel.font = [UIFont systemFontOfSize:15];
    _messageLabel.numberOfLines = 0;
    _messageLabel.text = Poseidon::DetectGameDataStatus(Poseidon::GameDataDir()) == Poseidon::GameDataStatus::Partial
                             ? @"A previous import didn't finish. Paste a download link, or import an archive "
                               @"you already have."
                             : @"This app needs its game-data package before it can start. Paste a download "
                               @"link, or import an archive you already have.";
    [self.view addSubview:_messageLabel];
    y += 74;

    _urlField = [[UITextField alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 40)];
    _urlField.borderStyle = UITextBorderStyleRoundedRect;
    _urlField.placeholder = @"https://example.com/gamedata.zip";
    _urlField.keyboardType = UIKeyboardTypeURL;
    _urlField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _urlField.autocorrectionType = UITextAutocorrectionTypeNo;
    [self.view addSubview:_urlField];
    y += 52;

    _downloadButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _downloadButton.frame = CGRectMake(columnX, y, columnWidth, 44);
    [_downloadButton setTitle:@"Download" forState:UIControlStateNormal];
    _downloadButton.backgroundColor = [UIColor colorWithWhite:0.2 alpha:1.0];
    [_downloadButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    _downloadButton.layer.cornerRadius = 8;
    [_downloadButton addTarget:self
                        action:@selector(downloadTapped)
              forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:_downloadButton];
    y += 56;

    _importButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _importButton.frame = CGRectMake(columnX, y, columnWidth, 44);
    [_importButton setTitle:@"Import from Files…" forState:UIControlStateNormal];
    _importButton.backgroundColor = [UIColor colorWithWhite:0.2 alpha:1.0];
    [_importButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    _importButton.layer.cornerRadius = 8;
    [_importButton addTarget:self action:@selector(importTapped) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:_importButton];
    y += 56;

    _progressView = [[UIProgressView alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 4)];
    _progressView.hidden = YES;
    [self.view addSubview:_progressView];
    y += 20;

    _statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 60)];
    _statusLabel.textColor = [UIColor systemOrangeColor];
    _statusLabel.font = [UIFont systemFontOfSize:13];
    _statusLabel.numberOfLines = 0;
    [self.view addSubview:_statusLabel];
}

- (void)setButtonsEnabled:(BOOL)enabled
{
    _downloadButton.enabled = enabled;
    _importButton.enabled = enabled;
    _urlField.enabled = enabled;
}

- (void)downloadTapped
{
    NSString* url = _urlField.text;
    if (url == nil || url.length == 0)
    {
        _statusLabel.text = @"Enter a URL first.";
        return;
    }
    [self setButtonsEnabled:NO];
    _progressView.hidden = NO;
    _progressView.progress = 0.0;
    _statusLabel.textColor = [UIColor lightGrayColor];
    _statusLabel.text = @"Starting download…";

    const std::string gameDataDir = Poseidon::GameDataDir();
    Poseidon::DownloadTask task = Poseidon::MakeGameDataDownloadTask(url.UTF8String, gameDataDir);
    _worker = std::make_unique<Poseidon::DownloadWorker>(Poseidon::MakeModDownloadTransport(""));
    _worker->Start({task});

    _progressTimer = [NSTimer scheduledTimerWithTimeInterval:0.1
                                                       target:self
                                                     selector:@selector(pollDownloadProgress)
                                                     userInfo:nil
                                                      repeats:YES];
}

- (void)pollDownloadProgress
{
    if (!_worker)
    {
        return;
    }
    const Poseidon::DownloadSnapshot snap = _worker->Poll();
    _progressView.progress = snap.overallFraction;
    if (!snap.done)
    {
        // overallFraction is 0 for the whole transfer when the server's size
        // is unknown upfront (this task never sets expectedBytes -- the mod
        // downloader that shares this code always has a catalog-known size,
        // we don't). A stalled 0% bar reads as "frozen", so always show live
        // bytes-received/speed text too -- that doesn't depend on knowing the
        // total, and is real evidence the transfer is actually moving.
        std::string text = Poseidon::DownloadProgress::FormatBytes(snap.overallReceived) + " downloaded";
        if (snap.speedBytesPerSec > 0.0)
        {
            text += " (" + Poseidon::DownloadProgress::FormatSpeed(snap.speedBytesPerSec) + ")";
        }
        _statusLabel.text = FromStdString(text);
        return;
    }

    [_progressTimer invalidate];
    _progressTimer = nil;
    _worker.reset();

    if (snap.failed)
    {
        _statusLabel.textColor = [UIColor systemRedColor];
        _statusLabel.text = FromStdString(snap.error.empty() ? "Download failed." : snap.error);
        [self setButtonsEnabled:YES];
        return;
    }

    [self finishIfReady];
}

- (void)importTapped
{
    UIDocumentPickerViewController* picker;
    if (@available(iOS 14.0, *))
    {
        picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeZIP ] asCopy:YES];
    }
    else
    {
        picker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[ @"public.zip-archive" ]
                                                                          inMode:UIDocumentPickerModeImport];
    }
    picker.delegate = self;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    if (urls.count == 0)
    {
        return;
    }
    NSURL* pickedUrl = urls.firstObject;
    [self setButtonsEnabled:NO];
    _statusLabel.textColor = [UIColor lightGrayColor];
    _statusLabel.text = @"Importing…";

    const std::string gameDataDir = Poseidon::GameDataDir();
    // Empty sourceUrl: MakeGameDataDownloadTask's postStep records it in the
    // manifest only when non-empty (WriteGameDataManifest), and an
    // import-from-Files archive has no source URL to record.
    Poseidon::DownloadTask task = Poseidon::MakeGameDataDownloadTask("", gameDataDir);
    NSURL* destUrl = [NSURL fileURLWithPath:FromStdString(task.destPath)];

    // Even in asCopy:YES mode, "On My iPhone"/iCloud locations are backed by
    // a file provider, not a plain path -- a raw NSFileManager path-string
    // copy fails ("you don't have permission to access 'System'"). Reading
    // through NSFileCoordinator is the Apple-documented way to safely access
    // a document-picker URL regardless of what's providing it.
    const BOOL accessing = [pickedUrl startAccessingSecurityScopedResource];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        __block NSError* copyError = nil;
        __block BOOL copied = NO;
        NSError* coordError = nil;
        NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
        [coordinator coordinateReadingItemAtURL:pickedUrl
                                         options:NSFileCoordinatorReadingWithoutChanges
                                           error:&coordError
                                      byAccessor:^(NSURL* newURL) {
                                          NSFileManager* fm = [NSFileManager defaultManager];
                                          [fm removeItemAtURL:destUrl error:nil]; // stale leftover, if any
                                          copied = [fm copyItemAtURL:newURL toURL:destUrl error:&copyError];
                                      }];

        if (accessing)
        {
            [pickedUrl stopAccessingSecurityScopedResource];
        }

        std::string error;
        const bool ok = copied && task.postStep(task, error);
        if (!ok && error.empty())
        {
            NSError* reportError = copyError != nil ? copyError : coordError;
            if (reportError != nil)
            {
                error = reportError.localizedDescription.UTF8String;
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            if (!ok)
            {
                self->_statusLabel.textColor = [UIColor systemRedColor];
                self->_statusLabel.text = FromStdString(error.empty() ? "Import failed." : error);
                [self setButtonsEnabled:YES];
                return;
            }
            [self finishIfReady];
        });
    });
}

- (void)finishIfReady
{
    if (Poseidon::DetectGameDataStatus(Poseidon::GameDataDir()) == Poseidon::GameDataStatus::Ready)
    {
        _statusLabel.textColor = [UIColor systemGreenColor];
        _statusLabel.text = @"Done — starting…";
        if (self.onDataReady)
        {
            self.onDataReady();
        }
        return;
    }
    // Unpack succeeded but a required path is still missing -- treat as a
    // bad archive rather than silently looping.
    _statusLabel.textColor = [UIColor systemRedColor];
    _statusLabel.text = @"That archive doesn't contain the expected game data.";
    [self setButtonsEnabled:YES];
}

@end

namespace Poseidon
{
namespace
{
UIWindowScene* ActiveWindowScene()
{
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        if ([scene isKindOfClass:UIWindowScene.class])
        {
            return (UIWindowScene*)scene;
        }
    }
    return nil;
}

// See IosBootModalSpike.mm for why this pump exists and why the settle delay
// before presenting anything is required (a just-created window's first
// layout/display pass hasn't happened yet in the same run-loop turn as
// makeKeyAndVisible).
void PumpRunLoopBriefly(double seconds)
{
    const NSTimeInterval deadline = [NSDate timeIntervalSinceReferenceDate] + seconds;
    while ([NSDate timeIntervalSinceReferenceDate] < deadline)
    {
        @autoreleasepool
        {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        }
    }
}
} // namespace

void RunIosGameDataGate()
{
    @autoreleasepool
    {
        // This runs before app.Run() -- well before GameBase.cpp's normal
        // boot-sequence call to the same Initialize(), which needs a parsed
        // AppConfig that doesn't exist yet at this point. Initialize() is
        // idempotent (no-op if already initialized), so calling it early with
        // the same fixed codename/cfgBase/productName GameBase.cpp always
        // uses is safe -- without this, GameDataDir() resolves UserContentDir()
        // as empty, i.e. an unwritable path at the filesystem root.
        Foundation::GamePaths::Instance().Initialize("CWR", "ColdWarAssault", "Cold War Assault");

        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        if ([defaults boolForKey:@"CWRResetGameData"])
        {
            DeleteGameData(GameDataDir());
            [defaults setBool:NO forKey:@"CWRResetGameData"];
            [defaults synchronize];
        }

        if (DetectGameDataStatus(GameDataDir()) == GameDataStatus::Ready)
        {
            return;
        }

        UIWindowScene* scene = ActiveWindowScene();
        UIWindow* window = scene != nil ? [[UIWindow alloc] initWithWindowScene:scene]
                                         : [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
        window.windowLevel = UIWindowLevelNormal + 2.0;
        PoseidonGameDataGateViewController* controller = [[PoseidonGameDataGateViewController alloc] init];
        window.rootViewController = controller;
        [window makeKeyAndVisible];

        PumpRunLoopBriefly(0.2);

        __block bool ready = false;
        controller.onDataReady = ^{
            ready = true;
        };

        while (!ready)
        {
            @autoreleasepool
            {
                [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                          beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            }
        }

        // Let the "Done — starting…" state render for a beat before tearing
        // the window down into the real boot.
        PumpRunLoopBriefly(0.4);
        window.hidden = YES;
        window = nil;
    }
}

} // namespace Poseidon

#endif
