#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Network/NetworkMessages.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

using namespace Poseidon;

// MP chat carries text as an NDTString field (ChatMessage::text),
// serialized with NCTNone — raw bytes + NUL on write, bytes-until-NUL on read.
// For a player to see a single line mixing Cyrillic, Czech and German, those
// high-bit UTF-8 multibyte sequences must round-trip the wire byte-identical:
// no 7-bit narrowing, no codepage transcode, no length truncation.
//
// Broken-state delta: a 7-bit or codepage-narrowing string serializer mangles
// every byte >= 0x80, so the Cyrillic letters and the umlauts come back wrong
// and the round-trip REQUIRE fails. (Byte count, not codepoint count, also
// guards a length field that counts codepoints and under-reads the tail.)

TEST_CASE("chat text round-trips UTF-8 over the wire (NCTNone)", "[network][chat][utf8]")
{
    // "Привет Čau Grüße" — Cyrillic + Czech + German in one string. The "\x" hex
    // escapes are split with string concatenation so a following hex/letter byte
    // is not swallowed into the escape.
    const RString original = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 " // "Привет "
                             "\xC4\x8C"
                             "au " // "Čau "
                             "Gr\xC3\xBC\xC3\x9F"
                             "e"; // "Grüße"

    // sanity: this really is a multibyte string with high-bit bytes.
    REQUIRE(original.GetLength() > 16);
    bool hasHighBit = false;
    for (int i = 0; i < original.GetLength(); i++)
        if ((unsigned char)((const char*)original)[i] >= 0x80)
            hasHighBit = true;
    REQUIRE(hasHighBit);

    NetworkMessageRaw writer;
    writer.Put(original, NCTNone);
    const int written = writer.GetPos();
    // bytes + NUL — byte count, not codepoint count.
    REQUIRE(written == original.GetLength() + 1);

    NetworkMessageRaw reader(writer.SetData(), written);
    RString out;
    REQUIRE(reader.Get(out, NCTNone));

    REQUIRE(out == original); // byte-identical
    REQUIRE(out.GetLength() == original.GetLength());
}
