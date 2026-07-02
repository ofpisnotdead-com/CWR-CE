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

@interface PoseidonGameDataGateViewController : UIViewController <UIDocumentPickerDelegate, UITextFieldDelegate>
@property(nonatomic, copy) void (^onDataReady)(void);
@end

@implementation PoseidonGameDataGateViewController
{
    UIScrollView* _scrollView;
    UILabel* _titleLabel;
    UILabel* _messageLabel;
    UITextField* _urlField;
    UIButton* _downloadButton;
    UIButton* _importButton;
    UILabel* _statusLabel;
    UIProgressView* _progressView;
    UIButton* _cancelButton;
    UIButton* _advancedToggleButton;
    UIView* _advancedPanel;
    UILabel* _pathLabel;
    UILabel* _freeSpaceLabel;
    UIButton* _clearDataButton;
    BOOL _advancedExpanded;
    NSTimer* _progressTimer;
    NSTimer* _importElapsedTimer;
    NSDate* _importStartDate;
    NSString* _importPhaseText;
    std::unique_ptr<Poseidon::DownloadWorker> _worker;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor blackColor];

    const CGRect bounds = self.view.bounds;
    const CGFloat columnWidth = MIN(bounds.size.width - 80.0, 520.0);
    const CGFloat columnX = (bounds.size.width - columnWidth) / 2.0;

    // A scroll view (not fixed frames straight on self.view) because this has
    // to work on the smallest landscape height in the device matrix -- e.g.
    // ~440pt on this phone -- where the full column (title through the
    // Advanced panel) doesn't fit; content below the fold must be reachable
    // by scrolling rather than silently clipped off the bottom edge.
    _scrollView = [[UIScrollView alloc] initWithFrame:bounds];
    _scrollView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _scrollView.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive;
    [self.view addSubview:_scrollView];

    UITapGestureRecognizer* tap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(backgroundTapped)];
    tap.cancelsTouchesInView = NO; // let button/field taps underneath still register
    [_scrollView addGestureRecognizer:tap];

    CGFloat y = 24.0;

    _titleLabel = [[UILabel alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 32)];
    _titleLabel.textColor = [UIColor whiteColor];
    _titleLabel.font = [UIFont boldSystemFontOfSize:22];
    _titleLabel.text = @"Game Data Required";
    [_scrollView addSubview:_titleLabel];
    y += 40;

    _messageLabel = [[UILabel alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 44)];
    _messageLabel.textColor = [UIColor lightGrayColor];
    _messageLabel.font = [UIFont systemFontOfSize:14];
    _messageLabel.numberOfLines = 0;
    _messageLabel.text = Poseidon::DetectGameDataStatus(Poseidon::GameDataDir()) == Poseidon::GameDataStatus::Partial
                             ? @"A previous import didn't finish. Paste a download link, or import an archive "
                               @"you already have."
                             : @"This app needs its game-data package before it can start. Paste a download "
                               @"link, or import an archive you already have.";
    [_scrollView addSubview:_messageLabel];
    y += 54;

    _urlField = [[UITextField alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 38)];
    _urlField.borderStyle = UITextBorderStyleRoundedRect;
    _urlField.placeholder = @"https://example.com/gamedata.zip";
    _urlField.keyboardType = UIKeyboardTypeURL;
    _urlField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _urlField.autocorrectionType = UITextAutocorrectionTypeNo;
    _urlField.returnKeyType = UIReturnKeyDone;
    _urlField.delegate = self;
    [_scrollView addSubview:_urlField];
    y += 46;

    _downloadButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _downloadButton.frame = CGRectMake(columnX, y, columnWidth, 40);
    [_downloadButton setTitle:@"Download" forState:UIControlStateNormal];
    _downloadButton.backgroundColor = [UIColor colorWithWhite:0.2 alpha:1.0];
    [_downloadButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    _downloadButton.layer.cornerRadius = 8;
    [_downloadButton addTarget:self
                        action:@selector(downloadTapped)
              forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_downloadButton];
    y += 48;

    _importButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _importButton.frame = CGRectMake(columnX, y, columnWidth, 40);
    [_importButton setTitle:@"Import from Files…" forState:UIControlStateNormal];
    _importButton.backgroundColor = [UIColor colorWithWhite:0.2 alpha:1.0];
    [_importButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    _importButton.layer.cornerRadius = 8;
    [_importButton addTarget:self action:@selector(importTapped) forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_importButton];
    y += 48;

    _progressView = [[UIProgressView alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 4)];
    _progressView.hidden = YES;
    [_scrollView addSubview:_progressView];
    y += 16;

    _statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 36)];
    _statusLabel.textColor = [UIColor systemOrangeColor];
    _statusLabel.font = [UIFont systemFontOfSize:13];
    _statusLabel.numberOfLines = 0;
    [_scrollView addSubview:_statusLabel];
    y += 40;

    _cancelButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _cancelButton.frame = CGRectMake(columnX, y, columnWidth, 32);
    [_cancelButton setTitle:@"Cancel" forState:UIControlStateNormal];
    [_cancelButton setTitleColor:[UIColor systemRedColor] forState:UIControlStateNormal];
    [_cancelButton addTarget:self action:@selector(cancelTapped) forControlEvents:UIControlEventTouchUpInside];
    _cancelButton.hidden = YES;
    [_scrollView addSubview:_cancelButton];
    y += 36;

    _advancedToggleButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _advancedToggleButton.frame = CGRectMake(columnX, y, columnWidth, 30);
    _advancedToggleButton.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    [_advancedToggleButton setTitle:@"Advanced ▾" forState:UIControlStateNormal];
    [_advancedToggleButton setTitleColor:[UIColor lightGrayColor] forState:UIControlStateNormal];
    _advancedToggleButton.titleLabel.font = [UIFont systemFontOfSize:13];
    [_advancedToggleButton addTarget:self
                              action:@selector(advancedToggleTapped)
                    forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_advancedToggleButton];
    y += 34;

    _advancedPanel = [[UIView alloc] initWithFrame:CGRectMake(columnX, y, columnWidth, 124)];
    _advancedPanel.hidden = YES;
    [_scrollView addSubview:_advancedPanel];

    _pathLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, columnWidth, 34)];
    _pathLabel.textColor = [UIColor lightGrayColor];
    _pathLabel.font = [UIFont systemFontOfSize:11];
    _pathLabel.numberOfLines = 2;
    _pathLabel.text = FromStdString("Location: " + Poseidon::GameDataDir());
    [_advancedPanel addSubview:_pathLabel];

    _freeSpaceLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 38, columnWidth, 18)];
    _freeSpaceLabel.textColor = [UIColor lightGrayColor];
    _freeSpaceLabel.font = [UIFont systemFontOfSize:11];
    [_advancedPanel addSubview:_freeSpaceLabel];
    [self updateFreeSpaceLabel];

    _clearDataButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _clearDataButton.frame = CGRectMake(0, 64, columnWidth, 36);
    [_clearDataButton setTitle:@"Clear Data" forState:UIControlStateNormal];
    [_clearDataButton setTitleColor:[UIColor systemRedColor] forState:UIControlStateNormal];
    _clearDataButton.layer.borderColor = [UIColor systemRedColor].CGColor;
    _clearDataButton.layer.borderWidth = 1;
    _clearDataButton.layer.cornerRadius = 8;
    [_clearDataButton addTarget:self action:@selector(clearDataTapped) forControlEvents:UIControlEventTouchUpInside];
    [_advancedPanel addSubview:_clearDataButton];
    y += 124;

    _scrollView.contentSize = CGSizeMake(bounds.size.width, y + 24.0);

    [[NSNotificationCenter defaultCenter] addObserver:self
                                              selector:@selector(keyboardWillChangeFrame:)
                                                  name:UIKeyboardWillChangeFrameNotification
                                                object:nil];
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)backgroundTapped
{
    [_urlField resignFirstResponder];
}

- (BOOL)textFieldShouldReturn:(UITextField*)textField
{
    [textField resignFirstResponder];
    return YES;
}

- (void)keyboardWillChangeFrame:(NSNotification*)notification
{
    // Keep whatever's below the URL field (Download/Import/Advanced/Clear
    // Data) reachable by scrolling once the keyboard covers part of the
    // screen, rather than leaving those buttons permanently hidden behind it
    // with no way to dismiss and get back to them.
    const CGRect screenFrame = [(NSValue*)notification.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    const CGRect localFrame = [self.view convertRect:screenFrame fromView:nil];
    const CGFloat overlap = MAX(0.0, self.view.bounds.size.height - localFrame.origin.y);
    _scrollView.contentInset = UIEdgeInsetsMake(0, 0, overlap, 0);
    _scrollView.scrollIndicatorInsets = _scrollView.contentInset;
}

- (void)updateFreeSpaceLabel
{
    NSError* error = nil;
    NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfFileSystemForPath:NSHomeDirectory() error:&error];
    NSNumber* freeBytes = attrs[NSFileSystemFreeSize];
    if (freeBytes != nil)
    {
        _freeSpaceLabel.text =
            FromStdString("Free space: " + Poseidon::DownloadProgress::FormatBytes(freeBytes.longLongValue));
    }
}

- (void)setButtonsEnabled:(BOOL)enabled
{
    _downloadButton.enabled = enabled;
    _importButton.enabled = enabled;
    _urlField.enabled = enabled;
}

- (void)advancedToggleTapped
{
    _advancedExpanded = !_advancedExpanded;
    [_advancedToggleButton setTitle:(_advancedExpanded ? @"Advanced ▴" : @"Advanced ▾") forState:UIControlStateNormal];
    if (_advancedExpanded)
    {
        [self updateFreeSpaceLabel]; // refresh in case an earlier attempt changed it
    }
    _advancedPanel.hidden = !_advancedExpanded;
}

- (void)clearDataTapped
{
    std::string error;
    Poseidon::DeleteGameData(Poseidon::GameDataDir(), &error);
    _statusLabel.textColor = [UIColor lightGrayColor];
    _statusLabel.text = @"Game data cleared.";
    _messageLabel.text = @"This app needs its game-data package before it can start. Paste a download "
                          @"link, or import an archive you already have.";
    [self updateFreeSpaceLabel];
}

- (void)cancelTapped
{
    if (_worker)
    {
        _worker->Cancel();
    }
}

- (void)tickImportElapsed
{
    const NSTimeInterval elapsed = -[_importStartDate timeIntervalSinceNow];
    _statusLabel.text = [NSString stringWithFormat:@"%@ (%lds)", _importPhaseText ?: @"Importing…", (long)elapsed];
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
    _cancelButton.hidden = NO;
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

    // Per DownloadWorker.cpp/RunDownloadJobs: a cancelled job is left neither
    // done nor failed, so it must be checked as its own terminal state or
    // polling would continue showing stale progress text forever.
    if (snap.cancelled)
    {
        [_progressTimer invalidate];
        _progressTimer = nil;
        _worker.reset();
        _cancelButton.hidden = YES;
        _statusLabel.textColor = [UIColor lightGrayColor];
        _statusLabel.text = @"Cancelled.";
        [self setButtonsEnabled:YES];
        return;
    }

    // IsDone() only ever reflects success -- DownloadProgress::Finish() (the
    // only thing that sets it) is never called on the failure path, so a
    // failed job is *also* left with done=false forever, same as cancelled.
    // Must be checked ahead of the "!done -> still in progress" branch below,
    // or a failure just looks like a permanently stuck transfer (0B, stale
    // text, buttons and Cancel never freed) instead of ever surfacing.
    if (snap.failed)
    {
        [_progressTimer invalidate];
        _progressTimer = nil;
        _worker.reset();
        _cancelButton.hidden = YES;
        _statusLabel.textColor = [UIColor systemRedColor];
        _statusLabel.text = FromStdString(snap.error.empty() ? "Download failed." : snap.error);
        [self setButtonsEnabled:YES];
        return;
    }

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
    _cancelButton.hidden = YES;
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

    // GameDataArchive::Unpack's progress callback isn't threaded through
    // MakeGameDataDownloadTask's opaque postStep, so (unlike the download
    // path) there's no byte-level signal available here -- a ticking elapsed
    // time is enough to show a large copy+unpack (500+MB) hasn't frozen.
    _importPhaseText = @"Importing…";
    _importStartDate = [NSDate date];
    _importElapsedTimer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                            target:self
                                                          selector:@selector(tickImportElapsed)
                                                          userInfo:nil
                                                           repeats:YES];

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

        if (copied)
        {
            dispatch_async(dispatch_get_main_queue(), ^{
                self->_importPhaseText = @"Unpacking…";
            });
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
            [self->_importElapsedTimer invalidate];
            self->_importElapsedTimer = nil;
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
