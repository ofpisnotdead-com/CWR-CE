// Join-requirements dialog (RscDisplayJoinRequirements, idd 75) — the one-screen
// "approve this modded join" surface shown by DisplayMultiplayer::BeginModdedJoin
// before any download/re-mount. triOpenJoinRequirements opens it with a synthetic
// diff + password field (no network), so this proves the resource parses and renders
// through the real notebook controls (catching the white-box / invisible-sizeEx /
// CStatic-not-clickable gotchas) and that it exits cleanly back to the menu.

triSetLanguage "Italian"
triSimUntil { triGameMode == 2 }
triAssertEq [(triDisplay), 0]

triOpenJoinRequirements 0
triAssertEq [(triDisplay), 75]              // IDD_JOIN_REQUIREMENTS
triScreenshot "01_requirements"

// Title (display-level CStatic) + the OK button render.
triAssertIncludes [(triVisibleTexts), "Entra in Pristar's CSLA Server"]
triAssertIncludes [(triVisibleTexts), "Scarica ed entra"]    // the OK button (CT_ACTIVETEXT)

// The diff (ST_MULTI 3D static, idc 136) breaks at its explicit '\n's, so
// "Will be disabled..." starts its OWN line. Without the FormatText '\n' handling the
// whole diff collapses onto one width-wrapped line and that label trails the previous
// mod row mid-line — no line starts with it (revert the fix → this assert fails,
// "1 lines"). Teeth on that engine fix.
triAssertControlLineStarts [136, "Verrà disattivato"]
triAssertControlLinesExclude [136, "\n"]
triAssertControlLinesExclude [136, "\r"]

triClick 2                         // IDC_CANCEL
triAssertEq [(triDisplay), 0]                 // dialog dismissed, back at the menu

triOpenJoinRequirements 1
triAssertEq [(triDisplay), 75]
triAssertIncludes [(triVisibleTexts), "Configura ed entra"]
triClick 2
triAssertEq [(triDisplay), 0]

triEndTest
