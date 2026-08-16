// End-to-end Graphics persistence: open Graphics by keyboard nav
// (clickText hits the 3D-mesh ray-cast issue at the Graphics nav
// row's y-position), change Terrain Detail via the UI, Esc to commit
// (live-apply pattern — Unmount writes graphics.cfg).  Pester wrapper
// asserts the file flipped.
//
// Index DefaultFocusIdc is 1101 (Audio).  Down twice → Graphics
// (Audio→Display→Graphics).  Enter activates the focused row's nav
// handler → push GraphicsPage.

triSimFrames 5;
triSetLanguage "English";
triSimFrames 5;
triClickText "OPTIONS";
triSimFrames 5;

// Down 2 from Audio → Graphics.
triSendKey 81;
triSimFrames 1;
triSendKey 81;
triSimFrames 1;
// Enter pushes GraphicsPage.
triSendKey 40;
triSimFrames 5;

// GraphicsPage focus starts on Quality Preset (row 0).
// One Down → Terrain Detail (row 1).
triSendKey 81;
triSimFrames 1;
// Right advances from Ultra to the manual Extreme tier.
triSendKey 79;
triSimFrames 2;

// Esc closes Graphics → GraphicsPage::Unmount → graphics.cfg saved.
triSendKey 41;
triSimFrames 3;

triEndTest;
