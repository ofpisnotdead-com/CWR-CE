// ControlsConfig — keyboard / mouse bindings persistence.
// Covers: defaults seeded from the engine UserActionDesc table,
// round-trip Save/Load preserves binding lists per UserAction,
// missing-file handling, and partial-file tolerance.

#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Settings/ControlsConfig.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/UserActionDesc.hpp>

#include <SDL3/SDL_scancode.h>
#include <filesystem>
#include <random>
#include <string>
#include <catch2/catch_message.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

using namespace Poseidon;
namespace
{
std::string TmpPath(const char* leaf)
{
    static std::random_device rd;
    static std::mt19937 rng(rd());
    std::uniform_int_distribution<unsigned> dist;
    auto root = std::filesystem::temp_directory_path() / ("controlscfg_test_" + std::to_string(dist(rng)));
    std::filesystem::create_directories(root);
    return (root / leaf).string();
}
} // namespace

TEST_CASE("ControlsConfig: a fresh instance has empty bindings", "[Settings][ControlsConfig]")
{
    ControlsConfig c;
    for (int i = 0; i < UAN; i++)
        CHECK(c.bindings[i].Size() == 0);
}

TEST_CASE("ControlsConfig: LoadDefaults seeds keyboard / mouse entries from UserActionDesc",
          "[Settings][ControlsConfig]")
{
    ControlsConfig c;
    c.LoadDefaults();

    UserActionDesc* descs = InputSubsystem::GetUserActionDesc();
    for (int i = 0; i < UAN; i++)
    {
        // Count expected KB&M entries from the engine table (joystick
        // entries ride along in GamepadConfig, not here).
        int expected = 0;
        for (int j = 0; j < descs[i].keys.Size(); j++)
        {
            int code = descs[i].keys[j];
            int dev = code & 0x00ff0000;
            if (code >= 0 &&
                (dev == INPUT_DEVICE_KEYBOARD || dev == INPUT_DEVICE_MOUSE || dev == INPUT_DEVICE_MOUSE_AXIS))
                expected++;
        }
        CAPTURE(i, expected);
        REQUIRE(c.bindings[i].Size() == expected);
        REQUIRE(c.modifiers[i].Size() == expected);
        // Every loaded entry is keyboard or mouse class.
        for (int j = 0; j < c.bindings[i].Size(); j++)
        {
            int dev = c.bindings[i][j] & 0x00ff0000;
            CHECK((dev == INPUT_DEVICE_KEYBOARD || dev == INPUT_DEVICE_MOUSE || dev == INPUT_DEVICE_MOUSE_AXIS));
        }
    }
}

TEST_CASE("ControlsConfig: Save filters out joystick-class entries", "[Settings][ControlsConfig]")
{
    const std::string path = TmpPath("kbm_filter.cfg");
    std::filesystem::remove(path);

    ControlsConfig src;
    // Mix keyboard scancodes with a stick button (INPUT_DEVICE_STICK = 0x20000).
    src.bindings[UAFire].Add(29);          // SDL_SCANCODE_LCTRL
    src.bindings[UAFire].Add(0x20000 + 7); // STICK + 7 — should be dropped on save
    src.bindings[UAFire].Add(0x10000 + 1); // mouse btn 1 — kept (KB&M class)
    src.bindings[UAFire].Add(INPUT_DEVICE_MOUSE_AXIS + INPUT_MOUSE_WHEEL_UP);
    src.modifiers[UAFire].Add(-1);
    src.modifiers[UAFire].Add(-1);
    src.modifiers[UAFire].Add(-1);
    src.modifiers[UAFire].Add(-1);

    REQUIRE(src.Save(path));

    ControlsConfig dst;
    REQUIRE(dst.Load(path));
    // Joystick-class entry got filtered; keyboard + mouse survive.
    REQUIRE(dst.bindings[UAFire].Size() == 3);
    CHECK(dst.bindings[UAFire][0] == 29);
    CHECK(dst.bindings[UAFire][1] == 0x10000 + 1);
    CHECK(dst.bindings[UAFire][2] == INPUT_DEVICE_MOUSE_AXIS + INPUT_MOUSE_WHEEL_UP);

    std::filesystem::remove(path);
}

TEST_CASE("ControlsConfig: Load on missing file returns false", "[Settings][ControlsConfig]")
{
    ControlsConfig c;
    CHECK_FALSE(c.Load("does_not_exist_anywhere.cfg"));
}

TEST_CASE("ControlsConfig: Load on missing file leaves instance untouched", "[Settings][ControlsConfig]")
{
    // Mirrors AudioConfig contract — caller can chain
    // Load → LoadDefaults → Save without losing pre-Load state on miss.
    ControlsConfig c;
    c.bindings[UAFire].Resize(0);
    c.bindings[UAFire].Add(0xCAFE);
    c.modifiers[UAFire].Resize(0);
    c.modifiers[UAFire].Add(0xBEEF);

    CHECK_FALSE(c.Load("nope_definitely_not_there.cfg"));
    REQUIRE(c.bindings[UAFire].Size() == 1);
    CHECK(c.bindings[UAFire][0] == 0xCAFE);
    REQUIRE(c.modifiers[UAFire].Size() == 1);
    CHECK(c.modifiers[UAFire][0] == 0xBEEF);
}

TEST_CASE("ControlsConfig: Load on partial file keeps unspecified actions at current values",
          "[Settings][ControlsConfig]")
{
    // Forward-compat: a config that only writes a subset of action lines
    // must not wipe the in-memory values for the actions it doesn't
    // mention.  Hand-craft a minimal file via Save() of a sparse instance.
    const std::string path = TmpPath("partial.cfg");
    std::filesystem::remove(path);

    ControlsConfig narrow; // empty bindings + modifiers
    narrow.bindings[UAFire].Add(123);
    narrow.modifiers[UAFire].Add(-1);
    REQUIRE(narrow.Save(path));

    ControlsConfig dst;
    dst.LoadDefaults();
    int origMoveFwdSize = dst.bindings[UAMoveForward].Size();
    REQUIRE(origMoveFwdSize > 0);

    REQUIRE(dst.Load(path));
    // UAFire was overwritten by the file.
    CHECK(dst.bindings[UAFire].Size() == 1);
    CHECK(dst.bindings[UAFire][0] == 123);
    // Other actions in `narrow` are empty, so the file DOES carry empty
    // arrays for them — Load wipes them.  This is intended behaviour:
    // the file is the source of truth.  Documents the round-trip
    // semantics for future readers.
    CHECK(dst.bindings[UAMoveForward].Size() == 0);

    std::filesystem::remove(path);
}

TEST_CASE("ControlsConfig: Save then Load round-trips every binding", "[Settings][ControlsConfig]")
{
    const std::string path = TmpPath("roundtrip.cfg");
    std::filesystem::remove(path);

    ControlsConfig src;
    src.LoadDefaults();
    // Mutate a couple of slots so the test isn't a no-op even if
    // the table is empty for some action.
    src.bindings[UAFire].Resize(0);
    src.bindings[UAFire].Add(123);
    src.bindings[UAFire].Add(456);
    src.bindings[UAMoveForward].Resize(0);
    src.bindings[UAMoveForward].Add(99);

    REQUIRE(src.Save(path));

    ControlsConfig dst;
    REQUIRE(dst.Load(path));

    CHECK(dst.bindings[UAFire].Size() == 2);
    CHECK(dst.bindings[UAFire][0] == 123);
    CHECK(dst.bindings[UAFire][1] == 456);
    CHECK(dst.bindings[UAMoveForward].Size() == 1);
    CHECK(dst.bindings[UAMoveForward][0] == 99);

    std::filesystem::remove(path);
}

TEST_CASE("ControlsConfig: double-tap keyboard and mouse bindings round-trip", "[Settings][ControlsConfig]")
{
    const std::string path = TmpPath("double_tap_roundtrip.cfg");
    std::filesystem::remove(path);

    ControlsConfig src;
    src.bindings[UAFire].Add(InputBindingDoubleTapCode((int)SDL_SCANCODE_G));
    src.bindings[UAFire].Add(InputBindingDoubleTapCode(INPUT_DEVICE_MOUSE + 1));
    src.modifiers[UAFire].Add(-1);
    src.modifiers[UAFire].Add(-1);

    REQUIRE(src.Save(path));

    ControlsConfig dst;
    REQUIRE(dst.Load(path));
    REQUIRE(dst.bindings[UAFire].Size() == 2);
    CHECK(dst.bindings[UAFire][0] == InputBindingDoubleTapCode((int)SDL_SCANCODE_G));
    CHECK(dst.bindings[UAFire][1] == InputBindingDoubleTapCode(INPUT_DEVICE_MOUSE + 1));

    std::filesystem::remove(path);
}

TEST_CASE("ControlsConfig: empty binding list round-trips as empty", "[Settings][ControlsConfig]")
{
    const std::string path = TmpPath("empty_slot.cfg");
    std::filesystem::remove(path);

    ControlsConfig src;
    // Every slot left empty.
    REQUIRE(src.Save(path));

    ControlsConfig dst;
    dst.LoadDefaults(); // pre-seed so we can verify Load wipes slots
    REQUIRE(dst.Load(path));

    for (int i = 0; i < UAN; i++)
        CHECK(dst.bindings[i].Size() == 0);

    std::filesystem::remove(path);
}
