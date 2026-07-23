#pragma once

#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits.h>

namespace Poseidon
{

enum class NetworkPlayerUploadKind
{
    Other,
    Face,
    Sound,
};

constexpr size_t NetworkSquadFileMaxSize = 1024 * 1024;

inline bool IsNetworkSquadFileSizeAllowed(size_t size)
{
    return size <= NetworkSquadFileMaxSize;
}

inline bool IsNetworkTransferredAssetSizeAllowed(size_t size, size_t maxSize)
{
    return size <= maxSize;
}

inline bool ShouldAcceptNetworkTransferredAsset(const RString& path, size_t size, size_t maxSize);

inline bool IsSafeNetworkAssetPathComponent(const char* value)
{
    if (value == nullptr || value[0] == '\0')
    {
        return false;
    }
    if (strcmp(value, ".") == 0 || strcmp(value, "..") == 0)
    {
        return false;
    }
    for (const char* ptr = value; *ptr; ++ptr)
    {
        if (*ptr == '/' || *ptr == '\\' || *ptr == ':')
        {
            return false;
        }
    }
    return true;
}

inline bool IsNetworkHttpUrl(const char* value)
{
    return value && (strncmp(value, "http://", 7) == 0 || strncmp(value, "https://", 8) == 0);
}

inline RString NetworkSquadPictureStorageName(const RString& picture)
{
    const char* value = picture;
    if (!value || value[0] == '\0')
    {
        return RString();
    }
    if (!IsNetworkHttpUrl(value))
    {
        return picture;
    }

    const char* slash = strrchr(value, '/');
    const char* backslash = strrchr(value, '\\');
    if (backslash && (!slash || backslash > slash))
    {
        slash = backslash;
    }
    return slash ? RString(slash + 1) : picture;
}

inline RString BuildNetworkSquadPictureRelativePath(const RString& squadNick, const RString& picture)
{
    const RString storageName = NetworkSquadPictureStorageName(picture);
    if (!IsSafeNetworkAssetPathComponent(squadNick) || !IsSafeNetworkAssetPathComponent(storageName))
    {
        return RString();
    }
    return RString("squads/") + squadNick + RString("/") + storageName;
}

inline RString BuildNetworkSquadPictureTmpPath(const RString& squadNick, const RString& picture)
{
    const RString relative = BuildNetworkSquadPictureRelativePath(squadNick, picture);
    return relative.GetLength() > 0 ? RString("tmp/") + relative : RString();
}

inline RString BuildNetworkSquadPictureDownloadUrl(const RString& squadXmlUrl, const RString& picture)
{
    const char* pictureUrl = picture;
    if (IsNetworkHttpUrl(pictureUrl))
    {
        if (strstr(pictureUrl, "/../") || strstr(pictureUrl, "/./") || strstr(pictureUrl, "\\..\\") ||
            strstr(pictureUrl, "\\.\\"))
        {
            return RString();
        }
        const RString storageName = NetworkSquadPictureStorageName(picture);
        return IsSafeNetworkAssetPathComponent(storageName) ? picture : RString();
    }

    if (!IsSafeNetworkAssetPathComponent(picture))
    {
        return RString();
    }

    const char* url = squadXmlUrl;
    if (!url || url[0] == '\0')
    {
        return picture;
    }

    const char* slash = strrchr(url, '/');
    const char* backslash = strrchr(url, '\\');
    if (backslash && (!slash || backslash > slash))
    {
        slash = backslash;
    }
    if (!slash)
    {
        return picture;
    }
    return RString(url, slash + 1 - url) + picture;
}

template <class FileExistsFn>
RString FindNetworkSquadPictureTmpPath(const RString& squadNick, const RString& picture, FileExistsFn&& fileExists)
{
    const RString path = BuildNetworkSquadPictureTmpPath(squadNick, picture);
    if (path.GetLength() == 0 || !fileExists(path))
    {
        return RString();
    }
    return path;
}

inline bool IsNetworkPlayerFaceAssetName(const RString& assetName)
{
    return stricmp(assetName, "face.paa") == 0 || stricmp(assetName, "face.jpg") == 0;
}

inline RString BuildNetworkPlayerStorageKey(int playerId)
{
    if (playerId <= 0)
    {
        return RString();
    }
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", playerId);
    return RString(buffer);
}

inline bool IsNetworkPlayerStorageKey(const RString& playerKey)
{
    const char* value = playerKey;
    if (!IsSafeNetworkAssetPathComponent(value))
    {
        return false;
    }
    for (const char* ptr = value; *ptr; ++ptr)
    {
        if (*ptr < '0' || *ptr > '9')
        {
            return false;
        }
    }
    return true;
}

inline RString BuildNetworkRelativePath(const RString& first, const RString& second)
{
    return first + RString("/") + second;
}

inline RString BuildNetworkFilesystemPath(const RString& first, const RString& second)
{
    if (first.GetLength() == 0 || second.GetLength() == 0)
    {
        return RString();
    }

    int firstLen = first.GetLength();
    while (firstLen > 0 && (first[firstLen - 1] == '/' || first[firstLen - 1] == '\\'))
    {
        --firstLen;
    }

    int secondBegin = 0;
    while (secondBegin < second.GetLength() && (second[secondBegin] == '/' || second[secondBegin] == '\\'))
    {
        ++secondBegin;
    }

    if (firstLen == 0 || secondBegin == second.GetLength())
    {
        return RString();
    }

    char buffer[1024];
    int offset = 0;
    for (int i = 0; i < firstLen; ++i)
    {
        if (offset + 1 >= static_cast<int>(sizeof(buffer)))
        {
            return RString();
        }
        buffer[offset++] = first[i];
    }
    if (offset + 1 >= static_cast<int>(sizeof(buffer)))
    {
        return RString();
    }
    buffer[offset++] = PATH_SEP;
    for (int i = secondBegin; i < second.GetLength(); ++i)
    {
        if (offset + 1 >= static_cast<int>(sizeof(buffer)))
        {
            return RString();
        }
        buffer[offset++] = (second[i] == '/' || second[i] == '\\') ? PATH_SEP : second[i];
    }
    buffer[offset] = 0;
    return RString(buffer);
}

inline RString NormalizeNetworkPathSeparators(const RString& path)
{
    char buffer[1024];
    const int length = path.GetLength();
    if (length >= static_cast<int>(sizeof(buffer)))
    {
        return RString();
    }
    for (int i = 0; i < length; ++i)
    {
        buffer[i] = path[i] == '\\' ? '/' : path[i];
    }
    buffer[length] = 0;
    return RString(buffer);
}

inline RString BuildNetworkPlayerAssetRelativePath(const RString& playerKey, const RString& assetName)
{
    if (!IsNetworkPlayerStorageKey(playerKey) || !IsSafeNetworkAssetPathComponent(assetName) ||
        !IsNetworkPlayerFaceAssetName(assetName))
    {
        return RString();
    }
    return BuildNetworkRelativePath(BuildNetworkRelativePath(RString("players"), playerKey), assetName);
}

inline RString BuildNetworkPlayerAssetRelativePath(int playerId, const RString& assetName)
{
    return BuildNetworkPlayerAssetRelativePath(BuildNetworkPlayerStorageKey(playerId), assetName);
}

inline RString BuildNetworkPlayerAssetRelativeDir(const RString& playerKey)
{
    if (!IsNetworkPlayerStorageKey(playerKey))
    {
        return RString();
    }
    return BuildNetworkRelativePath(RString("players"), playerKey) + RString("/");
}

inline RString BuildNetworkPlayerAssetRelativeDir(int playerId)
{
    return BuildNetworkPlayerAssetRelativeDir(BuildNetworkPlayerStorageKey(playerId));
}

inline RString BuildNetworkTmpPath(const RString& relative)
{
    return relative.GetLength() > 0 ? BuildNetworkRelativePath(RString("tmp"), relative) : RString();
}

inline RString BuildNetworkPlayerAssetTmpPath(const RString& playerKey, const RString& assetName)
{
    return BuildNetworkTmpPath(BuildNetworkPlayerAssetRelativePath(playerKey, assetName));
}

inline RString BuildNetworkPlayerAssetTmpPath(int playerId, const RString& assetName)
{
    return BuildNetworkPlayerAssetTmpPath(BuildNetworkPlayerStorageKey(playerId), assetName);
}

inline RString BuildNetworkPlayerAssetTmpDir(const RString& playerKey)
{
    return BuildNetworkTmpPath(BuildNetworkPlayerAssetRelativeDir(playerKey));
}

inline RString BuildNetworkPlayerAssetTmpDir(int playerId)
{
    return BuildNetworkPlayerAssetTmpDir(BuildNetworkPlayerStorageKey(playerId));
}

inline RString BuildNetworkServerUploadPath(const RString& serverTmpDir, const RString& relative)
{
    return relative.GetLength() > 0 ? BuildNetworkRelativePath(NormalizeNetworkPathSeparators(serverTmpDir),
                                                               NormalizeNetworkPathSeparators(relative))
                                    : RString();
}

inline RString BuildNetworkServerSquadPictureUploadPath(const RString& serverTmpDir, const RString& squadNick,
                                                        const RString& picture)
{
    return BuildNetworkServerUploadPath(serverTmpDir, BuildNetworkSquadPictureRelativePath(squadNick, picture));
}

inline RString BuildNetworkServerPlayerUploadDir(const RString& serverTmpDir, int playerId)
{
    return BuildNetworkServerUploadPath(serverTmpDir, BuildNetworkPlayerAssetRelativeDir(playerId));
}

inline RString BuildNetworkServerPlayerAssetUploadPath(const RString& serverTmpDir, int playerId,
                                                       const RString& assetName)
{
    return BuildNetworkServerUploadPath(serverTmpDir, BuildNetworkPlayerAssetRelativePath(playerId, assetName));
}

inline RString BuildNetworkPlayerSoundRelativePath(const RString& playerName, const RString& soundName)
{
    if (!IsNetworkPlayerStorageKey(playerName) || !IsSafeNetworkAssetPathComponent(soundName))
    {
        return RString();
    }
    return RString("players/") + playerName + RString("/sound/") + soundName;
}

inline RString BuildNetworkPlayerSoundRelativePath(int playerId, const RString& soundName)
{
    return BuildNetworkPlayerSoundRelativePath(BuildNetworkPlayerStorageKey(playerId), soundName);
}

inline RString BuildNetworkPlayerSoundRelativeDir(const RString& playerName)
{
    if (!IsNetworkPlayerStorageKey(playerName))
    {
        return RString();
    }
    return RString("players/") + playerName + RString("/sound/");
}

inline RString BuildNetworkPlayerSoundRelativeDir(int playerId)
{
    return BuildNetworkPlayerSoundRelativeDir(BuildNetworkPlayerStorageKey(playerId));
}

inline RString BuildNetworkPlayerSoundTmpPath(const RString& playerName, const RString& soundName)
{
    const RString relative = BuildNetworkPlayerSoundRelativePath(playerName, soundName);
    return relative.GetLength() > 0 ? RString("tmp/") + relative : RString();
}

inline RString BuildNetworkPlayerSoundTmpPath(int playerId, const RString& soundName)
{
    return BuildNetworkPlayerSoundTmpPath(BuildNetworkPlayerStorageKey(playerId), soundName);
}

inline RString BuildNetworkPlayerSoundTmpDir(const RString& playerName)
{
    const RString relative = BuildNetworkPlayerSoundRelativeDir(playerName);
    return relative.GetLength() > 0 ? RString("tmp/") + relative : RString();
}

inline RString BuildNetworkPlayerSoundTmpDir(int playerId)
{
    return BuildNetworkPlayerSoundTmpDir(BuildNetworkPlayerStorageKey(playerId));
}

inline RString BuildNetworkServerPlayerSoundUploadDir(const RString& serverTmpDir, int playerId)
{
    return BuildNetworkServerUploadPath(serverTmpDir, BuildNetworkPlayerSoundRelativeDir(playerId));
}

inline RString BuildNetworkServerPlayerSoundUploadPath(const RString& serverTmpDir, int playerId,
                                                       const RString& soundName)
{
    return BuildNetworkServerUploadPath(serverTmpDir, BuildNetworkPlayerSoundRelativePath(playerId, soundName));
}

inline RString BuildNetworkCustomRadioSoundPath(const RString& remotePlayerName, const RString& soundName,
                                                const RString& localSoundDir)
{
    if (remotePlayerName.GetLength() > 0)
    {
        return BuildNetworkPlayerSoundTmpPath(remotePlayerName, soundName);
    }
    if (!IsSafeNetworkAssetPathComponent(soundName))
    {
        return RString();
    }
    return localSoundDir + soundName;
}

inline RString BuildNetworkCustomRadioMenuText(const RString& soundName, Codepage preferredCodepage)
{
    const char* value = soundName;
    if (value == nullptr || value[0] == 0)
    {
        return RString();
    }

    const char* ext = strrchr(value, '.');
    const RString baseName = ext != nullptr ? soundName.Substring(0, ext - value) : soundName;
    return DecodeLegacyTextToRString(baseName, preferredCodepage);
}

inline RString BuildNetworkCustomRadioMenuText(const RString& soundName)
{
    return BuildNetworkCustomRadioMenuText(soundName, Codepage::CP1252);
}

inline NetworkPlayerUploadKind ClassifyNetworkPlayerUploadRelativePath(const RString& relativePath)
{
    const char* data = relativePath;
    if (!data || data[0] == 0)
    {
        return NetworkPlayerUploadKind::Other;
    }
    if (stricmp(data, "face.jpg") == 0 || stricmp(data, "face.paa") == 0)
    {
        return NetworkPlayerUploadKind::Face;
    }
    if (strnicmp(data, "sound/", 6) == 0 || strnicmp(data, "sound\\", 6) == 0)
    {
        return NetworkPlayerUploadKind::Sound;
    }
    return NetworkPlayerUploadKind::Other;
}

inline NetworkPlayerUploadKind ClassifyNetworkServerPlayerUploadPath(const RString& normalizedUploadPath,
                                                                     const RString& serverTmpDir, int playerId)
{
    const RString prefix = BuildNetworkServerPlayerUploadDir(serverTmpDir, playerId);
    if (prefix.GetLength() == 0 || strnicmp(normalizedUploadPath, prefix, prefix.GetLength()) != 0)
    {
        return NetworkPlayerUploadKind::Other;
    }
    return ClassifyNetworkPlayerUploadRelativePath(normalizedUploadPath.Substring(prefix.GetLength(), INT_MAX));
}

inline RString BuildNetworkTransferredAssetProbeTmpPath(const RString& kind, const RString& owner, const RString& name)
{
    if (stricmp(kind, "player") == 0 || stricmp(kind, "playerFace") == 0)
    {
        return BuildNetworkPlayerAssetTmpPath(owner, name);
    }
    if (stricmp(kind, "playerSound") == 0 || stricmp(kind, "sound") == 0)
    {
        return BuildNetworkPlayerSoundTmpPath(owner, name);
    }
    if (stricmp(kind, "squad") == 0 || stricmp(kind, "squadPicture") == 0)
    {
        return BuildNetworkSquadPictureTmpPath(owner, name);
    }
    return RString();
}

inline bool IsSafeNetworkTransferredAssetPath(const RString& path)
{
    const char* data = path;
    if (!data)
    {
        return false;
    }

    const char playerPrefix[] = "tmp/players/";
    const int playerPrefixLen = static_cast<int>(sizeof(playerPrefix) - 1);
    if (strncmp(data, playerPrefix, playerPrefixLen) == 0)
    {
        const char* name = data + playerPrefixLen;
        const char* slash = strchr(name, '/');
        if (!slash)
        {
            return false;
        }
        RString player(name, slash - name);
        RString relative(slash + 1);
        if (strncmp(relative, "sound/", 6) == 0)
        {
            return BuildNetworkPlayerSoundTmpPath(player, RString(relative.Data() + 6)) == path;
        }
        return BuildNetworkPlayerAssetTmpPath(player, relative) == path;
    }

    const char squadPrefix[] = "tmp/squads/";
    const int squadPrefixLen = static_cast<int>(sizeof(squadPrefix) - 1);
    if (strncmp(data, squadPrefix, squadPrefixLen) == 0)
    {
        const char* nick = data + squadPrefixLen;
        const char* slash = strchr(nick, '/');
        if (!slash)
        {
            return false;
        }
        return BuildNetworkSquadPictureTmpPath(RString(nick, slash - nick), RString(slash + 1)) == path;
    }

    return false;
}

inline bool IsNetworkSquadTransferredAssetPath(const RString& path)
{
    const char* data = path;
    if (!data)
    {
        return false;
    }
    const char squadPrefix[] = "tmp/squads/";
    const int squadPrefixLen = static_cast<int>(sizeof(squadPrefix) - 1);
    return strncmp(data, squadPrefix, squadPrefixLen) == 0 && IsSafeNetworkTransferredAssetPath(path);
}

inline size_t NetworkTransferredAssetMaxSize(const RString& path, size_t maxCustomFileSize)
{
    return IsNetworkSquadTransferredAssetPath(path) ? NetworkSquadFileMaxSize : maxCustomFileSize;
}

inline bool ShouldAcceptNetworkTransferredAsset(const RString& path, size_t size, size_t maxSize)
{
    return IsSafeNetworkTransferredAssetPath(path) &&
           IsNetworkTransferredAssetSizeAllowed(size, NetworkTransferredAssetMaxSize(path, maxSize));
}

inline bool IsSafeNetworkServerPlayerUploadPath(const RString& path, const RString& serverTmpDir, int playerId)
{
    RString prefix = BuildNetworkServerPlayerUploadDir(serverTmpDir, playerId);
    if (prefix.GetLength() == 0)
    {
        return false;
    }

    RString relative;
    const RString normalizedPath = NormalizeNetworkPathSeparators(path);
    if (strnicmp(normalizedPath, prefix, prefix.GetLength()) == 0)
    {
        relative = normalizedPath.Substring(prefix.GetLength(), INT_MAX);
    }
    else
    {
        prefix = RString("/") + BuildNetworkPlayerAssetRelativeDir(playerId);
        const char* match = strstr(static_cast<const char*>(normalizedPath), static_cast<const char*>(prefix));
        if (!match || match == static_cast<const char*>(normalizedPath) || match[prefix.GetLength()] == '\0')
        {
            return false;
        }
        relative = RString(match + prefix.GetLength());
    }

    const RString playerKey = BuildNetworkPlayerStorageKey(playerId);
    return IsSafeNetworkTransferredAssetPath(RString("tmp/players/") + playerKey + RString("/") + relative);
}

inline RString NormalizeNetworkServerPlayerUploadPath(const RString& path, const RString& serverTmpDir, int playerId)
{
    if (!IsSafeNetworkServerPlayerUploadPath(path, serverTmpDir, playerId))
    {
        return RString();
    }

    const RString normalizedPath = NormalizeNetworkPathSeparators(path);
    const RString serverPrefix = BuildNetworkServerPlayerUploadDir(serverTmpDir, playerId);
    if (serverPrefix.GetLength() > 0 && strnicmp(normalizedPath, serverPrefix, serverPrefix.GetLength()) == 0)
    {
        return normalizedPath;
    }

    const RString playerPrefix = RString("/") + BuildNetworkPlayerAssetRelativeDir(playerId);
    const char* match = strstr(static_cast<const char*>(normalizedPath), static_cast<const char*>(playerPrefix));
    if (!match || match == static_cast<const char*>(normalizedPath))
    {
        return RString();
    }

    return BuildNetworkServerUploadPath(serverTmpDir, RString(match + 1));
}

} // namespace Poseidon
