# FFT Optimization Implementation Notes

## Attempt 1: Packed Real FFT (REVERTED)

**Date:** November 1, 2025  
**Status:** ❌ REVERTED - Incorrect results and slower performance

### What We Tried

Implemented a "packed" real FFT algorithm that:

1. Packs N real samples as N/2 complex samples: `[(r0, r1), (r2, r3), ...]`
2. Performs N/2-point complex FFT (theoretically 2x faster)
3. Post-processes using symmetry to extract actual RFFT spectrum

### Why It Failed

1. **Incorrect post-processing formula**: The unpacking formula needs to be precisely correct

   - DC and Nyquist bins have special handling
   - Twiddle factors must match the packing scheme exactly
   - Off-by-one errors in angle calculations

2. **Failed correctness test**:

   ```
   Max reconstruction error: 1.00e+0 (should be < 1e-5)
   ```

3. **Actually SLOWER**: 41.96ms vs 33ms (original)
   - Post-processing overhead exceeded FFT savings
   - Additional loops and complex arithmetic
   - Cache-unfriendly access patterns

### Lessons Learned

1. **Real FFT packing is subtle**: Requires exact formulas from literature
2. **Test correctness FIRST**: Performance means nothing if results are wrong
3. **Simpler can be faster**: The "obvious" optimization isn't always better

### Correct References for Future Implementation

The proper packed real FFT formula (from FFTPACK/Numerical Recipes):

```cpp
// Forward: pack N reals as N/2 complex, do FFT, then unpack
for (k = 1; k < N/4; k++) {
    float theta = 2*M_PI*k/N;
    float wtemp = sin(0.5*theta);
    float wpr = -2.0*wtemp*wtemp;
    float wpi = sin(theta);

    // h1r + i*h1i = (Z[k] + conj(Z[n/2-k]))/2
    // h2r + i*h2i = (Z[k] - conj(Z[n/2-k]))/(2i)

    h1r = 0.5*(z[k].re + z[n2-k].re);
    h1i = 0.5*(z[k].im - z[n2-k].im);
    h2r = 0.5*(z[k].im + z[n2-k].im);
    h2i = -0.5*(z[k].re - z[n2-k].re);

    z[k].re = h1r + wpr*h2r - wpi*h2i;
    z[k].im = h1i + wpr*h2i + wpi*h2r;
    z[n2-k].re = h1r - wpr*h2r + wpi*h2i;
    z[n2-k].im = -h1i + wpr*h2i + wpi*h2r;
}
```

**Key insight**: The formulas are NOT symmetric - forward and inverse use different twiddle signs.

## Alternative Optimization Strategies

Since the packed FFT is complex to get right, here are better approaches:

### Strategy 1: Optimize the Cooley-Tukey Core ✅ RECOMMENDED

Current bottleneck is likely in `cooleyTukeyFFT()` itself:

- ✅ Already has SIMD AVX2/SSE butterflies
- ⚠️ Could improve cache locality
- ⚠️ Could use radix-4 for larger FFTs
- ⚠️ Could prefetch twiddle factors

**Expected gain**: 20-30% speedup  
**Risk**: Low (doesn't change algorithm)  
**Effort**: Medium

### Strategy 2: Reduce TypeScript Binding Overhead ✅ RECOMMENDED

Current `FftBindings.cc` has copy overhead:

- Allocates `std::vector<Complex>`
- Copies to two separate Float32Arrays
- Manual de-interleaving loop

**Optimization**:

```cpp
// Direct write to pre-allocated TypedArray
Napi::Float32Array output = Napi::Float32Array::New(env, (m_size/2 + 1) * 2);
m_engine->rfft_interleaved(input.Data(), output.Data());  // [re,im,re,im,...]
```

**Expected gain**: 10-15% speedup  
**Risk**: Low  
**Effort**: Low

### Strategy 3: Use FFTW Backend (Optional)

Replace custom FFT with battle-tested FFTW library:

- ✅ Extensively optimized (20+ years)
- ✅ Self-tuning "wisdom" system
- ⚠️ Adds external dependency
- ⚠️ License considerations (GPL vs MIT)

**Expected gain**: 50-100% speedup  
**Risk**: Medium (dependency management)  
**Effort**: High

## Recommended Next Steps

1. **Phase 1 - Low-hanging fruit** (do now):

   - Eliminate binding copy overhead
   - Use interleaved output format
   - Benchmark improvement

2. **Phase 2 - Core optimization** (if needed):

   - Profile `cooleyTukeyFFT()` hotspots
   - Improve cache access patterns
   - Consider radix-4 for large N

3. **Phase 3 - Advanced** (only if still needed):
   - Implement packed real FFT correctly (using validated reference code)
   - Or integrate FFTW as optional backend

## Benchmarks to Beat

**fft.js (Pure JavaScript):**

```
Small (1K):    3.67 M-samples/sec
Medium (65K):  90.69 M-samples/sec  ← Target
Large (1M):    62.63 M-samples/sec  ← Target
```

**dspx (Current):**

```
Small (1K):    50.87 M-samples/sec  ✅ Already winning
Medium (65K):  41.74 M-samples/sec  ❌ Need 2.2x speedup
Large (1M):    31.40 M-samples/sec  ❌ Need 2.0x speedup
```

**Target after optimizations:**

```
Small (1K):    60+ M-samples/sec   (keep lead)
Medium (65K):  100+ M-samples/sec  (beat fft.js by 10%)
Large (1M):    70+ M-samples/sec   (beat fft.js by 10%)
```

## References

1. **Numerical Recipes in C** - Chapter 12: Fast Fourier Transform

   - Has correct packed real FFT formulas
   - Clear explanation of packing scheme

2. **FFTPACK** - Fortran FFT library

   - `rfftf` and `rfftb` routines
   - Well-tested real FFT implementation

3. **KissFFT** - Simple C FFT library

   - `kiss_fftr.c` - real FFT implementation
   - Good reference for algorithm structure

4. **Intel MKL** - Math Kernel Library
   - `DftiComputeForward` with `DFTI_REAL`
   - Industry standard performance

## Conclusion

The packed real FFT optimization was premature. Better strategy:

1. Fix the easy wins first (bindings overhead)
2. Profile to find actual bottlenecks
3. Only implement complex optimizations if still needed
4. Always verify correctness before measuring performance!
