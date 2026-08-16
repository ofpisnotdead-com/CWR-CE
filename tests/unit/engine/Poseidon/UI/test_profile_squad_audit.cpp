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

std::filesystem::path PoseidonDir()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "Poseidon";
}

} // namespace

TEST_CASE("Profile editor keeps squad XML controls visible", "[ui][profile][squad]")
{
    const std::string body = ReadTextFile(PoseidonDir() / "UI" / "DisplayUIMenus.cpp");
    REQUIRE_FALSE(body.empty());

    REQUIRE(body.find("GetCtrl(IDC_NEW_USER_SQUAD)->ShowCtrl(false)") == std::string::npos);
    REQUIRE(body.find("GetCtrl(IDC_NEW_USER_SQUAD_TEXT)->ShowCtrl(false)") == std::string::npos);
}
