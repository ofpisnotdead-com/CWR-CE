#pragma once

namespace Poseidon::Foundation
{
// True when an interactive console/terminal is attached to stderr (a developer
// running from a shell). A Steam/GUI launch has none.
bool HasInteractiveConsole();

// True when the automatic per-run log file should be written: no interactive
// console AND not a test / tri-harness / --check run.
bool ShouldWriteAutoLog();

// True when a startup failure should pop a GUI dialog: ShouldWriteAutoLog()'s
// conditions plus, on Linux, a usable display.
bool ShouldShowGuiError();

// Report a fatal startup error: always to stderr, plus a native SDL dialog when
// ShouldShowGuiError(). Names the log and crash paths. Never throws; safe pre-init.
void ShowStartupError(const char* title, const char* message);
} // namespace Poseidon::Foundation
