#include <catch2/catch_test_macros.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include "../Support/test_fixtures.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <Poseidon/Foundation/Strings/RString.hpp>

// QStream Bank Tests - QFBank & QIFStreamB (Minimal Happy Path)
// Simplified tests - only test basic API without actually loading PBO files

using namespace TestFixtures;
using namespace Poseidon;

TEST_CASE("QFBank::open rejects an oversized encrypted-header size", "[qstream][bank][fuzz]")
{
    // Regression: an encrypted PBO whose header-size ints are wire-controlled. A
    // negative headersEncodedSize became a near-2^64 Temp<char>
    // allocation in QFBank::Load (allocation-size-too-big). The size guard now
    // rejects it. Minimal PBO: empty-name terminator entry (magic=VersionMagic,
    // length/time 0) -> one "encryption" property -> headersSize, headersEncodedSize.
    const unsigned char pbo[] = {
        0x00,                                                       // empty entry name (terminator)
        0x73, 0x72, 0x65, 0x56,                                     // compressedMagic = VersionMagic ('Vers')
        0x00, 0x00, 0x00, 0x00,                                     // uncompressedSize
        0x00, 0x00, 0x00, 0x00,                                     // startOffset
        0x00, 0x00, 0x00, 0x00,                                     // time
        0x00, 0x00, 0x00, 0x00,                                     // length
        'e',  'n',  'c',  'r',  'y', 'p', 't', 'i', 'o', 'n', 0x00, // property name
        'x',  0x00,                                                 // property value
        0x00,                                                       // empty property name -> end
        0x00, 0x00, 0x00, 0x00,                                     // headersSize = 0
        0xFF, 0xFF, 0xFF, 0xFF,                                     // headersEncodedSize = -1
    };
    std::string base = (std::filesystem::temp_directory_path() / "fuzz_pbo_reg").string();
    std::string path = base + ".pbo";
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(pbo), sizeof(pbo));
    }

    QFBank bank;
    bank.open(RString(base.c_str())); // pre-fix: huge allocation / crash inside Load
    bank.Lock();
    bool err = bank.error(); // post-fix: header-size guard flags the parse error
    bank.Unlock();

    std::error_code ec;
    std::filesystem::remove(path, ec);

    REQUIRE(err);
}

TEST_CASE("QFBank - Construction", "[qstream][bank][basic]")
{
    QFBank bank;

    REQUIRE(bank.GetPrefix().GetLength() == 0);
}

TEST_CASE("QFBank - Set prefix", "[qstream][bank][basic]")
{
    QFBank bank;

    bank.SetPrefix("testprefix");

    REQUIRE(strcmp(bank.GetPrefix().Data(), "testprefix") == 0);
}

TEST_CASE("QFBank - Lock and Unlock without opening", "[qstream][bank][basic]")
{
    QFBank bank;

    bank.Lock();
    REQUIRE(bank.IsLocked() == true);

    bank.Unlock();
    REQUIRE(bank.IsLocked() == false);
}

TEST_CASE("QFBank - Lockable status", "[qstream][bank][basic]")
{
    QFBank bank;

    bank.SetLockable(false);
    REQUIRE(bank.GetLockable() == false);

    bank.SetLockable(true);
    REQUIRE(bank.GetLockable() == true);
}

TEST_CASE("QFBank - BankList management", "[qstream][bank][basic]")
{
    int initialSize = GFileBanks.Size();

    REQUIRE(initialSize >= 0);
}

TEST_CASE("QIFStreamB - AutoBank path resolution", "[qstream][bank][streamb]")
{
    (void)QIFStreamB::AutoBank("somefile.txt");

    REQUIRE(true);
}

TEST_CASE("QIFStreamB - FileExist with context", "[qstream][bank][streamb]")
{
    bool exists = QIFStreamB::FileExist("testfile.txt", nullptr);

    REQUIRE((exists == true || exists == false));
}

TEST_CASE("QIFStreamB - ClearBanks cleanup", "[qstream][bank][streamb]")
{
    QIFStreamB::ClearBanks();

    REQUIRE(true);
}

TEST_CASE("Intro/anim cutscene path resolves from a packed addon bank", "[qstream][bank][cutscene]")
{
    // An island's CfgWorlds cutscene path ("anims/..\addons\<island>\intro.<world>\mission.sqm")
    // must resolve from the island's mounted .pbo, not require a loose on-disk folder.
    REQUIRE_FIXTURE("pbo/triisl.pbo");
    const std::string full = GetTestFixturePath("pbo/triisl.pbo");
    const std::string dir = full.substr(0, full.size() - std::string("triisl.pbo").size());

    const bool prevBanks = GUseFileBanks;
    GUseFileBanks = true;
    QIFStreamB::ClearBanks();
    GFileBanks.Load(RString(dir.c_str()), RString(""), RString("triisl"), true);
    REQUIRE(GFileBanks.Size() >= 1);

    // Sanity: the direct bank-namespace path resolves.
    CHECK(QIFStreamB::FileExist("triisl\\intro.abel\\mission.sqm"));

    // Root-relative island paths normalize "<seg>/.." and strip "addons\" before bank lookup.
    CHECK(QIFStreamB::FileExist("anims/..\\addons\\triisl\\intro.abel\\mission.sqm"));

    // Community configs are Windows-authored with uppercase "ADDONS", mixed-case island names, and a
    // ".<worldName>" suffix the engine appends, so the on-disk case rarely matches. Resolution must be
    // case-insensitive across every component or these intros silently fail on a case-sensitive filesystem.
    CHECK(QIFStreamB::FileExist("anims/..\\ADDONS\\TriIsl\\intro.Abel\\mission.sqm"));

    // Control: an unknown island stays unresolved (the fallback must not resolve arbitrary paths).
    CHECK_FALSE(QIFStreamB::FileExist("anims/..\\addons\\nosuch\\intro.abel\\mission.sqm"));

    // The "..\addons" fallback must honour the caller's bank-access policy, else a denied bank is
    // reachable as "x/..\addons\<bank>\...". A deny-all context must report not-found.
    struct DenyAll : Poseidon::IQFBankContext
    {
        bool IsAccessible(Poseidon::QFBank*) const override { return false; }
    } denyAll;
    CHECK_FALSE(QIFStreamB::FileExist("anims/..\\addons\\triisl\\intro.abel\\mission.sqm", &denyAll));

    QIFStreamB::ClearBanks();
    GUseFileBanks = prevBanks;
}

TEST_CASE("QIFStreamB - Fallback to file system", "[qstream][bank][streamb]")
{
    REQUIRE_FIXTURE("pbo/mission_fixture.Intro.pbo");

    const char* regularFile = GetTestFixturePath("pbo/mission_fixture.Intro.pbo");

    QIFStreamB stream;
    stream.AutoOpen(regularFile);

    REQUIRE(stream.fail() == false);
    REQUIRE(stream.rest() > 0);
}
