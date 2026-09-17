#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <new>
#include <Poseidon/Graphics/Dummy/TextureDummy.hpp>

using Poseidon::TextureDummy;

TEST_CASE("TextureDummy reports a defined size before a source is loaded", "[graphics][texture]")
{
    // Mipmap selection reads AWidth/AHeight, and Init leaves them untouched when the
    // texture source cannot be created, so construction has to define them.
    alignas(TextureDummy) unsigned char storage[sizeof(TextureDummy)];
    memset(storage, 0xBE, sizeof(storage));
    TextureDummy* texture = new (storage) TextureDummy();

    REQUIRE(texture->AWidth(0) == 0);
    REQUIRE(texture->AHeight(0) == 0);

    texture->~TextureDummy();
}
