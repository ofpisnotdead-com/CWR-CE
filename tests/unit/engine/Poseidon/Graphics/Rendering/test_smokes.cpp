#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_approx.hpp>
#include <initializer_list>

#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/Graphics/Rendering/Effects/Smokes.hpp>

namespace Poseidon
{
bool GetParticlePosition(Vector3& position, GameValuePar value, bool modelRelative);
bool IsParticleDropArgumentCountSupported(int count);
} // namespace Poseidon

TEST_CASE("smokes.hpp compiles", "[rendering][effects]")
{
    SUCCEED("header included successfully");
}

TEST_CASE("drop accepts legacy no-object argument count", "[rendering][effects][drop]")
{
    CHECK(Poseidon::IsParticleDropArgumentCountSupported(18));
    CHECK(Poseidon::IsParticleDropArgumentCountSupported(19));
    CHECK_FALSE(Poseidon::IsParticleDropArgumentCountSupported(17));
    CHECK_FALSE(Poseidon::IsParticleDropArgumentCountSupported(20));
}

namespace
{
class ParticleLandscape : public Poseidon::Landscape
{
  public:
    ParticleLandscape(float height, float water) : Landscape(nullptr, nullptr)
    {
        for (int z = 0; z < GetTerrainRange(); ++z)
            for (int x = 0; x < GetTerrainRange(); ++x)
                SetData(x, z, height);
        _seaLevelWave = water;
    }
};

class ParticleLandscapeScope
{
  public:
    explicit ParticleLandscapeScope(Poseidon::Landscape* landscape) : _previous(Poseidon::GLandscape)
    {
        Poseidon::GLandscape = landscape;
    }
    ~ParticleLandscapeScope() { Poseidon::GLandscape = _previous; }

  private:
    Poseidon::Landscape* _previous;
};

GameValue ParticlePosition(std::initializer_list<float> values)
{
    GameArrayType array;
    for (float value : values)
        array.Add(GameValue(value));
    return GameValue(array);
}
} // namespace

TEST_CASE("drop free positions resolve above terrain and water", "[rendering][effects][drop]")
{
    const float height = GENERATE(45.5f, -10.0f);
    const float water = 3.0f;
    const float surface = height > water ? height : water;
    ParticleLandscape landscape(height, water);
    ParticleLandscapeScope scope(&landscape);
    REQUIRE(landscape.SurfaceYAboveWater(100, 200) == Catch::Approx(surface));

    const bool includeHeight = GENERATE(false, true);
    const auto input = includeHeight ? ParticlePosition({100, 200, 2}) : ParticlePosition({100, 200});
    Vector3 position;
    REQUIRE(Poseidon::GetParticlePosition(position, input, false));
    CHECK(position.X() == 100);
    CHECK(position.Z() == 200);
    CHECK(position.Y() == Catch::Approx(surface + (includeHeight ? 2 : 0)));
}

TEST_CASE("drop model offsets ignore terrain and water", "[rendering][effects][drop]")
{
    ParticleLandscape landscape(45.5f, 3.0f);
    ParticleLandscapeScope scope(&landscape);
    Vector3 position;
    REQUIRE(Poseidon::GetParticlePosition(position, ParticlePosition({100, 200, 2}), true));
    CHECK(position.X() == 100);
    CHECK(position.Z() == 200);
    CHECK(position.Y() == 2);
    CHECK_FALSE(Poseidon::GetParticlePosition(position, ParticlePosition({100, 200}), true));
}

TEST_CASE("drop positions reject invalid coordinates", "[rendering][effects][drop]")
{
    const bool modelRelative = GENERATE(false, true);
    Vector3 position;
    CHECK_FALSE(Poseidon::GetParticlePosition(position, ParticlePosition({}), modelRelative));
    CHECK_FALSE(Poseidon::GetParticlePosition(position, ParticlePosition({100}), modelRelative));
    CHECK_FALSE(Poseidon::GetParticlePosition(position, ParticlePosition({100, 200, 2, 3}), modelRelative));
    CHECK_FALSE(Poseidon::GetParticlePosition(position, GameValue(2.0f), modelRelative));
    const int index = GENERATE(0, 1, 2);
    GameArrayType array;
    array.Add(GameValue(100.0f));
    array.Add(GameValue(200.0f));
    array.Add(GameValue(2.0f));
    array[index] = GameValue("invalid");
    CHECK_FALSE(Poseidon::GetParticlePosition(position, GameValue(array), modelRelative));
}
