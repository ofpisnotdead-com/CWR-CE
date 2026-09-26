#pragma once


#include <unordered_map>

#include <Poseidon/Foundation/Containers/Array.hpp>
namespace Poseidon::Foundation
{

// ArrayWithHashDuplicate is a container that pairs an array with a hash map,
// mapping keys to array indices for fast lookup by key.
// Elements are stored in insertion order. Duplicate keys are allowed:
// when inserting a key that already exists, the element is still appended to the array,
// but the hash map retains the index of the **first** occurrence.
// Lookup by key always returns the index of the first matching element.
template <typename ElementType, typename KeyType, typename Element2Key, typename hasher = std::hash<KeyType>, typename keyEq = std::equal_to<KeyType>>
class ArrayWithHashDuplicate
{
protected:
    AutoArray<ElementType> m_array;
    std::unordered_map<KeyType, int, hasher, keyEq> m_key2IdxMap;
    using KeyExtractor = Element2Key;
public:
    // Transmit to AutoArray
    int Add(const ElementType& src)
    {
        const int insertedIdx = m_array.Add(src);
        insertKeyToMap(Element2Key()(src), insertedIdx);
        return insertedIdx;
    }
    int Add() = delete; // not allow Add default object first and then rename it
    int Size() const
    {
        return m_array.Size();
    }
    void Clear()
    {
        m_array.Clear();
        m_key2IdxMap.clear();
    }
    void Delete(int index) = delete; // Derived class should implement its own Delete method to handle duplicate keys properly.
    ElementType& operator[](int i)
    {
        return m_array[i];
    }
    const ElementType& operator[](int i) const
    {
        return m_array[i];
    }

    // Search
    static bool ValidIdx(int index) { return index >= 0; }
    int FindIdx(const KeyType& name) const
    {
        auto it = m_key2IdxMap.find(name);
        if (it != m_key2IdxMap.cend())
            return it->second;
        return -1;
    }
    ElementType* Find(const KeyType& name)
    {
        const int idx = FindIdx(name);
        if (!ValidIdx(idx))
            return nullptr;
        return &m_array[idx];
    }
    const ElementType* Find(const KeyType& name) const
    {
        const int idx = FindIdx(name);
        if (!ValidIdx(idx))
            return nullptr;
        return &m_array[idx];
    }

    // For serialize
    AutoArray<ElementType>& ArrayForSerialize()
    {
        return m_array;
    }
    void OnSerialize(const int oldSize)
    {
        for (int i = oldSize, c = m_array.Size(); i < c; ++i)
        {
            insertKeyToMap(Element2Key()(m_array[i]), i);
        }
    }
protected:
    void defaultDelete(int index)
    {
        KeyType keyOfDeletedElement = Element2Key()(m_array[index]);

        m_array.Delete(index);

        int nextSameElementIndex = -1;
        constexpr bool mayHaveDuplicates = true; // We don't know if there are duplicates, so we must check.
        if (mayHaveDuplicates)
        {
            checkNextSameElementIndex(index, nextSameElementIndex, keyOfDeletedElement);
        }
        adjustIndexAfterDelete(index, nextSameElementIndex);
    }
    void checkNextSameElementIndex(int deletedIndex, int& nextSameElementIndex, const KeyType& keyOfDeletedElement) const
    {
        for (int i = deletedIndex, n = m_array.Size(); i < n; ++i)
        {
            if (keyEq()(keyOfDeletedElement, Element2Key()(m_array[i])))
            {
                nextSameElementIndex = i;
                break;
            }
        }
    }
    void adjustIndexAfterDelete(int deletedIndex, int nextSameElementIndex)
    {
        for (auto it = m_key2IdxMap.begin(); it != m_key2IdxMap.end(); )
        {
            if (it->second < deletedIndex)
            {
                ++it;
            }
            else if (it->second == deletedIndex)
            {
                if (ValidIdx(nextSameElementIndex))    // still exists duplicated element. Update the index to the next duplicated element's index
                {
                    it->second = nextSameElementIndex;
                    ++it;
                }
                else                                   // no duplicated element exists, remove the key from the map
                {
                    it = m_key2IdxMap.erase(it);
                }
            }
            else // it->second > deletedIndex
            {
                --it->second;
                ++it;
            }
        }
    }
private:
    void insertKeyToMap(const KeyType& name, int index)
    {
        m_key2IdxMap.insert(std::make_pair(name, index)); // can fail if name already exists
    }
};

} // namespace Poseidon::Foundation
