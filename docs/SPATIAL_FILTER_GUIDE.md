# Spatial Filtering Guide (CSP for BCI/EEG)

## Overview

This guide explains **Common Spatial Patterns (CSP)** for Brain-Computer Interface (BCI) and EEG signal classification in `dspx`.

**CSP** finds optimal spatial filters that maximize the difference between two classes of multi-channel EEG data. It's the gold standard for:

- Motor imagery BCI (left hand vs right hand)
- P300 speller systems
- SSVEP (Steady-State Visual Evoked Potentials)
- Sleep stage classification
- Seizure detection

---

## Table of Contents

1. [What is CSP?](#what-is-csp)
2. [Quick Start](#quick-start)
3. [API Reference](#api-reference)
4. [CSP vs PCA/ICA](#csp-vs-pcaica)
5. [Motor Imagery BCI](#motor-imagery-bci)
6. [Feature Extraction](#feature-extraction)
7. [Best Practices](#best-practices)

---

## What is CSP?

### Problem Statement

Given multi-channel EEG recordings from **two classes** (e.g., left hand movement vs right hand movement), find spatial filters that:

1. **Maximize variance** for one class
2. **Minimize variance** for the other class

### Mathematical Formulation

CSP solves the generalized eigenvalue problem:

```
Cov₁ · w = λ · Cov₂ · w
```

Where:

- `Cov₁`, `Cov₂` = covariance matrices for class 1 and class 2
- `w` = spatial filter weights (eigenvector)
- `λ` = eigenvalue (measures class separability)

**High λ** means filter strongly discriminates between classes.

### Intuition

Imagine 8-channel EEG during motor imagery:

- **Left hand**: High activity in **C3** (left motor cortex)
- **Right hand**: High activity in **C4** (right motor cortex)

CSP finds filters that emphasize C3 for left hand trials and C4 for right hand trials, making classification easy.

---

## Quick Start

### Motor Imagery BCI (Left vs Right Hand)

```typescript
import { createDspPipeline, calculateCommonSpatialPatterns } from "dspx";

// 1. Collect training data: 8 channels, 500 samples per trial
const leftHandTrials = new Float32Array(50 * 500 * 8); // 50 trials
const rightHandTrials = new Float32Array(50 * 500 * 8); // 50 trials

// ... record EEG during motor imagery tasks ...

// 2. Calculate CSP filters (keep top 4 most discriminative)
const csp = calculateCommonSpatialPatterns(
  leftHandTrials,
  rightHandTrials,
  8, // 8 EEG channels
  4 // Top 4 filters
);

console.log("Top eigenvalue (class separability):", csp.eigenvalues[0]);
console.log("Converged:", csp.eigenvalues[0] > 2 * csp.eigenvalues[3]);

// 3. Build real-time classification pipeline
const pipeline = createDspPipeline();
pipeline
  .BandpassFilter({ lowCutoff: 8, highCutoff: 30 }) // Motor imagery band (mu/beta)
  .CspTransform({
    cspMatrix: csp.cspMatrix,
    mean: csp.mean,
    numChannels: 8,
    numFilters: 4,
  })
  .Variance({ mode: "moving", windowSize: 100 }) // Extract variance features
  .MovingAverage({ mode: "moving", windowSize: 50 });

// 4. Process live EEG stream
const liveEeg = new Float32Array(80); // 10 samples × 8 channels
const result = await pipeline.process(liveEeg, { channels: 8 });

// result: 4-channel CSP features (high variance = one class, low = other)
// Feed to classifier (SVM, LDA, etc.)
```

---

## API Reference

### `calculateCommonSpatialPatterns()`

Computes CSP spatial filters for binary classification.

```typescript
function calculateCommonSpatialPatterns(
  dataClass1: Float32Array,
  dataClass2: Float32Array,
  numChannels: number,
  numFilters?: number // Default: numChannels
): CspResult;
```

**Parameters:**

- **`dataClass1`**: EEG trials from class 1 (interleaved: samples × channels)
  - Format: `[ch0_s0, ch1_s0, ..., ch7_s0, ch0_s1, ch1_s1, ...]`
- **`dataClass2`**: EEG trials from class 2 (same format)
- **`numChannels`**: Number of EEG channels
- **`numFilters`**: Number of top filters to return (default: all)
  - Typical: 2-6 filters (most discriminative)

**Returns:**

```typescript
{
  cspMatrix: Float32Array; // Size: numChannels × numFilters (column-major)
  eigenvalues: Float32Array; // Size: numFilters (sorted descending)
  mean: Float32Array; // Size: numChannels
  numChannels: number;
  numFilters: number;
}
```

**Eigenvalues interpretation:**

- `λ > 10`: Excellent class separability
- `λ = 2-10`: Good separability
- `λ < 2`: Poor separability (need more data or better preprocessing)

### `CspTransform()`

Applies pre-trained CSP filters in real-time pipeline.

```typescript
pipeline.CspTransform(params: {
  cspMatrix: Float32Array;
  mean: Float32Array;
  numChannels: number;
  numFilters: number;
}): this;
```

**Output**: Spatially filtered EEG with `numFilters` components (first `numFilters` channels non-zero, rest zeroed).

**Typical pipeline**:

```
Raw EEG → Bandpass → CSP Transform → Variance → Classifier
```

---

## CSP vs PCA/ICA

| Feature           | CSP                         | PCA                      | ICA                      |
| ----------------- | --------------------------- | ------------------------ | ------------------------ |
| **Goal**          | Maximize class separability | Maximize variance        | Find independent sources |
| **Supervised?**   | ✅ Yes (uses class labels)  | ❌ No                    | ❌ No                    |
| **Use Case**      | BCI classification          | Dimensionality reduction | Artifact removal         |
| **Output**        | Discriminative components   | Principal components     | Independent components   |
| **Training Data** | Two labeled classes         | Unlabeled data           | Unlabeled data           |

### When to Use Each

- **CSP**: You have **labeled** EEG data and want to **classify** (motor imagery, P300, SSVEP)
- **PCA**: You want to **reduce dimensions** without losing variance (preprocessing for ML)
- **ICA**: You want to **separate sources** (remove eye blinks, muscle artifacts from EEG)

### Example Workflow

```typescript
// 1. ICA: Remove artifacts
const ica = calculateIca(rawEeg, 8);
pipeline.IcaTransform({
  /* ica params */
});

// 2. CSP: Extract discriminative features
const csp = calculateCommonSpatialPatterns(cleanClass1, cleanClass2, 8, 4);
pipeline.CspTransform({
  /* csp params */
});

// 3. Feature extraction
pipeline.Variance({ mode: "moving", windowSize: 100 });

// Result: Clean, discriminative features for classification
```

---

## Motor Imagery BCI

### Background

**Motor imagery** = mentally simulating movement without physical execution.

**Neurophysiology**:

- **Mu rhythm (8-13 Hz)**: Sensorimotor cortex, desynchronizes during movement/imagery
- **Beta rhythm (13-30 Hz)**: Also desynchronizes during motor activity
- **Event-Related Desynchronization (ERD)**: Power decrease in mu/beta bands

### EEG Montage (10-20 System)

```
        Fz
        │
   F3───C3───Cz───C4───F4
        │         │
   P3───Pz───────P4

Left motor cortex:  C3, CP3
Right motor cortex: C4, CP4
```

**Typical channels**: C3, Cz, C4, CP3, CP4, P3, Pz, P4 (8 channels)

### Protocol

1. **Baseline**: 2s rest (eyes open)
2. **Cue**: Arrow appears (left or right)
3. **Motor Imagery**: 4s imagine hand movement
4. **Rest**: 2s between trials

**Typical session**: 50-100 trials per class

### Code Example

```typescript
// Preprocessing
const pipeline = createDspPipeline();
pipeline
  .HighpassFilter({ cutoff: 0.5 }) // Remove DC drift
  .BandpassFilter({ lowCutoff: 8, highCutoff: 30 }) // Mu/beta band
  .Notch({ frequency: 50, bandwidth: 2 }); // Remove powerline (50 Hz EU, 60 Hz US)

// Extract epochs (trials)
const leftEpochs = [];
const rightEpochs = [];

for (const trial of trials) {
  const processed = await pipeline.process(trial.eeg, { channels: 8 });

  // Extract 2-4s window (motor imagery period)
  const epoch = extractWindow(processed, trial.startTime + 2000, 2000);

  if (trial.class === "left") {
    leftEpochs.push(epoch);
  } else {
    rightEpochs.push(epoch);
  }
}

// Concatenate all trials
const leftData = concatenate(leftEpochs);
const rightData = concatenate(rightEpochs);

// Train CSP
const csp = calculateCommonSpatialPatterns(leftData, rightData, 8, 4);

// Check quality
const ratio = csp.eigenvalues[0] / csp.eigenvalues[3];
if (ratio < 2) {
  console.warn(
    "Poor class separability! Collect more data or check preprocessing."
  );
}
```

---

## Feature Extraction

CSP transforms EEG into spatial patterns, but we need **features** for classification.

### Variance Features (Standard)

```typescript
pipeline
  .CspTransform({
    /* csp params */
  })
  .Variance({ mode: "moving", windowSize: 100 });

// Output: Variance of each CSP component over 100-sample window
// High variance = class 1, low variance = class 2 (or vice versa)
```

**Why variance?**

- CSP maximizes variance difference between classes
- Variance captures signal power in each component
- Simple, robust, widely used in BCI literature

### Log-Variance Features (Better)

```typescript
// Manual log-variance calculation
const cspOutput = await pipeline.process(eeg, { channels: 8 });

const logVarianceFeatures = new Float32Array(numFilters);
for (let f = 0; f < numFilters; f++) {
  let variance = 0;
  for (let i = 0; i < numSamples; i++) {
    const value = cspOutput[i * numChannels + f];
    variance += value * value;
  }
  variance /= numSamples;

  logVarianceFeatures[f] = Math.log(variance + 1e-10); // Avoid log(0)
}

// Log-variance is more Gaussian-distributed → better for LDA classifier
```

### Power Spectral Density (Advanced)

```typescript
pipeline
  .CspTransform({
    /* csp params */
  })
  .fftAnalysis({ fftSize: 256, hopSize: 128 });

// Extract power in specific frequency bands
// Mu band (8-13 Hz) and beta band (13-30 Hz) features
```

---

## Best Practices

### Data Collection

✅ **Do**:

- Collect **≥ 50 trials per class** (more = better)
- Use **consistent task duration** (e.g., 4s motor imagery)
- Include **rest periods** between trials (avoid fatigue)
- **Randomize trial order** (prevent adaptation effects)
- Record **ground truth labels** accurately

❌ **Don't**:

- Use < 20 trials (overfitting risk)
- Mix different tasks in one class
- Forget to document electrode positions

### Preprocessing

```typescript
// Recommended pipeline BEFORE CSP training
pipeline
  .HighpassFilter({ cutoff: 0.5 }) // Remove slow drifts
  .BandpassFilter({ lowCutoff: 8, highCutoff: 30 }) // Task-relevant band
  .Notch({ frequency: 50, bandwidth: 2 }) // Powerline noise
  .IcaTransform({
    /* if artifacts present */
  });
```

**Band selection**:

- **Motor imagery**: 8-30 Hz (mu + beta)
- **P300**: 0.5-10 Hz (slow potentials)
- **SSVEP**: ±2 Hz around stimulation frequency (e.g., 10-14 Hz for 12 Hz SSVEP)

### Cross-Validation

**Never test on training data!** Use k-fold cross-validation.

```typescript
const k = 5; // 5-fold CV
const foldSize = Math.floor(numTrials / k);

for (let fold = 0; fold < k; fold++) {
  // Split data
  const testStart = fold * foldSize;
  const testEnd = (fold + 1) * foldSize;

  const trainClass1 = concatenate([
    dataClass1.slice(0, testStart),
    dataClass1.slice(testEnd),
  ]);
  const testClass1 = dataClass1.slice(testStart, testEnd);

  // Train CSP on training set
  const csp = calculateCommonSpatialPatterns(trainClass1, trainClass2, 8, 4);

  // Test on held-out set
  const accuracy = evaluateClassifier(csp, testClass1, testClass2);
  console.log(`Fold ${fold + 1} accuracy: ${accuracy}%`);
}
```

### Overfitting Prevention

1. **Regularization**: Use fewer filters (2-4 instead of 8)
2. **More data**: Collect 100+ trials per class
3. **Ensemble**: Train multiple CSP models on different subsets, average predictions

### Filter Selection

```typescript
const csp = calculateCommonSpatialPatterns(class1, class2, 8, 8); // Get all filters

// Select top N pairs: most extreme eigenvalues
// λ >> 1: Class 1 dominates
// λ << 1: Class 2 dominates
const numPairs = 2; // Use 2 pairs = 4 filters
const topFilters = [
  csp.eigenvalues[0], // Highest λ (class 1)
  csp.eigenvalues[1],
  csp.eigenvalues[6], // Lowest λ (class 2)
  csp.eigenvalues[7],
];
```

---

## P300 Speller Example

**Task**: Detect target letter in row/column flashes (BCI speller).

```typescript
// 16 channels, 200ms epochs, 30 target vs 150 non-target flashes
const targetErps = new Float32Array(30 * 200 * 16);
const nonTargetErps = new Float32Array(150 * 200 * 16);

// Preprocess: 0.5-10 Hz for slow P300 wave
pipeline.HighpassFilter({ cutoff: 0.5 }).LowpassFilter({ cutoff: 10 });

// Extract epochs time-locked to stimulus (0-600ms)
for (const flash of flashes) {
  const epoch = extractEpoch(eegData, flash.time, 600);
  const processed = await pipeline.process(epoch, { channels: 16 });

  if (flash.isTarget) {
    targetErps.set(processed, targetIndex * 200 * 16);
    targetIndex++;
  } else {
    nonTargetErps.set(processed, nonTargetIndex * 200 * 16);
    nonTargetIndex++;
  }
}

// Train CSP
const csp = calculateCommonSpatialPatterns(targetErps, nonTargetErps, 16, 6);

// Real-time detection
const classifierPipeline = createDspPipeline();
classifierPipeline
  .HighpassFilter({ cutoff: 0.5 })
  .LowpassFilter({ cutoff: 10 })
  .CspTransform({
    cspMatrix: csp.cspMatrix,
    mean: csp.mean,
    numChannels: 16,
    numFilters: 6,
  });

// Classify each flash
const flashEpoch = new Float32Array(200 * 16);
const features = await classifierPipeline.process(flashEpoch, { channels: 16 });

// Extract variance features, feed to LDA/SVM classifier
const isTarget = classifier.predict(features);
```

---

## Troubleshooting

### Poor Classification Accuracy

**Symptom**: Accuracy < 60% (chance = 50%).

**Causes & Fixes**:

1. **Insufficient data**

   - Collect 50-100 trials per class
   - Check for class imbalance

2. **Poor preprocessing**

   - Verify bandpass filter (8-30 Hz motor imagery)
   - Remove artifacts (ICA for eye blinks/muscle)
   - Check electrode impedance (< 10kΩ)

3. **Low eigenvalue ratio** (`λ_max / λ_min < 2`)

   - Classes not separable → refine task instructions
   - Subject may not be "BCI-literate" (10-30% can't modulate mu rhythm)

4. **Overfitting**
   - Use cross-validation
   - Reduce number of filters (try 2-4)
   - Collect more training data

### Eigenvalue Warning

```typescript
const csp = calculateCommonSpatialPatterns(class1, class2, 8);

if (csp.eigenvalues[0] / csp.eigenvalues[7] < 1.5) {
  console.warn("Classes are not well-separated!");
  console.warn("Suggestions:");
  console.warn("1. Collect more training data (> 50 trials/class)");
  console.warn("2. Check preprocessing (bandpass 8-30 Hz for motor imagery)");
  console.warn("3. Verify task instructions (clear left/right imagery cues)");
  console.warn("4. Consider alternative paradigm (SSVEP, P300)");
}
```

### NaN or Inf Values

**Symptom**: CSP calculation fails with NaN.

**Causes**:

- Constant channels (no variance)
- Too few samples (< numChannels)
- Covariance matrix singular (not invertible)

**Fix**:

```typescript
// Check data quality
const class1Std = calculateStd(class1Data);
if (class1Std.some((s) => s < 1e-6)) {
  console.error("Channel has near-zero variance! Check electrode contact.");
}

// Ensure enough samples
if (numSamples < numChannels * 2) {
  throw new Error("Need at least 2× more samples than channels");
}
```

---

## References

1. **CSP Original Paper**: Ramoser et al., "Optimal spatial filtering of single trial EEG during imagined hand movement", IEEE Trans. Rehab. Eng., 2000
2. **BCI Textbook**: _Brain-Computer Interfaces: Principles and Practice_, Wolpaw & Wolpaw, Oxford University Press, 2012
3. **Motor Imagery Review**: Pfurtscheller & Neuper, "Motor imagery and direct brain-computer communication", Proc. IEEE, 2001
4. **CSP Tutorial**: Blankertz et al., "Optimizing spatial filters for robust EEG single-trial analysis", IEEE Signal Proc. Mag., 2008

---

## Examples

See `src/ts/__tests__/SpatialFilter.test.ts` for complete working examples:

- Basic CSP calculation and validation
- Motor imagery BCI (left vs right hand)
- Feature extraction (variance, log-variance)
- P300 speller classification
- CSP vs PCA comparison

---

**Next**: [Advanced Logger Features](./ADVANCED_LOGGER_FEATURES.md)
**Previous**: [Beamformer Guide](./BEAMFORMER_GUIDE.md)
