#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/EntityAIType.hpp>
#include <Poseidon/AI/LicensePlateTextTuning.hpp>
#include <Poseidon/Audio/Dummy/WaveDummy.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

using namespace Poseidon;
class EntityAITestHooks : public EntityAI
{
  public:
    using EntityAI::WeaponSoundCacheStale;
};

// ---------------------------------------------------------------------------
// MP weapon-index out-of-bounds crash — AV in EntityAI::TrackTargets+0x105.
//
// At Briefing->Play the weapon delta-transfer writes the server's _currentWeapon
// straight into the client (VehicleAIDiag.cpp:1722 `ITRANSF(currentWeapon)`) with
// no clamp, while the client rebuilds _magazineSlots from its own weapon list.
// When the two disagree (the logged `m == n` assert, VehicleAIDiag.cpp:1875),
// _currentWeapon can end up >= NMagazineSlots(). The optics block in TrackTargets
// (Target.cpp:808) then guards only the lower bound:
//
//     if (_currentWeapon >= 0 && EnableViewThroughOptics())
//         const MagazineSlot& slot = GetMagazineSlot(_currentWeapon); // _magazineSlots[i]
//         if (slot._muzzle) ...                                       // 0xC0000005 reads here
//
// Every sibling call site (VehicleAI.cpp:2158/2174/2204, TransportCore.cpp:931)
// also guards `_currentWeapon < NMagazineSlots()`; only this one omits it. Under
// NDEBUG (the shipping / rwdi build) AutoArray's bounds AssertDebug compiles to
// nothing, so GetMagazineSlot(_currentWeapon) is a raw `_data[i]` out-of-range
// read -> access violation.
//
// `magazineSlots` below is the *real* EntityAI::_magazineSlots type, so this drives
// the exact production read (MuzzleNameCachedMagazineSlots::operator[] -> AutoArray
// ::Get -> _data[i]). It cannot use a live EntityAI because that needs a config-
// derived EntityAIType + shape; the container reproduces the faulting read alone.
//
// Hidden ([.]) — it deliberately performs the OOB read and crashes, so it is
// excluded from the default suite. Reproduce explicitly (under the debugger to
// catch the fault):
//   ./scripts/debug.ps1 -Target PoseidonTests -Args '"[oobWeapon]"'
// ---------------------------------------------------------------------------
TEST_CASE("MP weapon-index OOB read in optics block", "[vehicleAI][network][oobWeapon][.]")
{
    MuzzleNameCachedMagazineSlots magazineSlots; // real EntityAI::_magazineSlots type
    magazineSlots.Resize(2);                     // client rebuilt 2 slots == NMagazineSlots()

    // Server-sent _currentWeapon, unclamped, >= NMagazineSlots(). The field crash
    // is just-over-size (e.g. 5); a large index is used here so the read lands on
    // an unmapped page and faults deterministically in a unit harness, where a
    // small overrun would otherwise hit adjacent heap and read garbage silently.
    const int currentWeapon = 1 << 20;

    // Exact guard + access from Target.cpp:808-814 — lower bound only.
    if (currentWeapon >= 0 /* missing: && currentWeapon < magazineSlots.Size() */)
    {
        const MagazineSlot& slot = magazineSlots[currentWeapon]; // GetMagazineSlot(): _data[i] OOB
        volatile int touch = slot._mode;                         // read OOB memory -> AV
        (void)touch;
    }
    SUCCEED("reached without faulting — bounds bug not reproduced");
}

TEST_CASE("vehicleAI compiles", "[vehicleAI][tier3]")
{
    REQUIRE(sizeof(EntityAI) > 0);
}

TEST_CASE("license plate text tuning preserves legacy engine fallback by default", "[vehicleAI][licensePlate]")
{
    ResetLicensePlateTextTuning();

    const LicensePlateTextTuning tuning = GetLicensePlateTextTuning();
    const LicensePlateTextFrame frame =
        BuildLicensePlateTextFrame(Vector3(0, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0), 2.0f, tuning);

    REQUIRE(frame.up.Distance(Vector3(0, 2, 0)) == Catch::Approx(0.0f).margin(1e-5f));
    REQUIRE(frame.right.Distance(Vector3(1.6f, 0, 0)) == Catch::Approx(0.0f).margin(1e-5f));
    REQUIRE(frame.origin.Distance(Vector3(-4.8f, 1.0f, -0.01f)) == Catch::Approx(0.0f).margin(1e-5f));
    REQUIRE(tuning.softness == Catch::Approx(0.0f));
}

TEST_CASE("license plate text tuning loads remaster config values", "[vehicleAI][licensePlate]")
{
    ParamFile file;
    ParamClass* cfg = file.AddClass("CfgLicensePlateText");
    REQUIRE(cfg != nullptr);
    cfg->Add("widthScale", 0.960f);
    cfg->Add("horizontalOffset", -2.24f);
    cfg->Add("verticalOffset", 0.70f);
    cfg->Add("surfaceOffset", 0.030f);
    cfg->Add("softness", 0.042f);

    REQUIRE(LoadLicensePlateTextTuningFromConfig(file.FindEntry("CfgLicensePlateText")));
    const LicensePlateTextTuning tuning = GetLicensePlateTextTuning();

    REQUIRE(tuning.widthScale == Catch::Approx(0.960f));
    REQUIRE(tuning.horizontalOffset == Catch::Approx(-2.24f));
    REQUIRE(tuning.verticalOffset == Catch::Approx(0.70f));
    REQUIRE(tuning.surfaceOffset == Catch::Approx(0.030f));
    REQUIRE(tuning.softness == Catch::Approx(0.042f));

    ResetLicensePlateTextTuning();
}

TEST_CASE("license plate text tuning safely falls back when config is missing or partial", "[vehicleAI][licensePlate]")
{
    LicensePlateTextTuning tuning{};
    tuning.widthScale = 1.1f;
    tuning.horizontalOffset = -1.0f;
    tuning.verticalOffset = 1.0f;
    tuning.surfaceOffset = 0.02f;
    tuning.softness = 0.03f;
    SetLicensePlateTextTuning(tuning);

    REQUIRE_FALSE(LoadLicensePlateTextTuningFromConfig(nullptr));
    LicensePlateTextTuning actual = GetLicensePlateTextTuning();
    REQUIRE(actual.widthScale == Catch::Approx(0.8f));
    REQUIRE(actual.horizontalOffset == Catch::Approx(-3.0f));
    REQUIRE(actual.verticalOffset == Catch::Approx(0.5f));
    REQUIRE(actual.surfaceOffset == Catch::Approx(0.01f));
    REQUIRE(actual.softness == Catch::Approx(0.0f));

    ParamFile file;
    ParamClass* cfg = file.AddClass("CfgLicensePlateText");
    REQUIRE(cfg != nullptr);
    cfg->Add("horizontalOffset", -2.5f);

    REQUIRE(LoadLicensePlateTextTuningFromConfig(file.FindEntry("CfgLicensePlateText")));
    actual = GetLicensePlateTextTuning();
    REQUIRE(actual.widthScale == Catch::Approx(0.8f));
    REQUIRE(actual.horizontalOffset == Catch::Approx(-2.5f));
    REQUIRE(actual.verticalOffset == Catch::Approx(0.5f));
    REQUIRE(actual.surfaceOffset == Catch::Approx(0.01f));
    REQUIRE(actual.softness == Catch::Approx(0.0f));

    ResetLicensePlateTextTuning();
}

TEST_CASE("license plate text tuning clamps unsafe scale, surface, and softness values", "[vehicleAI][licensePlate]")
{
    LicensePlateTextTuning tuning{};
    tuning.widthScale = -5.0f;
    tuning.horizontalOffset = -2.25f;
    tuning.verticalOffset = 0.75f;
    tuning.surfaceOffset = -1.0f;
    tuning.softness = -2.0f;
    SetLicensePlateTextTuning(tuning);

    const LicensePlateTextTuning actual = GetLicensePlateTextTuning();
    REQUIRE(actual.widthScale == Catch::Approx(0.1f));
    REQUIRE(actual.horizontalOffset == Catch::Approx(-2.25f));
    REQUIRE(actual.verticalOffset == Catch::Approx(0.75f));
    REQUIRE(actual.surfaceOffset == Catch::Approx(0.0f));
    REQUIRE(actual.softness == Catch::Approx(0.0f));

    ResetLicensePlateTextTuning();
}

TEST_CASE("weapon sound cache drops terminated waves", "[vehicleAI][audio]")
{
    REQUIRE(EntityAITestHooks::WeaponSoundCacheStale(nullptr));

    Ref<WaveDummy> wave = new WaveDummy("m16-single");
    wave->Play();
    REQUIRE_FALSE(EntityAITestHooks::WeaponSoundCacheStale(wave));

    wave->LastLoop();
    REQUIRE(EntityAITestHooks::WeaponSoundCacheStale(wave));

    wave->Restart();
    wave->Play();
    REQUIRE_FALSE(EntityAITestHooks::WeaponSoundCacheStale(wave));
}

// Sanity that the network/AI assert conversion logs instead of aborting/compiling out.
// Hidden ([.]) — run explicitly to read the emitted LOG_ERROR lines:
//   ./scripts/debug.ps1 -Target PoseidonTests -ExeArgs '[logerror]'
TEST_CASE("NET_ERROR / AI_ERROR log and continue", "[network][logerror][.]")
{
    NET_ERROR(1 == 1);     // holds — silent
    NET_ERROR(2 + 2 == 5); // fails — LOG_ERROR(Network, "... check failed: 2 + 2 == 5")
    AI_ERROR(2 + 2 == 5);  // fails — LOG_ERROR(AI,      "... check failed: 2 + 2 == 5")
    SUCCEED("neither macro aborted the process");
}

// One log line per key at a call site, however many times that site is reached.
TEST_CASE("LogOnceForKey - one report per key per site", "[logging][logonce]")
{
    using Poseidon::Foundation::ForgetLogOnceKeys;
    using Poseidon::Foundation::LogOnceForKey;

    static const char siteA = 0;
    static const char siteB = 0;

    ForgetLogOnceKeys();

    CHECK(LogOnceForKey(&siteA, "FDF_MastoLightRed"));
    CHECK_FALSE(LogOnceForKey(&siteA, "FDF_MastoLightRed"));
    CHECK_FALSE(LogOnceForKey(&siteA, "FDF_MastoLightRed"));

    CHECK(LogOnceForKey(&siteA, "FDF_MastoLightWhite"));
    CHECK_FALSE(LogOnceForKey(&siteA, "FDF_MastoLightWhite"));

    CHECK(LogOnceForKey(&siteB, "FDF_MastoLightRed"));

    ForgetLogOnceKeys();
    CHECK(LogOnceForKey(&siteA, "FDF_MastoLightRed"));
}
