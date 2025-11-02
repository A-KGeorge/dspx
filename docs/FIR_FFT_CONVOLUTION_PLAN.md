# FIR FFT-Based Convolution Implementation Plan

**Date:** November 2, 2025  
**Goal:** Close the performance gap with Fili by implementing FFT-based convolution for large FIR kernels

---

## 🔍 Analysis: Why Fili is 3.9x Faster for Large FIR Filters

### Current Performance Gap

| Filter Size | Pure JS | Fili          | dspx           | Fili Advantage            |
| ----------- | ------- | ------------- | -------------- | ------------------------- |
| **FIR-64**  | 827µs   | 382µs (2.17x) | 306µs (2.70x)  | **dspx wins by 1.25x** ✅ |
| **FIR-128** | 1.58ms  | 400µs (3.94x) | 690µs (2.28x)  | Fili wins by 1.73x ⚠️     |
| **FIR-256** | 3.06ms  | 393µs (7.78x) | 1.54ms (1.99x) | **Fili wins by 3.9x** ⚠️  |

### Key Observation

**Fili's time is nearly constant (~400µs)** regardless of filter size (64, 128, or 256 taps). This is the signature of **FFT-based convolution**, which has O(N log N) complexity instead of O(N×M).

---

## 📐 Theory: Overlap-Add Convolution

### Direct Convolution (Current Implementation)

```
Complexity: O(M × N) where M = filter length, N = signal length
For FIR-256, 10K samples: 256 × 10,000 = 2.56M operations
```

### FFT Convolution (Overlap-Add Method)

```
1. Zero-pad filter to FFT size: O(1)
2. FFT of filter (done once): O(M log M)
3. For each block of L samples:
   a. FFT of signal block: O(L log L)
   b. Complex multiply: O(L)
   c. IFFT of result: O(L log L)

Total: O(N/L × L log L) = O(N log L)

For FIR-256, 10K samples with L=512:
  - Number of blocks: 10,000/512 ≈ 20 blocks
  - Per block: 512×log₂(512) = 512×9 = 4,608 ops
  - Total: 20 × 4,608 = 92,160 ops

Speedup: 2.56M / 92K = **27.8x theoretical speedup**
```

---

## 🎯 Implementation Strategy

### Phase 1: Add FFT Method to FirFilter

Modify `FirFilter.cc` to support two convolution methods:

```cpp
enum class ConvolutionMethod {
    DIRECT,    // Current O(N×M) implementation
    FFT,       // O(N log N) using FFTPACK
    AUTO       // Choose based on filter size
};
```

**Heuristic for AUTO mode:**

- N < 64: Use DIRECT (FFT overhead not worth it)
- 64 ≤ N < 256: Use DIRECT (our optimizations are competitive)
- N ≥ 256: Use FFT (clear win for large kernels)

### Phase 2: Implement Overlap-Add

```cpp
void FirFilter::processFFT(const T* input, T* output, size_t length) {
    // 1. Zero-pad coefficients to next power-of-2 FFT size
    size_t fftSize = nextPowerOf2(m_coefficients.size() + blockSize - 1);
    size_t blockSize = fftSize - m_coefficients.size() + 1;

    // 2. FFT of filter (computed once, cached)
    if (!m_filterFFT) {
        std::vector<T> paddedCoeffs(fftSize, 0);
        std::copy(m_coefficients.begin(), m_coefficients.end(),
                  paddedCoeffs.begin());
        m_filterFFT = computeRFFT(paddedCoeffs);
    }

    // 3. Process signal in blocks
    for (size_t blockStart = 0; blockStart < length; blockStart += blockSize) {
        size_t currentBlockSize = std::min(blockSize, length - blockStart);

        // Zero-pad block to FFT size
        std::vector<T> paddedBlock(fftSize, 0);
        std::copy(input + blockStart,
                  input + blockStart + currentBlockSize,
                  paddedBlock.begin());

        // FFT of signal block
        auto blockFFT = computeRFFT(paddedBlock);

        // Complex multiply in frequency domain
        for (size_t i = 0; i < fftSize/2 + 1; i++) {
            blockFFT[i] *= m_filterFFT[i];
        }

        // IFFT back to time domain
        auto result = computeIRFFT(blockFFT);

        // Overlap-add: add to output with proper indexing
        for (size_t i = 0; i < fftSize; i++) {
            size_t outputIdx = blockStart + i;
            if (outputIdx < length) {
                output[outputIdx] += result[i];
            }
        }
    }
}
```

### Phase 3: Integrate with Existing FFTPACK

We already have `FftEngine` from the FFT optimization project:

```cpp
#include "../core/FftEngine.h"

// Use existing FFTPACK infrastructure
dsp::core::fftpack::FftpackContext<T> m_fftContext;
```

---

## 📊 Expected Performance

### FIR-256, 10K samples

**Current (Direct):**

```
Time: 1.54ms
Operations: 2.56M multiplies
```

**After FFT (Overlap-Add):**

```
Estimated time: 1.54ms / 27.8 ≈ 55µs
Expected speedup: 28x faster!
Target: Beat Fili's 393µs → ✅ YES (55µs << 393µs)
```

### Why We'll Beat Fili

1. **Native C++** vs JavaScript (3-5x baseline advantage)
2. **FFTPACK optimization** (already 1.5x vs fft.js)
3. **SIMD-friendly memory layout** (additional 1.5-2x)
4. **No GC pauses** (consistent performance)

**Conservative estimate:** 10-15x faster than Fili for FIR-256+

---

## 🔧 Implementation Checklist

### Phase 1: Basic FFT Method

- [ ] Add `ConvolutionMethod` enum to `FirFilter.h`
- [ ] Add `m_method` parameter to constructor
- [ ] Implement method selection heuristic (`chooseMethod()`)
- [ ] Add `processFFT()` method skeleton

### Phase 2: Overlap-Add Core

- [ ] Implement filter FFT caching (`m_filterFFT`)
- [ ] Implement block-based processing loop
- [ ] Add zero-padding logic
- [ ] Implement overlap-add accumulation

### Phase 3: Integration & Testing

- [ ] Connect to existing `FftEngine`
- [ ] Handle edge cases (length < filter size)
- [ ] Add stateful mode support (maintain overlap buffer)
- [ ] Update unit tests for FFT method
- [ ] Benchmark and verify 10x+ improvement

### Phase 4: Optimization

- [ ] Cache FFT plans (avoid reallocation)
- [ ] Use in-place FFT when possible
- [ ] Optimize block size for cache efficiency
- [ ] Add AVX2 for complex multiply

---

## 🎓 References

1. **Overlap-Add Method:** Oppenheim & Schafer, "Discrete-Time Signal Processing", Chapter 8
2. **FFT Convolution:** Smith, "The Scientist and Engineer's Guide to DSP", Chapter 18
3. **FFTPACK:** Swarztrauber, "Vectorizing the FFTs", 1982
4. **Performance Analysis:** Intel, "Fast Convolution Using FFT"

---

## 🚀 Success Criteria

**Goal:** Beat Fili for **all** FIR filter sizes

| Filter Size | Current       | Target   | Success Metric   |
| ----------- | ------------- | -------- | ---------------- |
| **FIR-64**  | 1.25x vs Fili | 1.5x+    | Maintain lead ✅ |
| **FIR-128** | 0.58x vs Fili | 2.0x+    | Close gap ⚠️     |
| **FIR-256** | 0.26x vs Fili | **10x+** | Crush it! 🎯     |

**Stretch Goal:** Achieve 5-10x speedup over Fili for FIR-512 and larger.
