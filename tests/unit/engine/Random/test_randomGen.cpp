#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Random/randomGen.hpp>
#include <cmath>
#include <set>

// Test RandomTable - lookup table for random number generation
TEST_CASE("RandomTable construction and basic operations", "[common][random][randomtable]")
{
    SECTION("Default construction with seed=1")
    {
        RandomTable table;

        // Table should have predictable values for given indices
        int val0 = table(0);
        int val1 = table(1);
        int val100 = table(100);

        // Values should be in valid range [0, Size-1]
        REQUIRE(val0 >= 0);
        REQUIRE(val0 < RandomTable::Size);
        REQUIRE(val1 >= 0);
        REQUIRE(val1 < RandomTable::Size);
        REQUIRE(val100 >= 0);
        REQUIRE(val100 < RandomTable::Size);

        // Same index should give same value
        REQUIRE(table(0) == val0);
        REQUIRE(table(1) == val1);
    }

    SECTION("Construction with custom seed")
    {
        RandomTable table1(42);
        RandomTable table2(42);
        RandomTable table3(1337);

        // Same seed should give same values
        REQUIRE(table1(0) == table2(0));
        REQUIRE(table1(100) == table2(100));

        // Different seeds should give different values
        bool allSame = true;
        for (int i = 0; i < 10; i++)
        {
            if (table1(i) != table3(i))
            {
                allSame = false;
                break;
            }
        }
        REQUIRE_FALSE(allSame); // Different seeds should produce different tables
    }

    SECTION("Table size is 32768")
    {
        REQUIRE(RandomTable::Size == 32768);
    }

    SECTION("Index wrapping")
    {
        RandomTable table;

        // Indices beyond Size should wrap around
        int val0 = table(0);
        int valSize = table(RandomTable::Size);
        int val2Size = table(RandomTable::Size * 2);

        REQUIRE(val0 == valSize);
        REQUIRE(val0 == val2Size);

        // Negative indices should also wrap
        int valNeg = table(-1);
        REQUIRE(valNeg >= 0);
        REQUIRE(valNeg < RandomTable::Size);
    }
}

TEST_CASE("RandomTable seed combining", "[common][random][randomtable][seed]")
{
    RandomTable table;

    SECTION("Seed() with two coordinates")
    {
        int seed1 = table.Seed(10, 20);
        int seed2 = table.Seed(10, 20);

        // Same coordinates should give same seed
        REQUIRE(seed1 == seed2);

        // Seed should be in valid range
        REQUIRE(seed1 >= 0);
        REQUIRE(seed1 < RandomTable::Size);

        // Different coordinates should give different seeds
        int seed3 = table.Seed(11, 20);
        int seed4 = table.Seed(10, 21);

        REQUIRE(seed1 != seed3);
        REQUIRE(seed1 != seed4);
    }

    SECTION("Seed() with three coordinates")
    {
        int seed1 = table.Seed(10, 20, 30);
        int seed2 = table.Seed(10, 20, 30);

        // Same coordinates should give same seed
        REQUIRE(seed1 == seed2);

        // Seed should be in valid range
        REQUIRE(seed1 >= 0);
        REQUIRE(seed1 < RandomTable::Size);

        // Different coordinates should give different seeds
        int seed3 = table.Seed(10, 20, 31);

        REQUIRE(seed1 != seed3);
    }

    SECTION("SeedRef() matches Seed() for 2D")
    {
        // SeedRef is the reference C implementation, Seed is optimized
        for (int x = 0; x < 10; x++)
        {
            for (int z = 0; z < 10; z++)
            {
                int seedFast = table.Seed(x, z);
                int seedRef = table.SeedRef(x, z);

                REQUIRE(seedFast == seedRef);
            }
        }
    }
}

TEST_CASE("RandomGenerator construction", "[common][random][randomgen]")
{
    SECTION("Default construction")
    {
        RandomGenerator gen;

        // Should be able to generate values
        float val = gen.RandomValue();
        REQUIRE(val >= 0.0f);
        REQUIRE(val <= 1.0f);
    }

    SECTION("Construction with two seeds")
    {
        RandomGenerator gen1(42, 1337);
        RandomGenerator gen2(42, 1337);

        // Same seeds should produce same sequences
        float val1 = gen1.RandomValue();
        float val2 = gen2.RandomValue();

        REQUIRE(val1 == Catch::Approx(val2));
    }
}

TEST_CASE("RandomGenerator value generation", "[common][random][randomgen]")
{
    RandomGenerator gen(42, 1337);

    SECTION("RandomValue() with no args returns [0,1]")
    {
        for (int i = 0; i < 100; i++)
        {
            float val = gen.RandomValue();
            REQUIRE(val >= 0.0f);
            REQUIRE(val <= 1.0f);
        }
    }

    SECTION("RandomValue() with seed is deterministic")
    {
        float val1 = gen.RandomValue(123);
        float val2 = gen.RandomValue(123);

        REQUIRE(val1 == Catch::Approx(val2));

        float val3 = gen.RandomValue(456);
        REQUIRE(val1 != Catch::Approx(val3));
    }

    SECTION("RandomValue() with 2D coordinates")
    {
        float val1 = gen.RandomValue(10, 20);
        float val2 = gen.RandomValue(10, 20);

        // Same coordinates should give same value
        REQUIRE(val1 == Catch::Approx(val2));

        // Value should be in [0,1]
        REQUIRE(val1 >= 0.0f);
        REQUIRE(val1 <= 1.0f);

        // Different coordinates should give different values
        float val3 = gen.RandomValue(11, 20);
        REQUIRE(val1 != Catch::Approx(val3));
    }

    SECTION("RandomValue() with 3D coordinates")
    {
        float val1 = gen.RandomValue(10, 20, 30);
        float val2 = gen.RandomValue(10, 20, 30);

        // Same coordinates should give same value
        REQUIRE(val1 == Catch::Approx(val2));

        // Value should be in [0,1]
        REQUIRE(val1 >= 0.0f);
        REQUIRE(val1 <= 1.0f);

        // Different coordinates should give different values
        float val3 = gen.RandomValue(10, 20, 31);
        REQUIRE(val1 != Catch::Approx(val3));
    }
}

TEST_CASE("RandomGenerator seed management", "[common][random][randomgen][seed]")
{
    RandomGenerator gen(42, 1337);

    SECTION("SetSeed() affects RandomValue()")
    {
        gen.SetSeed(42);
        float val1 = gen.RandomValue();

        gen.SetSeed(42);
        float val2 = gen.RandomValue();

        REQUIRE(val1 == Catch::Approx(val2));

        gen.SetSeed(1337);
        float val3 = gen.RandomValue();

        // Different seed should give different value (high probability)
        REQUIRE(val1 != Catch::Approx(val3));
    }

    SECTION("GetSeed() combines coordinates")
    {
        int seed1 = gen.GetSeed(10, 20);
        int seed2 = gen.GetSeed(10, 20);

        REQUIRE(seed1 == seed2);

        int seed3 = gen.GetSeed(11, 20);
        REQUIRE(seed1 != seed3);
    }

    SECTION("GetSeedRef() matches GetSeed()")
    {
        for (int x = 0; x < 5; x++)
        {
            for (int z = 0; z < 5; z++)
            {
                int seedFast = gen.GetSeed(x, z);
                int seedRef = gen.GetSeedRef(x, z);

                REQUIRE(seedFast == seedRef);
            }
        }
    }
}

TEST_CASE("RandomGenerator distribution functions", "[common][random][randomgen][distribution]")
{
    RandomGenerator gen(42, 1337);

    SECTION("Gauss() returns values in range")
    {
        float min = 0.0f;
        float mid = 5.0f;
        float max = 10.0f;

        for (int i = 0; i < 100; i++)
        {
            float val = gen.Gauss(min, mid, max);

            REQUIRE(val >= min);
            REQUIRE(val <= max);
        }
    }

    SECTION("Gauss() distribution is centered around mid")
    {
        float min = 0.0f;
        float mid = 5.0f;
        float max = 10.0f;

        // Generate many samples
        float sum = 0.0f;
        int count = 1000;
        for (int i = 0; i < count; i++)
        {
            sum += gen.Gauss(min, mid, max);
        }

        float average = sum / count;

        // Average should be close to mid (within 20%)
        REQUIRE(average == Catch::Approx(mid).margin(2.0f));
    }

    SECTION("PlusMinus() returns values in range")
    {
        float a = 5.0f;
        float b = 2.0f;

        for (int i = 0; i < 100; i++)
        {
            float val = gen.PlusMinus(a, b);

            REQUIRE(val >= a - b);
            REQUIRE(val <= a + b);
        }
    }

    SECTION("PlusMinus() is centered around a")
    {
        float a = 5.0f;
        float b = 2.0f;

        // Generate many samples
        float sum = 0.0f;
        int count = 1000;
        for (int i = 0; i < count; i++)
        {
            sum += gen.PlusMinus(a, b);
        }

        float average = sum / count;

        // Average should be close to a (within 10%)
        REQUIRE(average == Catch::Approx(a).margin(0.5f));
    }
}

TEST_CASE("RandomGenerator common usage patterns", "[common][random][randomgen][usage]")
{
    RandomGenerator gen(1234, 5678);

    SECTION("Generate terrain noise")
    {
        // Typical usage: generate height values for terrain
        float height1 = gen.RandomValue(0, 0) * 10.0f; // Height at (0,0)
        float height2 = gen.RandomValue(1, 0) * 10.0f; // Height at (1,0)

        // Heights should be in valid range
        REQUIRE(height1 >= 0.0f);
        REQUIRE(height1 <= 10.0f);
        REQUIRE(height2 >= 0.0f);
        REQUIRE(height2 <= 10.0f);

        // Same position should give same height
        float height1Again = gen.RandomValue(0, 0) * 10.0f;
        REQUIRE(height1 == Catch::Approx(height1Again));
    }

    SECTION("Generate random spawn position")
    {
        // Generate position with some offset
        float offsetX = gen.PlusMinus(0.0f, 10.0f);
        float offsetY = gen.PlusMinus(0.0f, 10.0f);

        REQUIRE(offsetX >= -10.0f);
        REQUIRE(offsetX <= 10.0f);
        REQUIRE(offsetY >= -10.0f);
        REQUIRE(offsetY <= 10.0f);
    }

    SECTION("Generate random unit properties")
    {
        // Health variation: 80-120 HP
        float health = gen.PlusMinus(100.0f, 20.0f);
        REQUIRE(health >= 80.0f);
        REQUIRE(health <= 120.0f);

        // Speed variation: Gaussian distribution around 5 m/s
        float speed = gen.Gauss(4.0f, 5.0f, 6.0f);
        REQUIRE(speed >= 4.0f);
        REQUIRE(speed <= 6.0f);
    }
}

TEST_CASE("Global random generator", "[common][random][randomgen][global]")
{
    SECTION("GRandGen is accessible")
    {
        float val = GRandGen.RandomValue();

        REQUIRE(val >= 0.0f);
        REQUIRE(val <= 1.0f);
    }

    SECTION("GRandGen can be seeded")
    {
        GRandGen.SetSeed(42);
        float val1 = GRandGen.RandomValue();

        GRandGen.SetSeed(42);
        float val2 = GRandGen.RandomValue();

        REQUIRE(val1 == Catch::Approx(val2));
    }
}

TEST_CASE("RandomGenerator statistical properties", "[common][random][randomgen][stats]")
{
    RandomGenerator gen(9876, 5432);

    SECTION("RandomValue() has uniform distribution")
    {
        // Divide [0,1] into 10 buckets
        int buckets[10] = {0};
        int samples = 10000;

        for (int i = 0; i < samples; i++)
        {
            float val = gen.RandomValue();
            int bucket = (int)(val * 10.0f);
            if (bucket >= 10)
            {
                bucket = 9; // Handle edge case val=1.0
            }
            buckets[bucket]++;
        }

        // Each bucket should have roughly samples/10 entries
        int expected = samples / 10;
        for (int i = 0; i < 10; i++)
        {
            // Allow 20% deviation
            REQUIRE(buckets[i] == Catch::Approx(expected).margin(expected * 0.2));
        }
    }

    SECTION("Spatial randomValue() has good distribution")
    {
        // Generate random values for a grid
        std::set<float> uniqueValues;

        for (int x = 0; x < 10; x++)
        {
            for (int z = 0; z < 10; z++)
            {
                float val = gen.RandomValue(x, z);
                uniqueValues.insert(val);
            }
        }

        // Should have mostly unique values (at least 95 out of 100)
        REQUIRE(uniqueValues.size() >= 95);
    }
}
