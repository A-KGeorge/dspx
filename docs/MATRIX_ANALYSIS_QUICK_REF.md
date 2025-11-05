# Matrix Analysis Quick Reference

## Import

```typescript
import {
  calculatePca,
  calculateIca,
  calculateWhitening,
  createDspPipeline,
} from "dsp-ts-redis";
```

---

## PCA (Principal Component Analysis)

### Training

```typescript
const pca = calculatePca(data: Float32Array, numChannels: number);
```

**Returns:**

- `mean`: Mean vector
- `pcaMatrix`: Principal components (sorted by variance)
- `eigenvalues`: Variance per component
- `explainedVariance`: Normalized variance ratios (sum=1)

### Pipeline

```typescript
pipeline.PcaTransform({
  pcaMatrix: pca.pcaMatrix,
  mean: pca.mean,
  numChannels: 4,
  numComponents: 4, // Or fewer for dimensionality reduction
});
```

### Dimensionality Reduction

```typescript
// Extract top 3 components from 8 channels
const pca = calculatePca(data, 8);
const top3 = pca.pcaMatrix.slice(0, 8 * 3); // 8 rows × 3 columns

pipeline.PcaTransform({
  pcaMatrix: top3,
  mean: pca.mean,
  numChannels: 8,
  numComponents: 3, // Reduce to 3
});
```

---

## ICA (Independent Component Analysis)

### Training

```typescript
const ica = calculateIca(
  data: Float32Array,
  numChannels: number,
  maxIterations?: number = 200,
  tolerance?: number = 1e-4
);
```

**Returns:**

- `mean`: Mean vector
- `icaMatrix`: Unmixing matrix
- `converged`: Did it converge?
- `iterations`: Number of iterations used

**Requirement**: At least `5 × numChannels` samples

### Pipeline

```typescript
pipeline.IcaTransform({
  icaMatrix: ica.icaMatrix,
  mean: ica.mean,
  numChannels: 3,
  numComponents: 3,
});
```

---

## Whitening (ZCA)

### Training

```typescript
const whitening = calculateWhitening(
  data: Float32Array,
  numChannels: number,
  regularization?: number = 1e-5
);
```

**Returns:**

- `mean`: Mean vector
- `whiteningMatrix`: ZCA whitening matrix
- `regularization`: Regularization used

### Pipeline

```typescript
pipeline.WhiteningTransform({
  whiteningMatrix: whitening.whiteningMatrix,
  mean: whitening.mean,
  numChannels: 3,
  numComponents: 3,
});
```

---

## Common Patterns

### Pattern 1: PCA for Denoising

```typescript
// Keep top 90% of variance
const pca = calculatePca(trainingData, 8);
const cumVariance = pca.explainedVariance.reduce((acc, v, i) => {
  acc.push((acc[i - 1] || 0) + v);
  return acc;
}, []);

const numKeep = cumVariance.findIndex((v) => v >= 0.9) + 1;
const reduced = pca.pcaMatrix.slice(0, 8 * numKeep);

pipeline.PcaTransform({
  pcaMatrix: reduced,
  mean: pca.mean,
  numChannels: 8,
  numComponents: numKeep,
});
```

### Pattern 2: Whitening + ICA

```typescript
const whitening = calculateWhitening(mixedData, 3);
const ica = calculateIca(mixedData, 3);

pipeline
  .WhiteningTransform({ ...whitening, numChannels: 3, numComponents: 3 })
  .IcaTransform({ ...ica, numChannels: 3, numComponents: 3 });
```

### Pattern 3: Check ICA Convergence

```typescript
const ica = calculateIca(data, 4, 500, 1e-4);

if (!ica.converged) {
  console.warn(`ICA did not converge (${ica.iterations} iterations)`);
  // Try: more samples, more iterations, or lower tolerance
}
```

---

## Data Format

All functions expect **interleaved multi-channel data**:

```
[ch0_s0, ch1_s0, ch2_s0, ch0_s1, ch1_s1, ch2_s1, ...]
```

Example:

```typescript
const numSamples = 1000;
const numChannels = 3;
const data = new Float32Array(numSamples * numChannels);

for (let i = 0; i < numSamples; i++) {
  data[i * 3 + 0] = ...;  // Channel 0
  data[i * 3 + 1] = ...;  // Channel 1
  data[i * 3 + 2] = ...;  // Channel 2
}
```

---

## Error Handling

All functions validate inputs and throw descriptive errors:

```typescript
try {
  const pca = calculatePca(data, numChannels);
} catch (error) {
  if (error.message.includes("Float32Array")) {
    // Wrong data type
  } else if (error.message.includes("divisible")) {
    // Data length not divisible by numChannels
  } else if (error.message.includes("at least")) {
    // Insufficient samples
  }
}
```

---

## Performance Tips

1. **Training Sample Size**:

   - PCA: ≥100 samples per channel
   - ICA: ≥5 × numChannels (minimum), 100+ recommended
   - Whitening: ≥50 samples per channel

2. **Real-Time Processing**:

   - Pre-compute matrices once during initialization
   - Reuse matrices across multiple streams
   - Store matrices in database for persistence

3. **Dimensionality Reduction**:

   - Check `explainedVariance` to decide how many components to keep
   - Typical: keep 90-95% of variance
   - Reduces downstream processing cost

4. **ICA Convergence**:
   - More samples → better convergence
   - Increase `maxIterations` if needed (default: 200)
   - Decrease `tolerance` for higher precision (slower)

---

## Use Cases

| Algorithm     | Best For                                                                                          |
| ------------- | ------------------------------------------------------------------------------------------------- |
| **PCA**       | Dimensionality reduction, noise filtering, feature extraction, data compression                   |
| **ICA**       | Blind source separation, artifact removal (EEG/EMG), cocktail party problem, signal decomposition |
| **Whitening** | ICA preprocessing, feature normalization, decorrelation, ML preprocessing                         |

---

## Quick Examples

### EEG Artifact Removal

```typescript
const eeg = loadEegData(); // 4 channels
const ica = calculateIca(eeg, 4);

// Identify artifact component (e.g., component 2)
// Zero out that component in ICA matrix for artifact removal

pipeline.IcaTransform({
  icaMatrix: ica.icaMatrix,
  mean: ica.mean,
  numChannels: 4,
  numComponents: 4,
});
```

### EMG Dimensionality Reduction

```typescript
const emg = loadEmgData(); // 8 channels
const pca = calculatePca(emg, 8);

// Top 3 PCs capture most variance
const top3 = pca.pcaMatrix.slice(0, 8 * 3);

pipeline.PcaTransform({
  pcaMatrix: top3,
  mean: pca.mean,
  numChannels: 8,
  numComponents: 3,
});
```

### Audio Source Separation

```typescript
const mixed = loadMicData(); // 3 microphones
const ica = calculateIca(mixed, 3, 500);

console.log(`Converged: ${ica.converged}`);

pipeline.IcaTransform({
  icaMatrix: ica.icaMatrix,
  mean: ica.mean,
  numChannels: 3,
  numComponents: 3,
});
```

---

## See Also

- **Full Guide**: [docs/MATRIX_ANALYSIS_GUIDE.md](./MATRIX_ANALYSIS_GUIDE.md)
- **Tests**: [src/ts/**tests**/MatrixAnalysis.test.ts](../src/ts/__tests__/MatrixAnalysis.test.ts)
- **Implementation**: [docs/MATRIX_ANALYSIS_IMPLEMENTATION_SUMMARY.md](./MATRIX_ANALYSIS_IMPLEMENTATION_SUMMARY.md)
