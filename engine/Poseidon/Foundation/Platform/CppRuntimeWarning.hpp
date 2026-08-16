#pragma once

#include <cstdint>

namespace Poseidon::Foundation
{
struct CppRuntimeVersion
{
    std::uint16_t major;
    std::uint16_t minor;
};

constexpr bool IsCppRuntimeOlder(CppRuntimeVersion installed, CppRuntimeVersion required)
{
    if (installed.major != required.major)
        return installed.major < required.major;
    return installed.minor < required.minor;
}

void WarnIfCppRuntimeIsOlder();
} // namespace Poseidon::Foundation
