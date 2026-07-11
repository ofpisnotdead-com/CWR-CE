#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string ReadTextFile(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    REQUIRE(f.good());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::filesystem::path SourceRoot()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path();
}

} // namespace

TEST_CASE("UI key repeat uses SDL system repeat events", "[input][ui-repeat]")
{
    const std::string glWindow = ReadTextFile(SourceRoot() / "engine" / "PoseidonGL33" / "SDLEventWindow.hpp");
    const std::string dummyWindow =
        ReadTextFile(SourceRoot() / "engine" / "Poseidon" / "Graphics" / "Dummy" / "EngineDummy.cpp");
    const std::string dispatch =
        ReadTextFile(SourceRoot() / "engine" / "Poseidon" / "Input" / "InputProcessingSdl.cpp");

    const std::size_t glKeyDown = glWindow.find("else if (event.type == SDL_EVENT_KEY_DOWN)");
    REQUIRE(glKeyDown != std::string::npos);
    const std::size_t glKeyUp = glWindow.find("else if (event.type == SDL_EVENT_KEY_UP)", glKeyDown);
    REQUIRE(glKeyUp != std::string::npos);
    const std::string glRegion = glWindow.substr(glKeyDown, glKeyUp - glKeyDown);
    REQUIRE(glRegion.find("if (!event.key.repeat)") != std::string::npos);
    REQUIRE(glRegion.find("SDLInput_BufferKeyEvent") != std::string::npos);
    REQUIRE(glRegion.find("SDLInput_BufferUIKeyEvent") != std::string::npos);
    REQUIRE(glRegion.find("event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat") == std::string::npos);
    REQUIRE(glWindow.find("SDL_EVENT_FINGER_DOWN") != std::string::npos);
    REQUIRE(glWindow.find("SDL_GetTouchDeviceType(event.tfinger.touchID) == SDL_TOUCH_DEVICE_DIRECT") !=
            std::string::npos);
    REQUIRE(glWindow.find("TouchInput_HandleFingerEvent(event.tfinger)") != std::string::npos);

    const std::size_t dummyKeyDown = dummyWindow.find("case SDL_EVENT_KEY_DOWN:");
    REQUIRE(dummyKeyDown != std::string::npos);
    const std::size_t dummyKeyUp = dummyWindow.find("case SDL_EVENT_KEY_UP:", dummyKeyDown);
    REQUIRE(dummyKeyUp != std::string::npos);
    const std::string dummyRegion = dummyWindow.substr(dummyKeyDown, dummyKeyUp - dummyKeyDown);
    REQUIRE(dummyRegion.find("if (!event.key.repeat)") != std::string::npos);
    REQUIRE(dummyRegion.find("SDLInput_BufferKeyEvent") != std::string::npos);
    REQUIRE(dummyRegion.find("SDLInput_BufferUIKeyEvent") != std::string::npos);

    const std::size_t dispatchPos = dispatch.find("void SDLInput_DispatchUIKeys()");
    REQUIRE(dispatchPos != std::string::npos);
    const std::size_t nextFuncPos = dispatch.find("void SDLInput_BufferKeyEvent", dispatchPos);
    REQUIRE(nextFuncPos != std::string::npos);
    const std::string dispatchRegion = dispatch.substr(dispatchPos, nextFuncPos - dispatchPos);

    REQUIRE(dispatch.find("sHeldUIKeys") == std::string::npos);
    REQUIRE(dispatch.find("GetSystemUIKeyRepeatTiming") == std::string::npos);
    REQUIRE(dispatchRegion.find("GlobalTickCount()") == std::string::npos);
}
