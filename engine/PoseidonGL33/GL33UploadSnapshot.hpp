#pragma once

#include <array>
#include <cstddef>
#include <cstring>

namespace Poseidon
{

template<std::size_t Size>
class GL33UploadSnapshot
{
  public:
    bool Matches(unsigned int buffer, const void* data) const
    {
        return _valid && _buffer == buffer && std::memcmp(_data.data(), data, Size) == 0;
    }

    void Record(unsigned int buffer, const void* data)
    {
        std::memcpy(_data.data(), data, Size);
        _buffer = buffer;
        _valid = true;
    }

    void Invalidate() { _valid = false; }

  private:
    std::array<std::byte, Size> _data{};
    unsigned int _buffer = 0;
    bool _valid = false;
};

} // namespace Poseidon
