#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
std::string ReadTextFile(const std::filesystem::path& p)
{
    std::ifstream f(p);
    if (!f.is_open())
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string ExtractFunctionBody(const std::string& src, const std::string& prototype)
{
    const size_t protoPos = src.find(prototype);
    if (protoPos == std::string::npos)
        return {};
    const size_t openBrace = src.find('{', protoPos);
    if (openBrace == std::string::npos)
        return {};
    int depth = 1;
    for (size_t i = openBrace + 1; i < src.size(); ++i)
    {
        if (src[i] == '{')
            ++depth;
        else if (src[i] == '}')
        {
            if (--depth == 0)
                return src.substr(openBrace, i - openBrace + 1);
        }
    }
    return {};
}
} // namespace

TEST_CASE("Trident script commands are only registered for dev or test runs", "[dev][trident][regression]")
{
    const auto root = std::filesystem::path(TESTS_ROOT_DIR).parent_path();

    const std::string clientSrc =
        ReadTextFile(root / "engine" / "Poseidon" / "Game" / "Commands" / "GameStateExtTestAudio.cpp");
    REQUIRE_FALSE(clientSrc.empty());
    const std::string initBody = ExtractFunctionBody(clientSrc, "INIT_MODULE(GameStateExtTest, 3)");
    REQUIRE_FALSE(initBody.empty());
    const size_t clientGate = initBody.find("DevMode()");
    const size_t clientHarnessGate = initBody.find("GetHarnessPort()");
    const size_t clientTestMissionGate = initBody.find("GetTestMissionPath()");
    const size_t clientTri = initBody.find("\"triVersion\"");
    REQUIRE(clientGate != std::string::npos);
    REQUIRE(clientHarnessGate != std::string::npos);
    REQUIRE(clientTestMissionGate != std::string::npos);
    REQUIRE(clientTri != std::string::npos);
    REQUIRE(clientGate < clientTri);
    REQUIRE(clientHarnessGate < clientTri);
    REQUIRE(clientTestMissionGate < clientTri);

    const std::string serverSrc = ReadTextFile(root / "apps" / "cwr" / "Server" / "ServerApplication.cpp");
    REQUIRE_FALSE(serverSrc.empty());
    const std::string runBody = ExtractFunctionBody(serverSrc, "ServerApplication::RunServerStages");
    REQUIRE_FALSE(runBody.empty());
    const size_t serverGate = runBody.find("DevMode()");
    const size_t serverHarnessGate = runBody.find("GetHarnessPort()");
    const size_t serverTestMissionGate = runBody.find("GetTestMissionPath()");
    const size_t serverTri = runBody.find("RegisterServerTriCommands()");
    REQUIRE(serverGate != std::string::npos);
    REQUIRE(serverHarnessGate != std::string::npos);
    REQUIRE(serverTestMissionGate != std::string::npos);
    REQUIRE(serverTri != std::string::npos);
    REQUIRE(serverGate < serverTri);
    REQUIRE(serverHarnessGate < serverTri);
    REQUIRE(serverTestMissionGate < serverTri);

    const std::string triSrc = ReadTextFile(root / "engine" / "Trident" / "src" / "client" / "instance.rs");
    REQUIRE_FALSE(triSrc.empty());
    REQUIRE(triSrc.find("cmd.arg(\"--dev\")") == std::string::npos);
}

TEST_CASE("Debug overlay cannot be opened programmatically outside --dev", "[dev][debug][regression]")
{
    const auto root = std::filesystem::path(TESTS_ROOT_DIR).parent_path();
    const std::string src = ReadTextFile(root / "engine" / "Poseidon" / "Dev" / "Debug" / "DebugOverlay.cpp");
    REQUIRE_FALSE(src.empty());

    const std::string setVisibleBody = ExtractFunctionBody(src, "void SetVisible");
    REQUIRE_FALSE(setVisibleBody.empty());
    REQUIRE(setVisibleBody.find("DevMode()") != std::string::npos);

    const std::string toggleBody = ExtractFunctionBody(src, "void ToggleVisible");
    REQUIRE_FALSE(toggleBody.empty());
    REQUIRE(toggleBody.find("SetVisible") != std::string::npos);

    const std::string newFrameBody = ExtractFunctionBody(src, "void NewFrame");
    REQUIRE_FALSE(newFrameBody.empty());
    REQUIRE(newFrameBody.find("DevMode()") != std::string::npos);
    REQUIRE(newFrameBody.find("SetVisible(false)") != std::string::npos);
}

TEST_CASE("Release builds reject --dev before help or option registration", "[dev][release][regression]")
{
    const auto root = std::filesystem::path(TESTS_ROOT_DIR).parent_path();
    const std::string src = ReadTextFile(root / "engine" / "Poseidon" / "Foundation" / "Platform" / "AppConfig.cpp");
    REQUIRE_FALSE(src.empty());

    const std::string parseBody = ExtractFunctionBody(src, "void AppConfig::ParseCommandLine");
    REQUIRE_FALSE(parseBody.empty());
    REQUIRE(parseBody.find("BuildInfo::ReleaseBuild && ContainsCliArg(normalizedArgs, \"--dev\")") !=
            std::string::npos);
    REQUIRE(parseBody.find("\"--dev is not supported in release builds\"") != std::string::npos);

    const size_t releaseGuard = parseBody.find("BuildInfo::ReleaseBuild && ContainsCliArg(normalizedArgs, \"--dev\")");
    const size_t helpMode = parseBody.find("DetectHelpMode(normalizedArgs)");
    const size_t devBuildGuard = parseBody.find("if (!BuildInfo::ReleaseBuild)");
    const size_t devRegistration = parseBody.find("\"--dev\"", devBuildGuard);
    REQUIRE(releaseGuard != std::string::npos);
    REQUIRE(helpMode != std::string::npos);
    REQUIRE(devBuildGuard != std::string::npos);
    REQUIRE(devRegistration != std::string::npos);
    REQUIRE(releaseGuard < helpMode);
    REQUIRE(devBuildGuard < devRegistration);
}

TEST_CASE("Script error popups are restricted to rwdi dev runs", "[dev][script][regression]")
{
    const auto root = std::filesystem::path(TESTS_ROOT_DIR).parent_path();
    const std::string src = ReadTextFile(root / "engine" / "Poseidon" / "Game" / "Scripting" / "ExpressExt.cpp");
    REQUIRE_FALSE(src.empty());

    const std::string displayBody =
        ExtractFunctionBody(src, "void GameStateStringtableInfoFunctions::DisplayErrorMessage");
    REQUIRE_FALSE(displayBody.empty());

    const size_t buildTypeGate = displayBody.find("BuildInfo::BuildType");
    const size_t rwdiGate = displayBody.find("\"RelWithDebInfo\"");
    const size_t devModeGate = displayBody.find("DevMode()");
    const size_t popup = displayBody.find("GlobalShowMessage");
    REQUIRE(buildTypeGate != std::string::npos);
    REQUIRE(rwdiGate != std::string::npos);
    REQUIRE(devModeGate != std::string::npos);
    REQUIRE(popup != std::string::npos);
    REQUIRE(buildTypeGate < popup);
    REQUIRE(rwdiGate < popup);
    REQUIRE(devModeGate < popup);
}
