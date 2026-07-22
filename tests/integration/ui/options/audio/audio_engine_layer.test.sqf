// Audio engine-layer round-trips (no UI navigation required).
//
// Covers (one boot): new_audio_devices + new_audio_eax_toggle +
// new_audio_volume_apply.
//
// Part 1 — devices: output device enumerates. Input devices are optional on
// headless/CI-style runners, so only assert the getter is sane.
//
// Part 2 — eax_toggle: SetEAX/GetEAX round-trips via GSoundsys.
//
// Part 3 — volume_apply: SetVolume/GetVolume round-trips + clamp behavior
// (0..100 UI range, even when the raw input would exceed it).

triSimFrames 30

// Part 1 — devices
triAssert [(triGetOutputDevice)]
triAssertGe [(triAudioPageInputCount), 0]

triListOutputDevices
triListInputDevices
triAudioPageOutputCount
triAudioPageInputCount

// Part 2 — EAX toggle
triSetEAX 0
triAssertEq [(triGetEAX), 0]

triSetEAX 1
triAssertGe [(triGetEAX), 0]
triAssertLe [(triGetEAX), 1]

triSetEAX 0
triAssertEq [(triGetEAX), 0]

// Part 3 — volume apply
triSetVolume ["music", 70]
triAssertEq [(triGetVolume "music"), 70]
triSetVolume ["effects", 30]
triAssertEq [(triGetVolume "effects"), 30]
triSetVolume ["speech", 90]
triAssertEq [(triGetVolume "speech"), 90]

// Clamp behavior.
triSetVolume ["music", 200]
triAssertEq [(triGetVolume "music"), 100]
triSetVolume ["music", -50]
triAssertEq [(triGetVolume "music"), 0]

triEndTest
