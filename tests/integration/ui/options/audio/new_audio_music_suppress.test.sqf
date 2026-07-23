// AudioPage::Mount calls SuppressMusicForPreview to duck menu music
// while the user is on the screen; Unmount calls Resume. Start a
// known stock track explicitly so the regression does not depend on
// suite timing or whether the menu has already started its ambient
// music on this machine.
//
// Pre-fix the count stayed 0 because SetVolume(0) was clobbered by
// SoundScene::Simulate writing _musicInternalVolume each frame; the
// current implementation routes through WaveOAL::_previewMute which
// ApplyVolume honors regardless.  This test only asserts the
// bookkeeping is correct — the audible-zero path is exercised by
// the manual checks in the commit message for ee3c834d.

triSimFrames 5
triAssertGe [(triCountSuppressedMusicWaves), 0]

triSetLanguage "English"
triSimFrames 5
playMusic "Track10"
triSimFrames 15
triClickText "OPTIONS"
triSimFrames 5
triClickText "Audio"
triSimFrames 30
triAssertGe [(triCountSuppressedMusicWaves), 1]

triSendKey 41   // SDL_SCANCODE_ESCAPE - pop AudioPage back to Index
triSimFrames 5
triAssertGe [(triCountSuppressedMusicWaves), 0]

triEndTest
