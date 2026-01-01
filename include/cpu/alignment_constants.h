#pragma once

#include <cstddef>

namespace AlignmentConstants
{
    // All constants are in bytes
    constexpr std::size_t CPU_CACHE_LINE_SIZE = 64;
    // SIMD REGISTER WIDTHS
    constexpr std::size_t SIMD_SSE42_WIDTH = 16;
    constexpr std::size_t SIMD_AVX_WIDTH = 32;
    constexpr std::size_t SIMD_AVX2_WIDTH = 32;
    constexpr std::size_t SIMD_AVX512_WIDTH = 64;
    constexpr std::size_t SIMD_AVX10_WIDTH = 64;
    constexpr std::size_t MINIMUM_VECTORISATION_WIDTH = SIMD_SSE42_WIDTH;
    constexpr std::size_t LARGEST_VECTORISATION_WIDTH = SIMD_AVX10_WIDTH;
    // VIRTUAL MEMORY PAGE SIZES ARE HANDLED IN os/virtual_memory.h
}