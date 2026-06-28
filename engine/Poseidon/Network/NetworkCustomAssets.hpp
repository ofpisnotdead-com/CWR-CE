#pragma once

#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits.h>

namespace Poseidon
{

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
                                                               NormalizeNetworkPathSeparators(relative)) :
                                      RString();
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
    if (!IsSafeNetworkAssetPathComponent(playerName) || !IsSafeNetworkAssetPathComponent(soundName))
    {
        return RString();
    }
    return RString("players/") + playerName + RString("/sound/") + soundName;
}

inline RString BuildNetworkPlayerSoundRelativeDir(const RString& playerName)
{
    if (!IsSafeNetworkAssetPathComponent(playerName))
    {
        return RString();
    }
    return RString("players/") + playerName + RString("/sound/");
}

inline RString BuildNetworkPlayerSoundTmpPath(const RString& playerName, const RString& soundName)
{
    const RString relative = BuildNetworkPlayerSoundRelativePath(playerName, soundName);
    return relative.GetLength() > 0 ? RString("tmp/") + relative : RString();
}

inline RString BuildNetworkPlayerSoundTmpDir(const RString& playerName)
{
    const RString relative = BuildNetworkPlayerSoundRelativeDir(playerName);
    return relative.GetLength() > 0 ? RString("tmp/") + relative : RString();
}

inline RString BuildNetworkTransferredAssetProbeTmpPath(const RString& kind, const RString& owner, const RString& name)
{
    if (stricmp(kind, "player") == 0 || stricmp(kind, "playerFace") == 0)
    {
        return BuildNetworkPlayerAssetTmpPath(owner, name);
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

    return false;
}

inline bool ShouldAcceptNetworkTransferredAsset(const RString& path, size_t size, size_t maxSize)
{
    return IsSafeNetworkTransferredAssetPath(path) && IsNetworkTransferredAssetSizeAllowed(size, maxSize);
}

inline bool IsSafeNetworkServerPlayerUploadPath(const RString& path, const RString& serverTmpDir, int playerId)
{
    const RString prefix = BuildNetworkServerPlayerUploadDir(serverTmpDir, playerId);
    if (prefix.GetLength() == 0 || strnicmp(path, prefix, prefix.GetLength()) != 0)
    {
        return false;
    }
    const RString playerKey = BuildNetworkPlayerStorageKey(playerId);
    return IsSafeNetworkTransferredAssetPath(RString("tmp/players/") + playerKey +
                                             RString("/") +
                                             NormalizeNetworkPathSeparators(path.Substring(prefix.GetLength(), INT_MAX)));
}

} // namespace Poseidon
