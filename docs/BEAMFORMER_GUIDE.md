# Adaptive Beamforming Guide

## Overview

This guide explains how to use **Generalized Sidelobe Canceler (GSC)** architecture with **LMS/RLS adaptive filtering** for multi-channel spatial audio processing in `dspx`.

**Beamforming** spatially filters signals from microphone arrays to:

- Focus on a target sound source (speaker, instrument)
- Suppress interference from other directions (noise, reverberation)
- Adapt in real-time to changing acoustic environments

---

## Table of Contents

1. [What is the GSC?](#what-is-the-gsc)
2. [Quick Start](#quick-start)
3. [API Reference](#api-reference)
4. [Use Cases](#use-cases)
5. [Array Geometries](#array-geometries)
6. [Parameter Tuning](#parameter-tuning)
7. [Performance Optimization](#performance-optimization)

---

## What is the GSC?

The **Generalized Sidelobe Canceler** is a beamforming architecture that separates spatial filtering into two paths:

### Architecture

```
N-channel input (microphones)
         │
         ├──► Upper Path: Steering Weights w_s
         │         │
         │         └──► Desired Signal d[n]
         │
         └──► Lower Path: Blocking Matrix B
                   │
                   └──► Noise Reference x[n]
                               │
                         ┌─────┴─────┐
                         │  LMS/RLS  │
                         │  Adaptive │
                         │  Filter   │
                         └─────┬─────┘
                               │
                         Output e[n] (cleaned signal)
```

### Why GSC?

1. **Separates fixed and adaptive components**: Steering weights point the beam (fixed), adaptive filter removes residual noise (adaptive)
2. **Converts N-channel to 2-channel**: Works with standard `LmsStage`/`RlsStage`
3. **Proven performance**: Industry standard for speech enhancement, conferencing

---

## Quick Start

### Basic Conference Call System

```typescript
import { createDspPipeline, calculateBeamformerWeights } from "dspx";

// 1. Calculate beamformer weights for 8-mic linear array
const bf = calculateBeamformerWeights(
  8, // 8 microphones
  "linear", // Linear array (common for conference phones)
  0.0 // 0° = broadside (speaker directly in front)
);

// 2. Build adaptive beamforming pipeline
const pipeline = createDspPipeline();
pipeline
  .HighpassFilter({ cutoff: 80 }) // Remove DC and low-frequency noise
  .GscPreprocessor({
    numChannels: 8,
    steeringWeights: bf.steeringWeights,
    blockingMatrix: bf.blockingMatrix,
  })
  .LmsFilter({
    numTaps: 64, // 64-tap adaptive filter
    learningRate: 0.005, // Slow adaptation (stable)
    normalized: true, // NLMS (robust to signal power changes)
  });

// 3. Process 8-channel microphone data
const micData = new Float32Array(8000); // 1000 samples × 8 channels
// ... fill with microphone input ...

const result = await pipeline.process(micData, { channels: 8 });
// result: 1-channel cleaned audio (speaker voice)
```

---

## API Reference

### `calculateBeamformerWeights()`

Computes steering weights and blocking matrix for GSC architecture.

```typescript
function calculateBeamformerWeights(
  numChannels: number,
  arrayGeometry: "linear" | "circular" | "planar",
  targetAngleDeg: number,
  elementSpacing?: number // Default: 0.5 wavelengths
): BeamformerWeightsResult;
```

**Parameters:**

- **`numChannels`**: Number of microphones (≥ 2)
- **`arrayGeometry`**: Array configuration
  - `"linear"`: Uniform Linear Array (ULA) - most common
  - `"circular"`: Circular array - omnidirectional coverage
  - `"planar"`: 2D planar array - full 3D beamforming
- **`targetAngleDeg`**: Direction to focus beam (degrees)
  - 0° = broadside (perpendicular to array)
  - +90° / -90° = endfire (along array axis)
- **`elementSpacing`**: Microphone spacing in wavelengths
  - Default: 0.5λ (half-wavelength, prevents spatial aliasing)
  - For speech at 2 kHz: λ = 170mm → spacing = 85mm

**Returns:**

```typescript
{
  steeringWeights: Float32Array; // Size: numChannels
  blockingMatrix: Float32Array; // Size: numChannels × (numChannels-1)
  numChannels: number;
  geometry: string;
  targetAngleDeg: number;
}
```

### `GscPreprocessor()`

Applies GSC spatial preprocessing, converts N channels → 2 channels.

```typescript
pipeline.GscPreprocessor(params: {
  numChannels: number;
  steeringWeights: Float32Array;
  blockingMatrix: Float32Array;
}): this;
```

**Output:**

- **Channel 0**: Noise reference `x[n]` (for LMS/RLS primary input)
- **Channel 1**: Desired signal `d[n]` (for LMS/RLS reference)

**Must be followed by `LmsFilter()` or `RlsFilter()`**

---

## Use Cases

### 1. Conference Call System

**Scenario**: 8-microphone conference phone, suppress keyboard typing, AC hum, room reverberation.

```typescript
const bf = calculateBeamformerWeights(8, "linear", 0.0);

pipeline
  .HighpassFilter({ cutoff: 100 }) // Remove sub-vocal rumble
  .GscPreprocessor({
    numChannels: 8,
    steeringWeights: bf.steeringWeights,
    blockingMatrix: bf.blockingMatrix,
  })
  .LmsFilter({
    numTaps: 64,
    learningRate: 0.005,
    normalized: true,
  })
  .LowpassFilter({ cutoff: 3400 }); // Telephone bandwidth
```

**Why LMS?** Conference calls have relatively stable noise (fan noise, keyboard). LMS converges slowly but uses minimal CPU.

### 2. Smart Speaker (Voice Command)

**Scenario**: 4-mic circular array, user can speak from any direction.

```typescript
// Option 1: Fixed beam (if user position is known)
const bf = calculateBeamformerWeights(4, "circular", 45.0);

pipeline
  .GscPreprocessor({
    numChannels: 4,
    steeringWeights: bf.steeringWeights,
    blockingMatrix: bf.blockingMatrix,
  })
  .RlsFilter({
    numTaps: 32,
    lambda: 0.995, // Fast adaptation (track speaker movement)
    delta: 1.0,
  });

// Option 2: Dynamic beam steering (track loudest source)
// ... detect user angle via cross-correlation ...
// ... recalculate weights and update pipeline ...
```

**Why RLS?** Smart speakers need fast adaptation to track moving users and suppress dynamic noise (music playback, TV).

### 3. Acoustic Monitoring (Wildlife, Security)

**Scenario**: 6-mic planar array, detect and localize sound events.

```typescript
const bf = calculateBeamformerWeights(6, "planar", 30.0);

pipeline
  .BandpassFilter({ lowCutoff: 500, highCutoff: 8000 }) // Animal vocalizations
  .GscPreprocessor({
    numChannels: 6,
    steeringWeights: bf.steeringWeights,
    blockingMatrix: bf.blockingMatrix,
  })
  .LmsFilter({
    numTaps: 128, // Long filter (suppress diffuse noise)
    learningRate: 0.002,
    normalized: true,
  })
  .Rectify({ mode: "full" })
  .MovingAverage({ mode: "moving", windowSize: 100 }); // Envelope detection
```

### 4. Hands-Free Car Audio

**Scenario**: 2-mic array in car, suppress engine/road noise.

```typescript
const bf = calculateBeamformerWeights(2, "linear", 0.0);

pipeline
  .HighpassFilter({ cutoff: 200 }) // Remove engine rumble
  .GscPreprocessor({
    numChannels: 2,
    steeringWeights: bf.steeringWeights,
    blockingMatrix: bf.blockingMatrix,
  })
  .RlsFilter({
    numTaps: 48,
    lambda: 0.98, // Very fast adaptation (road noise changes quickly)
    delta: 0.5,
  })
  .BandpassFilter({ lowCutoff: 300, highCutoff: 3400 }); // Voice band
```

---

## Array Geometries

### Linear Array (ULA)

**Best for**: Directional applications (conference rooms, podiums)

```typescript
Mics:  [0]----[1]----[2]----[3]----[4]----[5]----[6]----[7]
       ◄─────────────────d = 85mm──────────────────────────►
                     (0.5λ at 2 kHz)
```

**Characteristics**:

- Sharp directivity in one plane (front/back)
- Ambiguity in perpendicular plane (can't distinguish left/right)
- Simple mounting (straight line)

**Element Spacing**:

- **Speech (300-3400 Hz)**: 85mm (0.5λ at 2 kHz center)
- **Broadband audio (20-20kHz)**: 8.5mm (0.5λ at 20 kHz)

### Circular Array

**Best for**: Omnidirectional coverage (smart speakers, video conferencing)

```typescript
              [0]
          [7]     [1]
       [6]    ●    [2]    ● = center
          [5]     [3]
              [4]
```

**Characteristics**:

- Uniform coverage in horizontal plane
- Can steer beam to any direction 0-360°
- Compact form factor

**Radius**:

- **Rule of thumb**: Radius = 0.5λ at lowest frequency
- **For 500 Hz**: r = 340m/s ÷ 500Hz ÷ 2 = 340mm

### Planar Array

**Best for**: Full 3D beamforming (sonar, radar, ultrasound)

```typescript
[0][1][2][3][4][5][6][7][8][9][10][11][12][13][14][15];
```

**Characteristics**:

- Control elevation AND azimuth
- Highest spatial resolution
- Most complex (requires 2D spacing optimization)

---

## Parameter Tuning

### Number of Taps (`numTaps`)

Controls adaptive filter length. More taps = better noise suppression, but slower adaptation.

| Taps   | Use Case                                   | Latency |
| ------ | ------------------------------------------ | ------- |
| 16-32  | Fast-changing noise (cars, moving sources) | Low     |
| 64-128 | Stationary noise (HVAC, fans)              | Medium  |
| 256+   | Diffuse reverberation, large rooms         | High    |

**Rule of thumb**: `numTaps = 1.5 × reverberation_time × sample_rate`

Example: 200ms reverberation @ 16kHz → `numTaps = 0.2s × 16000 = 3200` (impractical)
→ Use 128 taps + pre-filter to reduce reverberation.

### Learning Rate (LMS)

Controls adaptation speed. Higher = faster, but less stable.

| Learning Rate | Characteristics   | Use Case                           |
| ------------- | ----------------- | ---------------------------------- |
| 0.001-0.005   | Slow, very stable | Conference calls, stable noise     |
| 0.01-0.05     | Moderate          | General purpose                    |
| 0.1-0.5       | Fast, may diverge | Rapidly changing noise (with NLMS) |

**Always use `normalized: true`** (NLMS) for learning rates > 0.01.

### Forgetting Factor (RLS: `lambda`)

Controls memory length. Higher = longer memory, slower adaptation.

| Lambda     | Memory Length           | Use Case              |
| ---------- | ----------------------- | --------------------- |
| 0.99-0.999 | Long (100-1000 samples) | Stationary noise      |
| 0.98-0.99  | Medium (50-100 samples) | Slowly changing noise |
| 0.95-0.98  | Short (20-50 samples)   | Fast-changing noise   |

**Formula**: Effective memory = `1 / (1 - lambda)` samples

Example: `lambda = 0.99` → memory = 100 samples @ 16kHz = 6.25ms

---

## Performance Optimization

### Real-Time Guidelines

**Target Latency**:

- **Voice calls**: < 20ms (one-way)
- **Acoustic echo cancellation**: < 10ms
- **Live music**: < 5ms

**CPU Usage** (per 1000 samples @ 16kHz):

- `GscPreprocessor`: 0.2ms (8 channels → 2 channels)
- `LmsFilter` (64 taps): 0.5ms
- `RlsFilter` (64 taps): 2.0ms (4× slower than LMS)

### Trade-offs

| Technique        | Pro                       | Con                   |
| ---------------- | ------------------------- | --------------------- |
| More microphones | Better spatial resolution | Higher CPU, bandwidth |
| Larger `numTaps` | Better noise suppression  | Higher latency, CPU   |
| RLS vs LMS       | 3-5× faster convergence   | 4× higher CPU usage   |
| Normalized LMS   | Stable, robust            | Slightly higher CPU   |

### Memory Usage

```typescript
const memoryPerChannel = numTaps * 4;  // bytes (float32)

// Example: 8 mics, 128 taps
const totalMemory = 8 * 128 * 4 = 4096 bytes = 4 KB
```

---

## Advanced Topics

### Multi-Beam Beamforming

Process multiple beams simultaneously (e.g., speaker tracking).

```typescript
const beams = [0, 45, 90, 135, 180, 225, 270, 315].map((angle) =>
  calculateBeamformerWeights(8, "circular", angle)
);

// Create pipeline for each beam
const pipelines = beams.map((bf) => {
  const p = createDspPipeline();
  p.GscPreprocessor({
    /* bf params */
  }).LmsFilter({ numTaps: 32, learningRate: 0.01, normalized: true });
  return p;
});

// Process all beams, select loudest
const results = await Promise.all(
  pipelines.map((p) => p.process(micData, { channels: 8 }))
);

const loudestBeam = results.reduce((maxIdx, result, idx) => {
  const power = calculatePower(result);
  return power > results[maxIdx].power ? idx : maxIdx;
}, 0);
```

### Subband Processing

Apply beamforming independently in frequency subbands (better for wideband signals).

```typescript
// Not directly supported - use FFT-based beamforming instead
// Or apply multiple bandpass filters + separate GSC pipelines
```

---

## Troubleshooting

### No Noise Reduction

**Symptoms**: Output sounds same as input, no suppression.

**Causes**:

1. Learning rate too low → increase to 0.01-0.05
2. Desired signal and noise are correlated → ensure steering weights point at target
3. Not enough taps → increase `numTaps`

**Fix**:

```typescript
pipeline
  .GscPreprocessor({
    /* ... */
  })
  .LmsFilter({
    numTaps: 128, // Increase taps
    learningRate: 0.05, // Increase learning rate
    normalized: true, // Enable NLMS
  });
```

### Filter Diverges (Output Noise)

**Symptoms**: Output becomes louder/noisier over time.

**Causes**:

1. Learning rate too high
2. NLMS not enabled with high learning rate
3. RLS lambda too low

**Fix**:

```typescript
// Option 1: Reduce learning rate
.LmsFilter({ numTaps: 64, learningRate: 0.001, normalized: true })

// Option 2: Use RLS with higher lambda
.RlsFilter({ numTaps: 64, lambda: 0.999, delta: 0.1 })
```

### Target Signal Distorted

**Symptoms**: Speech sounds "processed" or has artifacts.

**Causes**:

1. Adaptive filter removing desired signal (double-talk)
2. Too many taps (over-fitting)

**Fix**:

```typescript
// Reduce taps
.LmsFilter({ numTaps: 32, learningRate: 0.005, normalized: true })

// Add voice activity detection (VAD) to freeze adaptation during speech
// (Not built-in - implement in application layer)
```

---

## References

1. **GSC Original Paper**: Griffiths & Jim, "An Alternative Approach to Linearly Constrained Adaptive Beamforming", IEEE Trans. Antennas Propagation, 1982
2. **Microphone Arrays**: _Microphone Array Signal Processing_, Benesty et al., Springer 2008
3. **Adaptive Filters**: _Adaptive Filter Theory_, Haykin, 5th Ed., Pearson 2013

---

## Examples

See `src/ts/__tests__/Beamformer.test.ts` for complete working examples:

- Basic 8-mic conference phone
- 4-mic circular array (smart speaker)
- Acoustic monitoring with 6-mic planar array
- 2-mic hands-free car system
- LMS vs RLS convergence comparison

---

**Next**: [Spatial Filter Guide](./SPATIAL_FILTER_GUIDE.md) - CSP for BCI/EEG classification
