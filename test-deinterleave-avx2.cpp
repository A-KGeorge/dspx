#include <iostream>
#include <iomanip>
#include <immintrin.h>

void deinterleave2Ch_avx2(const float *interleaved, float *ch0, float *ch1, size_t samples)
{
    const size_t simd_width = 8; // 8 samples per iteration
    const size_t simd_count = samples / simd_width;

    for (size_t i = 0; i < simd_count; ++i)
    {
        __m256 v0 = _mm256_loadu_ps(&interleaved[i * 16 + 0]); // [L0,R0,L1,R1,L2,R2,L3,R3]
        __m256 v1 = _mm256_loadu_ps(&interleaved[i * 16 + 8]); // [L4,R4,L5,R5,L6,R6,L7,R7]

        __m256 low_lane = _mm256_unpacklo_ps(v0, v1);
        __m256 high_lane = _mm256_unpackhi_ps(v0, v1);

        __m256 ch0_tmp = _mm256_shuffle_ps(low_lane, high_lane, 0x88);
        __m256 ch1_tmp = _mm256_shuffle_ps(low_lane, high_lane, 0xDD);

        __m256 ch0_vec = _mm256_permute2f128_ps(ch0_tmp, ch0_tmp, 0x20);
        __m256 ch1_vec = _mm256_permute2f128_ps(ch1_tmp, ch1_tmp, 0x20);

        _mm256_storeu_ps(&ch0[i * 8], ch0_vec);
        _mm256_storeu_ps(&ch1[i * 8], ch1_vec);
    }

    for (size_t i = simd_count * simd_width; i < samples; ++i)
    {
        ch0[i] = interleaved[i * 2 + 0];
        ch1[i] = interleaved[i * 2 + 1];
    }
}

int main()
{
    // Test with 8 samples (16 interleaved values)
    float interleaved[16] = {
        0, 10, 1, 11, 2, 12, 3, 13, // Samples 0-3
        4, 14, 5, 15, 6, 16, 7, 17  // Samples 4-7
    };

    float ch0[8], ch1[8];

    deinterleave2Ch_avx2(interleaved, ch0, ch1, 8);

    std::cout << "Input (interleaved):\n  ";
    for (int i = 0; i < 16; ++i)
        std::cout << interleaved[i] << " ";
    std::cout << "\n\nOutput Channel 0:\n  ";
    for (int i = 0; i < 8; ++i)
        std::cout << ch0[i] << " ";
    std::cout << "\n\nOutput Channel 1:\n  ";
    for (int i = 0; i < 8; ++i)
        std::cout << ch1[i] << " ";
    std::cout << "\n\nExpected:\n  Ch0: 0 1 2 3 4 5 6 7\n  Ch1: 10 11 12 13 14 15 16 17\n";

    return 0;
}
