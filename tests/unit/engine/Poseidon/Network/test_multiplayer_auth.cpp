// MachineIdToPlayerId - the non-Steam multiplayer identity hash.
// The client sends this decimal id as identity.id; the server round-trips it
// through _atoi64 and the decimal ban.txt, so the value must be deterministic,
// positive, fit a signed int64, dodge the 0/999 sentinels, and be insensitive to
// the trailing newline that /etc/machine-id carries.
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Network/MultiplayerAuth.hpp>
#include <cstdint>

using Poseidon::MachineIdToPlayerId;

TEST_CASE("MachineIdToPlayerId - empty / null / whitespace -> 0", "[network][mp-id]")
{
    REQUIRE(MachineIdToPlayerId(nullptr) == 0);
    REQUIRE(MachineIdToPlayerId("") == 0);
    REQUIRE(MachineIdToPlayerId("   \t\r\n") == 0);
}

TEST_CASE("MachineIdToPlayerId - deterministic and well-formed", "[network][mp-id]")
{
    const char* guid = "0123456789abcdef0123456789abcdef";
    const uint64_t a = MachineIdToPlayerId(guid);
    const uint64_t b = MachineIdToPlayerId(guid);

    REQUIRE(a == b);                     // stable across calls
    REQUIRE(a != 0);                     // a real machine id -> a real player id
    REQUIRE(a != 999);                   // dodges the server sentinels
    REQUIRE(a <= 0x7FFFFFFFFFFFFFFFULL); // fits a positive signed int64 (_atoi64 / ban.txt)
}

TEST_CASE("MachineIdToPlayerId - trims trailing newline like /etc/machine-id", "[network][mp-id]")
{
    // /etc/machine-id is written with a trailing '\n'; the id must not depend on it.
    REQUIRE(MachineIdToPlayerId("0123456789abcdef0123456789abcdef") ==
            MachineIdToPlayerId("0123456789abcdef0123456789abcdef\n"));
    REQUIRE(MachineIdToPlayerId("  abc  ") == MachineIdToPlayerId("abc"));
}

TEST_CASE("MachineIdToPlayerId - distinct machines get distinct ids", "[network][mp-id]")
{
    REQUIRE(MachineIdToPlayerId("0123456789abcdef0123456789abcdef") !=
            MachineIdToPlayerId("fedcba9876543210fedcba9876543210"));
    REQUIRE(MachineIdToPlayerId("deadbeef") != MachineIdToPlayerId("deadbeee"));
}

TEST_CASE("MachineIdToPlayerId - squad insignia fixture ids stay aligned", "[network][mp-id]")
{
    REQUIRE(MachineIdToPlayerId("trident:squad_insignia_https:client1") == 2227712454512799032ULL);
    REQUIRE(MachineIdToPlayerId("trident:squad_insignia_https:client2") == 2227715753047683665ULL);
}
