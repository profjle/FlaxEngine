// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#pragma once

#include "Engine/Core/Memory/Memory.h"
#include "Engine/Core/Memory/Allocation.h"
#include "Engine/Core/Collections/HashFunctions.h"
#include "Engine/Core/Collections/Config.h"

/// <summary>
/// Template for unordered set of values (without duplicates with O(1) lookup access).
/// </summary>
/// <typeparam name="T">The type of elements in the set.</typeparam>
/// <typeparam name="AllocationType">The type of memory allocator.</typeparam>
template<typename T, typename AllocationType = HeapAllocation>
API_CLASS(InBuild) class HashSet
{
    friend HashSet;
public:
    /// <summary>
    /// Describes single portion of space for the item in a hash map.
    /// </summary>
    struct Bucket
    {
        friend HashSet;

        enum State : byte
        {
            Empty,
            Deleted,
            Occupied,
        };

        /// <summary>The item.</summary>
        T Item;

    private:
        State _state;

        void Free()
        {
            if (_state == Occupied)
                Memory::DestructItem(&Item);
            _state = Empty;
        }

        void Delete()
        {
            _state = Deleted;
            Memory::DestructItem(&Item);
        }

        template<typename ItemType>
        void Occupy(const ItemType& item)
        {
            Memory::ConstructItems(&Item, &item, 1);
            _state = Occupied;
        }

        FORCE_INLINE bool IsEmpty() const
        {
            return _state == Empty;
        }

        FORCE_INLINE bool IsDeleted() const
        {
            return _state == Deleted;
        }

        FORCE_INLINE bool IsOccupied() const
        {
            return _state == Occupied;
        }

        FORCE_INLINE bool IsNotOccupied() const
        {
            return _state != Occupied;
        }
    };

    typedef typename AllocationType::template Data<Bucket> AllocationData;

private:
    int32 _elementsCount = 0;
    int32 _deletedCount = 0;
    int32 _size = 0;
    AllocationData _allocation;

public:
    /// <summary>
    /// Initializes a new instance of the <see cref="HashSet"/> class.
    /// </summary>
    HashSet()
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="HashSet"/> class.
    /// </summary>
    /// <param name="capacity">The initial capacity.</param>
    HashSet(int32 capacity)
    {
        SetCapacity(capacity);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="HashSet"/> class.
    /// </summary>
    /// <param name="other">The other collection to move.</param>
    HashSet(HashSet&& other) noexcept
        : _elementsCount(other._elementsCount)
        , _deletedCount(other._deletedCount)
        , _size(other._size)
    {
        _elementsCount = other._elementsCount;
        _deletedCount = other._deletedCount;
        _size = other._size;
        other._elementsCount = 0;
        other._deletedCount = 0;
        other._size = 0;
        _allocation.Swap(other._allocation);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="HashSet"/> class.
    /// </summary>
    /// <param name="other">Other collection to copy</param>
    HashSet(const HashSet& other)
    {
        Clone(other);
    }

    /// <summary>
    /// Clones the data from the other collection.
    /// </summary>
    /// <param name="other">The other collection to copy.</param>
    /// <returns>The reference to this.</returns>
    HashSet& operator=(const HashSet& other)
    {
        if (this != &other)
            Clone(other);
        return *this;
    }

    /// <summary>
    /// Moves the data from the other collection.
    /// </summary>
    /// <param name="other">The other collection to move.</param>
    /// <returns>The reference to this.</returns>
    HashSet& operator=(HashSet&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            _allocation.Free();
            _elementsCount = other._elementsCount;
            _deletedCount = other._deletedCount;
            _size = other._size;
            other._elementsCount = 0;
            other._deletedCount = 0;
            other._size = 0;
            _allocation.Swap(other._allocation);
        }
        return *this;
    }

    /// <summary>
    /// Finalizes an instance of the <see cref="HashSet"/> class.
    /// </summary>
    ~HashSet()
    {
        SetCapacity(0, false);
    }

public:
    /// <summary>
    /// Gets the amount of the elements in the collection.
    /// </summary>
    FORCE_INLINE int32 Count() const
    {
        return _elementsCount;
    }

    /// <summary>
    /// Gets the amount of the elements that can be contained by the collection.
    /// </summary>
    FORCE_INLINE int32 Capacity() const
    {
        return _size;
    }

    /// <summary>
    /// Returns true if collection is empty.
    /// </summary>
    FORCE_INLINE bool IsEmpty() const
    {
        return _elementsCount == 0;
    }

    /// <summary>
    /// Returns true if collection has one or more elements.
    /// </summary>
    FORCE_INLINE bool HasItems() const
    {
        return _elementsCount != 0;
    }

public:
    /// <summary>
    /// The hash set collection iterator.
    /// </summary>
    struct Iterator
    {
        friend HashSet;
    private:
        HashSet& _collection;
        int32 _index;

        Iterator(HashSet& collection, const int32 index)
            : _collection(collection)
            , _index(index)
        {
        }

        Iterator(HashSet const& collection, const int32 index)
            : _collection((HashSet&)collection)
            , _index(index)
        {
        }

    public:
        Iterator(const Iterator& i)
            : _collection(i._collection)
            , _index(i._index)
        {
        }

        Iterator(Iterator&& i)
            : _collection(i._collection)
            , _index(i._index)
        {
        }

    public:
        FORCE_INLINE bool IsEnd() const
        {
            return _index == _collection.Capacity();
        }

        FORCE_INLINE bool IsNotEnd() const
        {
            return _index != _collection.Capacity();
        }

        FORCE_INLINE Bucket& operator*() const
        {
            return _collection._allocation.Get()[_index];
        }

        FORCE_INLINE Bucket* operator->() const
        {
            return &_collection._allocation.Get()[_index];
        }

        FORCE_INLINE explicit operator bool() const
        {
            return _index >= 0 && _index < _collection._size;
        }

        FORCE_INLINE bool operator !() const
        {
            return !(bool)*this;
        }

        FORCE_INLINE bool operator==(const Iterator& v) const
        {
            return _index == v._index && &_collection == &v._collection;
        }

        FORCE_INLINE bool operator!=(const Iterator& v) const
        {
            return _index != v._index || &_collection != &v._collection;
        }

        Iterator& operator++()
        {
            const int32 capacity = _collection.Capacity();
            if (_index != capacity)
            {
                const Bucket* data = _collection._allocation.Get();
                do
                {
                    _index++;
                } while (_index != capacity && data[_index].IsNotOccupied());
            }
            return *this;
        }

        Iterator operator++(int)
        {
            Iterator i = *this;
            ++i;
            return i;
        }

        Iterator& operator--()
        {
            if (_index > 0)
            {
                const Bucket* data = _collection._allocation.Get();
                do
                {
                    _index--;
                } while (_index > 0 && data[_index].IsNotOccupied());
            }
            return *this;
        }

        Iterator operator--(int)
        {
            Iterator i = *this;
            --i;
            return i;
        }
    };

public:
    /// <summary>
    /// Removes all elements from the collection.
    /// </summary>
    void Clear()
    {
        if (_elementsCount + _deletedCount != 0)
        {
            Bucket* data = _allocation.Get();
            for (int32 i = 0; i < _size; i++)
                data[i].Free();
            _elementsCount = _deletedCount = 0;
        }
    }

    /// <summary>
    /// Clears the collection and delete value objects.
    /// Note: collection must contain pointers to the objects that have public destructor and be allocated using New method.
    /// </summary>
#if defined(_MSC_VER)
    template<typename = typename TEnableIf<TIsPointer<T>::Value>::Type>
#endif
    void ClearDelete()
    {
        for (Iterator i = Begin(); i.IsNotEnd(); ++i)
        {
            if (i->Value)
                ::Delete(i->Value);
        }
        Clear();
    }

    /// <summary>
    /// Changes capacity of the collection
    /// </summary>
    /// <param name="capacity">New capacity</param>
    /// <param name="preserveContents">Enable/disable preserving collection contents during resizing</param>
    void SetCapacity(int32 capacity, bool preserveContents = true)
    {
        if (capacity == Capacity())
            return;
        ASSERT(capacity >= 0);
        AllocationData oldAllocation;
        oldAllocation.Swap(_allocation);
        const int32 oldSize = _size;
        const int32 oldElementsCount = _elementsCount;
        _deletedCount = _elementsCount = 0;
        if (capacity != 0 && (capacity & (capacity - 1)) != 0)
        {
            // Align capacity value to the next power of two (http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2)
            capacity--;
            capacity |= capacity >> 1;
            capacity |= capacity >> 2;
            capacity |= capacity >> 4;
            capacity |= capacity >> 8;
            capacity |= capacity >> 16;
            capacity++;
        }
        if (capacity)
        {
            _allocation.Allocate(capacity);
            Bucket* data = _allocation.Get();
            for (int32 i = 0; i < capacity; i++)
                data[i]._state = Bucket::Empty;
        }
        _size = capacity;
        Bucket* oldData = oldAllocation.Get();
        if (oldElementsCount != 0 && preserveContents)
        {
            // TODO; move keys and values on realloc
            for (int32 i = 0; i < oldSize; i++)
            {
                if (oldData[i].IsOccupied())
                    Add(oldData[i].Item);
            }
        }
        if (oldElementsCount != 0)
        {
            for (int32 i = 0; i < oldSize; i++)
                oldData[i].Free();
        }
    }

    /// <summary>
    /// Ensures that collection has given capacity.
    /// </summary>
    /// <param name="minCapacity">The minimum required capacity.</param>
    /// <param name="preserveContents">True if preserve collection data when changing its size, otherwise collection after resize will be empty.</param>
    void EnsureCapacity(int32 minCapacity, bool preserveContents = true)
    {
        if (Capacity() >= minCapacity)
            return;
        if (minCapacity < DICTIONARY_DEFAULT_CAPACITY)
            minCapacity = DICTIONARY_DEFAULT_CAPACITY;
        const int32 capacity = _allocation.CalculateCapacityGrow(_size, minCapacity);
        SetCapacity(capacity, preserveContents);
    }

public:
    /// <summary>
    /// Add element to the collection.
    /// </summary>
    /// <param name="item">The element to add to the set.</param>
    /// <returns>True if element has been added to the collection, otherwise false if the element is already present.</returns>
    template<typename ItemType>
    bool Add(const ItemType& item)
    {
        // Ensure to have enough memory for the next item (in case of new element insertion)
        EnsureCapacity(_elementsCount + _deletedCount + 1);

        // Find location of the item or place to insert it
        FindPositionResult pos;
        FindPosition(item, pos);

        // Check if object has been already added
        if (pos.ObjectIndex != -1)
            return false;

        // Insert
        ASSERT(pos.FreeSlotIndex != -1);
        Bucket* bucket = &_allocation.Get()[pos.FreeSlotIndex];
        bucket->Occupy(item);
        _elementsCount++;

        return true;
    }

    /// <summary>
    /// Add element at iterator to the collection
    /// </summary>
    /// <param name="i">Iterator with item to add</param>
    void Add(const Iterator& i)
    {
        ASSERT(&i._collection != this && i);
        const Bucket& bucket = *i;
        Add(bucket.Item);
    }

    /// <summary>
    /// Removes the specified element from the collection.
    /// </summary>
    /// <param name="item">The element to remove.</param>
    /// <returns>True if cannot remove item from the collection because cannot find it, otherwise false.</returns>
    template<typename ItemType>
    bool Remove(const ItemType& item)
    {
        if (IsEmpty())
            return false;
        FindPositionResult pos;
        FindPosition(item, pos);
        if (pos.ObjectIndex != -1)
        {
            _allocation.Get()[pos.ObjectIndex].Delete();
            _elementsCount--;
            _deletedCount++;
            return true;
        }
        return false;
    }

    /// <summary>
    /// Removes an element at specified iterator position.
    /// </summary>
    /// <param name="i">The element iterator to remove.</param>
    /// <returns>True if cannot remove item from the collection because cannot find it, otherwise false.</returns>
    bool Remove(Iterator& i)
    {
        ASSERT(&i._collection == this);
        if (i)
        {
            ASSERT(_allocation.Get()[i._index].IsOccupied());
            _allocation.Get()[i._index].Delete();
            _elementsCount--;
            _deletedCount++;
            return true;
        }
        return false;
    }

public:
    /// <summary>
    /// Find element with given item in the collection
    /// </summary>
    /// <param name="item">Item to find</param>
    /// <returns>Iterator for the found element or End if cannot find it</returns>
    template<typename ItemType>
    Iterator Find(const ItemType& item) const
    {
        if (IsEmpty())
            return End();
        FindPositionResult pos;
        FindPosition(item, pos);
        return pos.ObjectIndex != -1 ? Iterator(*this, pos.ObjectIndex) : End();
    }

    /// <summary>
    /// Determines whether a collection contains the specified element.
    /// </summary>
    /// <param name="item">The item to locate.</param>
    /// <returns>True if value has been found in a collection, otherwise false</returns>
    template<typename ItemType>
    bool Contains(const ItemType& item) const
    {
        if (IsEmpty())
            return false;
        FindPositionResult pos;
        FindPosition(item, pos);
        return pos.ObjectIndex != -1;
    }

public:
    /// <summary>
    /// Clones other collection into this
    /// </summary>
    /// <param name="other">Other collection to clone</param>
    void Clone(const HashSet& other)
    {
        Clear();
        SetCapacity(other.Capacity(), false);
        for (Iterator i = other.Begin(); i != other.End(); ++i)
            Add(i);
        ASSERT(Count() == other.Count());
        ASSERT(Capacity() == other.Capacity());
    }

public:
    Iterator Begin() const
    {
        Iterator i(*this, -1);
        ++i;
        return i;
    }

    Iterator End() const
    {
        return Iterator(*this, _size);
    }

    Iterator begin()
    {
        Iterator i(*this, -1);
        ++i;
        return i;
    }

    FORCE_INLINE Iterator end()
    {
        return Iterator(*this, _size);
    }

    const Iterator begin() const
    {
        Iterator i(*this, -1);
        ++i;
        return i;
    }

    FORCE_INLINE const Iterator end() const
    {
        return Iterator(*this, _size);
    }

protected:
    /// <summary>
    /// The result container of the set item lookup searching.
    /// </summary>
    struct FindPositionResult
    {
        int32 ObjectIndex;
        int32 FreeSlotIndex;
    };

    /// <summary>
    /// Returns a pair of positions: 1st where the object is, 2nd where
    /// it would go if you wanted to insert it. 1st is -1
    /// if object is not found; 2nd is -1 if it is.
    /// Note: because of deletions where-to-insert is not trivial: it's the
    /// first deleted bucket we see, as long as we don't find the item later
    /// </summary>
    /// <param name="item">The item to find</param>
    /// <param name="result">Pair of values: where the object is and where it would go if you wanted to insert it</param>
    template<typename ItemType>
    void FindPosition(const ItemType& item, FindPositionResult& result) const
    {
        ASSERT(_size);
        const int32 tableSizeMinusOne = _size - 1;
        int32 bucketIndex = GetHash(item) & tableSizeMinusOne;
        int32 insertPos = -1;
        int32 numChecks = 0;
        const Bucket* data = _allocation.Get();
        result.FreeSlotIndex = -1;
        while (numChecks < _size)
        {
            // Empty bucket
            const Bucket& bucket = data[bucketIndex];
            if (bucket.IsEmpty())
            {
                // Found place to insert
                result.ObjectIndex = -1;
                result.FreeSlotIndex = insertPos == -1 ? bucketIndex : insertPos;
                return;
            }
            // Deleted bucket
            if (bucket.IsDeleted())
            {
                // Keep searching but mark to insert
                if (insertPos == -1)
                    insertPos = bucketIndex;
            }
            // Occupied bucket by target item
            else if (bucket.Item == item)
            {
                // Found item
                result.ObjectIndex = bucketIndex;
                return;
            }

            numChecks++;
            bucketIndex = (bucketIndex + DICTIONARY_PROB_FUNC(_size, numChecks)) & tableSizeMinusOne;
        }
        result.ObjectIndex = -1;
        result.FreeSlotIndex = insertPos;
    }
};
