#include "colibri_mobile/simd.hpp"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace colibri::mobile {

float dotProductF32(const float* lhs, const float* rhs,
                    std::size_t count) noexcept {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t sum = vdupq_n_f32(0.0F);
    std::size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        sum = vfmaq_f32(sum, vld1q_f32(lhs + i), vld1q_f32(rhs + i));
    }
    float result = vaddvq_f32(sum);
    for (; i < count; ++i) {
        result += lhs[i] * rhs[i];
    }
    return result;
#else
    float result = 0.0F;
    for (std::size_t i = 0; i < count; ++i) {
        result += lhs[i] * rhs[i];
    }
    return result;
#endif
}

}  // namespace colibri::mobile
