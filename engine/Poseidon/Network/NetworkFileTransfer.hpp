#pragma once

#include <algorithm>
#include <cstring>

#include <Poseidon/Foundation/Strings/RString.hpp>

// Keep mission/file transfer payloads comfortably below the transport's ~1490-byte data ceiling
// after message metadata/path overhead is added.

namespace Poseidon
{

constexpr int NetworkFileTransferSegmentSize = 1024;

inline int GetNetworkFileTransferSegmentCount(int totalSize, int maxSegmentSize = NetworkFileTransferSegmentSize)
{
    if (totalSize <= 0)
    {
        return 0;
    }

    return (totalSize + maxSegmentSize - 1) / maxSegmentSize;
}

inline bool ApplyReceivedNetworkFileTransferSegment(bool& segmentReceived, int segmentSize, int& receivedBytes,
                                                    int& remainingSegments)
{
    if (!segmentReceived)
    {
        segmentReceived = true;
        receivedBytes += segmentSize;
        --remainingSegments;
    }

    return remainingSegments <= 0;
}

template <typename MessageType, typename SendFn>
int SendNetworkFileTransferSegments(const RString& destinationPath, const void* data, int totalSize,
                                    SendFn&& sendSegment, int maxSegmentSize = NetworkFileTransferSegmentSize)
{
    MessageType msg;
    msg._path = destinationPath;
    msg._totSize = totalSize;
    msg._totSegments = GetNetworkFileTransferSegmentCount(totalSize, maxSegmentSize);
    msg._offset = 0;

    const char* bytes = static_cast<const char*>(data);
    for (int i = 0; i < msg._totSegments; i++)
    {
        msg._curSegment = i;
        const int size = std::min(maxSegmentSize, totalSize - msg._offset);
        msg._data.Resize(size);
        memcpy(msg._data.Data(), bytes + msg._offset, size);
        sendSegment(msg);
        msg._offset += size;
    }

    return msg._totSegments;
}

} // namespace Poseidon
