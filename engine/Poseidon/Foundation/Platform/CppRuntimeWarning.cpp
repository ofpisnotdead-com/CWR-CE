#include "CppRuntimeWarning.hpp"

#ifdef _WIN32

#include <windows.h>

namespace
{
constexpr Poseidon::Foundation::CppRuntimeVersion RequiredCppRuntimeVersion()
{
    return {14, static_cast<std::uint16_t>(_MSC_VER % 100)};
}
} // namespace

void Poseidon::Foundation::WarnIfCppRuntimeIsOlder()
{
    const HMODULE runtime = GetModuleHandleW(L"msvcp140.dll");
    if (!runtime)
        return;

    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(runtime, path, MAX_PATH) == 0)
        return;

    DWORD unused = 0;
    const DWORD infoSize = GetFileVersionInfoSizeW(path, &unused);
    if (infoSize == 0)
        return;

    const HANDLE heap = GetProcessHeap();
    void* const info = HeapAlloc(heap, 0, infoSize);
    if (!info)
        return;

    VS_FIXEDFILEINFO* versionInfo = nullptr;
    UINT versionInfoSize = 0;
    const bool found = GetFileVersionInfoW(path, 0, infoSize, info) &&
                       VerQueryValueW(info, L"\\", reinterpret_cast<void**>(&versionInfo), &versionInfoSize) &&
                       versionInfoSize >= sizeof(VS_FIXEDFILEINFO) && versionInfo->dwSignature == VS_FFI_SIGNATURE;

    if (found)
    {
        const CppRuntimeVersion installed = {
            HIWORD(versionInfo->dwFileVersionMS),
            LOWORD(versionInfo->dwFileVersionMS),
        };
        constexpr CppRuntimeVersion required = RequiredCppRuntimeVersion();
        if (IsCppRuntimeOlder(installed, required))
        {
            wchar_t message[512];
            wsprintfW(message,
                      L"An older Microsoft Visual C++ runtime was detected.\n\n"
                      L"Detected: %u.%u.%u\n"
                      L"Minimum: %u.%u\n\n"
                      L"Please install a newer runtime from https://aka.ms/vcruntime\n\n"
                      L"The game could crash or fail to start with the installed runtime.",
                      installed.major, installed.minor, HIWORD(versionInfo->dwFileVersionLS), required.major,
                      required.minor);
            MessageBoxW(nullptr, message, L"Arma: Cold War Assault - Remastered",
                        MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        }
    }

    HeapFree(heap, 0, info);
}

#else

void Poseidon::Foundation::WarnIfCppRuntimeIsOlder() {}

#endif
