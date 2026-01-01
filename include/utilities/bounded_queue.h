// NON THREAD SAFE ITEM QUEUE
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "../cpu/alignment_constants.h"
#include "../compiler/hints_hot_code.h"

template <typename T>
class SinglyLinkedList
{
    public:

        struct SinglyLinkedListNode
        {
            SinglyLinkedListNode* next = nullptr;
            T data;
        };

        void add_free_nodes(char* buffer, std::size_t capacity)
        {
            assert( buffer != nullptr);
            assert( capacity > 0 );

            m_capacity += capacity;

            for (std::size_t i{ 0 }; i < capacity; i++)
            {
                uint64_t address = reinterpret_cast<uint64_t>(buffer + (i * sizeof(SinglyLinkedListNode)) );
                push(reinterpret_cast<SinglyLinkedListNode*>(address));
            }
        }

        bool push(SinglyLinkedListNode* new_node)
        {
            if(m_size<m_capacity)
            {
                new_node->next = m_head;
                m_head = new_node;
                m_size++;
                
                return true;
            }
            
            return false;
        }

        SinglyLinkedListNode* pop()
        {
            if (m_head == nullptr)
            {
                return nullptr;
            }

            SinglyLinkedListNode* top = m_head;
            m_head = m_head->next;
            m_size--;
            return top;
        }

    private:
        LLMALLOC_ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) SinglyLinkedListNode* m_head = nullptr;
        std::size_t m_capacity = 0;
        std::size_t m_size = 0;
};

template <typename T, typename AllocatorType>
class BoundedQueue
{
    public:

        bool create(std::size_t capacity)
        {
            assert(capacity > 0);

            m_buffer_length = capacity * sizeof(typename SinglyLinkedList<T>::SinglyLinkedListNode) ;
            m_buffer = reinterpret_cast<char*>(AllocatorType::allocate(m_buffer_length));

            if(m_buffer == nullptr)
            {
                return false;
            }

            m_freelist.add_free_nodes(m_buffer, capacity);

            return true;
        }

        ~BoundedQueue()
        {
            if(m_buffer)
            {
                AllocatorType::deallocate(m_buffer, m_buffer_length);
            }
        }

        bool try_push(T t)
        {
            auto free_node = m_freelist.pop();

            if(free_node)
            {
                free_node->data = t;
                free_node->next = m_head;
                m_head = free_node;
                return true;
            }

            return false;
        }

        bool try_pop(T& t)
        {
            if (m_head == nullptr)
            {
                return false;
            }
            
            t = m_head->data;
            
            auto old_head = m_head;
            m_head = m_head->next;
            old_head->next = nullptr;
            m_freelist.push(old_head);
            
            return true;
        }
    
    private:
        LLMALLOC_ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) typename SinglyLinkedList<T>::SinglyLinkedListNode* m_head = nullptr;
        char* m_buffer = nullptr;
        std::size_t m_buffer_length = 0;
        SinglyLinkedList<T> m_freelist; // Underlying memory
};