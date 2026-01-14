# Technical Debt & Improvement Opportunities

This document tracks known issues, architectural concerns, and improvement opportunities identified through static analysis. Items are prioritized by severity and impact.

## 🔴 High Priority Issues

**All high priority issues have been resolved! 🎉**

---

## 🟡 Medium Priority Issues

### 1. Custom Module Loader vs node-gyp-build

**Status**: Not Fixed (Technical Debt)  
**Location**: `src/ts/bindings.ts` (lines 28-49)

**Issue**: Manual module loading loop checking multiple paths:

```typescript
const possiblePaths = [
  join(__dirname, "../build/dspx.node"),
  join(__dirname, "../../build/Release/dspx.node"),
  join(process.cwd(), "build/Release/dspx.node"),
  join(process.cwd(), "src/build/dspx.node"),
];

for (const path of possiblePaths) {
  try {
    DspAddon = require(path);
    break;
  } catch (err: any) {
    errors.push({ path, error: err.message });
  }
}
```

**Problem**: `node-gyp-build` package (already in dependencies) is designed to solve this exact problem more robustly.

**Recommendation**: Replace custom loader with:

```typescript
import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
const DspAddon = require("node-gyp-build")(join(__dirname, "../.."));
```

**Benefits**:

- Standard solution used by thousands of native modules
- Handles prebuild binaries
- Better error messages
- Less maintenance

---

### 2. Dead Code in DspPipeline::ProcessAsync

**Status**: Not Fixed (Code Cleanup)  
**Location**: `src/native/DspPipeline.cc`

**Issue**: The legacy `(buffer, options)` overload is never called because TypeScript wrapper always provides timestamps:

```cpp
// This else block is dead code:
else
{
    // Legacy mode: no timestamps
    timestamps = nullptr;
}
```

**TypeScript always generates timestamps**:

```typescript
if (!timestamps) {
  // Auto-generate timestamps from sample rate or indices
  timestamps = new Float32Array(numSamples);
  for (let i = 0; i < numSamples; i++) {
    timestamps[i] = options.sampleRate ? i / options.sampleRate : i;
  }
}
```

**Recommendation**:

1. Remove the dead code path
2. Simplify C++ signature to always require timestamps
3. Or add runtime assertion to catch if TypeScript behavior changes

---

## 🟢 Low Priority / Future Improvements

### 3. Brittle State Validation in LoadState

**Status**: Not Fixed (Design Decision)  
**Location**: `src/native/DspPipeline.cc` - `LoadState` method

**Issue**: Rigid validation `if (stageCount != m_stages.size())` makes all saved states invalid if pipeline structure changes.

**Impact**:

- Pipeline evolution is difficult
- All persisted states become invalid after stage changes
- No backward compatibility

**Possible Solutions** (Future):

1. **Semantic Versioning for States**: Add version field to state, support migrations
2. **Partial State Loading**: Load only compatible stages, skip others
3. **Stage Identification**: Use stage IDs/names instead of count
4. **State Migration Functions**: Define upgrade paths between versions

**Note**: This is a design trade-off. Current behavior is strict but predictable. Any change requires careful consideration of backward compatibility.

---

## ✅ Fixed Issues

### ~~1. Manual Memory Management in CircularBufferArray~~

**Status**: ✅ FIXED (October 2025)  
**Fix**: Replaced raw `T* buffer` with `std::unique_ptr<T[]>`, eliminated manual destructor and move operations (now use compiler-generated defaults per Rule of Zero)

### ~~2. Precision Loss in NapiArrayToVector<double>~~

**Status**: ✅ FIXED (October 2025)  
**Fix**: Now uses `DoubleValue()` for double types

### ~~3. DriftDetector Sample Rate Bug~~

**Status**: ✅ FIXED (October 2025)  
**Fix**: Now checks if sample rate changed and recreates detector

### ~~4. Missing <numeric> Header~~

**Status**: ✅ FIXED (October 2025)  
**Fix**: Added `#include <numeric>` to Policies.h

### ~~5. Fragile Build Configuration~~

**Status**: ✅ FIXED (October 2025)  
**Fix**: Explicitly listed all source files in binding.gyp

### ~~6. Dual Build Systems (node-gyp + cmake-js)~~

**Status**: ✅ FIXED (October 2025)
**Fix**: Removed cmake-js build system

---

## Contributing

If you'd like to tackle any of these issues:

1. Open an issue to discuss the approach
2. Reference this document in your PR
3. Update this file to mark items as "In Progress" or "Fixed"

## Prioritization Guide

- 🔴 **High**: Security, data corruption, or crash risks
- 🟡 **Medium**: Maintainability, confusion, or tech debt
- 🟢 **Low**: Nice-to-have improvements or future enhancements
