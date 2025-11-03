# ARM/macOS CI Fix - November 2025

## Problem

GitHub Actions macOS CI runners (ARM-based Apple Silicon) were failing tests with:

```
Error: Failed to allocate aligned memory for FIR state buffer
```

Three specific tests failed:

- `Convolution - Stateful Moving Mode`
- `Convolution - Default Parameters`
- `Convolution - Reset State`

## Root Cause

The `std::aligned_alloc()` function has different requirements on different platforms:

- **Linux/Windows**: Size can be any value
- **macOS/POSIX strict**: **Size must be a multiple of the alignment**

Our code was allocating:

```cpp
size_t stateSize = (m_bufferSize * 2) * sizeof(float);  // e.g., 16 bytes
std::aligned_alloc(32, stateSize);  // ❌ 16 is not multiple of 32
```

## Solution

### 1. Fixed Aligned Allocation (C++ side)

**File:** `src/native/core/FirFilterNeon.h`

**Before:**

```cpp
size_t stateSize = (m_bufferSize * 2) * sizeof(float);
m_stateAligned = static_cast<float *>(std::aligned_alloc(32, stateSize));
```

**After:**

```cpp
size_t stateSize = (m_bufferSize * 2) * sizeof(float);

// Round up size to multiple of alignment (required by some aligned_alloc implementations)
constexpr size_t alignment = 32;
size_t alignedSize = ((stateSize + alignment - 1) / alignment) * alignment;

m_stateAligned = static_cast<float *>(std::aligned_alloc(alignment, alignedSize));
```

**Why it works:**

- Formula: `alignedSize = ((size + align - 1) / align) * align`
- Example: size=16, align=32 → alignedSize=32 ✅
- Example: size=64, align=32 → alignedSize=64 ✅

### 2. Added TypeScript Warnings

**File:** `src/ts/bindings.ts` - `convolution()` method

Added runtime warning that appears **once** when ARM users create convolution pipelines in moving mode:

```typescript
// Warn about ARM experimental status for moving mode (uses FirFilterNeon)
if (mode === "moving" && (process.arch === "arm64" || process.arch === "arm")) {
  // Use static flag to warn only once
  if (!(globalThis as any).__dspx_arm_convolution_warned) {
    console.warn(
      "\n⚠️  ARM NEON convolution optimization is experimental for moving mode.\n" +
        "   Mobile devices may not show speedup vs. scalar due to thermal/power constraints.\n" +
        "   See: https://github.com/A-KGeorge/dsp_ts_redis#arm-platform-notice\n"
    );
    (globalThis as any).__dspx_arm_convolution_warned = true;
  }
}
```

**Why TypeScript warnings matter:**

- Users see warnings in their console/logs when running on ARM
- Links to comprehensive documentation
- Doesn't rely on users seeing stderr from native code
- Respects Node.js/JavaScript logging conventions

## Testing

### Local Testing (Windows x64)

```bash
npm run build
node test-arm-warning.js
# Expected: No warning (not ARM platform)
```

### CI Testing (macOS ARM)

```bash
npm test
# Expected: Tests pass, warnings appear in console
```

## Documentation Updates

Three layers of ARM warnings now in place:

1. **C++ Runtime Warning** (`FirFilterNeon.h` constructor)

   - Prints to stderr when first NEON filter is created
   - Explains mobile thermal/power constraints

2. **TypeScript Runtime Warning** (`bindings.ts` convolution method)

   - Prints to console.warn when ARM convolution pipeline created
   - Links to README and detailed docs

3. **Comprehensive Documentation**
   - README.md: High-level ARM/Mobile Platform Notice
   - docs/ARM_PLATFORM_STATUS.md: Detailed troubleshooting guide
   - Source code comments: Technical explanations

## Related Files

- `src/native/core/FirFilterNeon.h` - Fixed aligned_alloc
- `src/ts/bindings.ts` - Added TypeScript warning
- `README.md` - ARM platform notice (already present)
- `docs/ARM_PLATFORM_STATUS.md` - Comprehensive ARM guide (already present)

## Future Work

Consider platform-specific allocation strategies:

- macOS: Use `posix_memalign()` instead of `aligned_alloc()`
- C++17: Use `std::aligned_alloc()` from `<cstdlib>`
- Fallback: Use `memalign()` or manual alignment with `malloc()`

Currently, the rounding approach is simplest and works across all platforms.
