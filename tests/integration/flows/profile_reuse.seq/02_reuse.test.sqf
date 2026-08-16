// A missing active profile falls back to an existing profile without recreating it.

triSetLanguage "English"
triAssert [(triPlayerName)]
triAssertProfileMissing "GhostProfile"
triEndTest
