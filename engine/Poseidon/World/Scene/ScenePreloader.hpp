#pragma once

#include <string>
#include <vector>


namespace Poseidon
{
class Scene;

// ScenePreloader populates Scene's preloaded-shape slots from the
// `CfgScenePreload` config tree.  Apps that render terrain / particles
// / decals call `Initialize(scene)` once per constructed scene, and
// `Shutdown()` when that scene is torn down.  Apps that don't (utility
// tools) skip it; Scene's slots stay null and every consumer's
// `if (Preloaded(X))` guard short-circuits the feature.
//
// `Initialize` is strict: if a `CfgScenePreload.<class>.model` string
// names a file that can't be loaded, boot aborts with the offending
// path in the log.  Empty `model` strings are honoured as "feature
// disabled" (the slot stays null) — that's how a mod opts out without
// shipping a placeholder shape.
class ScenePreloader
{
public:
    static ScenePreloader& Instance();

    void Initialize(Scene& scene);
    void Shutdown();
    bool IsAvailable() const { return _initialized; }

    // Slot class names ScenePreloader reads from `CfgScenePreload`.
    // Exposed for unit tests + diagnostics.  Stays in lockstep with
    // the loader in ScenePreloader.cpp.
    static std::vector<std::string> SlotClassNames();

private:
    ScenePreloader() = default;
    bool _initialized = false;
};

}  // namespace Poseidon
