triSetLanguage "English"

triAssertMissionPlayable
triSetView [6706.5, 85.68, 5408.9, 1.0, -0.25, 0.0]
triSimFrames 60
triScreenshot "01_ground_clearance"

// A player stuck in a low/crouched visual state makes this body-silhouette
// sample terrain-bright (~110 max channel) instead of the dark standing
// soldier silhouette (~46 max channel).
triAssertLt [(triGetPixelMaxChannel [0.500, 0.800]), 80]

triAssertGt [triPlayerGroundClearance, -0.05]
triAssertLt [triPlayerGroundClearance, 0.50]
triAssertGt [triPlayerTerrainClearance, -0.05]
triAssertLt [triPlayerTerrainClearance, 0.50]
triAssertGt [triPlayerContactGroundClearance, -0.05]
triAssertLt [triPlayerContactGroundClearance, 0.50]

triEndTest
