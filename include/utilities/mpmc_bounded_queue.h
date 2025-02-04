/*
    REFERENCE : THIS CODE IS A COSMETICALLY MODIFIED VERSION OF ERIK RIGTORP'S IMPLEMENTATION : https://github.com/rigtorp/MPMCQueue/ ( MIT Licence )
*/
#ifndef _MPMC_BOUNDED_QUEUE_H_
#define _MPMC_BOUNDED_QUEUE_H_

#include "../compiler/hints_hot_code.h"
#include "../cpu/alignment_constants.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <new>

template <typename T> struct Slot 
{
    ~Slot() 
    {
        if (turn & 1) 
        {
            destroy();
        }
    }

    template <typename... Args> void construct(Args &&...args) 
    {
        new (&storage) T(std::forward<Args>(args)...); // Placement new
    }

    void destroy() 
    {
        reinterpret_cast<T*>(&storage)->~T();
    }

    T&& move() { return reinterpret_cast<T&&>(storage); }

    ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) std::atomic<std::size_t> turn = { 0 };
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
};

template <typename T, typename AllocatorType>
class MPMCBoundedQueue
{
public:

    MPMCBoundedQueue() 
    {
        static_assert( alignof(Slot<T>) == AlignmentConstants::CPU_CACHE_LINE_SIZE, "Slot must be aligned to cache line boundary to prevent false sharing");
        static_assert(sizeof(Slot<T>) % AlignmentConstants::CPU_CACHE_LINE_SIZE == 0, "Slot size must be a multiple of cache line size to prevent false sharing between adjacent slots");
        static_assert(sizeof(MPMCBoundedQueue) % AlignmentConstants::CPU_CACHE_LINE_SIZE == 0, "Queue size must be a multiple of cache line size to prevent false sharing between adjacent queues");
        static_assert(offsetof(MPMCBoundedQueue, m_tail) - offsetof(MPMCBoundedQueue, m_head) == static_cast<std::ptrdiff_t>(AlignmentConstants::CPU_CACHE_LINE_SIZE), "head and tail must be a cache line apart to prevent false sharing");
    }

    bool create(const std::size_t capacity)
    {
        m_capacity = capacity;

        if (m_capacity < 1)
        {
            return false;
        }

        m_slots = static_cast<Slot<T>*>(AllocatorType::allocate((m_capacity + 1) * sizeof(Slot<T>)));

        if (reinterpret_cast<size_t>(m_slots) % alignof(Slot<T>) != 0)
        {
            AllocatorType::deallocate(m_slots, (m_capacity + 1) * sizeof(Slot<T>));
            return false;
        }

        for (size_t i = 0; i < m_capacity; ++i)
        {
            new (&m_slots[i]) Slot<T>(); // Placement new
        }

        return true;
    }

    ~MPMCBoundedQueue()
    {
        for (size_t i = 0; i < m_capacity; ++i)
        {
            m_slots[i].~Slot();
        }

        AllocatorType::deallocate(m_slots, (m_capacity + 1) * sizeof(Slot<T>));
    }

    template <typename... Args> void emplace(Args &&...args) 
    {
        auto const head = m_head.fetch_add(1);
        auto& slot = m_slots[modulo_capacity(head)];

        while (turn(head) * 2 != slot.turn.load(std::memory_order_acquire))
            ;

        slot.construct(std::forward<Args>(args)...);
        slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
    }

    template <typename... Args> bool try_emplace(Args &&...args)
    {
        auto head = m_head.load(std::memory_order_acquire);

        for (;;) 
        {
            auto& slot = m_slots[modulo_capacity(head)];
            if (turn(head) * 2 == slot.turn.load(std::memory_order_acquire)) 
            {
                if (m_head.compare_exchange_strong(head, head + 1))
                {
                    slot.construct(std::forward<Args>(args)...);
                    slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
                    return true;
                }
            }
            else 
            {
                auto const prev_head = head;
                head = m_head.load(std::memory_order_acquire);

                if (head == prev_head)
                {
                    return false;
                }
            }
        }
    }

    void push(const T& v) 
    {
        emplace(v);
    }

    bool try_push(const T& v) 
    {
        return try_emplace(v);
    }

    bool try_pop(T& v)  
    {
        auto tail = m_tail.load(std::memory_order_acquire);

        for (;;) 
        {
            auto& slot = m_slots[modulo_capacity(tail)];

            if (turn(tail) * 2 + 1 == slot.turn.load(std::memory_order_acquire))
            {
                if (m_tail.compare_exchange_strong(tail, tail + 1)) 
                {
                    v = slot.move();
                    slot.destroy();
                    slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
                    return true;
                }
            }
            else 
            {
                auto const prev_tail = tail;

                tail = m_tail.load(std::memory_order_acquire);

                if (tail == prev_tail)
                {
                    return false;
                }
            }
        }
    }

    std::size_t size() const
    {
        return static_cast<std::size_t>(m_head.load(std::memory_order_relaxed) - m_tail.load(std::memory_order_relaxed));
    }

private:
    FORCE_INLINE std::size_t modulo_capacity(std::size_t input) const
    {
        assert(m_capacity > 0);
        return input - (input / m_capacity) * m_capacity;
    }

    FORCE_INLINE std::size_t turn(std::size_t i) const { return i / m_capacity; }

    std::size_t m_capacity = 0;
    Slot<T>* m_slots = nullptr;

    ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) std::atomic<std::size_t> m_head = 0;
    ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) std::atomic<std::size_t> m_tail = 0;
};

#endif