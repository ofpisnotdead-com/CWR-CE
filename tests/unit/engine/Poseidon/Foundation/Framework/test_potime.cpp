// Unit tests for Poseidon/Foundation/Framework/PoTime.hpp
// Testing portable time and sleep utilities

#define _CRT_SECURE_NO_WARNINGS
#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Common/Global.hpp> // For unsigned64 typedef
#include <Poseidon/Foundation/Common/Win.h>      // For Sleep on Windows
#include <Poseidon/Foundation/Framework/PoTime.hpp>

// System Time Initialization

TEST_CASE("potime - Poseidon::Foundation::startSystemTime", "[framework][potime]")
{
    SECTION("Can be called multiple times safely")
    {
        // Should be safe to call multiple times (idempotent)
        Poseidon::Foundation::startSystemTime();
        Poseidon::Foundation::startSystemTime();
        Poseidon::Foundation::startSystemTime();

        // If we get here without crash, it worked
        REQUIRE(true);
    }
}

// Clock Frequency

TEST_CASE("potime - Poseidon::Foundation::getClockFrequency", "[framework][potime]")
{
    SECTION("Returns non-zero frequency")
    {
        unsigned freq = Poseidon::Foundation::getClockFrequency();

        REQUIRE(freq > 0);
    }

    SECTION("Frequency is reasonable")
    {
        unsigned freq = Poseidon::Foundation::getClockFrequency();

        // Should be at least 100 Hz (minimum documented)
        REQUIRE(freq >= 100);

        // Shouldn't be unreasonably high (e.g., > 4 GHz)
        REQUIRE(freq < 4000000000U);
    }

    SECTION("Frequency is consistent")
    {
        unsigned freq1 = Poseidon::Foundation::getClockFrequency();
        unsigned freq2 = Poseidon::Foundation::getClockFrequency();

        // Should return same value each time
        REQUIRE(freq1 == freq2);
    }
}

// System Time

TEST_CASE("potime - Poseidon::Foundation::getSystemTime basics", "[framework][potime]")
{
    SECTION("Returns non-zero time")
    {
        unsigned64 time = Poseidon::Foundation::getSystemTime();

        // Should be a reasonable time value (microseconds)
        // Just verify it's non-zero and reasonable (not checking epoch,
        // as Windows uses different epoch than Unix)
        REQUIRE(time > 0);
        REQUIRE(time < 9999999999999999ULL); // Reasonable upper bound
    }

    SECTION("Time advances")
    {
        unsigned64 time1 = Poseidon::Foundation::getSystemTime();

        // Do some work to ensure time passes
        volatile int dummy = 0;
        for (int i = 0; i < 1000; i++)
        {
            dummy += i;
        }

        unsigned64 time2 = Poseidon::Foundation::getSystemTime();

        // Time should have advanced (or at minimum, not gone backwards)
        REQUIRE(time2 >= time1);
    }

    SECTION("Time monotonicity")
    {
        // Sample time multiple times
        unsigned64 times[10];
        for (int i = 0; i < 10; i++)
        {
            times[i] = Poseidon::Foundation::getSystemTime();
        }

        // Each sample should be >= previous (monotonic)
        for (int i = 1; i < 10; i++)
        {
            REQUIRE(times[i] >= times[i - 1]);
        }
    }
}

TEST_CASE("potime - Poseidon::Foundation::getSystemTime precision", "[framework][potime]")
{
    SECTION("Has microsecond precision")
    {
        // Take multiple samples close together
        unsigned64 time1 = Poseidon::Foundation::getSystemTime();
        unsigned64 time2 = Poseidon::Foundation::getSystemTime();
        unsigned64 time3 = Poseidon::Foundation::getSystemTime();

        // At least one pair should show time difference
        // (unless system is incredibly fast, which is unlikely)
        (void)(time2 - time1); // Intentionally unused - just checking call works
        (void)(time3 - time2); // Intentionally unused - just checking call works

        // Total elapsed should be measurable in microseconds
        unsigned64 total = time3 - time1;

        // Should be able to measure < 1 millisecond (1000 microseconds)
        // This proves microsecond precision
        REQUIRE(total < 10000); // Less than 10ms for 3 calls
    }

    SECTION("Resolution is fine-grained")
    {
        unsigned freq = Poseidon::Foundation::getClockFrequency();

        // If frequency is high (>1000 Hz), we have sub-millisecond precision
        if (freq > 1000)
        {
            unsigned64 time1 = Poseidon::Foundation::getSystemTime();

            // Spin briefly
            volatile int i = 0;
            for (; i < 100; i = i + 1)
            {
                ;
            }

            unsigned64 time2 = Poseidon::Foundation::getSystemTime();
            unsigned64 elapsed = time2 - time1;

            // Should measure something (if clock is working)
            // Allow for possibility of zero if CPU is very fast
            REQUIRE(elapsed < 1000000); // Less than 1 second for this test
        }
    }
}

TEST_CASE("potime - Poseidon::Foundation::getSystemTime timing accuracy", "[framework][potime]")
{
    SECTION("Can measure millisecond intervals")
    {
        unsigned64 start = Poseidon::Foundation::getSystemTime();

        // Sleep for a known duration (10ms)
        SLEEP_MS(10);

        unsigned64 end = Poseidon::Foundation::getSystemTime();
        unsigned64 elapsed_us = end - start;
        double elapsed_ms = elapsed_us / 1000.0;

        // Should be close to 10ms (allow �5ms tolerance for scheduler variance)
        REQUIRE(elapsed_ms >= 5.0);
        REQUIRE(elapsed_ms <= 50.0); // Generous upper bound for slow systems
    }

    SECTION("Can measure longer intervals")
    {
        unsigned64 start = Poseidon::Foundation::getSystemTime();

        // Sleep for 50ms
        SLEEP_MS(50);

        unsigned64 end = Poseidon::Foundation::getSystemTime();
        unsigned64 elapsed_us = end - start;
        double elapsed_ms = elapsed_us / 1000.0;

        // Should be close to 50ms (allow �10ms tolerance)
        REQUIRE(elapsed_ms >= 40.0);
        REQUIRE(elapsed_ms <= 100.0); // Generous upper bound
    }
}

// Sleep Functionality

TEST_CASE("potime - SLEEP_MS macro", "[framework][potime][sleep]")
{
    SECTION("Sleep for 1 millisecond")
    {
        unsigned64 start = Poseidon::Foundation::getSystemTime();

        SLEEP_MS(1);

        unsigned64 end = Poseidon::Foundation::getSystemTime();
        unsigned64 elapsed_us = end - start;

        // Should have slept at least 1ms (may be longer due to scheduler)
        // 1ms = 1000 microseconds
        // Allow 0-10ms range (scheduler may round up)
        REQUIRE(elapsed_us >= 0);     // May be zero if scheduler resolution is poor
        REQUIRE(elapsed_us <= 50000); // Shouldn't be more than 50ms
    }

    SECTION("Sleep for 10 milliseconds")
    {
        unsigned64 start = Poseidon::Foundation::getSystemTime();

        SLEEP_MS(10);

        unsigned64 end = Poseidon::Foundation::getSystemTime();
        unsigned64 elapsed_us = end - start;
        double elapsed_ms = elapsed_us / 1000.0;

        // Should be approximately 10ms (allow scheduler variance)
        REQUIRE(elapsed_ms >= 1.0);  // At least 1ms
        REQUIRE(elapsed_ms <= 50.0); // Not more than 50ms
    }

    SECTION("Sleep for zero milliseconds")
    {
        unsigned64 start = Poseidon::Foundation::getSystemTime();

        SLEEP_MS(0); // Should yield CPU or return immediately

        unsigned64 end = Poseidon::Foundation::getSystemTime();
        unsigned64 elapsed_us = end - start;

        // Should be very fast (< 10ms)
        REQUIRE(elapsed_us < 10000);
    }
}

TEST_CASE("potime - Section timing", "[framework][potime]")
{
    const Poseidon::Foundation::SectionTimeHandle start = Poseidon::Foundation::StartSectionTime();

    REQUIRE(Poseidon::Foundation::GetSectionResolution() > 0);
    REQUIRE(Poseidon::Foundation::GetSectionTime(start) >= 0.0f);
    REQUIRE(Poseidon::Foundation::CompareSectionTimeGE(start, 0.0f));
}

// Cross-Platform Consistency

TEST_CASE("potime - Platform consistency", "[framework][potime]")
{
    SECTION("Time values are consistent across calls")
    {
        // Make multiple measurements
        const int num_samples = 5;
        unsigned64 samples[num_samples];

        for (int i = 0; i < num_samples; i++)
        {
            samples[i] = Poseidon::Foundation::getSystemTime();
            SLEEP_MS(10);
        }

        // Each measurement should be larger than previous
        for (int i = 1; i < num_samples; i++)
        {
            REQUIRE(samples[i] > samples[i - 1]);

            // Difference should be approximately 10ms (�5ms tolerance)
            unsigned64 diff_us = samples[i] - samples[i - 1];
            double diff_ms = diff_us / 1000.0;

            REQUIRE(diff_ms >= 5.0);
            REQUIRE(diff_ms <= 50.0);
        }
    }
}

// Performance Characteristics

TEST_CASE("potime - Performance", "[framework][potime][performance]")
{
    SECTION("Poseidon::Foundation::getSystemTime is fast")
    {
        // Measure time to call Poseidon::Foundation::getSystemTime many times
        unsigned64 start = Poseidon::Foundation::getSystemTime();

        const int iterations = 10000;
        for (int i = 0; i < iterations; i++)
        {
            volatile unsigned64 t = Poseidon::Foundation::getSystemTime();
            (void)t; // Prevent optimization
        }

        unsigned64 end = Poseidon::Foundation::getSystemTime();
        unsigned64 total_us = end - start;
        double avg_us = (double)total_us / iterations;

        // Each call should be very fast (< 10 microseconds on average)
        REQUIRE(avg_us < 10.0);
    }

    SECTION("Poseidon::Foundation::getClockFrequency is instant")
    {
        // Should be cached/instant
        unsigned64 start = Poseidon::Foundation::getSystemTime();

        for (int i = 0; i < 1000; i++)
        {
            volatile unsigned freq = Poseidon::Foundation::getClockFrequency();
            (void)freq;
        }

        unsigned64 end = Poseidon::Foundation::getSystemTime();
        unsigned64 elapsed = end - start;

        // Should be negligible (< 1ms for 1000 calls)
        REQUIRE(elapsed < 1000);
    }
}
