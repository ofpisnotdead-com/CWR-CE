#include <Poseidon/Graphics/Rendering/Draw/FontSystem.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontMapping.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <cstdlib>
#include <Poseidon/Foundation/Framework/Log.hpp>

namespace Poseidon
{

namespace
{
// Slot-0 (the default OFL set) is the contract for "fonts are present".
// Cycling to another slot at runtime through the dev hotkey still goes
// through the existing lazy renderer-cache; if a target-slot TTF is missing
// the cycle falls back to slot 0.  Init only locks in the slot-0 set so
// new boots fail fast when the artist-default fonts are absent.
constexpr int kCanonicalSlot = 0;

// Mirrors `font.cpp`'s s_slot0 table.  The unit test
// `FontSystem matches font.cpp's slot-0 mapping` pins the two together.
const char* const kSlot0FontPaths[] = {
    "Fonts\\cwr_title.ttf", "Fonts\\cwr_body.ttf", "Fonts\\cwr_mono.ttf", "Fonts\\cwr_serif.ttf", "Fonts\\cwr_hand.ttf",
};
} // namespace

FontSystem& FontSystem::Instance()
{
    static FontSystem instance;
    return instance;
}

std::vector<std::string> FontSystem::RequiredFonts()
{
    std::vector<std::string> paths;
    paths.reserve(sizeof(kSlot0FontPaths) / sizeof(kSlot0FontPaths[0]));
    for (const char* path : kSlot0FontPaths)
        paths.emplace_back(path);
    return paths;
}

std::vector<std::string> FontSystem::RequiredFontsMissing()
{
    std::vector<std::string> missing;
    for (const char* path : kSlot0FontPaths)
    {
        QIFStreamB stream;
        stream.AutoOpen(path);
        if (stream.rest() <= 0)
            missing.emplace_back(path);
    }
    return missing;
}

void FontSystem::Initialize()
{
    if (_initialized)
        return;

    auto missing = RequiredFontsMissing();
    if (!missing.empty())
    {
        for (const auto& path : missing)
            LOG_ERROR(Graphics, "FontSystem: required font missing: {}", path);
        LOG_ERROR(Graphics, "FontSystem::Initialize aborting — {} font(s) absent", missing.size());
        std::exit(1);
    }

    _initialized = true;
    LOG_INFO(Graphics, "FontSystem initialized ({} fonts in slot {})",
             sizeof(kSlot0FontPaths) / sizeof(kSlot0FontPaths[0]), kCanonicalSlot);
}

void FontSystem::Shutdown()
{
    ClearFreeTypeCaches();
    _initialized = false;
}

} // namespace Poseidon
