// The scotopic night pass must be optional, and must leave the sky alone:
// EnableNightEye is armed only after the sky draw, which is what the sky
// samples pin. Both readings come from one camera in one run, so only the
// pass differs between them.

triSetLanguage "English"

triAssertMissionPlayable
triSetView [6706.5, 85.68, 5408.9, 1.0, -0.25, 0.0]
triSimFrames 120

triAssertLt [(triGetRegionChroma [0.10, 0.55, 0.90, 0.95, 5]), 0.20]
triAssertGt [(triGetRegionChroma [0.10, 0.02, 0.90, 0.20, 5]), 3.00]

triSetNightEye 0
triSimFrames 60

triAssertGt [(triGetRegionChroma [0.10, 0.55, 0.90, 0.95, 5]), 0.30]
triAssertGt [(triGetRegionChroma [0.10, 0.02, 0.90, 0.20, 5]), 3.00]

triEndTest
