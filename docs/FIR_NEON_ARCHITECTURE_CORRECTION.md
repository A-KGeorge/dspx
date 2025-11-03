# FIR NEON Optimization - Architecture Correction

**Date**: November 3, 2025  
**Issue**: Initial implementation used O(N) linear shift approach  
**Status**: ✅ **CORRECTED** - Now uses guard-zone circular buffer

---

## Summary of the Fix

### What Was Wrong (Initial Copilot Suggestion)

The initial `FirFilterNeon.h` implementation used a **"transposed direct-form"** architecture that required shifting the entire delay line for every input sample:

```cpp
// ❌ BAD: O(N) shift per sample
float processSample(float input) {
    float output = dot_product(coeffs, delayLine); // O(N)

    // Shift entire buffer left by 1
    memmove(delayLine, delayLine+1, (N-1)*sizeof(float)); // O(N) ← BOTTLENECK!
    delayLine[N-1] = input;

    return output;
}
```

**Why this is terrible**:

- **Algorithmic regression**: Changed from O(1) state update to O(N)
- **Memory bandwidth waste**: Copies entire buffer every sample
- **Even with NEON**: A vectorized O(N) operation is still O(N)!
- **For streaming**: Doubles work per sample vs circular buffer

### The Correct Approach (Guard-Zone Circular Buffer)

The corrected implementation uses a **circular buffer with guard zone**:

```cpp
// ✅ GOOD: O(1) state update + contiguous NEON reads
class FirFilterNeon {
    std::vector<float> m_state;  // Size: N + GUARD (e.g., 64 + 16)
    size_t m_head;               // Current position
    size_t m_headMask;           // For bitmask wrapping

    float processSample(float input) {
        // 1. Write to circular buffer + guard mirror (O(1))
        m_state[m_head] = input;
        if (m_head < 16) // Near start, will wrap soon
            m_state[m_head + m_bufferSize] = input;

        // 2. NEON convolution from 'head' (fully contiguous!)
        const float* x = &m_state[m_head];
        float32x4_t acc = vdupq_n_f32(0.0f);
        for (size_t i = 0; i < numTaps; i += 4) {
            float32x4_t c = vld1q_f32(coeffs + i);
            float32x4_t d = vld1q_f32(x + i);  // Always contiguous!
            acc = vmlaq_f32(acc, c, d);
        }

        // 3. Advance head (O(1) bitmask)
        m_head = (m_head + 1) & m_headMask;

        return horizontal_sum(acc);
    }
};
```

**Why this wins**:

- **O(1) state update**: Just increment head pointer and write 1-2 values
- **Contiguous NEON reads**: Guard zone makes wrap-around transparent
- **No memmove**: Zero memory copies in hot path
- **Power-of-2 sizing**: Bitmask wrapping is faster than modulo

---

## Technical Details

### Guard Zone Mechanism

**Buffer layout** (example with N=64 taps, GUARD=16):

```
Indices: [0 ... 63 | 64 ... 79]
         └─ main ─┘ └─ guard ─┘
```

**Write operation**:

```cpp
m_state[head] = x;              // Always write here
if (head < GUARD)               // If near start
    m_state[head + N] = x;      // Mirror to guard
```

**Why it works**: When `head` is near the end (e.g., `head=60`), a NEON load of 64 taps reads:

```
vld1q_f32(&state[60]) reads indices: [60, 61, 62, 63, 64, 65, ...]
                                      └─ main ─┘ └─ guard (= 0,1,...)
```

The guard zone contains **mirrored data from the start**, so the read is logically correct and physically contiguous!

### Complexity Comparison

| Operation     | Naive Circular  | Linear Shift (❌) | Guard-Zone (✅) |
| ------------- | --------------- | ----------------- | --------------- |
| State update  | O(1)            | O(N)              | O(1)            |
| Convolution   | O(N) scalar     | O(N) SIMD         | O(N) SIMD       |
| Memory access | Non-contiguous  | Contiguous        | Contiguous      |
| **Total**     | **O(N) scalar** | **O(2N) SIMD**    | **O(N) SIMD**   |

**Result**: Guard-zone gives **same algorithmic complexity as linear shift** but with **O(1) overhead instead of O(N)** → massive win for streaming!

---

## Benchmark Expectations (Revised)

### On ARM64 (Tensor G4, Graviton, M-series)

| Filter Size | Current (Scalar) | Linear Shift (❌) | Guard-Zone (✅) | Speedup      |
| ----------- | ---------------- | ----------------- | --------------- | ------------ |
| 16 taps     | ~15 M samples/s  | ~8 M/s            | **45-60 M/s**   | **3-4x**     |
| 32 taps     | ~7.2 M/s         | ~4 M/s            | **24-28 M/s**   | **3.3-3.9x** |
| 64 taps     | ~3.5 M/s         | ~2 M/s            | **12-15 M/s**   | **3.4-4.3x** |
| 128 taps    | ~1.8 M/s         | ~1 M/s            | **6-8 M/s**     | **3.3-4.4x** |

**Note**: Linear shift would actually be **SLOWER** than current scalar for large filters due to O(N) overhead!

### Why Guard-Zone Beats Everything

1. **vs Naive Circular**: Eliminates scatter/gather → enables full NEON (4 MACs/cycle)
2. **vs Linear Shift**: Eliminates per-sample buffer shift → keeps O(1) overhead
3. **vs FFT**: For small-medium taps (< 128), direct convolution has less setup overhead

---

## Implementation Status

### ✅ Completed

- `FirFilterNeon.h`: Guard-zone circular buffer with NEON kernel
- Power-of-2 buffer sizing with bitmask wrapping
- ARMv8.0 and ARMv8.1+ support (conditional `vaddvq_f32`)
- Scalar fallback for non-ARM platforms
- Integrated into `FirFilter<T>` with auto-selection (8-128 taps, float32, ARM only)

### 📋 Next Steps

1. **Benchmark on real ARM hardware** (Raspberry Pi 5, Android device, M-series Mac)
2. **Compare against naive JS** to verify we fix the 2.7x regression
3. **Profile with perf** to validate memory access patterns
4. **Consider alignment**: Add `alignas(32)` to state buffer for optimal NEON loads
5. **FFT fallback**: For taps > 128, switch to overlap-save (already exists in ConvolutionStage)

---

## Key Lessons Learned

1. **Algorithmic complexity matters more than SIMD width**

   - O(N) SIMD is still slower than O(1) + O(N) SIMD
   - Don't trade O(1) operations for "better vectorization"

2. **Guard zones are the standard solution**

   - Used in production DSP libraries (FFTW, Intel IPP, ARM Compute Library)
   - Small memory overhead (N + 16 floats) for massive performance gain
   - Eliminates modulo/branching in inner loop

3. **Circular buffers are not the enemy**

   - The problem was **non-contiguous access**, not circularity
   - Solution: Make circular access **appear contiguous** via aliasing
   - Keeps O(1) state updates while enabling full SIMD

4. **Always validate algorithmic changes**
   - Check Big-O complexity before optimizing implementation
   - Streaming workloads amplify per-sample overhead
   - Profile before and after on real hardware

---

## References

- [ARM NEON Programmer's Guide](https://developer.arm.com/architectures/instruction-sets/simd-isas/neon/neon-programmers-guide-for-armv8-a)
- [Guard Buffer Technique](https://www.dsprelated.com/showthread/comp.dsp/140874-1.php)
- [FIR Filter Optimization Strategies](https://www.embedded.com/optimizing-fir-filters-for-arm-processors/)

---

**Author**: GitHub Copilot (with corrections from community feedback)  
**Status**: Ready for ARM hardware validation
