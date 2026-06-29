# Touch Interface WIP Plan

This is a working plan for improving touch controls while preserving OFP/CWR's
existing control vocabulary. The goal is to map touch gestures onto the game's
current interaction concepts first, then add richer mobile affordances where they
make the original systems easier to use.

## Current Principles

- Touch should emulate existing OFP inputs where possible.
- One-finger interactions should usually mean primary pointer/selection.
- Two-finger interactions should usually mean viewport manipulation.
- Contextual UI should reduce permanent on-screen buttons.
- Destructive or high-risk actions should require a clear confirm step.
- Existing command/action/menu systems should remain usable from keyboard,
  mouse, gamepad, and touch.

## Current State

- Touch movement/look input exists.
- Touch aim sensitivity and cursor sensitivity are configurable.
- Cursor sensitivity now uses a curved slider mapping, so low cursor speeds have
  more usable slider travel.
- Map one-finger primary interaction, two-finger pan, and pinch zoom are wired
  through the existing mouse/map paths.
- Action-button hold-drag scrolling is wired through the existing action-menu
  mouse-wheel path.
- Command-bar tapping is not yet touch-friendly.

## Track 1: Map Touch Gestures

Map interaction has to preserve selection-box drag, so one-finger drag should
not become map panning.

Proposed mapping:

- One-finger tap: primary map click. Implemented as primary mouse input.
- One-finger drag: primary map drag, including selection box. Implemented as
  primary mouse input.
- Two-finger drag: pan map. Implemented as right-button map drag.
- Pinch: zoom map in/out. Implemented as mouse wheel zoom.
- Two-finger tap: possible future context/right-click equivalent.
- Long-press one finger: possible future context action, only if it does not
  conflict with selection or waypoint placement.

Implementation notes:

- Prefer routing map gestures into existing map pan/zoom paths instead of
  generic mouse motion when a map display is active.
- Keep one-finger cursor behavior compatible with existing map hit-testing.
- Two-finger gesture recognition should cancel primary drag behavior while the
  gesture is active.
- Pinch zoom should have a conservative scale curve and clamp to existing map
  zoom limits.

Open questions:

- Which map display/control owns the canonical right-drag pan behavior?
- Is mouse wheel zoom handled at the display level or by a shared cursor delta?
- Should two-finger drag pan around the midpoint between fingers or follow the
  centroid delta directly?

## Track 2: Action Menu Touch Scrolling

The action menu is central to OFP, and touch needs a way to scroll it without a
mouse wheel.

Proposed mapping:

- Clean tap-release on the action button: execute the currently highlighted/default action.
- Hold action button: keep/open the action menu.
- While held, drag up/down: scroll/highlight action menu entries. Implemented as
  mouse-wheel action-menu scrolling.
- Flick up/down: future tuning pass.
- Release after dragging/flicking: leave the highlighted action selected, but do
  not execute it.
- Tap action button again: execute the highlighted action.

Rationale:

- Separating scroll from execute avoids accidental high-risk actions.
- The original action list remains the source of truth.
- A quick touch-and-flick can become a fast expert path once tuned.

Implementation notes:

- Start with discrete row stepping from vertical drag distance. Implemented.
- Add flick velocity handling after the basic hold/drag model is stable.
- Use haptic/audio/menu tick feedback where platform support exists.
- Make sure hold-to-scroll does not interfere with normal action-button press
  behavior in gameplay.

Open questions:

- Where is the current action-menu selection index stored?
- Is action-menu scrolling currently represented as mouse wheel, key up/down, or
  direct selection changes?
- Should a long no-movement hold still count as a clean tap release, or should
  long-hold always require a follow-up tap?

## Track 3: Tappable Command Bar

OFP already exposes squad selection and commands through the command UI at the
bottom of the screen. Touch should make that UI tappable before adding a custom
command layer.

Phase 1:

- Hit-test visible unit slots in the bottom command bar.
- Tapping unit slots emulates F1 through F12.
- Tapping empty or hidden slots does nothing.

Phase 2:

- Hit-test visible command menu options.
- Tapping command options emulates the corresponding number-key selection.
- Preserve keyboard/gamepad command flow.

Phase 3:

- Consider a touch-specific command overlay or radial only after the native
  command UI can be operated reliably.

Implementation notes:

- Prefer exposing command UI layout rectangles from the renderer/controller over
  duplicating layout math in input code.
- Unit selection should respect existing multi-select behavior where possible.
- The command hit-test layer should only be active when the command UI is visible
  and touch input is enabled.

Open questions:

- Does the command UI already track per-slot screen rectangles?
- How does the current F1-F12 emulation path enter input events?
- Are command menu options drawn from a layout we can query, or are they only
  emitted during rendering?

## Track 4: Contextual Touch Buttons

After the map, action menu, and command bar are usable, reduce touch clutter with
state-aware controls.

Candidate contextual buttons:

- Fire / primary action.
- Optics or zoom.
- Reload.
- Stance cycle or stance selector.
- Action menu.
- Map.
- Command mode.
- Weapon switch.
- Vehicle-specific enter/exit, commander/gunner, and freelook controls.

Rules of thumb:

- Show fewer controls by default.
- Reveal state-specific buttons only when they are useful.
- Keep critical controls in consistent positions.
- Avoid covering command UI, action menu, and map content.

## Track 5: Direct Touch On Existing UI

OFP has many small native buttons and controls. Touch should be able to operate
those directly where it makes sense, without requiring every screen to get a
custom mobile rewrite.

First pass:

- Tap existing UI controls by moving the software cursor to the touch point and
  emitting a primary click.
- Drag existing controls through the normal primary button drag path.
- Keep this routed through existing display/container hit-testing so behavior
  stays consistent with mouse input.

Later tuning:

- Add finger-sized hit expansion for tiny buttons where it is safe.
- Avoid expanding hit regions when controls are densely packed and ambiguity
  would cause wrong actions.
- Consider per-display touch affordances for especially small or important
  controls.

## Suggested Implementation Order

1. Add map two-finger pan.
2. Add map pinch zoom.
3. Add action-button hold/drag scrolling. Implemented.
4. Add action-button flick tuning.
5. Add tappable F1-F12 command-bar slots.
6. Add tappable command-menu options.
7. Add direct tap/drag support for existing UI controls.
8. Add contextual button visibility rules.
9. Add layout customization: position, scale, opacity, handedness.

## Test Ideas

- Unit-test gesture recognizers with synthetic finger input.
- Add map gesture tests for one-finger selection preservation.
- Add map gesture tests for two-finger pan and pinch deltas.
- Add action-menu tests for hold, drag step, release-no-execute, and tap-confirm.
- Add command-bar hit-test tests for F1-F12 slot mapping.
- Add integration screenshots or scripted checks for command UI hit regions.

## Notes For Future Tuning

- Map pan/zoom should feel like a normal mobile view, but OFP selection behavior
  must win for one-finger gestures.
- Cursor, map, aim, and stick-cursor sensitivity may need separate settings.
- Haptics are useful for action-menu row changes and command selection, but
  should be optional.
- Any custom command overlay should build on the native command system rather
  than replacing it.
