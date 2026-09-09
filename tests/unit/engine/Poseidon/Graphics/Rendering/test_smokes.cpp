#include <catch2/catch_test_macros.hpp>

#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/Graphics/Rendering/Effects/Smokes.hpp>

namespace Poseidon
{
bool GetVector3(Vector3& ret, GameValuePar oper);
bool IsParticleDropArgumentCountSupported(int count);
} // namespace Poseidon

TEST_CASE("smokes.hpp compiles", "[rendering][effects]")
{
    SUCCEED("header included successfully");
}

TEST_CASE("drop vector arrays map script axes onto engine axes", "[rendering][effects][drop]")
{
    GameArrayType array;
    array.Add(GameValue(100.0f));
    array.Add(GameValue(200.0f));
    array.Add(GameValue(0.0f));
    GameValue value(array);

    Vector3 position;

    REQUIRE(Poseidon::GetVector3(position, value));
    CHECK(position[0] == 100.0f);
    CHECK(position[1] == 0.0f);
    CHECK(position[2] == 200.0f);
}

TEST_CASE("drop accepts legacy no-object argument count", "[rendering][effects][drop]")
{
    CHECK(Poseidon::IsParticleDropArgumentCountSupported(18));
    CHECK(Poseidon::IsParticleDropArgumentCountSupported(19));
    CHECK_FALSE(Poseidon::IsParticleDropArgumentCountSupported(17));
    CHECK_FALSE(Poseidon::IsParticleDropArgumentCountSupported(20));
}
