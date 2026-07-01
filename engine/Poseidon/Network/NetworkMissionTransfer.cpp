#include <Poseidon/Network/NetworkMissionTransfer.hpp>

#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Foundation/Common/Filenames.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Algorithms/Crc.hpp>

#include <filesystem>
#include <stdio.h>
#include <string.h>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Containers/RStringArray.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{

bool ValidateNetworkMissionFileOnDisk(const RString& missionPath, int expectedFileSizeL, int expectedFileSizeH,
                                      int expectedFileCrc)
{
    const RString fileNameExt = missionPath + RString(".pbo");

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(std::string(fileNameExt), ec);
    if (ec)
    {
        return false;
    }

    QIFStream f;
    f.open(fileNameExt);
    Poseidon::Foundation::CRCCalculator crc;
    return DoesNetworkMissionFileMatchHeader(fileSize, crc.CRC(f.act(), f.rest()), expectedFileSizeL, expectedFileSizeH,
                                             expectedFileCrc);
}

bool IsSafeNetworkMissionFileName(const char* missionFileName)
{
    return missionFileName != nullptr && missionFileName[0] != 0 && IsRelativePath(missionFileName) &&
           strchr(missionFileName, '/') == nullptr && strchr(missionFileName, '\\') == nullptr;
}

RString BuildNetworkMissionTransferCacheBasePath(const char* missionFileName)
{
    if (!IsSafeNetworkMissionFileName(missionFileName))
    {
        return RString();
    }

    return RString(GamePaths::Instance().CacheDir().c_str()) + RString(GameDirs::MPMissionsCachePath().c_str()) +
           RString(missionFileName);
}

RString BuildNetworkMissionTransferCachePboPath(const char* missionFileName)
{
    const RString basePath = BuildNetworkMissionTransferCacheBasePath(missionFileName);
    return basePath.GetLength() > 0 ? basePath + RString(".pbo") : RString();
}

RString BuildNetworkMissionTransferBankPath(const char* transferPath)
{
    if (transferPath == nullptr)
    {
        return RString();
    }

    const char* ext = strrchr(transferPath, '.');
    if (ext == nullptr || stricmp(ext, ".pbo") != 0)
    {
        return RString(transferPath);
    }

    return RString(transferPath).Substring(0, ext - transferPath);
}

RString BuildNetworkMissionTransferFailureMessage(const char* format, const char* playerName, const char* transferPath)
{
    char message[512];
    snprintf(message, sizeof(message), "%s", "");

    if (format != nullptr && playerName != nullptr)
    {
        snprintf(message, sizeof(message), format, playerName);
        if (transferPath != nullptr)
        {
            sprintf(message + strlen(message), " - %s", transferPath);
        }
    }

    return message;
}

RString BuildNetworkMissionBriefingErrorMessage(const char* loadErrorMessage, const char* missingAddonMessage)
{
    if (loadErrorMessage != nullptr && loadErrorMessage[0] != 0)
    {
        return RString(loadErrorMessage);
    }

    return missingAddonMessage != nullptr ? RString(missingAddonMessage) : RString();
}

RString BuildNetworkMissionMissingAddonMessage(const char* baseMessage, const FindArrayRStringCI& missingAddons)
{
    RString message = baseMessage != nullptr ? RString(baseMessage) : RString();
    for (int i = 0; i < missingAddons.Size(); ++i)
    {
        if (i > 0)
        {
            message = message + RString(", ");
        }
        message = message + missingAddons[i];
    }
    return message;
}

} // namespace Poseidon
