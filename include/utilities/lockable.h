#pragma once

#include <type_traits>
#include "../cpu/alignment_constants.h"
#include "userspace_spinlock.h"

enum class LockPolicy
{
    NO_LOCK,
    USERSPACE_LOCK,
    USERSPACE_LOCK_CACHELINE_ALIGNED
};

// Since it is a template base class, deriving classes need "this" or full-qualification in order to call its methods
template <LockPolicy lock_policy>
class Lockable
{
public:

    using LockType = std::conditional_t<
        lock_policy == LockPolicy::USERSPACE_LOCK,
        UserspaceSpinlock<>, 
        UserspaceSpinlock<AlignmentConstants::CPU_CACHE_LINE_SIZE>
        >;

    Lockable()
    {
        m_lock.initialise();
    }

    void enter_concurrent_context()
    {
        if constexpr (lock_policy != LockPolicy::NO_LOCK)
        {
            m_lock.lock();
        }
    }

    void leave_concurrent_context()
    {
        if constexpr (lock_policy != LockPolicy::NO_LOCK)
        {
            m_lock.unlock();
        }
    }
private:
    LockType m_lock;
};