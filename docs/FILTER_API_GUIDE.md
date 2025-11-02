# Filter Design API - User Guide

## Quick Start

```typescript
import { createFilter, FirFilter, IirFilter } from "dspx";

// Method 1: Unified API
const filter = createFilter({
  type: "fir",
  mode: "lowpass",
  cutoffFrequency: 1000, // Hz
  sampleRate: 8000, // Hz
  order: 51,
  windowType: "hamming",
});

// Method 2: Direct class methods
const fir = FirFilter.createLowPass({
  cutoffFrequency: 1000,
  sampleRate: 8000,
  order: 51,
  windowType: "hamming",
});

const iir = IirFilter.createButterworthLowPass({
  cutoffFrequency: 1000,
  sampleRate: 8000,
  order: 4,
});

// Process signal
const filtered = await filter.process(signal);
```

## Filter Types

### FIR Filters

**Pros**: Always stable, linear phase possible, precise frequency response  
**Cons**: More taps = more computation

**Available Modes**:

- `lowpass` - Pass low frequencies, attenuate high
- `highpass` - Pass high frequencies, attenuate low
- `bandpass` - Pass only frequencies in specified band
- `bandstop` - Reject frequencies in specified band (notch filter)

**Window Types**:

- `hamming` - Good for general use (default)
- `hann` - Smooth frequency response
- `blackman` - Best sidelobe rejection
- `bartlett` - Simple triangular window

### IIR Butterworth Filters

**Pros**: Efficient (fewer coefficients), sharp roll-off  
**Cons**: Non-linear phase, can be unstable if poorly designed

**Available Modes**:

- `lowpass` - Maximally flat passband
- `highpass` - Maximally flat passband
- `bandpass` - Two Butterworth filters cascaded

**Orders**: 1-8 (higher order = sharper transition)

### First-Order IIR Filters

**Pros**: Very fast, low latency, minimal state  
**Cons**: Gentle rolloff (-20 dB/decade)

**Available Modes**:

- `lowpass` - Simple RC low-pass
- `highpass` - Simple RC high-pass

## API Reference

### `createFilter(options)`

Unified function to create any filter type.

```typescript
function createFilter(options: FilterOptions): FirFilter | IirFilter;

type FilterOptions = {
  type: "fir" | "butterworth";
  mode: "lowpass" | "highpass" | "bandpass" | "bandstop" | "notch";

  // For low-pass and high-pass
  cutoffFrequency: number; // Hz

  // For band-pass and band-stop
  lowCutoffFrequency?: number; // Hz
  highCutoffFrequency?: number; // Hz

  sampleRate: number; // Hz
  order: number; // Number of taps (FIR) or filter order (IIR)

  // FIR only
  windowType?: "hamming" | "hann" | "blackman" | "bartlett";
};
```

### `FirFilter` Class

```typescript
class FirFilter {
  // Factory methods
  static createLowPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
    order: number;
    windowType?: string;
  }): FirFilter;

  static createHighPass(options: {...}): FirFilter;
  static createBandPass(options: {...}): FirFilter;
  static createBandStop(options: {...}): FirFilter;

  // Instance methods
  process(input: Float32Array): Promise<Float32Array>;
  processSample(sample: number): Promise<number>;
  reset(): void;
  getOrder(): number;
  getCoefficients(): Float32Array;
}
```

### `IirFilter` Class

```typescript
class IirFilter {
  // Butterworth factory methods
  static createButterworthLowPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
    order: number;
  }): IirFilter;

  static createButterworthHighPass(options: {...}): IirFilter;
  static createButterworthBandPass(options: {...}): IirFilter;

  // First-order factory methods
  static createFirstOrderLowPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
  }): IirFilter;

  static createFirstOrderHighPass(options: {...}): IirFilter;

  // Instance methods
  process(input: Float32Array): Promise<Float32Array>;
  processSample(sample: number): Promise<number>;
  reset(): void;
  getOrder(): number;                        // Returns max(feedforward, feedback) order
  getBCoefficients(): Float32Array;          // Feedforward coefficients
  getACoefficients(): Float32Array;          // Feedback coefficients
  getFeedforwardOrder(): number;             // B coefficient order
  getFeedbackOrder(): number;                // A coefficient order
  isStable(): boolean;
}
```

## Usage Examples

### Example 1: Anti-Aliasing Filter

```typescript
const antiAlias = createFilter({
  type: "butterworth",
  mode: "lowpass",
  cutoffFrequency: 4000, // Nyquist - 1000 Hz
  sampleRate: 10000,
  order: 4,
});

const filtered = await antiAlias.process(rawSignal);
```

### Example 2: DC Offset Removal

```typescript
const dcBlocker = IirFilter.createFirstOrderHighPass({
  cutoffFrequency: 20, // Remove below 20 Hz
  sampleRate: 8000,
});

const noDC = await dcBlocker.process(signal);
```

### Example 3: Voice Band Extraction

```typescript
const voiceFilter = FirFilter.createBandPass({
  lowCutoffFrequency: 300, // Telephone voice band
  highCutoffFrequency: 3400,
  sampleRate: 8000,
  order: 101,
  windowType: "blackman", // Best frequency selectivity
});

const voiceOnly = await voiceFilter.process(audio);
```

### Example 4: 50/60 Hz Notch Filter

```typescript
// Remove powerline hum
const notch50Hz = createFilter({
  type: "fir",
  mode: "notch",
  lowCutoffFrequency: 48,
  highCutoffFrequency: 52,
  sampleRate: 8000,
  order: 201, // Higher order = narrower notch
});

const clean = await notch50Hz.process(noisySignal);
```

### Example 5: Real-Time Sample-by-Sample

```typescript
const realtimeFilter = createFilter({
  type: "butterworth",
  mode: "lowpass",
  cutoffFrequency: 1000,
  sampleRate: 8000,
  order: 4,
});

// Stream processing
for (const sample of incomingData) {
  const filtered = await realtimeFilter.processSample(sample);
  sendToOutput(filtered);
}
```

### Example 6: Batch Processing with State

```typescript
const filter = FirFilter.createLowPass({
  cutoffFrequency: 1000,
  sampleRate: 8000,
  order: 51,
});

// Process first batch
const batch1 = await filter.process(chunk1);

// Process second batch (state maintained)
const batch2 = await filter.process(chunk2);

// Reset state if needed
filter.reset();
```

## Performance Guide

### Choosing Filter Type

| Use Case             | Recommended Filter            |
| -------------------- | ----------------------------- |
| Audio processing     | FIR (linear phase)            |
| Real-time control    | IIR Butterworth (low latency) |
| Voice communications | FIR band-pass                 |
| Powerline removal    | FIR notch (narrow band)       |
| DC removal           | First-order IIR high-pass     |
| Anti-aliasing        | IIR Butterworth (efficiency)  |

### FIR Order Guidelines

- **Anti-aliasing**: 51-101 taps
- **Voice band-pass**: 101-201 taps
- **Notch filter**: 151-301 taps
- **General smoothing**: 31-51 taps

**Rule of thumb**: Transition width ≈ (4-8) / order

### IIR Order Guidelines

- **Order 2**: Basic smoothing, gentle rolloff
- **Order 4**: Good selectivity, common choice
- **Order 6-8**: Sharp transition, more latency

**Rule of thumb**: Each order adds ~6 dB/octave rolloff

## Validation and Error Handling

The API automatically validates:

✅ Cutoff frequency ≤ Nyquist frequency (sampleRate / 2)  
✅ Normalized cutoff frequency must be in range (0, 1.0]  
✅ Order is in valid range (FIR: > 0, IIR: 1-8)  
✅ Band-pass/stop: low cutoff < high cutoff  
✅ All parameters are positive numbers

**Important Note:** Cutoff frequencies are normalized internally as `cutoffFreq / (sampleRate / 2)`, resulting in a range of (0, 1.0] where 1.0 represents the Nyquist frequency. Both FIR and IIR filters accept cutoff values up to and including the Nyquist frequency.

Error messages:

```typescript
// Cutoff > Nyquist
Error: "Cutoff frequency must be between 0 and 4000 Hz (Nyquist frequency)";

// Invalid normalized cutoff
Error: "Cutoff frequency must be between 0 and 1.0 (normalized)";

// Invalid order
Error: "Order must be between 1 and 8 for IIR Butterworth filters";

// Invalid band
Error: "Low cutoff must be less than high cutoff";
```

## Technical Details

### Frequency Normalization

You provide frequencies in **Hz**, the API normalizes internally:

```typescript
normalizedFreq = cutoffFrequency / (sampleRate / 2); // Range: 0 to 1
```

### FIR Implementation

- **Method**: Windowed sinc design
- **Optimization**: SIMD-accelerated convolution (6.7x speedup)
- **Phase**: Linear (symmetric coefficients)
- **Stability**: Always stable

### IIR Implementation

- **Method**: Bilinear transform (analog → digital)
- **Structure**: Direct Form II (biquad sections)
- **Phase**: Non-linear
- **Stability**: Checked on construction

### State Management

Both FIR and IIR filters maintain state:

- **FIR**: Circular buffer of past inputs
- **IIR**: Past inputs and outputs (feedback)

Call `reset()` to clear state between independent signals.

## Common Patterns

### Pattern 1: Multi-Stage Filtering

```typescript
// Anti-alias then downsample
const antiAlias = createFilter({
  type: "butterworth",
  mode: "lowpass",
  cutoffFrequency: sampleRate / 4,
  sampleRate,
  order: 4,
});

const step1 = await antiAlias.process(signal);
const downsampled = downsample(step1, 2);
```

### Pattern 2: Cascade for Sharp Rolloff

```typescript
// Two 2nd-order filters = 4th-order response
const stage1 = IirFilter.createButterworthLowPass({
  cutoffFrequency: 1000,
  sampleRate: 8000,
  order: 2,
});

const stage2 = IirFilter.createButterworthLowPass({
  cutoffFrequency: 1000,
  sampleRate: 8000,
  order: 2,
});

const filtered1 = await stage1.process(signal);
const filtered2 = await stage2.process(filtered1);
```

### Pattern 3: Adaptive Filtering

```typescript
let cutoff = 1000;

function updateFilter() {
  filter = createFilter({
    type: "butterworth",
    mode: "lowpass",
    cutoffFrequency: cutoff,
    sampleRate: 8000,
    order: 4,
  });
}

// Adjust cutoff based on signal characteristics
if (noiseLevel > threshold) {
  cutoff = 500; // More aggressive filtering
  updateFilter();
}
```

## See Also

- **Examples**: `src/ts/examples/filter-examples.ts`
- **Tests**: Run `npm test`
- **C++ Implementation**: `docs/FILTERS_IMPLEMENTATION.md`
- **FFT Guide**: `docs/FFT_USER_GUIDE.md`

## Support

For more information:

- TypeScript types provide inline documentation
- Examples demonstrate all filter types
- Test suite shows validation patterns
