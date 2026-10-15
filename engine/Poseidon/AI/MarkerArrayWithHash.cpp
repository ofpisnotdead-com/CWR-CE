#include <Poseidon/AI/MarkerArrayWithHash.hpp>


namespace Poseidon
{

void GlobalMarkerArrayWithHash::Delete(int index)
{
    // Note: Markers are created in three distinct ways. User-created markers (via the map) are shared across the network, carry a "_user_defined" prefix, and possess a unique ID, thus they never duplicate. Script-created markers (via createMarker) are local and have a duplicate-name check. Markers defined in mission.sqm may have duplicate names.
    // Since duplicates are possible, when deleting a marker by name, the deletion logic must advance its search index to the next marker with the same name if one exists.
    // For markers with the "_user_defined" prefix, uniqueness can usually be assumed. However, this assumption breaks if script-created or mission.sqm markers also employ that prefix, although such practices are rarely encountered.

    RString deletedMarkerName = KeyExtractor()(m_array[index]);
    const char* userDefined = "_user_defined";
    const bool isUserDefMarker = strnicmp(deletedMarkerName, userDefined, strlen(userDefined)) == 0;

    m_array.Delete(index);

    int nextSameElementIndex = -1;
    const bool mayHaveDuplicates = !isUserDefMarker;
    if (mayHaveDuplicates)
    {
        checkNextSameElementIndex(index, nextSameElementIndex, deletedMarkerName);
    }
    adjustIndexAfterDelete(index, nextSameElementIndex);
}

}  // namespace Poseidon
