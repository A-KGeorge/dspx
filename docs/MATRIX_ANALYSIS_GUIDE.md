# Matrix Analysis Guide: PCA, ICA, and Whitening

This guide covers the matrix analysis algorithms implemented in this DSP library: Principal Component Analysis (PCA), Independent Component Analysis (ICA), and Whitening (ZCA).

## Table of Contents

- [Overview](#overview)
- [Installation](#installation)
- [PCA (Principal Component Analysis)](#pca-principal-component-analysis)
- [ICA (Independent Component Analysis)](#ica-independent-component-analysis)
- [Whitening (ZCA)](#whitening-zca)
- [Combined Workflows](#combined-workflows)
- [Real-World Applications](#real-world-applications)
- [API Reference](#api-reference)
- [Performance Considerations](#performance-considerations)

## Overview

These three matrix analysis algorithms follow a **train-then-apply** architecture:

1. **Training Phase (Batch)**: Compute transformation matrices from a training dataset
2. **Application Phase (Real-time)**: Apply pre-trained matrices to streaming data in a pipeline

This approach is ideal for scenarios where you:

- Have a representative training dataset
- Need real-time processing with fixed transformations
- Want to separate the expensive computation from the streaming phase

## Installation

The matrix analysis features use the Eigen C++ library (included) and require no additional dependencies:

```bash
npm install dsp-ts-redis
```

Build from source (already done during installation):

```bash
npm run build
```

## PCA (Principal Component Analysis)

PCA finds the directions (principal components) of maximum variance in your data. It's perfect for:

- **Dimensionality reduction**: 8 channels → 3 components
- **Noise reduction**: Keep only high-variance components
- **Feature extraction**: Find most important patterns

### Basic Usage

```typescript
import { calculatePca, createDspPipeline } from "dsp-ts-redis";

// 1. Training Phase: Calculate PCA from training data
const numSamples = 1000;
const numChannels = 4;
const trainingData = new Float32Array(numSamples * numChannels);

// Fill with your multi-channel training data (interleaved)
// ... populate trainingData ...

const pca = calculatePca(trainingData, numChannels);

console.log("Explained variance:", pca.explainedVariance);
// Output: [0.65, 0.25, 0.08, 0.02] — first PC explains 65% of variance

// 2. Application Phase: Apply PCA to streaming data
const pipeline = createDspPipeline();
pipeline.PcaTransform({
  pcaMatrix: pca.pcaMatrix,
  mean: pca.mean,
  numChannels: 4,
  numComponents: 4, // Keep all components
});

const streamData = new Float32Array(40); // 10 samples × 4 channels
// ... populate streamData ...

const transformed = await pipeline.process(streamData, { channels: 4 });
// transformed is now in PCA space (decorrelated, sorted by variance)
```

### Dimensionality Reduction

```typescript
// Train PCA on 8-channel EMG data
const emgData = new Float32Array(5000 * 8); // 5000 samples, 8 channels
// ... populate emgData ...

const pca = calculatePca(emgData, 8);

console.log("Explained variance:", pca.explainedVariance);
// Output: [0.45, 0.30, 0.15, 0.05, 0.03, 0.01, 0.01, 0.00]

// Top 3 components explain 90% of variance
const top3Variance = pca.explainedVariance
  .slice(0, 3)
  .reduce((a, b) => a + b, 0);
console.log(
  `Top 3 PCs explain ${(top3Variance * 100).toFixed(1)}% of variance`
);

// Extract only top 3 components (for dimensionality reduction)
const reducedMatrix = pca.pcaMatrix.slice(0, 8 * 3); // 8 rows × 3 columns

const pipeline = createDspPipeline();
pipeline.PcaTransform({
  pcaMatrix: reducedMatrix,
  mean: pca.mean,
  numChannels: 8,
  numComponents: 3, // Reduce to 3
});

const streamEmg = new Float32Array(80); // 10 samples × 8 channels
const reduced = await pipeline.process(streamEmg, { channels: 8 });

// Result has 8 channels, but only first 3 have meaningful data
// Channels 3-7 are zeroed (unused dimensions)
```

### PCA Results

The `calculatePca()` function returns:

```typescript
interface PcaResult {
  mean: Float32Array; // Mean vector (numChannels)
  pcaMatrix: Float32Array; // Principal components (numChannels × numChannels)
  eigenvalues: Float32Array; // Variance explained by each PC (sorted descending)
  explainedVariance: Float32Array; // Normalized variance ratios (sum = 1.0)
  numChannels: number;
  numComponents: number;
}
```

## ICA (Independent Component Analysis)

ICA separates mixed signals into statistically independent components. Perfect for:

- **Blind source separation**: "Cocktail party problem"
- **Artifact removal**: Remove eye blinks from EEG
- **Signal decomposition**: Separate overlapping EMG signals

### Basic Usage

```typescript
import { calculateIca, createDspPipeline } from "dsp-ts-redis";

// 1. Training Phase: Calculate ICA from mixed signals
const numSamples = 2000;
const numChannels = 3;
const mixedSignals = new Float32Array(numSamples * numChannels);

// mixedSignals contains 3 channels that are mixtures of independent sources
// ... populate mixedSignals ...

const ica = calculateIca(mixedSignals, numChannels, 200, 1e-4);
//                                                     ^max iterations  ^tolerance

console.log(`ICA converged: ${ica.converged} in ${ica.iterations} iterations`);

// 2. Application Phase: Apply ICA to streaming data
const pipeline = createDspPipeline();
pipeline.IcaTransform({
  icaMatrix: ica.icaMatrix,
  mean: ica.mean,
  numChannels: 3,
  numComponents: 3,
});

const streamMixed = new Float32Array(30); // 10 samples × 3 channels
const separated = await pipeline.process(streamMixed, { channels: 3 });
// separated contains 3 independent components
```

### EEG Artifact Removal Example

```typescript
// EEG data with eye blink artifacts
const eegData = new Float32Array(10000 * 4); // 10000 samples, 4 channels
// Channels: Frontal, Central, Parietal, Occipital
// ... load EEG data with eye blink contamination ...

const ica = calculateIca(eegData, 4);

// In practice, you would:
// 1. Inspect the independent components to identify the artifact
// 2. Zero out the artifact component in the ICA matrix
// 3. Reconstruct the clean EEG

// For real-time processing:
const pipeline = createDspPipeline();
pipeline.IcaTransform({
  icaMatrix: ica.icaMatrix, // Modified to remove artifact component
  mean: ica.mean,
  numChannels: 4,
  numComponents: 4,
});

// Process streaming EEG
const cleanEeg = await pipeline.process(eegStream, { channels: 4 });
```

### ICA Results

The `calculateIca()` function returns:

```typescript
interface IcaResult {
  mean: Float32Array; // Mean vector (numChannels)
  icaMatrix: Float32Array; // Unmixing matrix (numChannels × numChannels)
  numChannels: number;
  numComponents: number;
  converged: boolean; // Did ICA converge?
  iterations: number; // Number of iterations used
}
```

**Note**: ICA requires at least `5 × numChannels` samples for reliable convergence.

## Whitening (ZCA)

Whitening transforms data to have identity covariance (decorrelates and normalizes). Perfect for:

- **Preprocessing for ICA**: ICA requires whitened data
- **Feature normalization**: Machine learning preprocessing
- **Signal conditioning**: Remove correlations

### Basic Usage

```typescript
import { calculateWhitening, createDspPipeline } from "dsp-ts-redis";

// 1. Training Phase: Calculate whitening matrix
const numSamples = 1000;
const numChannels = 3;
const correlatedData = new Float32Array(numSamples * numChannels);

// ... populate correlatedData ...

const whitening = calculateWhitening(correlatedData, numChannels);
// Default regularization: 1e-5 (prevents division by zero for small eigenvalues)

// 2. Application Phase: Apply whitening to streaming data
const pipeline = createDspPipeline();
pipeline.WhiteningTransform({
  whiteningMatrix: whitening.whiteningMatrix,
  mean: whitening.mean,
  numChannels: 3,
  numComponents: 3,
});

const streamData = new Float32Array(30); // 10 samples × 3 channels
const whitened = await pipeline.process(streamData, { channels: 3 });
// whitened data has decorrelated features with unit variance
```

### Custom Regularization

```typescript
// For data with small eigenvalues, increase regularization
const whitening = calculateWhitening(data, 4, 1e-3); // Higher regularization

console.log(`Regularization used: ${whitening.regularization}`);
```

### Whitening Results

The `calculateWhitening()` function returns:

```typescript
interface WhiteningResult {
  mean: Float32Array; // Mean vector (numChannels)
  whiteningMatrix: Float32Array; // ZCA whitening matrix (numChannels × numChannels)
  numChannels: number;
  numComponents: number;
  regularization: number; // Regularization parameter used
}
```

## Combined Workflows

### Whitening + ICA for Source Separation

A common pattern is to whiten data before applying ICA:

```typescript
import {
  calculateWhitening,
  calculateIca,
  createDspPipeline,
} from "dsp-ts-redis";

// Training data: 3 mixed audio sources
const mixedAudio = new Float32Array(10000 * 3); // 10000 samples, 3 mics
// ... populate mixedAudio ...

// Step 1: Whiten the data
const whitening = calculateWhitening(mixedAudio, 3);

// Step 2: Perform ICA on whitened data
const ica = calculateIca(mixedAudio, 3);

console.log(`ICA converged: ${ica.converged}`);

// Step 3: Build pipeline with both transformations
const pipeline = createDspPipeline();
pipeline
  .WhiteningTransform({
    whiteningMatrix: whitening.whiteningMatrix,
    mean: whitening.mean,
    numChannels: 3,
    numComponents: 3,
  })
  .IcaTransform({
    icaMatrix: ica.icaMatrix,
    mean: ica.mean,
    numChannels: 3,
    numComponents: 3,
  });

// Process streaming audio
const streamAudio = new Float32Array(300); // 100 samples × 3 channels
const separated = await pipeline.process(streamAudio, { channels: 3 });
// separated now contains 3 independent sources
```

### PCA for Dimensionality Reduction + Denoising

```typescript
// Reduce 16-channel sensor data to 5 components
const sensorData = new Float32Array(5000 * 16);
// ... populate sensorData ...

const pca = calculatePca(sensorData, 16);

// Check how much variance is retained
const cumVariance = pca.explainedVariance.reduce((acc, v, i) => {
  acc.push((acc[i - 1] || 0) + v);
  return acc;
}, []);

console.log("Cumulative variance:", cumVariance);
// [0.45, 0.70, 0.85, 0.92, 0.96, 0.98, 0.99, ...]

// Keep top 5 PCs (96% of variance)
const reducedMatrix = pca.pcaMatrix.slice(0, 16 * 5);

const pipeline = createDspPipeline();
pipeline
  .PcaTransform({
    pcaMatrix: reducedMatrix,
    mean: pca.mean,
    numChannels: 16,
    numComponents: 5,
  })
  .MovingAverage({ mode: "moving", windowSize: 10 }); // Smooth the components

const streamSensors = new Float32Array(160); // 10 samples × 16 channels
const denoised = await pipeline.process(streamSensors, { channels: 16 });
// denoised has 16 channels, but only first 5 contain PCA-filtered data
```

## Real-World Applications

### 1. EEG Signal Processing

**Problem**: Remove eye blink artifacts from EEG recordings

```typescript
// Load 4-channel EEG data (frontal, central, parietal, occipital)
const eegData = loadEegData(); // Float32Array with 10000 samples × 4 channels

// Train ICA to find independent components
const ica = calculateIca(eegData, 4, 500); // 500 max iterations

if (!ica.converged) {
  console.warn("ICA did not converge. Consider more samples or iterations.");
}

// In a real application:
// 1. Visualize the independent components to identify artifacts
// 2. Manually or automatically detect the eye blink component
// 3. Zero out that component in the ICA matrix
// 4. Apply ICA transform in pipeline for real-time artifact removal

const pipeline = createDspPipeline();
pipeline.IcaTransform({
  icaMatrix: ica.icaMatrix, // Modified to remove artifact
  mean: ica.mean,
  numChannels: 4,
  numComponents: 4,
});

// Process streaming EEG
const cleanEeg = await pipeline.process(eegStream, { channels: 4 });
```

### 2. EMG Feature Extraction

**Problem**: Extract muscle activity features from multi-channel EMG

```typescript
// 8 EMG channels from forearm muscles
const emgData = loadEmgData(); // Float32Array with 5000 samples × 8 channels

// Use PCA to find principal patterns of muscle activation
const pca = calculatePca(emgData, 8);

console.log("Explained variance:", pca.explainedVariance);
// [0.40, 0.25, 0.15, 0.10, 0.05, 0.03, 0.01, 0.01]

// Top 3 PCs capture 80% of variance
const top3Matrix = pca.pcaMatrix.slice(0, 8 * 3);

const pipeline = createDspPipeline();
pipeline
  .PcaTransform({
    pcaMatrix: top3Matrix,
    mean: pca.mean,
    numChannels: 8,
    numComponents: 3,
  })
  .Rms({ mode: "moving", windowSize: 50 }); // Compute RMS of each PC

// Real-time feature extraction
const features = await pipeline.process(emgStream, { channels: 8 });
// First 3 channels contain muscle activation features, channels 3-7 are zero
```

### 3. Sensor Fusion with Whitening

**Problem**: Normalize and decorrelate multi-sensor data

```typescript
// 6-axis IMU: 3 accelerometer + 3 gyroscope channels
const imuData = loadImuData(); // Float32Array with 2000 samples × 6 channels

// Whiten to decorrelate and normalize
const whitening = calculateWhitening(imuData, 6, 1e-4);

const pipeline = createDspPipeline();
pipeline
  .WhiteningTransform({
    whiteningMatrix: whitening.whiteningMatrix,
    mean: whitening.mean,
    numChannels: 6,
    numComponents: 6,
  })
  .MovingAverage({ mode: "moving", windowSize: 10 });

// Process streaming IMU data
const normalizedImu = await pipeline.process(imuStream, { channels: 6 });
// All channels now have zero mean, unit variance, and are decorrelated
```

### 4. Audio Source Separation

**Problem**: Separate mixed audio from multiple microphones

```typescript
// 3 microphones recording 2 speakers + background noise
const micData = loadMicrophoneRecordings(); // Float32Array, 44100 samples × 3 channels

// Apply ICA to separate sources
const ica = calculateIca(micData, 3, 1000, 1e-5);

console.log(`ICA converged in ${ica.iterations} iterations`);

const pipeline = createDspPipeline();
pipeline.IcaTransform({
  icaMatrix: ica.icaMatrix,
  mean: ica.mean,
  numChannels: 3,
  numComponents: 3,
});

// Real-time source separation
const separated = await pipeline.process(audioStream, { channels: 3 });
// Each of the 3 channels contains one separated source
```

## API Reference

### Calculation Functions (Training Phase)

#### `calculatePca(data, numChannels): PcaResult`

Performs Principal Component Analysis on multi-channel data.

**Parameters**:

- `data`: `Float32Array` - Interleaved multi-channel data
- `numChannels`: `number` - Number of channels

**Requirements**:

- `data.length` must be divisible by `numChannels`
- Need at least `numChannels` samples (i.e., `data.length >= numChannels²`)

**Returns**: `PcaResult` with principal components sorted by variance

---

#### `calculateWhitening(data, numChannels, regularization?): WhiteningResult`

Computes ZCA whitening transformation.

**Parameters**:

- `data`: `Float32Array` - Interleaved multi-channel data
- `numChannels`: `number` - Number of channels
- `regularization`: `number` (optional, default: `1e-5`) - Regularization parameter

**Returns**: `WhiteningResult` with ZCA whitening matrix

---

#### `calculateIca(data, numChannels, maxIterations?, tolerance?): IcaResult`

Performs Independent Component Analysis using FastICA algorithm.

**Parameters**:

- `data`: `Float32Array` - Interleaved multi-channel data
- `numChannels`: `number` - Number of channels
- `maxIterations`: `number` (optional, default: `200`) - Maximum iterations
- `tolerance`: `number` (optional, default: `1e-4`) - Convergence tolerance

**Requirements**:

- Need at least `5 × numChannels` samples for reliable convergence

**Returns**: `IcaResult` with unmixing matrix and convergence info

---

### Pipeline Transform Methods (Application Phase)

#### `pipeline.PcaTransform(params): DspProcessor`

Applies PCA transformation in a pipeline stage.

**Parameters**:

```typescript
{
  pcaMatrix: Float32Array; // Principal components matrix
  mean: Float32Array; // Mean vector
  numChannels: number; // Number of input channels
  numComponents: number; // Number of components to keep
}
```

**Example**:

```typescript
const pca = calculatePca(trainingData, 8);
const reducedMatrix = pca.pcaMatrix.slice(0, 8 * 3); // Keep top 3

pipeline.PcaTransform({
  pcaMatrix: reducedMatrix,
  mean: pca.mean,
  numChannels: 8,
  numComponents: 3, // Dimensionality reduction
});
```

---

#### `pipeline.IcaTransform(params): DspProcessor`

Applies ICA transformation in a pipeline stage.

**Parameters**:

```typescript
{
  icaMatrix: Float32Array; // Unmixing matrix
  mean: Float32Array; // Mean vector
  numChannels: number; // Number of input channels
  numComponents: number; // Number of components
}
```

---

#### `pipeline.WhiteningTransform(params): DspProcessor`

Applies whitening transformation in a pipeline stage.

**Parameters**:

```typescript
{
  whiteningMatrix: Float32Array; // ZCA whitening matrix
  mean: Float32Array; // Mean vector
  numChannels: number; // Number of input channels
  numComponents: number; // Number of components
}
```

---

## Performance Considerations

### Memory Usage

- **PCA Matrix**: `numChannels × numChannels × 4 bytes` (Float32)
- **ICA Matrix**: `numChannels × numChannels × 4 bytes` (Float32)
- **Whitening Matrix**: `numChannels × numChannels × 4 bytes` (Float32)

Example: 8 channels = 8 × 8 × 4 = 256 bytes per matrix

### Computational Complexity

**Training Phase** (one-time cost):

- **PCA**: O(C² × N) where C = channels, N = samples
- **ICA**: O(C² × N × I) where I = iterations (typically 10-200)
- **Whitening**: O(C² × N)

**Application Phase** (per sample):

- All transforms: O(C²) matrix-vector multiplication

### Recommendations

1. **Training Data Size**:

   - PCA: At least 100 samples per channel
   - ICA: At least 5 × numChannels samples (more is better)
   - Whitening: At least 50 samples per channel

2. **Real-Time Processing**:

   - Pre-compute matrices during initialization
   - Store matrices for reuse across sessions
   - Pipeline stages are highly optimized (C++ + Eigen)

3. **Dimensionality Reduction**:

   - Start with PCA, check explained variance
   - Keep components that explain 90-95% of variance
   - Reducing dimensions speeds up downstream processing

4. **ICA Convergence**:
   - Use more samples for better convergence
   - Increase `maxIterations` if convergence fails
   - Decrease `tolerance` for higher precision (slower)

### Benchmarks

On a typical system (Intel i7, 16GB RAM):

| Operation     | Channels | Samples | Time (ms) |
| ------------- | -------- | ------- | --------- |
| PCA training  | 4        | 1000    | 2-3       |
| PCA training  | 8        | 5000    | 25-35     |
| ICA training  | 3        | 2000    | 5-10      |
| ICA training  | 4        | 10000   | 50-100    |
| Whitening     | 4        | 1000    | 1-2       |
| PCA transform | 8        | 1000    | 0.5-1     |
| ICA transform | 4        | 1000    | 0.3-0.5   |

Real-time processing (application phase) can handle >100,000 samples/sec for 8 channels.

---

## Further Reading

- **PCA**: [Wikipedia - Principal Component Analysis](https://en.wikipedia.org/wiki/Principal_component_analysis)
- **ICA**: [Wikipedia - Independent Component Analysis](https://en.wikipedia.org/wiki/Independent_component_analysis)
- **FastICA**: Hyvärinen, A., & Oja, E. (2000). Independent component analysis: algorithms and applications.
- **Whitening**: [Wikipedia - Whitening transformation](https://en.wikipedia.org/wiki/Whitening_transformation)
- **Eigen Library**: [Eigen Documentation](https://eigen.tuxfamily.org/)

---

## License

This implementation uses the Eigen C++ library (MPL2 license). See [LICENSE](../LICENSE) for details.
