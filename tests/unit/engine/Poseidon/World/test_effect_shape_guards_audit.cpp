#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <catch2/catch_message.hpp>

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

std::string SliceAround(const std::string& body, const std::string& tag, size_t width)
{
    const size_t pos = body.find(tag);
    if (pos == std::string::npos)
        return {};
    return body.substr(pos, std::min(width, body.size() - pos));
}

} // namespace

TEST_CASE("Effect decals tolerate missing preloaded blood shape", "[world][effects][audit]")
{
    const std::string body = ReadTextFile(PoseidonDir() / "World" / "Entities" / "Infantry" / "SoldierOldMove.cpp");
    REQUIRE_FALSE(body.empty());

    const std::string region = SliceAround(body, "GLOB_SCENE->Preloaded(SlopBlood)", 700);
    REQUIRE_FALSE(region.empty());
    CAPTURE(region);

    REQUIRE(region.find("if (!shape)") != std::string::npos);
    REQUIRE(region.find("break;") != std::string::npos);
    REQUIRE(region.find("shape->BoundingCenter()") != std::string::npos);
    REQUIRE(region.find("if (!shape)") < region.find("shape->BoundingCenter()"));
}

TEST_CASE("Explosion crater placement tolerates missing preloaded crater shape", "[world][effects][audit]")
{
    const std::string body = ReadTextFile(PoseidonDir() / "World" / "Simulation" / "Collisions.cpp");
    REQUIRE_FALSE(body.empty());

    REQUIRE(body.find("shape ? transform.Rotate(shape->BoundingCenter()) : VZero") != std::string::npos);
}
