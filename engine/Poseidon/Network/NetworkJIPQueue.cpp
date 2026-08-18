#include <Poseidon/Network/NetworkJIPQueue.hpp>

namespace Poseidon
{
bool AddOrReplaceJIPMessage(AutoArray<NetworkJIPMessage>& queue, NetworkMessageType type, Ref<NetworkMessage> msg,
                            const RString& key, int maxMessages)
{
    if ((type == NMTPublicVariable || type == NMTRemoteExec) && key.GetLength() > 0)
    {
        for (int i = 0; i < queue.Size(); ++i)
        {
            if (queue[i].type == type && queue[i].key == key)
            {
                queue[i].msg = msg;
                return true;
            }
        }
    }

    if (queue.Size() >= maxMessages)
    {
        return false;
    }

    NetworkJIPMessage jipMessage;
    jipMessage.type = type;
    jipMessage.msg = msg;
    jipMessage.key = key;
    queue.Add(jipMessage);
    return true;
}

bool RemoveJIPMessage(AutoArray<NetworkJIPMessage>& queue, NetworkMessageType type, const RString& key)
{
    bool removed = false;
    for (int i = 0; i < queue.Size();)
    {
        if (queue[i].type == type && queue[i].key == key)
        {
            queue.Delete(i);
            removed = true;
            continue;
        }
        ++i;
    }
    return removed;
}
} // namespace Poseidon
