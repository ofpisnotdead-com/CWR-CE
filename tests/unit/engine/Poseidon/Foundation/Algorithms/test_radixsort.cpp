// Unit tests for RadixSortByFloatDesc

#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Foundation/Algorithms/RadixSort.hpp>
#include <algorithm>
#include <functional>
#include <vector>

using Poseidon::Foundation::FloatToOrderedU32;
using Poseidon::Foundation::RadixSortBuffers;
using Poseidon::Foundation::RadixSortByFloatDesc;

static float Identity(float f)
{
    return f;
}

TEST_CASE("RadixSortByFloatDesc - descending order", "[radixsort]")
{
    SECTION("Positive floats")
    {
        float data[] = {5.0f, 2.0f, 8.0f, 1.0f, 9.0f, 3.0f, 7.0f, 4.0f, 6.0f};
        RadixSortByFloatDesc(data, 9, Identity);
        for (int i = 0; i < 8; i++)
        {
            REQUIRE(data[i] >= data[i + 1]);
        }
        REQUIRE(data[0] == 9.0f);
        REQUIRE(data[8] == 1.0f);
    }

    SECTION("Mixed signs")
    {
        float data[] = {3.0f, -1.0f, 2.0f, -5.0f, 0.0f, -0.5f, 10.0f};
        float expected[] = {10.0f, 3.0f, 2.0f, 0.0f, -0.5f, -1.0f, -5.0f};
        RadixSortByFloatDesc(data, 7, Identity);
        for (int i = 0; i < 7; i++)
        {
            REQUIRE(data[i] == expected[i]);
        }
    }
}

TEST_CASE("RadixSortByFloatDesc - edge cases", "[radixsort]")
{
    SECTION("Empty (n=0)")
    {
        float data[] = {1.0f, 2.0f};
        RadixSortByFloatDesc(data, 0, Identity); // must not touch data / crash
        REQUIRE(data[0] == 1.0f);
        REQUIRE(data[1] == 2.0f);
    }

    SECTION("Single element")
    {
        float data[] = {42.0f};
        RadixSortByFloatDesc(data, 1, Identity);
        REQUIRE(data[0] == 42.0f);
    }

    SECTION("All equal keys")
    {
        float data[] = {5.0f, 5.0f, 5.0f, 5.0f};
        RadixSortByFloatDesc(data, 4, Identity);
        for (int i = 0; i < 4; i++)
        {
            REQUIRE(data[i] == 5.0f);
        }
    }
}

TEST_CASE("RadixSortByFloatDesc - payload follows key", "[radixsort]")
{
    struct Item
    {
        float k;
        int id;
    };
    Item data[] = {{2.0f, 20}, {5.0f, 50}, {1.0f, 10}, {4.0f, 40}, {3.0f, 30}};
    RadixSortByFloatDesc(data, 5, [](const Item& it) { return it.k; });
    REQUIRE(data[0].id == 50);
    REQUIRE(data[1].id == 40);
    REQUIRE(data[2].id == 30);
    REQUIRE(data[3].id == 20);
    REQUIRE(data[4].id == 10);
}

TEST_CASE("FloatToOrderedU32 - order preserving", "[radixsort]")
{
    SECTION("Monotonic across sign boundary")
    {
        float vals[] = {-1e30f, -1000.0f, -1.5f, -0.001f, 0.0f, 0.001f, 1.5f, 1000.0f, 1e30f};
        int n = (int)(sizeof(vals) / sizeof(vals[0]));
        for (int i = 0; i + 1 < n; i++)
        {
            REQUIRE(FloatToOrderedU32(vals[i]) < FloatToOrderedU32(vals[i + 1]));
        }
    }
}

TEST_CASE("RadixSortByFloatDesc - reused buffers match", "[radixsort]")
{
    RadixSortBuffers<float> buffers;
    float a[] = {3.0f, 1.0f, 2.0f};
    float b[] = {9.0f, 5.0f, 7.0f, 6.0f, 8.0f};
    RadixSortByFloatDesc(a, 3, Identity, buffers);
    RadixSortByFloatDesc(b, 5, Identity, buffers);
    REQUIRE(a[0] == 3.0f);
    REQUIRE(a[2] == 1.0f);
    REQUIRE(b[0] == 9.0f);
    REQUIRE(b[4] == 5.0f);
}
