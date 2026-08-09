// Download dialog (RscDisplayModDownload). Two things are proven here:
//
//  Part 1: ticking an Available Workshop mod and choosing Load Mods opens the
//  download dialog before re-mounting.
//
//  Part 2: triOpenModDownload injects synthetic
//  tasks + a FAKE in-process transport (no network/disk) so the two bars, the
//  "N / N addons" overall line and the Complete path are exercised offline and
//  deterministically. The agnostic DownloadProgress/Worker/View components are
//  unit-tested; this proves they render through the real notebook controls.

triSetLanguage "Italian"
triSimUntil { triGameMode == 2 }
triAssertEq [(triDisplay), 0]

triClick 119                 // IDC_MAIN_MODS
triAssertEq [(triDisplay), 72]          // IDD_MODS
triWaitFrames 10

// The Installed tab keeps missing catalog entries out of the normal active list.
triSeedWorkshopMods 2
triWaitFrames 5
triClick 7003                // Workshop tab
_workshop = triModsVisibleCount
if (_workshop != 2) exitWith { format ["FAIL:rows=%1 (want 2 workshop)", _workshop] }

// Name-sorted rows: 0=Workshop Mod 1 (Available).
triModsRowClick [0, 0.03]    // tick Workshop Mod 1 (Missing + has download URL)
triWaitFrames 5
triClick 115                 // IDC_MODS_APPLY
triAssertEq [(triDisplay), 74]        // IDD_MODS_DOWNLOAD — gated, not re-mounted
triAssertIncludes [(triVisibleTexts), "Scarica"]      // the Download button
triScreenshot "01_apply_gate"
triClick 2                   // IDC_CANCEL — dismiss without downloading
triAssertEq [(triDisplay), 72]        // back on the MODS screen

triOpenModDownload 3
triAssertEq [(triDisplay), 74]
triScreenshot "02_download_prompt"
triClick 125                 // IDC_MODS_DOWNLOAD_GO — start the (fake) download
triAssertIncludes [(triVisibleTexts), "Estrazione."] // unpacking remains visibly active after download
triScreenshot "03_unpacking"
triAssertIncludes [(triVisibleTexts), "Completato"]   // localized status line; auto-retries until the worker finishes + is polled
triAssertIncludes [(triVisibleTexts), "3 / 3 addons   100%"]
triAssertIncludes [(triVisibleTexts), "Continua"]     // the action button relabels on success
triAssertIncludes [(triVisibleTexts), "Indietro"]     // completed downloads can return without activation
triScreenshot "04_download_complete"
triClick 2                   // Back keeps completed downloads and returns to MODS
triAssertEq [(triDisplay), 72]        // dialog dismissed, back on MODS
triEndTest
