# Wavelet and Hilbert Functions - Quick Reference

## Standalone Functions in `advanced-dsp.ts`

The DWT and Hilbert Envelope functionality is now available as standalone async functions in addition to the pipeline stages.

---

## Discrete Wavelet Transform (DWT)

### Function Signature

```typescript
async function dwt(
  signal: Float32Array,
  wavelet: WaveletType
): Promise<Float32Array>;
```

### Supported Wavelets

- `"haar"` - Haar wavelet (simplest, db1)
- `"db1"` through `"db10"` - Daubechies wavelets (increasing filter length)

### Output Format

Returns concatenated array: `[approximation_coeffs | detail_coeffs]`

- First half: approximation (low-frequency) coefficients
- Second half: detail (high-frequency) coefficients
- Output length equals input length (may be padded)

### Examples

#### Basic Usage

```typescript
import { dwt } from "./src/ts/advanced-dsp.js";

const signal = new Float32Array([1, 2, 3, 4, 5, 4, 3, 2]);
const coeffs = await dwt(signal, "haar");

// Split into approximation and detail
const halfLen = Math.floor(coeffs.length / 2);
const approximation = coeffs.slice(0, halfLen);
const detail = coeffs.slice(halfLen);

console.log("Approximation (low-freq):", approximation);
console.log("Detail (high-freq):", detail);
```

#### Different Wavelets

```typescript
// Haar wavelet (fastest, simplest)
const haarCoeffs = await dwt(signal, "haar");

// db4 wavelet (good general-purpose choice)
const db4Coeffs = await dwt(signal, "db4");

// db10 wavelet (longer filter, better frequency separation)
const db10Coeffs = await dwt(signal, "db10");
```

#### Signal Denoising

```typescript
// Decompose signal
const coeffs = await dwt(noisySignal, "db4");

// Threshold detail coefficients (simple denoising)
const halfLen = Math.floor(coeffs.length / 2);
const threshold = 0.5;

for (let i = halfLen; i < coeffs.length; i++) {
  if (Math.abs(coeffs[i]) < threshold) {
    coeffs[i] = 0; // Remove small details (noise)
  }
}

// Note: To reconstruct, you'd need inverse DWT (not yet implemented)
```

---

## Hilbert Envelope

### Function Signature

```typescript
async function hilbertEnvelope(
  signal: Float32Array,
  windowSize: number = 512,
  hopSize?: number
): Promise<Float32Array>;
```

### Parameters

- `signal` - Input signal
- `windowSize` - FFT window size (power of 2 recommended, default 512)
- `hopSize` - Hop between windows (default: `windowSize/2` for 50% overlap)

### Output

Returns instantaneous amplitude envelope (same length as input)

### Examples

#### Basic Envelope Detection

```typescript
import { hilbertEnvelope } from "./src/ts/advanced-dsp.js";

const signal = new Float32Array(1024);
// ... fill with modulated signal ...

const envelope = await hilbertEnvelope(signal, 256);
console.log("Envelope:", envelope);
```

#### Amplitude Modulation Detection

```typescript
// Create AM signal: carrier modulated by envelope
const sampleRate = 1000;
const numSamples = 1000;
const signal = new Float32Array(numSamples);

for (let i = 0; i < numSamples; i++) {
  const t = i / sampleRate;
  const modulation = 0.5 + 0.5 * Math.cos(2 * Math.PI * 5 * t); // 5 Hz envelope
  const carrier = Math.cos(2 * Math.PI * 100 * t); // 100 Hz carrier
  signal[i] = modulation * carrier;
}

// Extract envelope
const detectedEnvelope = await hilbertEnvelope(signal, 128);

// The detected envelope should approximate the modulation signal
console.log("Max envelope:", Math.max(...detectedEnvelope));
console.log("Min envelope:", Math.min(...detectedEnvelope));
```

#### Custom Hop Size (Overlap Control)

```typescript
// 75% overlap (hopSize = windowSize/4)
const envelope75 = await hilbertEnvelope(signal, 512, 128);

// 50% overlap (default)
const envelope50 = await hilbertEnvelope(signal, 512, 256);

// No overlap (hopSize = windowSize)
const envelopeNoOverlap = await hilbertEnvelope(signal, 512, 512);
```

#### Beat Detection Example

```typescript
// Detect beats in audio signal
const audioSignal = new Float32Array(44100); // 1 second at 44.1kHz
// ... load audio data ...

// Extract envelope
const envelope = await hilbertEnvelope(audioSignal, 2048, 512);

// Find peaks in envelope (beats)
const threshold = 0.5;
const beats = [];

for (let i = 1; i < envelope.length - 1; i++) {
  if (
    envelope[i] > threshold &&
    envelope[i] > envelope[i - 1] &&
    envelope[i] > envelope[i + 1]
  ) {
    beats.push(i);
  }
}

console.log(`Detected ${beats.length} beats`);
```

---

## Implementation Notes

### Async Nature

Both functions are **async** and return **Promises**. Always use `await` or `.then()`:

```typescript
// ✅ Correct
const result = await dwt(signal, "haar");

// ✅ Also correct
dwt(signal, "haar").then((result) => {
  console.log(result);
});

// ❌ Wrong
const result = dwt(signal, "haar"); // result is a Promise, not Float32Array
```

### Pipeline vs Standalone

These functions internally use the C++ pipeline infrastructure:

- **Standalone functions**: Quick one-off computations
- **Pipeline stages**: Better for chaining multiple operations

```typescript
// Standalone (simpler for one operation)
const coeffs = await dwt(signal, "db4");

// Pipeline (better for multiple operations)
const processor = createDspPipeline();
processor
  .WaveletTransform({ wavelet: "db4" })
  .MovingAverage({ mode: "moving", windowSize: 10 })
  .HilbertEnvelope({ windowSize: 256 });
const result = await processor.process(signal, options);
processor.dispose();
```

### Performance

- Both functions leverage SIMD-optimized C++ implementations
- Small overhead from creating/disposing pipeline for each call
- For batch processing multiple signals, consider using pipelines directly

### Error Handling

```typescript
try {
  // Invalid wavelet type
  const result = await dwt(signal, "invalid" as any);
} catch (error) {
  console.error("Error:", error.message);
  // Error: WaveletTransform: Unknown wavelet type 'invalid'
}

try {
  // Invalid window size
  const envelope = await hilbertEnvelope(signal, -10);
} catch (error) {
  console.error("Error:", error.message);
  // Error: HilbertEnvelope: window size must be greater than 0
}
```

---

## See Also

- **Pipeline API**: `docs/WAVELET_HILBERT_GUIDE.md`
- **Full Tests**: `src/ts/__tests__/WaveletHilbert.test.ts`
- **All Advanced DSP Functions**: `src/ts/advanced-dsp.ts`
