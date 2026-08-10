#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>
#include <ShlObj.h>

namespace
{

std::string getWindowsFolder(int csidl, const char* appName)
{
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, buf)))
    {
        std::wstring dir = std::wstring(buf) + L"\\" + Poseidon::Utf8PathToWide(appName);
        std::string utf8 = Poseidon::WidePathToUtf8(dir.c_str());
        if (!Poseidon::CreateDirectoryUtf8(utf8.c_str()))
        {
            LOG_ERROR(Core, "Failed to create user directory '{}'", utf8);
        }
        return utf8;
    }
    return std::string(".\\") + appName;
}

} // anonymous namespace

namespace Poseidon::Foundation
{

std::string getUserConfigDir(const char* appName)
{
    return getWindowsFolder(CSIDL_APPDATA, appName);
}

std::string getUserDataDir(const char* appName)
{
    return getWindowsFolder(CSIDL_APPDATA, appName);
}

std::string getUserCacheDir(const char* appName)
{
    return getWindowsFolder(CSIDL_LOCAL_APPDATA, appName);
}

std::string getUserDocumentsDir(const char* appName)
{
    // CSIDL_PERSONAL = the user's Documents folder — discoverable and NOT roaming.
    return getWindowsFolder(CSIDL_PERSONAL, appName);
}

std::string getCurrentUserName()
{
    wchar_t buf[256] = {};
    DWORD size = 256;
    if (::GetUserNameW(buf, &size) && size > 1)
        return Poseidon::WidePathToUtf8(buf);
    return {};
}

} // namespace Poseidon::Foundation
