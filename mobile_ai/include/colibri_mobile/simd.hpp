#pragma once

#include <cstddef>

namespace colibri::mobile {

// Uses ARM NEON when the target exposes it and a scalar fallback elsewhere.
float dotProductF32(const float* lhs, const float* rhs, std::size_t count) noexcept;

}  // namespace colibri::mobile
