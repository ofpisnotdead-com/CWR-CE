#include <catch2/catch_test_macros.hpp>

#include <Poseidon/World/Model/ShapeAdapter.hpp>

using namespace Poseidon::Model;

TEST_CASE("ShapeAdapter strips OFP proxy selection syntax before loading proxy model", "[ShapeAdapter][proxy]")
{
    const auto proxy = ShapeAdapter::normalizeProxyModelName("proxy:bmpdriver.01");

    REQUIRE(proxy.modelName == "bmpdriver");
    REQUIRE(proxy.id == 1);
}

TEST_CASE("ShapeAdapter keeps plain proxy model names", "[ShapeAdapter][proxy]")
{
    const auto proxy = ShapeAdapter::normalizeProxyModelName("cargo");

    REQUIRE(proxy.modelName == "cargo");
    REQUIRE(proxy.id == -1);
}
