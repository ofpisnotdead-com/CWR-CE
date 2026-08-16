#pragma once

#include <Poseidon/UI/Settings/AspectRatio.hpp>

// Presentation — the single aspect decision point.
//
// Every path that decides what fills the engine's AspectSettings (boot,
// window resize, the Options Display page, dev-panel / tri overrides via
// FireResizePostHook) funnels through Apply().  Nothing else resolves
// aspect policy or calls Engine::SetAspectSettings — the resolver
// functions in AspectRatio.hpp are implementation detail of this module
// (pinned by test_presentation_single_entry.cpp).

namespace Poseidon
{
class UserConfig;

namespace Presentation
{

// Pure resolve for a viewport: honors the live dev-panel / tri override
// when enabled, otherwise the applied display policy.  Exposed separately
// from Apply so the rule table is unit-testable without an engine.
AspectRatio::Settings Resolve(int viewportWidth, int viewportHeight);

// Applied display policy used by Resolve/Apply when no live dev override
// is active.  The Display options page and boot config loader update this
// once; resize then reuses the same policy.
void SetPolicy(AspectRatio::DisplayStyle style, AspectRatio::UltrawideClamp clamp);

// Returns true when the profile migration needs to be persisted.
bool ConfigureUserFov(UserConfig& config, int viewportWidth, int viewportHeight);

// Resolve + map + Engine::SetAspectSettings — the one apply site.
// Returns the resolved settings for the caller's diagnostics.
AspectRatio::Settings Apply(int viewportWidth, int viewportHeight);

} // namespace Presentation

} // namespace Poseidon
