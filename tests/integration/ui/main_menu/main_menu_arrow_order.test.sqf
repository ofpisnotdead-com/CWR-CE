// The arrows move to the nearest menu item that way, even one in the next column, and
// stop at the edge rather than wrapping. The copyright line under every column takes
// focus to scroll, so an arrow that could land on it would leave nothing highlighted.
// Scancodes: Down 81, Up 82. IDCs: 109 Player, 115 Editor, 102 Options, 119 Mods,
// 101 Campaign, 117 Single mission, 105 Multiplayer, 106 Quit.
triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triWaitFrames 10
triAssertEq [(triGetControlFocused 117), "1"]

triKeyDown 81
triWaitFrames 3
triKeyUp 81
triWaitFrames 3
triAssertEq [(triGetControlFocused 105), "1"]
triKeyDown 81
triWaitFrames 3
triKeyUp 81
triWaitFrames 3
triAssertEq [(triGetControlFocused 115), "1"]
triKeyDown 81
triWaitFrames 3
triKeyUp 81
triWaitFrames 3
triAssertEq [(triGetControlFocused 102), "1"]
triKeyDown 81
triWaitFrames 3
triKeyUp 81
triWaitFrames 3
triAssertEq [(triGetControlFocused 119), "1"]
triKeyDown 81
triWaitFrames 3
triKeyUp 81
triWaitFrames 3
triAssertEq [(triGetControlFocused 106), "1"]
triKeyDown 81
triWaitFrames 3
triKeyUp 81
triWaitFrames 3
triAssertEq [(triGetControlFocused 106), "1"]

// The d-pad takes the nearest item back up and stops at the top.
triGpadPov 0
triWaitFrames 4
triAssertEq [(triGetControlFocused 119), "1"]
triGpadPov 0
triWaitFrames 4
triAssertEq [(triGetControlFocused 102), "1"]
triGpadPov 0
triWaitFrames 4
triAssertEq [(triGetControlFocused 115), "1"]
triGpadPov 0
triWaitFrames 4
triAssertEq [(triGetControlFocused 105), "1"]
triGpadPov 0
triWaitFrames 4
triAssertEq [(triGetControlFocused 117), "1"]
triGpadPov 0
triWaitFrames 4
triAssertEq [(triGetControlFocused 101), "1"]
triGpadPov 0
triWaitFrames 4
triAssertEq [(triGetControlFocused 101), "1"]

// Sideways jumps to the nearest item on that side.
triGpadPov 6
triWaitFrames 4
triAssertEq [(triGetControlFocused 109), "1"]
triGpadPov 2
triWaitFrames 4
triAssertEq [(triGetControlFocused 101), "1"]

triEndTest
