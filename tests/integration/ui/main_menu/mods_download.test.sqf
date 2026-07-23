// Download dialog (RscDisplayModDownload). Two things are proven here:
//
//  Part 1 — the Apply gate: ticking an Available (not-yet-downloaded) Workshop
//  mod and pressing Apply opens the download dialog instead of re-mounting.
//  Without the gate, BuildCheckedDownloadTasks wouldn't fire and Apply would
//  silently mount nothing for that row.
//
//  Part 2 — the live progress dialog: triOpenModDownload injects synthetic
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

// 1 local fixture mod (--mods-dir) + 2 seeded Workshop mods (each with a URL).
triSeedWorkshopMods 2
triWaitFrames 5
_all = triModsVisibleCount
if (_all != 3) exitWith { format ["FAIL:rows=%1 (want 1 local + 2 workshop)", _all] }

// Name-sorted rows: 0=Fixture Mod (local), 1=Workshop Mod 1 (Available).
triModsRowClick [1, 0.03]    // tick Workshop Mod 1 (Missing + has download URL)
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
triAssertIncludes [(triVisibleTexts), "Completato"]   // localized status line; auto-retries until the worker finishes + is polled
triAssertIncludes [(triVisibleTexts), "3 / 3 addons   100%"]
triAssertIncludes [(triVisibleTexts), "Continua"]     // the action button relabels on success
triScreenshot "03_download_complete"
triClick 2                   // close (Cancel) — avoids the IDC_OK re-mount on synthetic tasks
triAssertEq [(triDisplay), 72]        // dialog dismissed, back on MODS
triEndTest
