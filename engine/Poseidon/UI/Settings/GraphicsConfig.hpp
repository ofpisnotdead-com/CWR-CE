#pragma once

// Graphics user-settings persisted to <user-dir>/graphics.cfg.
//
// Sibling to AudioConfig + DisplayConfig — same SectionConfig pattern:
// plain-data class with sensible defaults, Normalize(env) against a
// runtime Environment, Load + Save against a path.  System-global —
// one file per machine, not per game profile.
//
// Live-apply screen (no Apply button): values flow to the engine the
// frame they change.  Persistence on Unmount mirrors AudioPage.
//
// Field grouping:
//
//   Tier rows           — driven by Quality Preset; touching one
//                         drops the preset display to "Custom".
//                           terrainDetail, objectLod, shadowQuality,
//                           particlesQuality
//
//   Per-user knobs      — independent of preset, keep value across
//                         preset changes:
//                           vsync, fpsCap, brightness, gamma
//
// Autodetect on first boot: GraphicsConfig::PickPresetFromRam()
// returns a tier from SDL_GetSystemRAM(); ApplyPresetToTiers fills the
// four tier rows from a known bundle.  No CPU benchmark loop; benchmarks are
// unreliable on modern thermal-throttled CPUs.

#include <string>


namespace Poseidon
{
class GraphicsConfig
{
public:
	enum Preset
	{
		PresetLow    = 0,
		PresetMedium = 1,
		PresetHigh   = 2,
		PresetUltra  = 3,
		PresetCustom = 4,   // sentinel — UI shows "Custom" when the four tier rows
		                     // don't match any of Low/Medium/High/Ultra's bundle
	};
	// Terrain / Object / Shadow / Particles share the same Off-to-Ultra
	// shape.  Off only legal where called out (Shadow, Particles).
	enum Tier
	{
		TierOff    = 0,
		TierLow    = 1,
		TierMedium = 2,
		TierHigh   = 3,
		TierUltra  = 4,
	};
	enum VsyncMode
	{
		VsyncOff      = 0,
		VsyncOn       = 1,
		VsyncAdaptive = 2,
	};

	// Persistable fields.
	Preset    qualityPreset    = PresetUltra;
	Tier      terrainDetail    = TierUltra;     // → grid 6.25 m
	Tier      objectLod        = TierUltra;
	Tier      shadowQuality    = TierHigh;      // High caps Shadow tier; "Ultra shadow" needs
	                                             // shadow-distance bias hook that doesn't exist yet
	Tier      particlesQuality = TierHigh;
	VsyncMode vsync            = VsyncOn;
	int       fpsCap           = 0;             // 0 = Unlimited; valid: 0/30/60/90/120/144/240
	bool      alphaToCoverage  = true;          // MSAA alpha-to-coverage on cutout draws (fence wire,
	                                             // foliage) — grades sub-pixel features instead of the
	                                             // hard alpha-test keeping/killing whole pixels
	float     renderScale      = 1.0f;          // SSAA: render at scale x window and downsample.
	                                             // 1.0 = off; up to 2.0.  The only general cure for
	                                             // sub-pixel OPAQUE geometry sparkle (fence bars)
	int       msaaSamples      = 0;             // MSAA on the frame target: 0 (off) / 2 / 4 / 8.
	                                             // Default off — AA is being tuned via the dev panel
	                                             // Render tab before a shipped default is picked
	float     brightness       = 1.6f;          // 0.4..1.8
	float     gamma            = 1.2f;          // 0.5..2.3

	// Schema version of the persisted file.  Load resets this to 0 first, so a
	// file written before versioning reads back as 0 and Migrate can act on it.
	static constexpr int kVersion = 1;
	int       version          = kVersion;

	// Bring a loaded config up to kVersion.  Returns true when the caller needs
	// to persist the result.
	bool Migrate();

	// Reset every field to factory defaults.
	void LoadDefaults();

	// Validation against the runtime environment.  Out-of-range tier
	// values reset to the per-row default; out-of-range float sliders
	// clamp to their edges; FPS cap not in the allowed set rounds to
	// the nearest allowed value or 0.  Each field validates
	// independently — a stale terrain tier doesn't affect FPS cap.
	// Returns true if any field changed.
	struct Environment
	{
		virtual ~Environment() = default;
		// Total system RAM in MB (0 = unknown).  Used by PickPresetFromRam
		// for autodetect on first boot.
		virtual int GetSystemRamMB() const = 0;
	};
	bool Normalize(const Environment& env);

	bool Load(const std::string& path);
	bool Save(const std::string& path) const;

	// Helpers — used by the boot path's autodetect-on-first-run flow,
	// the Quality Preset row's write-through behaviour, and the
	// "preset == Custom when tiers diverge" UI logic.

	// Picks Low / Medium / High / Ultra from the system RAM amount.
	// 0 (unknown) maps to High — middle of the road; on any modern
	// machine SDL_GetSystemRAM should return a valid value.
	static Preset PickPresetFromRam(int ramMB);

	// Stamps the four tier rows from a preset's bundle.  Per-user knobs
	// (vsync / fpsCap / brightness / gamma) are NOT touched.  No-op for
	// PresetCustom (the UI never writes Custom to the bundle).
	void ApplyPresetToTiers(Preset preset);

	// Returns the Preset whose bundle matches the current tier rows,
	// or PresetCustom if no preset matches.  Drives the Quality Preset
	// row's displayed value when the user has touched a tier row.
	Preset DerivePresetFromTiers() const;
};

} // namespace Poseidon
