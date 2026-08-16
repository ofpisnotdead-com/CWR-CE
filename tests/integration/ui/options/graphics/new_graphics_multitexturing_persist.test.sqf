// Pins that the Multitexturing row reaches graphics.cfg on unmount.
// Scancodes: 81 Down, 79 Right, 40 Enter, 41 Esc.

triSimFrames 5;
triSetLanguage "English";
triSimFrames 5;
triClickText "OPTIONS";
triSimFrames 5;

// Index focus starts on Audio; two rows down is Graphics.
triSendKey 81;
triSimFrames 1;
triSendKey 81;
triSimFrames 1;
triSendKey 40;
triSimFrames 5;

// Page focus starts on Quality Preset; Multitexturing is eleven focus steps
// down, the Advanced header being skipped rather than focused.
for "_i" from 1 to 11 do {
    triSendKey 81;
    triSimFrames 1;
};
triSendKey 79;
triSimFrames 2;

triSendKey 41;
triSimFrames 3;

triEndTest;
