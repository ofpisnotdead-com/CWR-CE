#pragma once

#include <Poseidon/Core/RuntimeFlags.hpp>
#include <Poseidon/Core/EngineState.hpp>
#include <Poseidon/Core/Application.hpp>

#include <string>

/// Configuration block consumed across the engine via the ENGINE_CONFIG
/// macro.  Constructed once by ConfigurationSystem.

namespace Poseidon
{
class EngineConfig
{
public:
	explicit EngineConfig(RuntimeFlags& rf, EngineState& es);

	// Non-copyable/non-movable (reference members)
	EngineConfig(const EngineConfig&) = delete;
	EngineConfig& operator=(const EngineConfig&) = delete;

	// --- Visibility & View Distances (live in EngineState) ---
	float& horizontZ;
	float& objectsZ;
	float& tacticalZ;
	float& radarZ;
	float& shadowsZ;
	float& trackTimeToLive;
	float& invTrackTimeToLive;

	// --- Display Settings (owned by value) ---
	int wantBpp = 32;
	int wantW = 800;
	int wantH = 600;
	std::string displayMode = "borderless"; // "windowed" / "borderless" / "exclusive"
	bool useWindow = false;
	bool noSplash = false;
	bool noMenuScene = false;

	// --- Object Limits ---
	int maxObjects = 256;

	// --- Memory Management (MB) ---
	int heapSize = 512;
	int fileHeapSize = 256;
	// Coarse process-wide heap limits, in MB. -1 = auto (the default): derive a
	// near-OOM backstop from physical RAM (80% soft / 92% hard) — inert on any
	// normal machine, see DeriveDefaultMemoryLimits. 0 = explicit unlimited. >0 =
	// explicit MB. soft = pressure/observability watermark; hard = ceiling that
	// evicts caches (FreeOnDemand). Per-system policy lives in the FreeOnDemand
	// registry, not here
	int memorySoftLimitMB = -1;
	int memoryHardLimitMB = -1;

	// --- Texture Limits (max dimension in pixels per category) ---
	int maxCockText = 4096;
	int maxLandText = 4096;
	int maxObjText = 4096;
	int maxAnimText = 2048;
	int autoDropText = 1; // 1 = never drop, power-of-2 reduction otherwise

	// --- Level of Detail (LOD) ---
	float lodCoef = 10.0f;
	float objectLODLimit = 0.0f;
	float shadowLODLimit = 0.0f;

	// --- Lighting ---
	// lights is a bitmask: LIGHT_EXPLO(1) | LIGHT_MISSILE(2) | LIGHT_STATIC(4)
	int lights = 7;
	int maxLights = 16;

	// --- Audio ---
	int maxSounds = 6;
	bool singleVoice = false;
	bool noSound = false;
	std::string requestedAudioBackend; // empty = system default

	// --- Performance ---
	float benchmark = 600.0f;

	// --- Gameplay Options ---
	bool& blood;

#if _ENABLE_CHEATS
	bool& super;
#endif

	// --- Network ---
	bool netLogEnabled = false;

	// --- Runtime Flags (live in RuntimeFlags) ---
	bool& checkInitAndExit;
	bool& noLandscape;
	bool& noTerrainCache;
	bool& doCreateServer;
	bool& doCreateDedicatedServer;
	int& showFps;
	bool& landEditor;
	bool& hideCursor;
	bool& noRedrawWindow;
	bool& gUseFileBanks;
	bool& gEnableCaching;
	bool& gReplaceProxies;
	bool& gMergeTextures;

	// --- Graphics (hardware feature flags) ---
	bool enableHWTL = true;
	bool enablePIII = false;
	int d3dAdapter = -1;
	// GPU skinning master switch (default OFF).  Read by both World (Man::Animate,
	// to retain the per-object bone palette) and the GL33 backend (to build
	// SSkinnedVertex buffers + upload the BonePalette UBO).  Single source of
	// truth so the two layers never disagree.  See PERF-gpu-skinning-scope.md.
	bool enableGpuSkinning = false;
};

// Convenience macro for accessing engine config
// Usage: ENGINE_CONFIG.horizontZ instead of GApp->GetConfig().GetEngineConfig().horizontZ
#define ENGINE_CONFIG Poseidon::GApp->GetConfig().GetEngineConfig()
} // namespace Poseidon
