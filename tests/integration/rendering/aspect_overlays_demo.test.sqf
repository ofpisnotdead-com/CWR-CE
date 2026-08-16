// Aspect-ratio overlay coverage on the Demo island (card #272 + follow-ups).
// The player is "Důstojník (noční)" / OfficerWNight, which carries NVGoggles
// natively (NV works at any time of day).  One run, three 4:3-overlay paths,
// each in Modern then stretch (Legacy):
//   1. intro RscTitles resource binocular  (cutRsc "binocular" -> TitleEffectBasic::DrawRsc)
//   2. player-held binocular               (Man::Draw)
//   3. night-vision goggles                (Man::DrawNVOptics)
//
// In stretch mode the lateral pillarbox bars are disabled, so each overlay must
// stretch to fill the viewport; the far edges then show the (dark) overlay frame
// instead of leaking the lit 3D scene.  The stretch far-edge asserts are the
// card #272 regression guard.  (The held binocular is NOT asserted in Modern: its
// pillarbox is gated on IsGameplayActive(), which is off for a harness-loaded
// mission, so the Modern edges legitimately show the scene.)

triSetLanguage "English"
triSimFrames 60

// ---------- 1) intro resource binocular (cutRsc) ----------
player switchCamera "EXTERNAL"
triSimFrames 40
triSetDisplayStyle 0
triSetPillarboxBarsEnabled true
cutRsc ["binocular", "plain", 0]
triSimFrames 60
triScreenshot "00_resource_modern"
triAssertNear [(triSamplePixel [0.5, 0.55]), "0,0,0", 32]
triAssertGt [(triGetPixelMaxChannel [0.35, 0.55]), 10]

triSetDisplayStyle 1
triSetPillarboxBarsEnabled false
triSimFrames 30
cutRsc ["binocular", "plain", 0]
triSimFrames 60
triScreenshot "01_resource_stretch"
triAssertNear [(triSamplePixel [0.02, 0.5]), "0,0,0", 16]
triAssertNear [(triSamplePixel [0.97, 0.5]), "0,0,0", 16]
triAssertNear [(triSamplePixel [0.5, 0.55]), "0,0,0", 32]
triAssertGt [(triGetPixelMaxChannel [0.35, 0.55]), 10]

cutRsc ["default", "plain", 0]
triSetDisplayStyle 0
triSetPillarboxBarsEnabled true
triSimFrames 30

// ---------- 2) player-held binocular ----------
player switchCamera "GUNNER"
player addWeapon "Binocular"
player selectWeapon "Binocular"
triSimFrames 90
triScreenshot "02_player_binoc_modern"
triAssertGt [(triGetPixelMaxChannel [0.5, 0.55]), 10]

triSetDisplayStyle 1
triSetPillarboxBarsEnabled false
triSimFrames 60
triScreenshot "03_player_binoc_stretch"
triAssertNear [(triSamplePixel [0.02, 0.5]), "0,0,0", 16]
triAssertNear [(triSamplePixel [0.97, 0.5]), "0,0,0", 16]

triSetDisplayStyle 0
triSetPillarboxBarsEnabled true
triSimFrames 20

// ---------- 3) NVG (night officer's goggles) ----------
// The player (OfficerWNight) already carries NVGoggles; just toggle night-vision
// with the N key (UANightVision).  The held binocular's optic only draws in
// GUNNER cam, so switching to INTERNAL hides it without removing the weapon.
player switchCamera "INTERNAL"
triSimFrames 20
triSendKey 17
triSimFrames 60
triScreenshot "04_nvg_modern"
triAssertGt [(triGetPixelMaxChannel [0.5, 0.5]), 15]

triSetDisplayStyle 1
triSetPillarboxBarsEnabled false
triSimFrames 60
triScreenshot "05_nvg_stretch"
triAssertGt [(triGetPixelMaxChannel [0.5, 0.5]), 15]
triEndTest
