#pragma once

#include <string>
#include <vector>
#include <functional>

class IControl;
class ControlsContainer;

namespace Poseidon
{
class UITestEngine;

// Global recorder pointer — set by UITestEngine when recording is active
// Allows input subsystem to notify the recorder of events
extern UITestEngine* GUIRecorder;

// UI display/control utility and recording engine
class UITestEngine
{
  public:
    UITestEngine() = default;

    // Enable dump mode — log controls on every display change
    void SetDumpMode(bool enabled) { _dumpMode = enabled; }

    // Whether the engine is active (dump or record mode)
    bool IsActive() const { return _dumpMode || !_recordPath.empty(); }

    // Per-frame update: monitor display changes for dump/record mode
    void Step();

    // Set output directory for screenshots
    void SetScreenshotDir(const std::string& dir) { _screenshotDir = dir; }

    // Dump all visible controls on the given container to the log
    static void DumpControls(ControlsContainer* container, int depth = 0);

    // Set frame counter reference (for recording)
    void SetFrameCounter(int* counter) { _frameCounter = counter; }

    // Enable recording mode — write events to file on Flush()
    void SetRecordFile(const std::string& path);

    // Record a raw SDL key event (call from HandleEvents)
    void RecordKey(int scancode, bool down);

    // Record a mouse button event
    void RecordMouseButton(int button, bool down, float x, float y);

    // Flush recorded events to file (call on shutdown)
    void FlushRecording();

    // Get the IDD of the current active display (for harness queries), or -1
    int GetActiveDisplayIDD() const;

    // Get the active display container (for harness queries), or nullptr
    ControlsContainer* GetActiveDisplay() const { return FindActiveDisplay(); }

    // Get text from a control (handles dynamic_cast to known types)
    static std::string GetControlText(IControl* ctrl);
    // Concatenated text of an HTML control's parsed section fields ("" if not HTML)
    static std::string GetHtmlText(IControl* ctrl);
    static void SetSemanticControlText(IControl* ctrl, const char* text);
    static void ClearSemanticControlText(IControl* ctrl);

    // Get type name of a control
    static const char* GetControlTypeName(IControl* ctrl);

    // Check if display changed since last call. Returns true and sets idd if changed.
    bool PollDisplayChanged(int& idd);

  private:
    // Find the topmost active display via GWorld
    ControlsContainer* FindActiveDisplay() const;

    // Record a display change event
    void RecordDisplayChange(int idd);

    struct RecordedEvent
    {
        int frame;
        std::string type; // "key", "mouse", "display"
        int param1 = 0;   // scancode / button / idd
        int param2 = 0;   // down (0/1)
        float x = 0, y = 0;
    };

    bool _dumpMode = false;
    int _lastDumpIDD = -999;
    int _harnessLastIDD = -2; // Separate from _lastDumpIDD to avoid interference
    std::string _screenshotDir;
    int* _frameCounter = nullptr;
    std::string _recordPath;
    std::vector<RecordedEvent> _recordedEvents;
};

} // namespace Poseidon
