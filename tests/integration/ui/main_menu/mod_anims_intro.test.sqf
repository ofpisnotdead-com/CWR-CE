// A mod's Anims/<name>.<world>/ folder must host the menu intro cutscene, mod-first, like any other
// mission type. @triintro ships a tiny one-soldier intro at Anims/intro.<world>/; resolved from the
// mod, the menu backdrop loads that scene (~315 resident shapes) instead of the Demo base intro.demo
// (22 groups, ~343). Without the Anims resolution the base intro loads and the count is ~343, failing
// the assert below.
triSetLanguage "English"
triSimUntil { triGameMode == 2 && triLoadedShapeCount > 0 }
triSimFrames 60
triScreenshot "01_mod_anims_intro"
triAssertLt [(triLoadedShapeCount), 330]
triEndTest
