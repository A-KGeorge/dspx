# Wavelet Transform and Hilbert Envelope Guide

## Overview

This guide covers two advanced signal analysis techniques available in dspx:

1. **Wavelet Transform** - Multi-resolution time-frequency decomposition using Daubechies wavelets
2. **Hilbert Envelope** - Amplitude envelope extraction using the analytic signal

Both are implemented as chainable pipeline stages with C++ performance and SIMD optimizations.

---

## Wavelet Transform

### What is Wavelet Transform?

Wavelet transform decomposes a signal into **approximation** (low-frequency) and **detail** (high-frequency) components, providing both time and frequency localization. Unlike Fourier transforms which use fixed-width windows, wavelets adapt their resolution based on frequency.

### Supported Wavelets

| Wavelet | Name          | Filter Length | Use Case                         |
| ------- | ------------- | ------------- | -------------------------------- |
| `haar`  | Haar (db1)    | 2             | Edge detection, simplest wavelet |
| `db2`   | Daubechies-2  | 4             | General purpose, smooth signals  |
| `db3`   | Daubechies-3  | 6             | ECG analysis, feature extraction |
| `db4`   | Daubechies-4  | 8             | **Most popular**, good balance   |
| `db5`   | Daubechies-5  | 10            | Biomedical signals               |
| `db6`   | Daubechies-6  | 12            | Seismic data, vibration analysis |
| `db7`   | Daubechies-7  | 14            | Audio processing                 |
| `db8`   | Daubechies-8  | 16            | High-quality reconstruction      |
| `db9`   | Daubechies-9  | 18            | Medical imaging                  |
| `db10`  | Daubechies-10 | 20            | Precision applications           |

### Basic Usage

```typescript
import { createDspPipeline } from "dspx";

const pipeline = createDspPipeline();

// Apply Daubechies-4 wavelet transform
pipeline.WaveletTransform({ wavelet: "db4" });

const signal = new Float32Array([1, 3, 2, 4, 3, 5, 4, 6]);
const output = await pipeline.process(signal, { channels: 1 });

// Output format: [approximation_coeffs | detail_coeffs]
const halfLen = Math.floor(output.length / 2);
const approximation = output.slice(0, halfLen); // Low-frequency content
const details = output.slice(halfLen); // High-frequency content

console.log("Approximation:", approximation); // Smooth trend
console.log("Details:", details); // Rapid variations
```

### Output Format

The wavelet transform outputs a signal of the **same length** as the input, organized as:

```
[approximation_0, approximation_1, ..., detail_0, detail_1, ...]
 └─────────── halfLen samples ────────┘  └──── halfLen samples ────┘
```

- **Approximation coefficients**: Low-pass filtered and downsampled (smooth trend)
- **Detail coefficients**: High-pass filtered and downsampled (edges, transients)

### Multi-Channel Processing

Each channel is processed independently:

```typescript
// Stereo audio wavelet decomposition
const stereo = new Float32Array(2048); // 1024 samples × 2 channels

pipeline.WaveletTransform({ wavelet: "db4" });
const output = await pipeline.process(stereo, { channels: 2 });

// Left channel: output[0], output[2], output[4], ...
// Right channel: output[1], output[3], output[5], ...
```

### Performance Characteristics

| Wavelet | Filter Length | Throughput\* | SIMD Optimization       |
| ------- | ------------- | ------------ | ----------------------- |
| `haar`  | 2             | ~15M samp/s  | ✅ FirFilter (NEON/SSE) |
| `db4`   | 8             | ~12M samp/s  | ✅ FirFilter (NEON/SSE) |
| `db10`  | 20            | ~8M samp/s   | ✅ FirFilter (NEON/SSE) |

\*Approximate throughput on modern CPUs. Actual performance depends on hardware.

### Use Cases

#### 1. Signal Denoising

Remove high-frequency noise by zeroing detail coefficients:

```typescript
const pipeline = createDspPipeline();
pipeline.WaveletTransform({ wavelet: "db4" });

const noisy = new Float32Array([
  /* sensor data */
]);
const output = await pipeline.process(noisy, { channels: 1 });

// Zero out high-frequency details
const halfLen = Math.floor(output.length / 2);
output.fill(0, halfLen); // Zero detail coefficients

// Reconstruct (in practice, you'd use inverse wavelet transform)
```

#### 2. Feature Extraction

Extract transient events from signals:

```typescript
const pipeline = createDspPipeline();
pipeline.WaveletTransform({ wavelet: "db3" });

const ecg = new Float32Array([
  /* ECG signal */
]);
const output = await pipeline.process(ecg, { channels: 1 });

// QRS complex detection using detail coefficients
const halfLen = Math.floor(output.length / 2);
const details = output.slice(halfLen);
const qrsEvents = details.filter((d) => Math.abs(d) > threshold);
```

#### 3. Multi-Resolution Analysis

Analyze signals at different frequency scales:

```typescript
// Vibration monitoring: detect bearing faults
const pipeline = createDspPipeline();
pipeline.WaveletTransform({ wavelet: "db6" });

const vibration = new Float32Array([
  /* accelerometer data */
]);
const output = await pipeline.process(vibration, { channels: 1 });

const halfLen = Math.floor(output.length / 2);
const approximation = output.slice(0, halfLen); // Slow drift
const details = output.slice(halfLen); // High-freq impacts
```

### Mathematical Properties

- **Orthogonality**: Daubechies wavelets form an orthonormal basis
- **Energy Conservation** (Parseval): Sum of squared coefficients ≈ signal energy
- **Compact Support**: Wavelets have finite length (better time localization than sine waves)
- **Vanishing Moments**: Higher db wavelets better represent polynomial trends

### Pipeline Chaining

```typescript
// Wavelet decomposition → envelope detection
const pipeline = createDspPipeline();
pipeline
  .WaveletTransform({ wavelet: "db4" })
  .HilbertEnvelope({ windowSize: 128 })
  .MovingAverage({ mode: "moving", windowSize: 10 });

const signal = new Float32Array([
  /* data */
]);
const smoothedEnvelope = await pipeline.process(signal, { channels: 1 });
```

---

## Hilbert Envelope

### What is Hilbert Envelope?

The Hilbert envelope extracts the **instantaneous amplitude** of a signal, revealing its amplitude modulation (AM) envelope. It's computed using the **analytic signal** method via FFT.

### Basic Usage

```typescript
import { createDspPipeline } from "dspx";

const pipeline = createDspPipeline();

// Extract envelope with 256-sample window
pipeline.HilbertEnvelope({
  windowSize: 256,
  hopSize: 128, // Optional: overlap for smoother output
});

// Amplitude-modulated signal: carrier × envelope
const sampleRate = 1000;
const t = Array.from({ length: 1000 }, (_, i) => i / sampleRate);
const carrier = t.map((t) => Math.cos(2 * Math.PI * 50 * t)); // 50 Hz
const envelope = t.map((t) => 0.5 + 0.5 * Math.cos(2 * Math.PI * 2 * t)); // 2 Hz
const signal = new Float32Array(carrier.map((c, i) => c * envelope[i]));

const output = await pipeline.process(signal, { channels: 1 });

// Output is the detected envelope (should be close to original envelope)
console.log("Detected envelope:", output);
```

### Parameters

- **`windowSize`** (required): FFT window size (must be positive integer)

  - Larger windows: Better frequency resolution, more delay
  - Smaller windows: Faster tracking, less frequency precision
  - Typical values: 128, 256, 512, 1024

- **`hopSize`** (optional): Hop size between windows (default: `windowSize / 2`)
  - Controls overlap between successive windows
  - Smaller hop size: Smoother output, more computation
  - Must satisfy: `0 < hopSize <= windowSize`

### Algorithm

The Hilbert envelope is computed using the **analytic signal** method:

1. **FFT**: Transform signal to frequency domain
2. **Analytic Signal**: Zero negative frequencies, double positive frequencies
3. **IFFT**: Transform back to time domain
4. **Magnitude**: Compute `sqrt(real² + imag²)` for envelope

This approach is more robust than traditional Hilbert transform filters.

### Multi-Channel Processing

Each channel maintains independent sliding window state:

```typescript
// Stereo envelope detection
const pipeline = createDspPipeline();
pipeline.HilbertEnvelope({ windowSize: 256 });

const stereo = new Float32Array(2048); // 1024 samples × 2 channels
const envelopes = await pipeline.process(stereo, { channels: 2 });

// Left/right channels processed independently
```

### Performance Characteristics

| Window Size | Throughput\* | Latency     | Memory Usage   |
| ----------- | ------------ | ----------- | -------------- |
| 128         | ~8M samp/s   | 64 samples  | ~2 KB/channel  |
| 256         | ~6M samp/s   | 128 samples | ~4 KB/channel  |
| 512         | ~4M samp/s   | 256 samples | ~8 KB/channel  |
| 1024        | ~3M samp/s   | 512 samples | ~16 KB/channel |

\*Approximate throughput. Smaller windows are faster but less accurate.

### Use Cases

#### 1. AM Demodulation

Extract message signal from amplitude-modulated carrier:

```typescript
// Radio AM demodulation
const pipeline = createDspPipeline();
pipeline
  .HilbertEnvelope({ windowSize: 512 })
  .MovingAverage({ mode: "moving", windowSize: 10 }); // Smooth envelope

const amSignal = new Float32Array([
  /* received AM signal */
]);
const message = await pipeline.process(amSignal, { channels: 1 });
```

#### 2. EMG Envelope Detection

Extract muscle activation envelope from EMG signal:

```typescript
// EMG processing pipeline
const pipeline = createDspPipeline();
pipeline
  .Rectify({ mode: "full" }) // Full-wave rectification
  .HilbertEnvelope({ windowSize: 256 }) // Envelope detection
  .MovingAverage({ mode: "moving", windowSize: 50 }); // Smooth

const emg = new Float32Array([
  /* raw EMG data */
]);
const muscleActivation = await pipeline.process(emg, {
  sampleRate: 2000,
  channels: 1,
});
```

#### 3. Audio Dynamics Analysis

Track instantaneous amplitude variations:

```typescript
// Audio envelope follower
const pipeline = createDspPipeline();
pipeline.HilbertEnvelope({
  windowSize: 2048, // ~42ms at 48kHz
  hopSize: 512, // 75% overlap for smooth tracking
});

const audio = new Float32Array([
  /* audio samples */
]);
const dynamics = await pipeline.process(audio, {
  sampleRate: 48000,
  channels: 1,
});
```

#### 4. Bearing Fault Detection

Detect modulation patterns in vibration signals:

```typescript
// Vibration monitoring
const pipeline = createDspPipeline();
pipeline
  .HilbertEnvelope({ windowSize: 512 })
  .Rms({ mode: "moving", windowSize: 100 }); // Envelope power

const vibration = new Float32Array([
  /* accelerometer data */
]);
const faultIndicator = await pipeline.process(vibration, { channels: 1 });
```

### Edge Effects

The Hilbert envelope can produce small negative values near signal boundaries due to windowing artifacts. This is normal and typically falls in the range `[-1.0, 0.0]` for unit-amplitude signals.

To handle edge effects:

```typescript
// Post-process to ensure non-negative envelope
const output = await pipeline.process(signal, { channels: 1 });
const cleanEnvelope = output.map((x) => Math.max(0, x)); // Clip negatives
```

### Pipeline Chaining

```typescript
// Complex analysis: Wavelet → Hilbert → Smoothing
const pipeline = createDspPipeline();
pipeline
  .WaveletTransform({ wavelet: "db4" }) // Multi-resolution decomposition
  .HilbertEnvelope({ windowSize: 256 }) // Extract amplitude envelope
  .MovingAverage({ mode: "moving", windowSize: 20 }); // Smooth result

const bioSignal = new Float32Array([
  /* EEG/EMG/ECG */
]);
const features = await pipeline.process(bioSignal, {
  sampleRate: 1000,
  channels: 1,
});
```

---

## Combined Wavelet + Hilbert Analysis

### Multi-Scale Envelope Detection

Analyze envelope at different frequency scales:

```typescript
const pipeline = createDspPipeline();

// Decompose signal into frequency bands
pipeline.WaveletTransform({ wavelet: "db4" });

// Extract envelope of detail coefficients (high-frequency activity)
pipeline.HilbertEnvelope({ windowSize: 256 });

const signal = new Float32Array([
  /* data */
]);
const output = await pipeline.process(signal, { channels: 1 });

// Output is envelope of wavelet details (transient activity envelope)
```

### Modulation Analysis

Detect amplitude modulation at specific frequency scales:

```typescript
// Detect 10-50 Hz modulation in EMG
const pipeline = createDspPipeline();
pipeline
  .WaveletTransform({ wavelet: "db3" }) // Decompose
  .HilbertEnvelope({ windowSize: 512 }) // Envelope of details
  .Rms({ mode: "moving", windowSize: 100 }); // Smooth envelope power

const emg = new Float32Array([
  /* muscle activity */
]);
const modulationEnvelope = await pipeline.process(emg, {
  sampleRate: 2000,
  channels: 8, // 8-channel EMG
});
```

---

## Error Handling

### Wavelet Transform Errors

```typescript
// Invalid wavelet name
pipeline.WaveletTransform({ wavelet: "db99" });
// ❌ Error: Unknown wavelet 'db99'. Valid: haar, db2-db10

// Missing parameter
pipeline.WaveletTransform({});
// ❌ Error: Wavelet parameter is required
```

### Hilbert Envelope Errors

```typescript
// Missing windowSize
pipeline.HilbertEnvelope({});
// ❌ Error: windowSize is required

// Invalid windowSize
pipeline.HilbertEnvelope({ windowSize: 0 });
// ❌ Error: windowSize must be greater than 0

// Invalid hopSize
pipeline.HilbertEnvelope({ windowSize: 256, hopSize: 512 });
// ❌ Error: hopSize must be <= windowSize
```

---

## Best Practices

### Choosing a Wavelet

| Application              | Recommended Wavelet | Reason                         |
| ------------------------ | ------------------- | ------------------------------ |
| Quick prototyping        | `db4`               | Best all-around balance        |
| Edge detection           | `haar`              | Sharpest transitions           |
| ECG/heart rate           | `db3` or `db4`      | Good for QRS complex detection |
| EMG/muscle activity      | `db4` or `db5`      | Smooth enough for biomedical   |
| Audio/speech             | `db6` or `db7`      | Better frequency resolution    |
| Precision reconstruction | `db8` to `db10`     | Higher vanishing moments       |

### Choosing Window Size

For Hilbert envelope:

| Signal Type    | Sample Rate | Recommended Window | Rationale                     |
| -------------- | ----------- | ------------------ | ----------------------------- |
| Audio (speech) | 16 kHz      | 512-1024           | ~30-60ms for phoneme tracking |
| Audio (music)  | 48 kHz      | 2048-4096          | ~40-80ms for beat tracking    |
| EMG            | 2 kHz       | 256-512            | ~125-250ms for muscle bursts  |
| Vibration      | 10 kHz      | 512-1024           | ~50-100ms for impact events   |
| Radio AM       | 100 kHz     | 1024-2048          | Match modulation bandwidth    |

**Rule of thumb**: Window size ≈ (sample rate / lowest modulation frequency)

### Memory Considerations

Both stages use per-channel state:

- **Wavelet**: Minimal state (filter coefficients only, ~80 bytes/channel)
- **Hilbert**: Sliding window buffer (~4 × windowSize bytes/channel)

For 8-channel EMG at 2 kHz with 256-sample Hilbert window:

- Memory: 8 channels × (256 samples × 4 bytes) ≈ **8 KB**

---

## Performance Tips

1. **SIMD Optimization**: Wavelet transform automatically uses SIMD-optimized FirFilter (ARM NEON + x86 SSE)
2. **Batch Processing**: Process larger chunks to amortize FFT overhead in Hilbert stage
3. **Window Size**: Larger windows increase latency but improve frequency resolution
4. **Hop Size**: Use 50% overlap (hopSize = windowSize/2) for good quality/performance balance

---

## Complete Example

```typescript
import { createDspPipeline } from "dspx";

async function analyzeEMG() {
  const pipeline = createDspPipeline();

  // Multi-stage analysis pipeline
  pipeline
    .Rectify({ mode: "full" }) // 1. Full-wave rectification
    .WaveletTransform({ wavelet: "db4" }) // 2. Wavelet decomposition
    .HilbertEnvelope({ windowSize: 256 }) // 3. Envelope detection
    .MovingAverage({ mode: "moving", windowSize: 50 }); // 4. Smooth

  // 8-channel EMG data at 2 kHz
  const emg = new Float32Array(16000); // 2000 samples × 8 channels
  // ... fill with real data ...

  const features = await pipeline.process(emg, {
    sampleRate: 2000,
    channels: 8,
  });

  console.log("Extracted muscle activation features:", features);
}

analyzeEMG();
```

---

## References

- **Daubechies Wavelets**: Ingrid Daubechies, "Ten Lectures on Wavelets" (1992)
- **Hilbert Transform**: Marple, "Computing the discrete-time analytic signal via FFT" (1999)
- **EMG Processing**: Merletti & Parker, "Electromyography: Physiology, Engineering, and Non-Invasive Applications" (2004)

---

## See Also

- [Time-Series Guide](./time-series-guide.md) - Irregular timestamp processing
- [SIMD Optimizations](./SIMD_OPTIMIZATIONS.md) - Performance details
- [Filter API Guide](./FILTER_API_GUIDE.md) - All available filters
