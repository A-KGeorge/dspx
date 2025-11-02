# Irregular Timestamps with Pipeline Processing

## Overview

This guide explains how to process data with irregular timestamps through DSP pipelines, including convolution and other stages.

## Important Concepts

### How Timestamps Flow Through Pipelines

Unlike some systems that require explicit "resampling to regular" operations, **dspx handles irregular timestamps throughout the entire pipeline**:

1. **Timestamps are passed to all stages** - Every stage receives the timestamp array
2. **Time-based vs Sample-based processing**:
   - **Time-based stages** (with `windowDuration` parameter) use timestamps to maintain time windows
   - **Sample-based stages** (with `windowSize` parameter) process samples sequentially, ignoring timestamps
3. **No explicit resampling needed** - Stages automatically adapt based on their parameters

### Stage Behavior with Irregular Timestamps

| Stage                                     | Timestamp Handling | Example                                                             |
| ----------------------------------------- | ------------------ | ------------------------------------------------------------------- |
| **Convolution**                           | Sample-based       | Applies kernel sample-by-sample, doesn't use timestamps             |
| **RMS** (with `windowDuration`)           | Time-based         | Maintains exact time window (e.g., 100ms) regardless of sample rate |
| **MovingAverage** (with `windowDuration`) | Time-based         | Uses timestamps to compute time-based averages                      |
| **Interpolate/Decimate/Resample**         | Resizing           | Adjusts timestamps based on resampling factor                       |

## Working Example

### Processing Irregular Data with Convolution

```javascript
import { createDspPipeline } from "dspx";

const pipeline = createDspPipeline();

// 1. Convolution Stage (sample-based)
// Applies kernel sample-by-sample, doesn't care about irregular timestamps
pipeline.convolution({
  kernel: new Float32Array([0.2, 0.6, 0.2]), // 3-tap smoothing
  mode: "moving",
});

// 2. RMS Stage (time-based)
// Uses timestamps to maintain EXACTLY 100ms window, even with irregular data
pipeline.Rms({
  mode: "moving",
  windowDuration: 100, // 100ms time window (NOT 100 samples!)
});

// Irregular sensor data (network jitter, variable sampling)
const irregularSamples = new Float32Array([1.2, 3.4, 2.1, 4.5, 3.3]);
const irregularTimestamps = new Float32Array([
  0, // t=0ms
  100, // Δ=100ms
  250, // Δ=150ms (jitter!)
  400, // Δ=150ms
  500, // Δ=100ms
]);

// Process - timestamps flow through entire pipeline
const output = await pipeline.process(irregularSamples, irregularTimestamps, {
  channels: 1,
});
```

### Output

```
Input data (irregular timing):
  [0] value=1.20, t=0ms, Δ=0ms
  [1] value=3.40, t=100ms, Δ=100ms
  [2] value=2.10, t=250ms, Δ=150ms
  [3] value=4.50, t=400ms, Δ=150ms
  [4] value=3.30, t=500ms, Δ=100ms

Output data (after convolution + time-based RMS):
  [0] 0.0000  (buffer filling)
  [1] 0.0000  (buffer filling)
  [2] 2.7000  (convolved: 0.2*1.2 + 0.6*3.4 + 0.2*2.1, then RMS over 100ms)
  [3] 2.8400
  [4] 3.7800
```

## Common Misconception: "Resample to Regular"

### ❌ Incorrect Conceptual Model

Some users expect to need an explicit "bridge" stage:

```javascript
// ❌ This conceptual model is NOT how dspx works
pipeline.Resample({ targetSampleRate: 1000 }); // ← Doesn't exist!
pipeline.convolution({ kernel: myKernel }); // ← Would be "regular" after bridge
pipeline.Rms({ windowSize: 100 }); // ← Would use samples, not time
```

This approach would:

1. Convert irregular → regular at 1000 Hz
2. Process everything in sample-domain afterward
3. Require explicit resampling step

### ✅ Correct dspx Model

**dspx handles irregular timestamps natively throughout:**

```javascript
// ✅ Actual dspx approach - timestamps flow through
pipeline.convolution({ kernel: myKernel, mode: "moving" });
pipeline.Rms({ mode: "moving", windowDuration: 100 }); // Uses timestamps!

// Process with original irregular timestamps
const output = await pipeline.process(samples, timestamps, { channels: 1 });
```

This approach:

1. **No explicit resampling** - timestamps flow through all stages
2. **Convolution** processes samples (ignores timestamps)
3. **RMS** uses timestamps to maintain 100ms time window
4. **More accurate** - preserves original timing information

## When to Use Each Approach

### Use Time-Based Windows (`windowDuration`)

✅ When processing irregular data from:

- IoT sensors with network jitter
- Event-driven systems
- Multi-rate systems
- Real-time streaming with variable latency

```javascript
pipeline.MovingAverage({
  mode: "moving",
  windowDuration: 1000, // 1 second, regardless of sample rate
});
```

### Use Sample-Based Windows (`windowSize`)

✅ When processing uniform data:

- Regular ADC sampling
- Fixed-rate audio
- Pre-resampled data

```javascript
pipeline.MovingAverage({
  mode: "moving",
  windowSize: 100, // 100 samples
});
```

## Mixing Time-Based and Sample-Based Stages

You can mix both types in a single pipeline:

```javascript
const pipeline = createDspPipeline();

// Sample-based: Apply filter based on sample count
pipeline.convolution({
  kernel: new Float32Array([...]),
  mode: "moving",
});

// Time-based: Maintain 100ms RMS window using timestamps
pipeline.Rms({
  mode: "moving",
  windowDuration: 100,
});

// Sample-based: Z-score normalize over 50 samples
pipeline.ZScoreNormalize({
  mode: "moving",
  windowSize: 50,
});

// Process with irregular timestamps
// - Convolution: uses samples
// - RMS: uses timestamps (100ms)
// - ZScoreNormalize: uses samples (50 samples)
const output = await pipeline.process(samples, timestamps, { channels: 1 });
```

## Resampling Operations

If you DO need to resample (change the number of samples), use:

### Rational Resampling (Existing Feature)

```javascript
// Resample from one rate to another (e.g., 44.1 kHz → 48 kHz)
pipeline.Resample({
  upFactor: 160,
  downFactor: 147,
  sampleRate: 44100,
});
```

This changes the number of samples and adjusts timestamps accordingly.

## Best Practices

1. **Always provide timestamps** for irregular data
2. **Use `windowDuration`** for stages that need time-based windows
3. **Use `windowSize`** for stages that operate on fixed sample counts
4. **Mix and match** - stages automatically handle their domain (time vs sample)
5. **Trust the pipeline** - timestamps flow through automatically

## Complete Working Example

See `test-irregular-example.js` for a complete, runnable example that demonstrates:

- Irregular timestamp input
- Convolution (sample-based)
- RMS (time-based with 100ms window)
- Successful processing

Run it with:

```bash
node test-irregular-example.js
```

## Summary

| Aspect                   | dspx Approach                        |
| ------------------------ | ------------------------------------ |
| **Timestamp Flow**       | Automatic through entire pipeline    |
| **Explicit Resampling**  | Not needed for time-based processing |
| **Time-Based Windows**   | Use `windowDuration` parameter       |
| **Sample-Based Windows** | Use `windowSize` parameter           |
| **Mixing Approaches**    | Fully supported in same pipeline     |
| **Irregular Data**       | Handled natively without conversion  |
