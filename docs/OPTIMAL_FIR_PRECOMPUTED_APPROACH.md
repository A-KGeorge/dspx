# Pre-computed Optimal FIR Coefficients

## Overview

Instead of implementing the Parks-McClellan algorithm in C++ (which is complex and requires Eigen/numerical libraries), we take a **hybrid approach**:

1. **Design Time** (once, during library development): Use Python + scipy to generate optimal coefficients
2. **Ship Time**: Include pre-computed coefficients as TypeScript constants in the library
3. **Runtime**: Users just load the coefficients - **no Python dependency needed!**

## Why This Approach?

### ✅ Advantages

- **Zero runtime dependencies**: No Python, scipy, or numpy needed in production
- **Instant filter creation**: No computation overhead - coefficients are already optimal
- **Simple implementation**: Avoids complex Remez exchange algorithm in C++
- **Battle-tested**: Uses scipy's proven implementation for coefficient generation
- **Easy maintenance**: Regenerate coefficients anytime with one command
- **Type-safe**: Full TypeScript support with IntelliSense
- **Small footprint**: ~35 KB for comprehensive filter library

### ❌ What We Avoid

- **Complex C++ implementation**: Remez exchange algorithm is non-trivial
- **Numerical library dependencies**: No need for Eigen, FFTW, etc. just for filter design
- **Build complexity**: No additional compilation requirements
- **Maintenance burden**: Parks-McClellan is stable; coefficients rarely change

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     DESIGN TIME (Once)                      │
│  Developer runs: python scripts/generate_optimal_tables.py │
│                                                             │
│  ┌────────────┐    ┌─────────────┐    ┌────────────────┐  │
│  │ scipy.remez│───▶│ Optimal     │───▶│ TypeScript     │  │
│  │ (Python)   │    │ Coefficients│    │ Source File    │  │
│  └────────────┘    └─────────────┘    └────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                      SHIP WITH LIBRARY                      │
│         src/ts/optimal-fir-tables.ts (~35 KB)              │
│                                                             │
│  export const OPTIMAL_LOWPASS_COEFFS = {                   │
│    cutoff_0_2: new Float32Array([...87 values...])        │
│  };                                                         │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                 RUNTIME (User's Application)                │
│                    NO PYTHON NEEDED!                        │
│                                                             │
│  import { OPTIMAL_LOWPASS_COEFFS } from 'dsp-ts-redis';   │
│  const filter = new FirFilter(                             │
│    OPTIMAL_LOWPASS_COEFFS.cutoff_0_2,                     │
│    true                                                     │
│  );                                                         │
│                                                             │
│  🚀 32% faster than traditional window-based design!       │
└─────────────────────────────────────────────────────────────┘
```

## Pre-computed Filter Library

The library includes optimal coefficients for common scenarios:

### Lowpass Filters (80 dB stopband)

- **cutoff_0_1**: 127 taps (vs 189 window = 33% fewer)
- **cutoff_0_2**: 87 taps (vs 128 window = 32% fewer) ← Most common
- **cutoff_0_3**: 73 taps (vs 107 window = 32% fewer)
- **cutoff_0_4**: 61 taps (vs 89 window = 31% fewer)

### Highpass Filters (80 dB stopband)

- **cutoff_0_1**: 129 taps (vs 189 window = 32% fewer)
- **cutoff_0_2**: 97 taps (vs 141 window = 31% fewer)
- **cutoff_0_3**: 81 taps (vs 117 window = 31% fewer)

### Bandpass Filters (60 dB stopband)

- **band_0_15_0_35**: 101 taps (narrow band)
- **band_0_2_0_4**: 89 taps (moderate band)
- **band_0_25_0_45**: 73 taps (wide band)

### Notch Filters

- **60 Hz @ 1000 Hz sampling**: 89 taps (±2 Hz)
- **50 Hz @ 1000 Hz sampling**: 89 taps (±2 Hz)
- **60 Hz @ 500 Hz sampling**: 89 taps (±3.5 Hz)

## Usage Examples

### Basic Usage

```typescript
import { FirFilter } from "dsp-ts-redis";
import { OPTIMAL_LOWPASS_COEFFS } from "dsp-ts-redis/optimal-fir-tables";

// Create optimal filter - 87 taps instead of 128!
const filter = new FirFilter(OPTIMAL_LOWPASS_COEFFS.cutoff_0_2, true);

// Process signal - 32% faster than window-based design
const filtered = filter.process(signal);
```

### Helper Functions

```typescript
import {
  getOptimalLowpass,
  getPowerLineNotch,
} from "dsp-ts-redis/optimal-fir-tables";

// Automatically select best filter for cutoff
const coeffs = getOptimalLowpass(0.2); // Returns cutoff_0_2 (87 taps)
const filter = new FirFilter(coeffs, true);

// Get power line interference notch
const notch = new FirFilter(getPowerLineNotch(1000, 60), true);
```

### In Pipeline

```typescript
import { DspPipeline, Convolution } from "dsp-ts-redis";
import { OPTIMAL_LOWPASS_COEFFS } from "dsp-ts-redis/optimal-fir-tables";

const pipeline = new DspPipeline(1000);
pipeline.addStage(
  new Convolution({
    kernel: OPTIMAL_LOWPASS_COEFFS.cutoff_0_2,
  })
);

const output = pipeline.process(signal);
```

## Regenerating Coefficients

If you need to add more filters or change specifications:

```bash
# 1. Edit scripts/generate_optimal_tables.py
#    Add new configurations to the configs arrays

# 2. Regenerate the TypeScript file
python scripts/generate_optimal_tables.py

# 3. Rebuild and test
npm run build
node examples/optimal-fir-tables-demo.js
```

## Custom Coefficients

For non-standard filters, users can still design custom coefficients:

### Option 1: Python Design (offline)

```bash
python scripts/design_optimal_fir.py \
  --type lowpass \
  --taps 67 \
  --cutoff 0.25 \
  --output custom.json
```

Then load in TypeScript:

```typescript
const coeffs = require("./custom.json");
const filter = new FirFilter(new Float32Array(coeffs), true);
```

### Option 2: Embed in Code

```bash
python scripts/design_optimal_fir.py \
  --type lowpass \
  --taps 67 \
  --cutoff 0.25 \
  --format typescript \
  --output custom-filter.ts
```

Then import:

```typescript
import { OPTIMAL_FIR_COEFFS } from "./custom-filter";
const filter = new FirFilter(OPTIMAL_FIR_COEFFS, true);
```

## Performance Benefits

All pre-computed filters provide **30-50% better performance** than window-based designs:

| Filter Type   | Window Taps | Optimal Taps | Speedup   |
| ------------- | ----------- | ------------ | --------- |
| Lowpass 0.2π  | 128         | 87           | **1.47x** |
| Highpass 0.2π | 141         | 97           | **1.45x** |
| Bandpass      | 131         | 89           | **1.47x** |

The speedup comes from:

1. **32% fewer taps** → 32% fewer operations
2. **SIMD amplification** → Your AVX2/SSE2/NEON optimizations benefit proportionally
3. **Better cache locality** → Smaller kernels fit better in L1 cache

## Comparison to C++ Implementation

| Aspect              | Pre-computed Tables    | C++ Remez Implementation    |
| ------------------- | ---------------------- | --------------------------- |
| Runtime dependency  | None                   | Eigen, BLAS                 |
| Build complexity    | Simple                 | Complex                     |
| Coefficient quality | Optimal (scipy)        | Depends on implementation   |
| Maintenance         | Easy                   | Difficult                   |
| Design time         | Instant (pre-computed) | ~100ms per filter           |
| Filter creation     | 0ms                    | ~100ms                      |
| Code size           | 35 KB data             | 5-10 KB code + 500 KB Eigen |
| Flexibility         | Pre-defined filters    | Arbitrary filters           |
| **Best for**        | **Common cases (95%)** | **Custom cases (5%)**       |

## When to Use Each Approach

### ✅ Use Pre-computed Tables When:

- Standard lowpass/highpass/bandpass/notch filters
- Common cutoff frequencies (0.1π, 0.2π, 0.3π, 0.4π)
- Fixed sampling rates
- Production deployment (zero dependencies)
- 95% of use cases

### 🔧 Use Custom Design When:

- Non-standard cutoff frequencies
- Unusual transition widths
- Specialized filter requirements
- Research/experimentation
- 5% of use cases

For custom cases, users design offline with Python (once) and ship coefficients with their app.

## FAQ

### Q: What if I need a filter not in the pre-computed library?

**A**: Design it once offline with Python, then ship the coefficients with your application:

```bash
python scripts/design_optimal_fir.py --type lowpass --taps 91 --cutoff 0.234 --output my-filter.json
```

Then in your app:

```typescript
const myCoeffs = require("./my-filter.json");
const filter = new FirFilter(new Float32Array(myCoeffs), true);
```

### Q: Can I add more filters to the pre-computed library?

**A**: Yes! Edit `scripts/generate_optimal_tables.py`, add your configuration, and run:

```bash
python scripts/generate_optimal_tables.py
```

### Q: How much space do the coefficients take?

**A**: The current library is ~35 KB for 10 filters. Each filter is ~3-5 KB depending on tap count.

### Q: Why not implement Remez in C++?

**A**: Several reasons:

1. **Complexity**: Remez exchange is non-trivial (~1000 lines of careful numerical code)
2. **Dependencies**: Would require Eigen or similar (adds 500 KB+ to binary)
3. **Maintenance**: Numerical algorithms are tricky to debug and maintain
4. **Design time**: Filters are designed once, not at runtime
5. **Quality**: scipy's implementation is battle-tested over decades

### Q: What about WebAssembly or browser usage?

**A**: Pre-computed coefficients work perfectly! The TypeScript file compiles to JavaScript and works in browsers with zero dependencies.

### Q: Can I use this approach with other FIR designs (Kaiser, Blackman, etc.)?

**A**: Yes! The same pattern works. Generate coefficients offline, ship as TypeScript constants. Parks-McClellan is optimal, but you can pre-compute any design.

## Summary

The **pre-computed optimal coefficients approach** provides:

✅ **30-50% performance improvement** over window-based designs  
✅ **Zero runtime dependencies** (no Python, scipy, numpy)  
✅ **Instant filter creation** (no computation overhead)  
✅ **Simple implementation** (avoids complex C++ numerical code)  
✅ **Production-ready** (battle-tested scipy coefficients)  
✅ **Type-safe** (full TypeScript/JavaScript integration)

This is the **best solution for a Node.js library** because it combines optimal performance with zero complexity for end users.
