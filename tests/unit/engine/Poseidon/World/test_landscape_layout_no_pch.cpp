#include <Poseidon/World/Terrain/Landscape.hpp>

#include <cstddef>

extern "C" std::size_t NoPchLandscapeSize()
{
    return sizeof(Poseidon::Landscape);
}
