# ARM Platform Status and Performance Notes

## ⚠️ Experimental Status

ARM NEON optimizations in this library are **experimental**. While they work correctly, performance on mobile and embedded ARM devices may not exceed scalar implementations.

## Challenges with Mobile ARM

### 1. **Thermal Throttling**

Mobile devices aggressively throttle CPU frequency under sustained load. Benchmarks that run for more than a few seconds will see degraded performance as the device heats up.

### 2. **Power Management**

ARM chips are optimized for power efficiency, not raw throughput. The CPU governor may scale down frequency during "bursty" workloads, making benchmarks inconsistent.

### 3. **Memory Hierarchy Differences**

- Cache sizes and latencies differ significantly from x86_64 desktop CPUs
- Memory bandwidth is often the bottleneck on mobile SoCs
- SIMD optimizations that work well on desktop may not translate to mobile

### 4. **Compiler Optimizations**

Mobile compilers (often older GCC/Clang versions) may not generate optimal NEON code compared to modern x86_64 compilers with mature SIMD support.

## Current Implementation Status

| Component                       | Status              | Notes                                                                                       |
| ------------------------------- | ------------------- | ------------------------------------------------------------------------------------------- |
| **FIR Filters (Moving Mode)**   | ✅ Implemented      | Uses guard-zone circular buffer + NEON vectorization. May not outperform scalar on mobile.  |
| **FIR Filters (Batch Mode)**    | ⚠️ Scalar Only      | Uses standard convolution (works on all platforms). NEON version removed due to complexity. |
| **Convolution (Large Kernels)** | ✅ FFT Fallback     | Auto-switches to FFT for kernels > 64 taps. FFT may be slower on mobile ARM.                |
| **Rectification**               | ✅ NEON Optimized   | Simple SIMD absolute value. Should work well.                                               |
| **Moving Average**              | ✅ Platform Generic | No SIMD - relies on compiler auto-vectorization.                                            |

## Tested Devices

- **Google Pixel 9 Pro XL (Tensor G4)**: Works correctly. Benchmarks show mixed results - some operations faster, some slower than naive JS implementations.

## What You Can Do

### If You're Using ARM in Production

1. **Run your own benchmarks** on your target hardware
2. **Monitor thermal behavior** if running continuous processing
3. **Consider disabling NEON** if scalar code performs better (file an issue!)

### If You Want to Contribute

We welcome ARM optimization expertise! Here's how you can help:

1. **Profile on different ARM chips** (Raspberry Pi, Apple M-series, server-grade ARM, etc.)
2. **Identify bottlenecks** using `perf` or ARM-specific profilers
3. **Submit optimized implementations** with benchmark comparisons
4. **Document thermal/power behavior** for different workload patterns

### Opening an Issue

When reporting ARM performance issues, please include:

- Device model and chip (e.g., "Raspberry Pi 4B - Cortex-A72")
- OS and kernel version
- Node.js version
- Benchmark results (include comparison with scalar/JS implementations)
- Thermal state (was device hot/throttling?)

## Known Issues

1. **FFT on mobile may be slower than naive implementations**

   - Cause unknown - needs profiling
   - Workaround: None currently

2. **Batch processing may not benefit from NEON**

   - Mobile memory bandwidth limitations
   - Thermal throttling during large batch operations

3. **Benchmarks show high variance on mobile**
   - Background processes interfere
   - CPU frequency scaling unpredictable
   - Solution: Run multiple iterations, take median

## References

- [ARM NEON Optimization Guide](https://developer.arm.com/documentation/102159/latest/)
- [NEON Intrinsics Reference](https://developer.arm.com/architectures/instruction-sets/intrinsics/)
- [Optimizing Code for Mobile](https://source.android.com/docs/core/perf)

## Future Work

Potential improvements (contributions welcome):

1. **Investigate FFT performance** - Why do naive JS implementations outperform on mobile?
2. **Add NEON optimizations for IIR filters** - Currently all scalar
3. **Implement block processing** - Better cache utilization on mobile
4. **Add CPU feature detection** - Disable NEON automatically if slower
5. **Benchmark on more devices** - Raspberry Pi, Apple Silicon, AWS Graviton

---

**Bottom Line:** ARM NEON support works but is not production-proven for mobile. Desktop ARM (M-series Mac, AWS Graviton) likely performs better. Help us improve mobile DSP!
