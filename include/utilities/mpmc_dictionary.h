/*
    - MPMC THREAD SAFE HOWEVER DESIGNED FOR A CERTAIN SCENARIO THEREFORE DON'T USE IT SOMEWHERE ELSE !
      
      USE CASE : WHEN INSERTS ARE VERY RARE AND SEARCHS ARE VERY FREQUENT, AND WHEN IT IS GUARANTEED THAT 
      SEARCH FOR A SPECIFIC ITEM WILL ALWAYS GUARANTEEDLY BE CALLED AFTER ITS INSERTION :

            - INSERTS ARE PROTECTED BY A SPINLOCK SO NO ABA RISK

            - USES SEPARATE CHAINING WITH ATOMIC LINKED LIST NODES AND ATOMIC HEAD AND CAS TO MAKE SEARCHS LOCKFREE WHILE THERE ARE ONGOING INSERTIONS

            - FIXED SIZE BUCKETS/TABLE WITH NO GROWS SO THERE IS NO RISK OF A GROW AND REHASHING FROM AN INSERTION CALLSTACK DURING A SEARCH,
              HOWEVER TABLE SIZE SHOULD BE CHOSEN CAREFULLY OTHERWISE COLLISIONS CAN DEGRADE THE PERFORMANCE

            - DTOR IS NOT THREAD SAFE HOWEVER IT IS TIED TO THE END OF HOST PROGRAM

    - ITEMS REMOVALS & OBJECTS CTORS WITH ARGUMENTS NOT SUPPORTED

    - DEFAULT HASH FUNCTION : MurmurHash3 https://en.wikipedia.org/wiki/MurmurHash
*/
#pragma once

#include <atomic>
#include <array>
#include <cassert>
#include <cstddef>
#include <type_traits>

#include "../compiler/hints_branch_predictor.h"
#include "../compiler/hints_hot_code.h"
#include "../compiler/unused.h"

#include "../cpu/alignment_constants.h"

#include "murmur_hash3.h"
#include "userspace_spinlock.h"

template <typename Key, typename Value, typename Allocator, typename HashFunction = MurmurHash3<Key>>
class MPMCDictionary 
{
    public:

        struct DictionaryNode
        {
            Key key;
            Value value;
            std::atomic<DictionaryNode*> next = nullptr;

            DictionaryNode() = default;
        };

        MPMCDictionary() = default;

        ~MPMCDictionary()
        {
            if (m_node_cache)
            {
                Allocator::deallocate(m_node_cache, sizeof(DictionaryNode) * m_node_cache_capacity);
            }

            if (m_table)
            {
                Allocator::deallocate(m_node_cache, m_table_size * sizeof(std::atomic<DictionaryNode*>));
            }
        }

        bool initialise(std::size_t capacity)
        {
            assert(capacity > 0);

            m_node_cache_capacity = capacity;
            m_table_size = m_node_cache_capacity;

            m_table = reinterpret_cast<std::atomic<DictionaryNode*>*>(Allocator::allocate(m_table_size * sizeof(std::atomic<DictionaryNode*>)));

            if (m_table == nullptr)
            {
                return false;
            }

            for (std::size_t i = 0; i < m_table_size; i++)
            {
                m_table[i].store(nullptr, std::memory_order_relaxed);
            }

            m_insertion_lock.initialise();

            if (build_node_cache() == false)
            {
                return false;
            }

            return true;
        }

        bool insert(const Key& key, const Value& value) 
        {
            assert(m_table && m_table_size > 0);

            m_insertion_lock.lock();
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            if (m_node_cache_index >= m_node_cache_capacity)
            {
                if (llmalloc_unlikely(build_node_cache() == false))
                {
                    m_insertion_lock.unlock();
                    return false;
                }
            }

            DictionaryNode* new_node = m_node_cache + m_node_cache_index;
            new_node->key = key;
            new_node->value = value;

            std::size_t index = hash(key);
            DictionaryNode* old_head = m_table[index].load(std::memory_order_relaxed);

            do
            {
                new_node->next.store(old_head, std::memory_order_relaxed);
            }
            while (!m_table[index].compare_exchange_weak(old_head, new_node, std::memory_order_release, std::memory_order_relaxed));

            ++m_node_cache_index;
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            m_insertion_lock.unlock();
            return true;
        }

        bool get(const Key& key, Value& value) const 
        {
            assert(m_table && m_table_size > 0);

            std::size_t index = hash(key);

            DictionaryNode* current = m_table[index].load(std::memory_order_acquire);

            while (current) 
            {
                if (current->key == key) 
                {
                    value = current->value;
                    return true;
                }

                current = current->next.load(std::memory_order_acquire);
            }

            return false;
        }

    private:
        LLMALLOC_ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) std::atomic<DictionaryNode*>* m_table = nullptr;
        std::size_t m_table_size = 0;

        HashFunction m_hash;
        UserspaceSpinlock<> m_insertion_lock;

        DictionaryNode* m_node_cache = nullptr;
        std::size_t m_node_cache_index = 0;
        std::size_t m_node_cache_capacity = 0;

        bool build_node_cache()
        {
            auto new_node_cache = reinterpret_cast<DictionaryNode*>(Allocator::allocate(sizeof(DictionaryNode) * m_node_cache_capacity));

            if (new_node_cache == nullptr)
            {
                return false;
            }
            
            // Construct DictionaryNode objects
            for (std::size_t i = 0; i < m_node_cache_capacity; i++)
            {
                DictionaryNode* new_node = new (new_node_cache + i) DictionaryNode(); // Placement new
                LLMALLOC_UNUSED(new_node);
            }
            
            m_node_cache = new_node_cache;
            m_node_cache_index = 0;
            return true;
        }

        LLMALLOC_FORCE_INLINE std::size_t hash(const Key& key) const
        {
            auto hash_value = m_hash(key);
            auto result = hash_value - (hash_value / m_table_size) * m_table_size;
            return result;
        }
};