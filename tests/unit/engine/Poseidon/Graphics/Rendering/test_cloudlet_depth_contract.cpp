#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/Rendering/Effects/Smokes.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>

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

std::filesystem::path RepoRoot()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path();
}

ParamFile ParseConfig(const std::string& text)
{
    ParamFile pf;
    QIStream in(text.c_str(), static_cast<int>(text.size()));
    pf.Parse(in);
    return pf;
}

std::string ShapePathForConfig(const std::filesystem::path& path)
{
    std::string s = path.generic_string();
    return "/" + s;
}

class InspectableCloudletSource : public CloudletSource
{
  public:
    LODShapeWithShadow* ShapeForTest() const { return _cloudletShape; }
};
} // namespace

TEST_CASE("Cloudlet billboard shapes never write depth", "[Graphics][Effects][Cloudlet]")
{
    // Rocket exhaust / smoke cloudlets are rendered through the screen-space
    // DrawDecal queue before later 3D objects can still be drawn.  If a
    // cloudlet writes depth, it punches holes into those later objects.
    const std::string smokes =
        ReadTextFile(RepoRoot() / "engine" / "Poseidon" / "Graphics" / "Rendering" / "Effects" / "Smokes.cpp");
    REQUIRE_FALSE(smokes.empty());

    const size_t helper = smokes.find("void ApplyCloudletShapeSpecials");
    REQUIRE(helper != std::string::npos);
    const size_t helperEnd = smokes.find("} // namespace", helper);
    REQUIRE(helperEnd != std::string::npos);
    const std::string helperRegion = smokes.substr(helper, helperEnd - helper);

    REQUIRE(helperRegion.find("NoZWrite") != std::string::npos);
    REQUIRE(helperRegion.find("ClampU") != std::string::npos);
    REQUIRE(helperRegion.find("ClampV") != std::string::npos);
    REQUIRE(helperRegion.find("IsAlphaFog") != std::string::npos);
    REQUIRE(helperRegion.find("IsAlphaOrdered") != std::string::npos);

    const size_t ctor = smokes.find("Cloudlet::Cloudlet");
    REQUIRE(ctor != std::string::npos);
    const size_t sourceCtor = smokes.find("CloudletSource::CloudletSource", ctor);
    REQUIRE(sourceCtor != std::string::npos);
    const std::string ctorRegion = smokes.substr(ctor, sourceCtor - ctor);
    REQUIRE(ctorRegion.find("ApplyCloudletShapeSpecials(shape)") != std::string::npos);

    const size_t load = smokes.find("void CloudletSource::Load");
    REQUIRE(load != std::string::npos);
    const size_t serialize = smokes.find("LSError CloudletSource::Serialize", load);
    REQUIRE(serialize != std::string::npos);
    const std::string loadRegion = smokes.substr(load, serialize - load);
    REQUIRE(loadRegion.find("ApplyCloudletShapeSpecials(_cloudletShape)") != std::string::npos);
}

TEST_CASE("CloudletSource::Load applies no-depth billboard flags to synthetic billboard",
          "[Graphics][Effects][Cloudlet]")
{
    const std::filesystem::path billboard =
        RepoRoot() / "tests" / "fixtures" / "p3d" / "flat_quad.p3d";
    REQUIRE(std::filesystem::exists(billboard));

    ParamFile pf = ParseConfig("class CloudletsMissile\n"
                               "{\n"
                               "    interval = 0.005;\n"
                               "    cloudletDuration = 0.45;\n"
                               "    cloudletAnimPeriod = 1;\n"
                               "    cloudletSize = 3;\n"
                               "    cloudletAlpha = 0.5;\n"
                               "    cloudletGrowUp = 0.05;\n"
                               "    cloudletFadeIn = 0;\n"
                               "    cloudletFadeOut = 0.5;\n"
                               "    cloudletAccY = 0;\n"
                               "    cloudletMinYSpeed = -10;\n"
                               "    cloudletMaxYSpeed = 10;\n"
                               "    cloudletColor[] = {1,1,1,0};\n"
                               "    initT = 0;\n"
                               "    deltaT = 0;\n"
                               "    class Table\n"
                               "    {\n"
                               "        class T0\n"
                               "        {\n"
                               "            maxT = 0;\n"
                               "            color[] = {1,1,1,0};\n"
                               "        };\n"
                               "    };\n"
                               "    cloudletShape = \"" +
                               ShapePathForConfig(billboard) +
                               "\";\n"
                               "};\n");

    InspectableCloudletSource source;
    source.Load(pf >> "CloudletsMissile");

    LODShapeWithShadow* shape = source.ShapeForTest();
    REQUIRE(shape != nullptr);
    const int special = shape->Special();
    REQUIRE((special & NoZWrite) != 0);
    REQUIRE((special & ClampU) != 0);
    REQUIRE((special & ClampV) != 0);
    REQUIRE((special & IsAlpha) != 0);
    REQUIRE((special & IsAlphaFog) != 0);
    REQUIRE((special & NoShadow) != 0);
    REQUIRE((special & IsColored) != 0);
    REQUIRE((special & IsAlphaOrdered) != 0);
}

TEST_CASE("Metal DrawDecal keeps billboard depth test inputs", "[Graphics][Effects][Cloudlet][Metal]")
{
    const std::string metal =
        ReadTextFile(RepoRoot() / "engine" / "PoseidonMTL" / "EngineMTL.cpp");
    REQUIRE_FALSE(metal.empty());

    const size_t drawDecal = metal.find("void EngineMTL::DrawDecal");
    REQUIRE(drawDecal != std::string::npos);
    const size_t drawLine = metal.find("void EngineMTL::DrawLine", drawDecal);
    REQUIRE(drawLine != std::string::npos);
    const std::string drawDecalRegion = metal.substr(drawDecal, drawLine - drawDecal);

    REQUIRE(drawDecalRegion.find("const float z[4]") != std::string::npos);
    REQUIRE(drawDecalRegion.find("const float rhwValues[4]") != std::string::npos);
    REQUIRE(drawDecalRegion.find("DrawFan2D(xy, z, rhwValues") != std::string::npos);
}
