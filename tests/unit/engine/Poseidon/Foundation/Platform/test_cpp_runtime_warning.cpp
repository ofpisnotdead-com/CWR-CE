#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Foundation/Platform/CppRuntimeWarning.hpp>

using Poseidon::Foundation::CppRuntimeVersion;
using Poseidon::Foundation::IsCppRuntimeOlder;

TEST_CASE("C++ runtime version check rejects an older redistributable", "[platform][runtime]")
{
    constexpr CppRuntimeVersion required{14, 44};

    REQUIRE(IsCppRuntimeOlder({14, 27}, required));
    REQUIRE(IsCppRuntimeOlder({14, 43}, required));
    REQUIRE(IsCppRuntimeOlder({13, 99}, required));
    REQUIRE_FALSE(IsCppRuntimeOlder(required, required));
    REQUIRE_FALSE(IsCppRuntimeOlder({14, 51}, required));
}
