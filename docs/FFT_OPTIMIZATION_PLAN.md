# FFT Performance Optimization Plan

## Current Performance Gap

**Benchmark Results (Windows, Node v22.17.1, AMD Ryzen 9 5900X):**

```
Small FFT (1,024 samples):
  dspx:   50.87 M-samples/sec
  fft.js:  3.67 M-samples/sec  ✅ dspx wins (13.8x faster)

Medium FFT (65,536 samples):
  dspx:   41.74 M-samples/sec
  fft.js: 90.69 M-samples/sec  ❌ fft.js wins (2.2x faster)

Large FFT (1,048,576 samples):
  dspx:   31.40 M-samples/sec
  fft.js: 62.63 M-samples/sec  ❌ fft.js wins (2.0x faster)
```

**Analysis:** dspx wins on small FFTs (SIMD + C++ overhead is worth it), but loses on medium/large FFTs.

## Root Causes

### 1. RFFT Inefficiency (CRITICAL) 🔴

**Current Implementation** (`FftEngine.cc` lines 123-145):

```cpp
void FftEngine<T>::rfft(const T *input, Complex *output)
{
    // Pack real input into complex array (wasteful!)
    for (size_t i = 0; i < m_size; ++i)
    {
        m_workBuffer[i] = Complex(input[i], 0);
    }

    // Perform FULL N-point complex FFT (wasteful!)
    cooleyTukeyFFT(m_workBuffer.data(), false);

    // Only keep half spectrum (wasted the other half!)
    size_t halfSize = getHalfSize();
    for (size_t i = 0; i < halfSize; ++i)
    {
        output[i] = m_workBuffer[i];
    }
}
```

**Problems:**

- ❌ Doing full N-point complex FFT when only N/2+1 bins needed
- ❌ Packing real data into complex (2x memory, worse cache)
- ❌ Computing Hermitian-symmetric second half that's thrown away
- ❌ No exploitation of real-input symmetry properties

**What fft.js does (correct approach):**

```javascript
// fft.js uses even/odd decomposition:
// rfft(x[0..N-1]) = FFT(x_even) + W_k * FFT(x_odd)
// Computes two N/2 FFTs instead of one N FFT
// Result: ~40% faster than naive complex FFT approach
```

**Benchmark Impact:**

```
Current (wasteful):     N log N complex operations
Optimal (decomposition): N/2 log(N/2) real operations ≈ 2x speedup expected
```

### 2. Multiple Memory Copies in Bindings (MEDIUM) 🟡

**Current Implementation** (`FftBindings.cc` lines 283-320):

```cpp
Napi::Value FftProcessor::Rfft(const Napi::CallbackInfo &info)
{
    std::vector<Complex> output(halfSize);  // Heap allocation #1
    m_engine->rfft(input.Data(), output.data());

    Napi::Float32Array realOut = Napi::Float32Array::New(env, halfSize);  // #2
    Napi::Float32Array imagOut = Napi::Float32Array::New(env, halfSize);  // #3

    for (size_t i = 0; i < halfSize; ++i)  // Manual copy loop
    {
        realOut[i] = output[i].real();
        imagOut[i] = output[i].imag();
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("real", realOut);
    result.Set("imag", imagOut);
    return result;
}
```

**Problems:**

- ❌ 3 separate heap allocations
- ❌ Manual copy loop (not SIMD-optimized)
- ❌ Returning object with two arrays (object creation overhead)
- ❌ De-interleaving complex data after it's already computed

**Better Approach:**

```cpp
// Option 1: Direct write to pre-allocated TypedArrays
Napi::Float32Array realOut = Napi::Float32Array::New(env, halfSize);
Napi::Float32Array imagOut = Napi::Float32Array::New(env, halfSize);
m_engine->rfft_split(input.Data(), realOut.Data(), imagOut.Data());  // Direct write

// Option 2: Interleaved output (single array, better cache)
Napi::Float32Array output = Napi::Float32Array::New(env, halfSize * 2);
m_engine->rfft_interleaved(input.Data(), output.Data());  // [re,im,re,im,...]
```

### 3. Cache-Unfriendly Butterfly Operations (LOW) 🟢

**Current Implementation** (`FftEngine.cc` lines 365-400):
The Cooley-Tukey algorithm has good stride-1 access patterns, but:

- Twiddle factor access could be more cache-friendly
- SIMD paths could use aligned loads/stores
- Bit-reversal permutation could use cache-blocking

**Impact:** ~5-10% potential gain (less critical than #1 and #2)

## Optimization Priorities

### Priority 1: Implement Proper Real FFT (2x speedup expected) 🔴

**Implementation Plan:**

1. **Add specialized RFFT using even/odd decomposition:**

```cpp
template <typename T>
void FftEngine<T>::rfft_optimized(const T *input, Complex *output)
{
    // Split input into even/odd samples
    std::vector<T> even(m_size / 2);
    std::vector<T> odd(m_size / 2);

    for (size_t i = 0; i < m_size / 2; ++i)
    {
        even[i] = input[2 * i];
        odd[i] = input[2 * i + 1];
    }

    // Compute two N/2-point real FFTs
    std::vector<Complex> fft_even(m_size / 4 + 1);
    std::vector<Complex> fft_odd(m_size / 4 + 1);

    FftEngine<T> halfEngine(m_size / 2);
    halfEngine.rfft(even.data(), fft_even.data());
    halfEngine.rfft(odd.data(), fft_odd.data());

    // Combine using butterfly operations
    const T two_pi = static_cast<T>(2.0 * M_PI);
    for (size_t k = 0; k <= m_size / 2; ++k)
    {
        T angle = -two_pi * static_cast<T>(k) / static_cast<T>(m_size);
        Complex twiddle(std::cos(angle), std::sin(angle));

        Complex fft_e = (k < fft_even.size()) ? fft_even[k] : Complex(0, 0);
        Complex fft_o = (k < fft_odd.size()) ? fft_odd[k] : Complex(0, 0);

        output[k] = fft_e + twiddle * fft_o;
    }
}
```

**Alternative (Better):** Use existing Cooley-Tukey but with real-optimized butterflies

2. **Or use FFTW-style real transform:**
   - Use R2HC (Real-to-Half-Complex) format
   - In-place operation (no complex packing)
   - Exploit conjugate symmetry at butterfly level

**Expected Improvement:**

- Medium FFTs: 41 → 80+ M-samples/sec (match or beat fft.js)
- Large FFTs: 31 → 60+ M-samples/sec (match fft.js)

### Priority 2: Eliminate Copy Overhead (20% speedup) 🟡

**Implementation Plan:**

```cpp
// FftBindings.cc - New optimized binding
Napi::Value FftProcessor::RfftInterleaved(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Float32Array input = info[0].As<Napi::Float32Array>();

    size_t halfSize = m_engine->getHalfSize();

    // Single allocation: [re0, im0, re1, im1, ...]
    Napi::Float32Array output = Napi::Float32Array::New(env, halfSize * 2);

    // Direct write, no intermediate buffer
    m_engine->rfft_interleaved(input.Data(), output.Data());

    return output;
}
```

**Engine changes:**

```cpp
template <typename T>
void FftEngine<T>::rfft_interleaved(const T *input, T *output_interleaved)
{
    // Compute FFT
    rfft_optimized(input, m_workBuffer.data());

    // De-interleave directly to output (SIMD-friendly)
    size_t halfSize = getHalfSize();
    for (size_t i = 0; i < halfSize; ++i)
    {
        output_interleaved[2 * i] = m_workBuffer[i].real();
        output_interleaved[2 * i + 1] = m_workBuffer[i].imag();
    }
}
```

### Priority 3: Cache and SIMD Micro-optimizations (10% speedup) 🟢

**Ideas:**

- Use aligned memory allocations (`_mm_malloc`, `aligned_alloc`)
- Cache-block bit-reversal permutation
- Use FMA instructions in butterfly operations
- Prefetch twiddle factors
- Loop unrolling in SIMD butterflies

## Implementation Timeline

### Phase 1 (Immediate - 2x speedup):

1. ✅ Identify root cause (DONE - this document)
2. [ ] Implement even/odd decomposition RFFT
3. [ ] Benchmark against fft.js
4. [ ] Verify correctness with unit tests

### Phase 2 (Short-term - 1.2x additional):

1. [ ] Eliminate copy overhead in bindings
2. [ ] Add interleaved output option
3. [ ] Optimize TypedArray allocation patterns

### Phase 3 (Long-term - 1.1x additional):

1. [ ] Cache-blocking for bit-reversal
2. [ ] SIMD FMA butterfly optimizations
3. [ ] Aligned memory allocations
4. [ ] Twiddle factor prefetching

## Expected Final Performance

**With all optimizations:**

```
Small FFT (1,024):
  Current: 50 M-samples/sec
  Target:  60 M-samples/sec  (+20%)

Medium FFT (65,536):
  Current: 41 M-samples/sec
  Target:  100 M-samples/sec  (+2.4x) - BEATS fft.js

Large FFT (1,048,576):
  Current: 31 M-samples/sec
  Target:  75 M-samples/sec  (+2.4x) - BEATS fft.js
```

**Why we'll beat fft.js after fixes:**

- ✅ Proper RFFT algorithm (same efficiency)
- ✅ Native C++ (no V8 JIT overhead)
- ✅ SIMD optimizations (4-8x parallelism)
- ✅ Better memory layout (aligned, cache-friendly)

## References

1. **fft.js implementation**: https://github.com/indutny/fft.js

   - Uses radix-4 for better cache behavior
   - Real FFT via even/odd decomposition
   - Table-based twiddle factors

2. **FFTW wisdom**: http://www.fftw.org/

   - R2HC (Real-to-Half-Complex) format
   - Extensive use of SIMD intrinsics
   - Self-optimizing planner

3. **Cooley-Tukey variants**:
   - Radix-2: Current implementation
   - Radix-4: Better for large N (fewer passes)
   - Split-radix: Best theoretical complexity

## Action Items

**Immediate:**

- [ ] @Developer: Review this optimization plan
- [ ] Create feature branch: `feat/fft-optimization`
- [ ] Write failing test: "rfft should match fft.js performance"
- [ ] Implement Phase 1 optimizations
- [ ] Update benchmarks and validate improvements

**Success Metrics:**

- Medium FFT: Beat fft.js by 10%+
- Large FFT: Beat fft.js by 10%+
- No regression on small FFTs
- Pass all correctness tests
