#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/Application.hpp>

#include <string>

TEST_CASE("AppName contains no characters reserved by common filesystems", "[core][application][filesystem]")
{
    const std::string name = AppName;
    REQUIRE_FALSE(name.empty());

    // Characters reserved in path components on supported filesystems.
    static const std::string reserved = "<>:\"/\\|?*";

    for (unsigned char c : name)
    {
        CAPTURE(c);
        CHECK(reserved.find(static_cast<char>(c)) == std::string::npos);
        CHECK(c >= 0x20);
    }

    CHECK(name.back() != '.');
    CHECK(name.back() != ' ');
}
