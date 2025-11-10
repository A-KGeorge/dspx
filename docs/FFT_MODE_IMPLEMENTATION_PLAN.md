# FFT Mode Implementation Plan

## Current State

After reviewing the codebase, I discovered that **stateful/moving FFT functionality already exists**:

### Existing Implementation

1. **`MovingFftFilter<T>`** (C++ core) - Full moving window FFT with circular buffers
2. **`MovingFftProcessor`** (N-API binding) - TypeScript wrapper for MovingFftFilter
3. **`StftStage`** (Pipeline adapter) - STFT implementation for pipeline use
4. **`.stft()`** method (TypeScript bindings) - User-facing STFT API

## Problem

The user requested `mode: "batch" | "moving"` for `.fft()` method, similar to how `MovingAverage` works.

## Analysis

There are **two architectural approaches**:

### Option 1: Duplicate STFT inside FftStage ❌

**Pros:**

- Consistent API with MovingAverage pattern

**Cons:**

- Duplicates existing MovingFftFilter functionality
- More complex: need CircularBufferArray, hop logic, windowing
- State serialization complexity
- Test duplication
- Maintenance burden

### Option 2: Use Existing STFT (Recommended) ✅

**Pros:**

- Reuses battle-tested MovingFftFilter
- No code duplication
- Leverages existing windowing, hop logic, state management
- `.stft()` already does exactly what `fft({ mode: 'moving' })` would do

**Cons:**

- Different API pattern than MovingAverage
- Users need to learn both `.fft()` and `.stft()`

## Recommendation: Hybrid Approach

**Make `.fft({ mode: 'moving' })` internally delegate to `.stft()`**

### TypeScript Implementation

```typescript
fft(params: fftParams): this {
  // ... validation ...

  const mode = params.mode || "batch";

  if (mode === "moving") {
    // Delegate to STFT with equivalent parameters
    return this.stft({
      windowSize: params.size,
      hopSize: params.hopSize ?? Math.floor(params.size / 2),
      method: params.type === 'fft' || params.type === 'rfft' ? 'fft' : 'dft',
      type: params.type?.includes('r') ? 'real' : 'complex',
      forward: params.forward ?? true,
      output: params.output || 'magnitude',
      window: 'hann', // Default window for moving mode
    });
  }

  // Batch mode: use stateless FftStage
  this.nativeInstance.addStage("fft", {
    mode: "batch",
    size: params.size,
    type,
    forward,
    output,
  });

  this.stages.push(`fft:${type}:${params.size}:batch:${direction}:${output}`);
  return this;
}
```

### Benefits

✅ **User convenience**: One `.fft()` method with `mode` parameter  
✅ **No duplication**: Reuses existing STFT implementation  
✅ **Consistent API**: Matches MovingAverage pattern  
✅ **Full features**: Windows, hop sizes, state management (already implemented in STFT)  
✅ **Minimal code**: Just parameter mapping, no new C++ code

## Alternative: Documentation Approach

If we want to keep implementations separate:

**Document clearly:**

- `.fft({ mode: 'batch' })` → Single FFT of entire input
- `.fft({ mode: 'moving' })` → Use `.stft()` instead (it's the same thing!)

**Update README:**

````markdown
## FFT Processing

### Single FFT (Batch Mode)

```typescript
pipeline.fft({ size: 1024, type: "rfft", output: "magnitude" });
```
````

### Moving Window FFT (STFT)

For sliding window FFT with overlap, use `.stft()`:

```typescript
pipeline.stft({
  windowSize: 512,
  hopSize: 256, // 50% overlap
  output: "magnitude",
});
```

Note: `fft({ mode: 'moving' })` is equivalent to `stft()` with same parameters.

```

## Conclusion

**I recommend the Hybrid Approach** because:

1. **Simplifies user experience** - one method with mode parameter
2. **No code duplication** - delegates to existing implementation
3. **Maintains consistency** - matches MovingAverage/RMS pattern
4. **Zero new bugs** - reuses tested code

The only code change needed is in `src/ts/bindings.ts` - about 15 lines to add the delegation logic.

## Implementation

Would you like me to:
1. ✅ **Implement the hybrid approach** (add mode parameter, delegate moving→stft)
2. ❌ **Implement full duplication** (CircularBufferArray in FftStage, 500+ lines of C++)
3. ❌ **Document-only** (explain that moving mode = stft, don't add mode parameter)

**My recommendation: Option 1** - It gives users the API they expect while being smart about reusing existing code.
```
