# Test Cleanup Guide: Adding dispose() to Tests

## Overview

All `DspProcessor`/`DspPipeline` instances should call `.dispose()` when finished to ensure:

- Deterministic resource cleanup
- Prevention of memory leaks
- Safe shutdown during async operations
- Proper resource management in test suites

## Patterns

### Pattern 1: beforeEach/afterEach (Recommended for Multiple Tests)

```typescript
describe("My Test Suite", () => {
  let processor: DspProcessor;

  beforeEach(() => {
    processor = createDspPipeline();
  });

  afterEach(() => {
    processor.dispose(); // ✅ Always dispose in afterEach
  });

  test("test 1", async () => {
    processor.addStage("filter", {
      /* ... */
    });
    await processor.process(buffer);
    // No need to dispose - afterEach handles it
  });
});
```

### Pattern 2: Inline Disposal (For Single-Use Pipelines)

```typescript
test("my test", async () => {
  const pipeline = createDspPipeline();

  try {
    pipeline.addStage("filter", {
      /* ... */
    });
    await pipeline.process(buffer);
    // assertions...
  } finally {
    pipeline.dispose(); // ✅ Always dispose, even on error
  }
});
```

### Pattern 3: Multiple Pipelines in One Test

```typescript
test("comparing pipelines", async () => {
  const pipeline1 = createDspPipeline();
  const pipeline2 = createDspPipeline();

  try {
    // Use both pipelines...
    await pipeline1.process(buffer1);
    await pipeline2.process(buffer2);
    // assertions...
  } finally {
    pipeline1.dispose();
    pipeline2.dispose();
  }
});
```

## Test Files Status

### ✅ Files with dispose() implemented:

- MovingAverage.test.ts
- Chaining.test.ts
- ChannelMerge.test.ts
- WillisonAmplitude.test.ts
- WaveformLength.test.ts
- WaveletHilbert.test.ts
- RMS.test.ts
- Rectify.test.ts
- MeanAbsoluteValue.test.ts
- SlopeSignChange.test.ts
- LinearRegression.test.ts
- ComprehensiveChaining.test.ts

### ⚠️ Files needing disposal (inline pattern):

The following test files create pipelines inline and should add dispose() calls using Pattern 2 above:

- Variance.test.ts
- ZScoreNormalize.test.ts
- TimeSeries.test.ts
- TimeBasedVarianceAndZScore.test.ts
- TimeBasedRmsAndMav.test.ts
- TimeBasedExpiration.test.ts
- Tap.test.ts
- Stft.test.ts
- SpatialFilter.test.ts
- Snr.test.ts
- Resampling.test.ts
- PipelineFilter.test.ts
- PeakDetection.test.ts
- MelMfcc.test.ts
- MatrixAnalysis.test.ts
- LmsFilterPipeline.test.ts
- Integrator.test.ts
- Differentiator.test.ts
- Detrend.test.ts
- CrossCorrelation.test.ts
- Convolution.test.ts
- ClipDetection.test.ts
- ChebyshevBiquad.test.ts
- ChannelSelect.test.ts
- Beamformer.test.ts
- Autocorrelation.test.ts
- AdaptiveLMSFilter.test.ts
- RlsNlms.test.ts
- ListState.test.ts
- Redis.test.ts (special case - may use different cleanup)

## Migration Script (Future)

To automate adding dispose() to inline tests:

```bash
npm run test:add-dispose
```

This will scan all test files and add try/finally blocks with dispose() calls.

## Best Practices

1. **Always dispose** - Even if tests pass without it, dispose() prevents memory leaks
2. **Use finally blocks** - Ensures cleanup even when tests fail
3. **Dispose in order** - If pipelines depend on each other, dispose in reverse creation order
4. **Check async operations** - Don't dispose while `process()` is running
5. **Test the disposal** - Add tests that verify disposal prevents further use

## Example: Converting a Test

### Before:

```typescript
test("my test", async () => {
  const pipeline = createDspPipeline();
  pipeline.filter({ type: "lowpass", cutoff: 1000 });
  const output = await pipeline.process(buffer);
  assert.ok(output.length > 0);
});
```

### After:

```typescript
test("my test", async () => {
  const pipeline = createDspPipeline();

  try {
    pipeline.filter({ type: "lowpass", cutoff: 1000 });
    const output = await pipeline.process(buffer);
    assert.ok(output.length > 0);
  } finally {
    pipeline.dispose();
  }
});
```

## Why This Matters

Without `.dispose()`:

- Resources may not be freed until garbage collection
- Tests may leak memory when run in large batches
- Race conditions possible between GC and async operations
- No deterministic cleanup timing

With `.dispose()`:

- ✅ Immediate resource cleanup
- ✅ Predictable memory usage
- ✅ Safe async operation shutdown
- ✅ Professional resource management
