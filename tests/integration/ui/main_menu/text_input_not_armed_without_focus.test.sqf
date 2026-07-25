// ControlsContainer::SetFocus arms text input/IME per focused control;
// nothing has focus at the main menu, so this must read inactive.

triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triSimFrames 30

triAssertEq [(triTextInputActive), 0];
triEndTest
