#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <climits>
#include <vector>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Network/NetworkFileTransfer.hpp>
#include <Poseidon/Network/WireBounds.hpp>

using namespace Poseidon;

// Decode-check primitives for the table-driven dispatcher. These are the seam
// migrated message handlers route wire-read counts/lengths/indices through,
// replacing the per-handler "trust the wire then Resize/memcpy/arr[i]" pattern.
// Each predicate is tested at its boundary, where malformed input is caught.

TEST_CASE("WireBounds::ValidCount caps and rejects negatives", "[network][dispatch][wirebounds]")
{
    REQUIRE(WireBounds::ValidCount(0, 100));
    REQUIRE(WireBounds::ValidCount(100, 100));
    REQUIRE_FALSE(WireBounds::ValidCount(101, 100));
    REQUIRE_FALSE(WireBounds::ValidCount(-1, 100));
    REQUIRE_FALSE(WireBounds::ValidCount(INT_MIN, 100));
}

TEST_CASE("WireBounds::MulFitsInt rejects overflowing count*size", "[network][dispatch][wirebounds]")
{
    REQUIRE(WireBounds::MulFitsInt(1000, 16));
    REQUIRE(WireBounds::MulFitsInt(0, 16));
    // An unchecked `count*size` can wrap to a small or negative int, which would
    // under-allocate a later Resize.
    REQUIRE_FALSE(WireBounds::MulFitsInt(INT_MAX, 16));
    REQUIRE_FALSE(WireBounds::MulFitsInt(INT_MAX / 4 + 1, 4));
    REQUIRE_FALSE(WireBounds::MulFitsInt(-1, 16));
    REQUIRE_FALSE(WireBounds::MulFitsInt(10, 0));
    // exactly at the boundary fits.
    REQUIRE(WireBounds::MulFitsInt(INT_MAX / 4, 4));
}

TEST_CASE("WireBounds::RangeInBounds guards offset+span vs size", "[network][dispatch][wirebounds]")
{
    REQUIRE(WireBounds::RangeInBounds(0, 10, 10));
    REQUIRE(WireBounds::RangeInBounds(5, 5, 10));
    REQUIRE_FALSE(WireBounds::RangeInBounds(6, 5, 10));
    REQUIRE_FALSE(WireBounds::RangeInBounds(-1, 5, 10));
    REQUIRE_FALSE(WireBounds::RangeInBounds(0, -1, 10));
    // offset+span must not overflow into a false pass.
    REQUIRE_FALSE(WireBounds::RangeInBounds(INT_MAX, INT_MAX, 10));
}

TEST_CASE("WireBounds::ValidIdentifier accepts well-formed names, rejects junk", "[network][dispatch][wirebounds]")
{
    // A wire-supplied name must be a bounded identifier before it is used as a
    // script variable name.
    REQUIRE(WireBounds::ValidIdentifier("myVar", 256));
    REQUIRE(WireBounds::ValidIdentifier("_x", 256));
    REQUIRE(WireBounds::ValidIdentifier("a1_b2", 256));
    REQUIRE(WireBounds::ValidIdentifier("A", 256));

    REQUIRE_FALSE(WireBounds::ValidIdentifier(nullptr, 256));  // null
    REQUIRE_FALSE(WireBounds::ValidIdentifier("", 256));       // empty
    REQUIRE_FALSE(WireBounds::ValidIdentifier("1abc", 256));   // leading digit
    REQUIRE_FALSE(WireBounds::ValidIdentifier("a b", 256));    // space
    REQUIRE_FALSE(WireBounds::ValidIdentifier("a;drop", 256)); // punctuation
    REQUIRE_FALSE(WireBounds::ValidIdentifier("a.b", 256));    // dot

    // Length bound: a 10-char name with maxLen 8 is rejected; within bound passes.
    REQUIRE_FALSE(WireBounds::ValidIdentifier("abcdefghij", 8));
    REQUIRE(WireBounds::ValidIdentifier("abcdefgh", 8));
}

TEST_CASE("AutoArray::AtOrNull bounds-checks in release builds", "[foundation][containers][wirebounds]")
{
    AutoArray<int> a;
    a.Add(10);
    a.Add(20);
    a.Add(30);

    REQUIRE(a.AtOrNull(0) != nullptr);
    REQUIRE(*a.AtOrNull(0) == 10);
    REQUIRE(*a.AtOrNull(2) == 30);

    // operator[] (AssertDebug) does not bounds-check under NDEBUG; AtOrNull returns
    // nullptr for the same out-of-range index instead.
    REQUIRE(a.AtOrNull(3) == nullptr);
    REQUIRE(a.AtOrNull(-1) == nullptr);
    REQUIRE(a.AtOrNull(INT_MAX) == nullptr);

    *a.AtOrNull(1) = 99;
    REQUIRE(a[1] == 99);

    const AutoArray<int>& ca = a;
    REQUIRE(ca.AtOrNull(1) != nullptr);
    REQUIRE(*ca.AtOrNull(1) == 99);
    REQUIRE(ca.AtOrNull(5) == nullptr);
}

TEST_CASE("file transfer segments advance offsets for multi-segment payloads", "[network][file-transfer]")
{
    struct TestTransferFileMessage
    {
        RString path;
        AutoArray<char> data;
        int totSize = 0;
        int offset = 0;
        int totSegments = 0;
        int curSegment = 0;
    };

    constexpr int payload = NetworkFileTransferSegmentSize;
    const int totalSize = payload * 2 + 17;
    std::vector<char> data(totalSize);
    std::vector<int> offsets;
    std::vector<int> sizes;
    std::vector<int> indices;
    std::vector<char> reconstructed(totalSize);

    const int segmentCount = SendNetworkFileTransferSegments<TestTransferFileMessage>(
        RString("tmp/players/player/face.jpg"), data.data(), static_cast<int>(data.size()),
        [&](const TestTransferFileMessage& msg)
        {
            REQUIRE(msg.totSize == totalSize);
            REQUIRE(msg.totSegments == 3);
            offsets.push_back(msg.offset);
            sizes.push_back(msg.data.Size());
            indices.push_back(msg.curSegment);
            std::copy(msg.data.Data(), msg.data.Data() + msg.data.Size(), reconstructed.begin() + msg.offset);
        });

    REQUIRE(segmentCount == 3);
    REQUIRE(indices == std::vector<int>{0, 1, 2});
    REQUIRE(offsets == std::vector<int>{0, payload, payload * 2});
    REQUIRE(sizes == std::vector<int>{payload, payload, 17});
    REQUIRE(reconstructed == data);
}

TEST_CASE("file transfer duplicate receives do not inflate progress", "[network][file-transfer]")
{
    bool received = false;
    int receivedBytes = 0;
    int remainingSegments = 2;

    REQUIRE_FALSE(ApplyReceivedNetworkFileTransferSegment(received, 1024, receivedBytes, remainingSegments));
    REQUIRE(received);
    REQUIRE(receivedBytes == 1024);
    REQUIRE(remainingSegments == 1);

    REQUIRE_FALSE(ApplyReceivedNetworkFileTransferSegment(received, 1024, receivedBytes, remainingSegments));
    REQUIRE(receivedBytes == 1024);
    REQUIRE(remainingSegments == 1);

    bool finalSegmentReceived = false;
    REQUIRE(ApplyReceivedNetworkFileTransferSegment(finalSegmentReceived, 17, receivedBytes, remainingSegments));
    REQUIRE(receivedBytes == 1041);
    REQUIRE(remainingSegments == 0);
}
