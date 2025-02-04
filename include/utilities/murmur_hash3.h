#ifndef _MURMUR_HASH3_H_
#define _MURMUR_HASH3_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

template <typename T, typename Enable = void>
struct MurmurHash3 
{
    std::size_t operator()(T h) const noexcept 
    {
        h ^= h >> 16;
        h *= 0x85ebca6b;
        h ^= h >> 13;
        h *= 0xc2b2ae35;
        h ^= h >> 16;
        return static_cast<std::size_t>(h);
    }
};

// 64 bit specialisation
template <typename T>
struct MurmurHash3<T, typename std::enable_if<std::is_same<T, uint64_t>::value>::type>
{
    std::size_t operator()(uint64_t h) const noexcept 
    {
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccd;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53;
        h ^= h >> 33;
        return static_cast<std::size_t>(h);
    }
};

#endif