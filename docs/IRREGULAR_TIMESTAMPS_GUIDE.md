# Irregular Timestamps with Pipeline Processing

## Overview

This guide explains how to process data with irregular timestamps through DSP pipelines. There are two main approaches:

1. **Pass-through approach** - Let timestamps flow through the pipeline (existing stages)
2. **Time-alignment approach** - Resample irregular data to uniform time grid (NEW: TimeAlignment stage)

Choose based on your use case:

- Use **pass-through** when stages can handle irregular timing naturally
- Use **TimeAlignment** when you need uniform sampling for downstream processing

---

## Table of Contents

- [Pass-Through Approach (Legacy)](#pass-through-approach-legacy)
- [TimeAlignment Approach (Recommended)](#timealignment-approach-recommended)
- [Comparison: TimeAlignment vs Legacy](#comparison-timealignment-vs-legacy)
- [Production Examples](#production-examples)

---

## Pass-Through Approach (Legacy)

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
| **WaveletTransform**                      | Sample-based       | Decomposes signal sample-by-sample, doesn't use timestamps          |
| **HilbertEnvelope**                       | Sample-based       | Windowed FFT processing, doesn't use timestamps                     |
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

// Sample-based: Wavelet decomposition (NEW!)
pipeline.WaveletTransform({ wavelet: "db4" });

// Sample-based: Hilbert envelope (NEW!)
pipeline.HilbertEnvelope({ windowSize: 256 });

// Process with irregular timestamps
// - Convolution: uses samples
// - RMS: uses timestamps (100ms)
// - ZScoreNormalize: uses samples (50 samples)
// - WaveletTransform: uses samples
// - HilbertEnvelope: uses samples
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

## Advanced Signal Analysis

The following sample-based stages work seamlessly with irregular timestamps in pipelines:

### Wavelet Transform (NEW in v0.2.0-alpha.15)

Decomposes signals into approximation and detail coefficients:

```javascript
pipeline.WaveletTransform({ wavelet: "db4" });

// Processes samples sequentially, ignoring timestamp irregularities
// Useful for: Multi-resolution analysis, denoising, feature extraction
```

**See [Wavelet & Hilbert Guide](./WAVELET_HILBERT_GUIDE.md) for detailed examples.**

### Hilbert Envelope (NEW in v0.2.0-alpha.15)

Extracts amplitude envelope using FFT-based analytic signal:

```javascript
pipeline.HilbertEnvelope({ windowSize: 256, hopSize: 128 });

// Windowed FFT processing, sample-based (doesn't use timestamps)
// Useful for: AM demodulation, EMG envelope, bearing fault detection
```

**See [Wavelet & Hilbert Guide](./WAVELET_HILBERT_GUIDE.md) for detailed examples.**

### Combined Multi-Scale Analysis

```javascript
// Irregular sensor data with jitter
const pipeline = createDspPipeline();
pipeline
  .WaveletTransform({ wavelet: "db4" }) // Multi-resolution (sample-based)
  .HilbertEnvelope({ windowSize: 256 }) // Envelope extraction (sample-based)
  .Rms({ mode: "moving", windowDuration: 100 }); // Time-based smoothing (uses timestamps!)

// The pipeline intelligently mixes sample-based and time-based processing
const output = await pipeline.process(samples, timestamps, { channels: 1 });
```

## Summary

| Aspect                   | Pass-Through Approach                | TimeAlignment Approach                         |
| ------------------------ | ------------------------------------ | ---------------------------------------------- |
| **Timestamp Flow**       | Automatic through entire pipeline    | Resamples to uniform grid at alignment stage   |
| **Explicit Resampling**  | Not needed for time-based processing | Explicit TimeAlignment stage required          |
| **Time-Based Windows**   | Use `windowDuration` parameter       | Not needed (uniform after alignment)           |
| **Sample-Based Windows** | Use `windowSize` parameter           | Can use sample-based after alignment           |
| **Gap Handling**         | Stage-dependent                      | Configurable policies (interpolate, hold, etc) |
| **Clock Drift**          | No compensation                      | Built-in drift compensation (regression, PLL)  |
| **Use Case**             | Simple pipelines, time-based windows | Complex pipelines, uniform sampling required   |

---

## TimeAlignment Approach (Recommended)

For production systems with irregular timestamps, use the **TimeAlignment stage** to resample data to a uniform time grid. This is especially important for:

- IoT sensors with network jitter
- GPS tracking with dropped packets
- Medical vitals monitoring with multiple sensor rates
- Audio streams with clock drift
- Event-driven data that needs uniform processing

### Basic Example

```javascript
import { createDspPipeline } from "dspx";

const pipeline = createDspPipeline();

// Resample irregular data to uniform 10 Hz (100ms intervals)
pipeline.TimeAlignment({
  targetSampleRate: 10, // Target sample rate (Hz)
  interpolationMethod: "linear", // linear, cubic, or sinc
  gapPolicy: "interpolate", // How to handle gaps
  gapThreshold: 2.0, // Detect gaps > 200ms
  driftCompensation: "regression", // Compensate for clock drift
});

// Irregular sensor data
const samples = new Float32Array([1.2, 3.4, 2.1, 4.5, 3.3]);
const timestamps = new Float32Array([0, 100, 250, 400, 500]); // Irregular!

// Process - outputs uniform 100ms grid
const result = await pipeline.process(samples, timestamps, { channels: 1 });
// Result: 6 samples at [0, 100, 200, 300, 400, 500]ms
```

### Configuration Options

#### Interpolation Methods

| Method   | Description                          | Use Case                          |
| -------- | ------------------------------------ | --------------------------------- |
| `linear` | Linear interpolation (C0 continuous) | Fast, general-purpose             |
| `cubic`  | Catmull-Rom spline (C1 continuous)   | Smooth trajectories (GPS, motion) |
| `sinc`   | Band-limited (windowed sinc, 8-tap)  | Audio, high-quality resampling    |

#### Gap Policies

| Policy        | Behavior                                    | Use Case                           |
| ------------- | ------------------------------------------- | ---------------------------------- |
| `interpolate` | Interpolate linearly across gap (default)   | Small gaps, continuous signals     |
| `zero-fill`   | Fill gaps with zeros                        | Audio (silence), missing data      |
| `hold`        | Hold last valid value before gap            | GPS (position), vitals (safe hold) |
| `extrapolate` | Extrapolate from last two samples           | Short-term prediction              |
| `error`       | Throw error when gap detected (strict mode) | Critical applications              |

#### Drift Compensation

| Method       | Description                                    | Use Case          |
| ------------ | ---------------------------------------------- | ----------------- |
| `none`       | No drift compensation (default)                | Short recordings  |
| `regression` | Linear regression to estimate true sample rate | Long recordings   |
| `pll`        | Phase-locked loop (adaptive tracking)          | Real-time streams |

### Production Examples

See [examples/timeseries/production-irregular-timestamps.ts](../../examples/timeseries/production-irregular-timestamps.ts) for complete runnable examples:

1. **IoT Sensor with Network Jitter** - Temperature sensor over WiFi
2. **GPS Tracking with Dropouts** - Vehicle tracking with signal loss
3. **Medical Vitals Monitoring** - Multi-rate sensors (HR, SpO2, BP)
4. **Audio Streams with Clock Drift** - Network audio with clock mismatch
5. **Event-Driven Comparison** - TimeAlignment vs legacy Interpolate
6. **Multi-Channel Sensor Fusion** - IMU data (accel + gyro)

Run examples:

```bash
node --import tsx examples/timeseries/production-irregular-timestamps.ts
```

---

## Comparison: TimeAlignment vs Legacy

### When to Use TimeAlignment

✅ **Use TimeAlignment when:**

- Data has irregular timestamps with gaps or jitter
- Downstream stages require uniform sampling
- Need explicit gap detection and handling
- Clock drift is present (long recordings)
- Processing multi-rate sensors (sensor fusion)

```javascript
pipeline
  .TimeAlignment({ targetSampleRate: 100, gapPolicy: "hold" })
  .filter({ type: "butterworth", mode: "lowpass", cutoffFrequency: 20 })
  .MovingAverage({ mode: "moving", windowSize: 10 }); // Sample-based OK after alignment
```

### When to Use Legacy Pass-Through

✅ **Use pass-through (no TimeAlignment) when:**

- Data already has uniform timestamps
- Using time-based windows (`windowDuration`)
- Stages naturally handle irregular timing
- No gap detection needed

```javascript
pipeline
  .convolution({ kernel: myKernel, mode: "moving" })
  .Rms({ mode: "moving", windowDuration: 100 }); // Time-based, handles irregular
```

### Key Differences

| Feature              | TimeAlignment             | Legacy Pass-Through                 |
| -------------------- | ------------------------- | ----------------------------------- |
| Gap Detection        | ✅ Configurable policies  | ❌ No detection                     |
| Clock Drift          | ✅ Compensation available | ❌ No compensation                  |
| Uniform Output       | ✅ Guaranteed             | ❌ Preserves irregular              |
| Time-Based Windows   | ⚠️ Not needed after       | ✅ Use windowDuration               |
| Sample-Based Windows | ✅ Use windowSize after   | ⚠️ May give unexpected results      |
| Interpolation        | ✅ Time-based (accurate)  | ❌ Index-based (legacy Interpolate) |

---

## Summary

| Aspect                   | dspx Approach                         |
| ------------------------ | ------------------------------------- |
| **Timestamp Flow**       | Automatic through entire pipeline     |
| **Explicit Resampling**  | Not needed for time-based processing  |
| **Time-Based Windows**   | Use `windowDuration` parameter        |
| **Sample-Based Windows** | Use `windowSize` parameter            |
| **Mixing Approaches**    | Fully supported in same pipeline      |
| **Irregular Data**       | Handled natively without conversion   |
| **Advanced Stages**      | Wavelet, Hilbert work with timestamps |
