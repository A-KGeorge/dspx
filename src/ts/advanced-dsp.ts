/**
 * Advanced DSP Operations
 *
 * This module contains TypeScript implementations of advanced DSP operations:
 * - Hjorth Parameters (Activity, Mobility, Complexity)
 * - Spectral Features (Centroid, Rolloff, Flux)
 * - Entropy Measures (Shannon, Sample Entropy, Approximate Entropy)
 *
 * Note: Resampling operations (decimate, interpolate, resample) are being
 * implemented in C++ with polyphase FIR filtering for maximum efficiency.
 * Expected release: within next few days.
 */

import type {
  HjorthParameters,
  HjorthParams,
  SpectralFeatures,
  SpectralFeaturesParams,
  EntropyParams,
  SampleEntropyParams,
  ApproximateEntropyParams,
} from "./types.js";

/**
 * Calculate Hjorth parameters for signal complexity analysis
 *
 * Hjorth parameters describe three characteristics of a signal:
 * - Activity: Variance (spread) of the signal
 * - Mobility: How much the signal changes (based on first derivative)
 * - Complexity: How much the signal's change changes (based on second derivative)
 *
 * @param signal - Input signal samples
 * @returns Hjorth parameters object
 *
 * @example
 * const signal = new Float32Array([1, 2, 3, 4, 5, 4, 3, 2, 1]);
 * const hjorth = calculateHjorthParameters(signal);
 * console.log(`Activity: ${hjorth.activity}`);
 * console.log(`Mobility: ${hjorth.mobility}`);
 * console.log(`Complexity: ${hjorth.complexity}`);
 */
export function calculateHjorthParameters(
  signal: Float32Array
): HjorthParameters {
  const n = signal.length;
  if (n < 3) {
    throw new Error(
      "Signal must have at least 3 samples for Hjorth parameters"
    );
  }

  // Calculate variance of signal (Activity)
  let mean = 0;
  for (let i = 0; i < n; i++) {
    mean += signal[i];
  }
  mean /= n;

  let variance = 0;
  for (let i = 0; i < n; i++) {
    const diff = signal[i] - mean;
    variance += diff * diff;
  }
  variance /= n;

  // Calculate first derivative
  const firstDeriv = new Float32Array(n - 1);
  for (let i = 0; i < n - 1; i++) {
    firstDeriv[i] = signal[i + 1] - signal[i];
  }

  // Calculate variance of first derivative
  let derivMean = 0;
  for (let i = 0; i < firstDeriv.length; i++) {
    derivMean += firstDeriv[i];
  }
  derivMean /= firstDeriv.length;

  let derivVariance = 0;
  for (let i = 0; i < firstDeriv.length; i++) {
    const diff = firstDeriv[i] - derivMean;
    derivVariance += diff * diff;
  }
  derivVariance /= firstDeriv.length;

  // Calculate Mobility
  const mobility = Math.sqrt(derivVariance / variance);

  // Calculate second derivative
  const secondDeriv = new Float32Array(n - 2);
  for (let i = 0; i < n - 2; i++) {
    secondDeriv[i] = firstDeriv[i + 1] - firstDeriv[i];
  }

  // Calculate variance of second derivative
  let secondDerivMean = 0;
  for (let i = 0; i < secondDeriv.length; i++) {
    secondDerivMean += secondDeriv[i];
  }
  secondDerivMean /= secondDeriv.length;

  let secondDerivVariance = 0;
  for (let i = 0; i < secondDeriv.length; i++) {
    const diff = secondDeriv[i] - secondDerivMean;
    secondDerivVariance += diff * diff;
  }
  secondDerivVariance /= secondDeriv.length;

  // Calculate Mobility of first derivative
  const derivMobility = Math.sqrt(secondDerivVariance / derivVariance);

  // Calculate Complexity
  const complexity = derivMobility / mobility;

  return {
    activity: variance,
    mobility,
    complexity,
  };
}

/**
 * Calculate spectral centroid (center of mass of spectrum)
 *
 * @param magnitudeSpectrum - FFT magnitude spectrum
 * @param sampleRate - Sample rate in Hz
 * @returns Spectral centroid in Hz
 */
export function calculateSpectralCentroid(
  magnitudeSpectrum: Float32Array,
  sampleRate: number
): number {
  const n = magnitudeSpectrum.length;
  const freqResolution = sampleRate / (2 * n);

  let weightedSum = 0;
  let magnitudeSum = 0;

  for (let i = 0; i < n; i++) {
    const frequency = i * freqResolution;
    const magnitude = magnitudeSpectrum[i];
    weightedSum += frequency * magnitude;
    magnitudeSum += magnitude;
  }

  return magnitudeSum > 0 ? weightedSum / magnitudeSum : 0;
}

/**
 * Calculate spectral rolloff (frequency below which X% of energy is contained)
 *
 * @param magnitudeSpectrum - FFT magnitude spectrum
 * @param sampleRate - Sample rate in Hz
 * @param percentage - Rolloff percentage (0-100, default: 85)
 * @returns Rolloff frequency in Hz
 */
export function calculateSpectralRolloff(
  magnitudeSpectrum: Float32Array,
  sampleRate: number,
  percentage: number = 85
): number {
  const n = magnitudeSpectrum.length;
  const freqResolution = sampleRate / (2 * n);

  // Calculate total energy
  let totalEnergy = 0;
  for (let i = 0; i < n; i++) {
    totalEnergy += magnitudeSpectrum[i];
  }

  const threshold = (percentage / 100) * totalEnergy;
  let cumulativeEnergy = 0;

  for (let i = 0; i < n; i++) {
    cumulativeEnergy += magnitudeSpectrum[i];
    if (cumulativeEnergy >= threshold) {
      return i * freqResolution;
    }
  }

  return (n - 1) * freqResolution;
}

/**
 * Calculate spectral flux (change in spectrum from previous frame)
 *
 * @param currentSpectrum - Current frame's magnitude spectrum
 * @param previousSpectrum - Previous frame's magnitude spectrum (or null for first frame)
 * @returns Spectral flux value
 */
export function calculateSpectralFlux(
  currentSpectrum: Float32Array,
  previousSpectrum: Float32Array | null
): number {
  if (!previousSpectrum || previousSpectrum.length !== currentSpectrum.length) {
    return 0; // First frame or size mismatch
  }

  let flux = 0;
  for (let i = 0; i < currentSpectrum.length; i++) {
    const diff = currentSpectrum[i] - previousSpectrum[i];
    flux += diff * diff;
  }

  return Math.sqrt(flux);
}

/**
 * Calculate all spectral features from magnitude spectrum
 *
 * @param magnitudeSpectrum - FFT magnitude spectrum
 * @param sampleRate - Sample rate in Hz
 * @param previousSpectrum - Previous frame's spectrum for flux calculation
 * @param rolloffPercentage - Rolloff percentage (default: 85)
 * @returns Object containing all spectral features
 *
 * @example
 * const fft = performFFT(signal, 2048);
 * const features = calculateSpectralFeatures(fft.magnitude, 44100, null, 85);
 * console.log(`Centroid: ${features.centroid} Hz`);
 * console.log(`Rolloff: ${features.rolloff} Hz`);
 * console.log(`Flux: ${features.flux}`);
 */
export function calculateSpectralFeatures(
  magnitudeSpectrum: Float32Array,
  sampleRate: number,
  previousSpectrum: Float32Array | null = null,
  rolloffPercentage: number = 85
): SpectralFeatures {
  return {
    centroid: calculateSpectralCentroid(magnitudeSpectrum, sampleRate),
    rolloff: calculateSpectralRolloff(
      magnitudeSpectrum,
      sampleRate,
      rolloffPercentage
    ),
    flux: calculateSpectralFlux(magnitudeSpectrum, previousSpectrum),
  };
}

/**
 * Calculate Shannon entropy of a signal
 * Measures the uncertainty/randomness in the signal's amplitude distribution
 *
 * @param signal - Input signal samples
 * @param numBins - Number of histogram bins (default: 256)
 * @returns Shannon entropy value
 *
 * @example
 * const signal = new Float32Array(1000);
 * // ... fill signal with data
 * const entropy = calculateShannonEntropy(signal, 256);
 * console.log(`Entropy: ${entropy} bits`);
 */
export function calculateShannonEntropy(
  signal: Float32Array,
  numBins: number = 256
): number {
  const n = signal.length;
  if (n === 0) return 0;

  // Find min and max for binning
  let min = signal[0];
  let max = signal[0];
  for (let i = 1; i < n; i++) {
    if (signal[i] < min) min = signal[i];
    if (signal[i] > max) max = signal[i];
  }

  if (min === max) return 0; // Constant signal has zero entropy

  // Create histogram
  const histogram = new Array(numBins).fill(0);
  const range = max - min;
  const binWidth = range / numBins;

  for (let i = 0; i < n; i++) {
    let binIndex = Math.floor((signal[i] - min) / binWidth);
    if (binIndex >= numBins) binIndex = numBins - 1; // Handle edge case
    histogram[binIndex]++;
  }

  // Calculate entropy
  let entropy = 0;
  for (let i = 0; i < numBins; i++) {
    if (histogram[i] > 0) {
      const probability = histogram[i] / n;
      entropy -= probability * Math.log2(probability);
    }
  }

  return entropy;
}

/**
 * Calculate Sample Entropy (SampEn)
 * Measures the likelihood that similar patterns remain similar on next increment
 *
 * @param signal - Input signal samples
 * @param m - Pattern length (default: 2)
 * @param r - Tolerance for matching (default: 0.2 * std deviation)
 * @returns Sample entropy value
 *
 * @example
 * const signal = new Float32Array(1000);
 * // ... fill signal with data
 * const sampEn = calculateSampleEntropy(signal, 2, 0.2);
 * console.log(`Sample Entropy: ${sampEn}`);
 */
export function calculateSampleEntropy(
  signal: Float32Array,
  m: number = 2,
  r?: number
): number {
  const n = signal.length;
  if (n < m + 1) {
    throw new Error(`Signal too short for pattern length ${m}`);
  }

  // Calculate tolerance if not provided
  if (r === undefined) {
    // Calculate standard deviation
    let mean = 0;
    for (let i = 0; i < n; i++) {
      mean += signal[i];
    }
    mean /= n;

    let variance = 0;
    for (let i = 0; i < n; i++) {
      const diff = signal[i] - mean;
      variance += diff * diff;
    }
    const stdDev = Math.sqrt(variance / n);
    r = 0.2 * stdDev;
  }

  // Count matches for patterns of length m and m+1
  let countM = 0;
  let countMplus1 = 0;

  for (let i = 0; i < n - m; i++) {
    for (let j = i + 1; j < n - m; j++) {
      // Check if patterns of length m match
      let matchM = true;
      for (let k = 0; k < m; k++) {
        if (Math.abs(signal[i + k] - signal[j + k]) > r) {
          matchM = false;
          break;
        }
      }

      if (matchM) {
        countM++;

        // Check if patterns of length m+1 also match
        if (
          i < n - m - 1 &&
          j < n - m - 1 &&
          Math.abs(signal[i + m] - signal[j + m]) <= r
        ) {
          countMplus1++;
        }
      }
    }
  }

  // Calculate SampEn
  if (countM === 0 || countMplus1 === 0) {
    return Infinity; // Patterns don't repeat
  }

  return -Math.log(countMplus1 / countM);
}

/**
 * Calculate Approximate Entropy (ApEn)
 * Similar to SampEn but includes self-matches
 *
 * @param signal - Input signal samples
 * @param m - Pattern length (default: 2)
 * @param r - Tolerance for matching (default: 0.2 * std deviation)
 * @returns Approximate entropy value
 *
 * @example
 * const signal = new Float32Array(1000);
 * // ... fill signal with data
 * const apEn = calculateApproximateEntropy(signal, 2, 0.2);
 * console.log(`Approximate Entropy: ${apEn}`);
 */
export function calculateApproximateEntropy(
  signal: Float32Array,
  m: number = 2,
  r?: number
): number {
  const n = signal.length;
  if (n < m + 1) {
    throw new Error(`Signal too short for pattern length ${m}`);
  }

  // Calculate tolerance if not provided
  if (r === undefined) {
    let mean = 0;
    for (let i = 0; i < n; i++) {
      mean += signal[i];
    }
    mean /= n;

    let variance = 0;
    for (let i = 0; i < n; i++) {
      const diff = signal[i] - mean;
      variance += diff * diff;
    }
    const stdDev = Math.sqrt(variance / n);
    r = 0.2 * stdDev;
  }

  // Helper function to calculate phi
  const phi = (patternLength: number): number => {
    const counts = new Array(n - patternLength + 1).fill(0);

    for (let i = 0; i <= n - patternLength; i++) {
      for (let j = 0; j <= n - patternLength; j++) {
        let match = true;
        for (let k = 0; k < patternLength; k++) {
          if (Math.abs(signal[i + k] - signal[j + k]) > r) {
            match = false;
            break;
          }
        }
        if (match) {
          counts[i]++;
        }
      }
    }

    let sum = 0;
    for (let i = 0; i <= n - patternLength; i++) {
      if (counts[i] > 0) {
        sum += Math.log(counts[i] / (n - patternLength + 1));
      }
    }

    return sum / (n - patternLength + 1);
  };

  return phi(m) - phi(m + 1);
}

/**
 * Stateful class for tracking Hjorth parameters over a sliding window
 */
export class HjorthTracker {
  private buffer: Float32Array;
  private writeIndex: number = 0;
  private isFull: boolean = false;

  constructor(private windowSize: number) {
    this.buffer = new Float32Array(windowSize);
  }

  /**
   * Add a new sample and get updated Hjorth parameters
   */
  update(sample: number): HjorthParameters | null {
    this.buffer[this.writeIndex] = sample;
    this.writeIndex = (this.writeIndex + 1) % this.windowSize;

    if (this.writeIndex === 0) {
      this.isFull = true;
    }

    if (!this.isFull) {
      return null; // Not enough samples yet
    }

    // Create properly ordered view of circular buffer
    const orderedBuffer = new Float32Array(this.windowSize);
    for (let i = 0; i < this.windowSize; i++) {
      orderedBuffer[i] = this.buffer[(this.writeIndex + i) % this.windowSize];
    }

    return calculateHjorthParameters(orderedBuffer);
  }

  reset(): void {
    this.writeIndex = 0;
    this.isFull = false;
    this.buffer.fill(0);
  }
}

/**
 * Stateful class for tracking spectral features with previous frame memory
 */
export class SpectralFeaturesTracker {
  private previousSpectrum: Float32Array | null = null;

  /**
   * Calculate spectral features, maintaining state for flux calculation
   */
  calculate(
    magnitudeSpectrum: Float32Array,
    sampleRate: number,
    rolloffPercentage: number = 85
  ): SpectralFeatures {
    const features = calculateSpectralFeatures(
      magnitudeSpectrum,
      sampleRate,
      this.previousSpectrum,
      rolloffPercentage
    );

    // Store current spectrum for next call
    this.previousSpectrum = new Float32Array(magnitudeSpectrum);

    return features;
  }

  reset(): void {
    this.previousSpectrum = null;
  }
}

/**
 * Stateful class for tracking entropy over a sliding window
 */
export class EntropyTracker {
  private buffer: Float32Array;
  private writeIndex: number = 0;
  private isFull: boolean = false;

  constructor(private windowSize: number, private numBins: number = 256) {
    this.buffer = new Float32Array(windowSize);
  }

  /**
   * Add a new sample and get updated Shannon entropy
   */
  update(sample: number): number | null {
    this.buffer[this.writeIndex] = sample;
    this.writeIndex = (this.writeIndex + 1) % this.windowSize;

    if (this.writeIndex === 0) {
      this.isFull = true;
    }

    if (!this.isFull) {
      return null; // Not enough samples yet
    }

    // Create properly ordered view of circular buffer
    const orderedBuffer = new Float32Array(this.windowSize);
    for (let i = 0; i < this.windowSize; i++) {
      orderedBuffer[i] = this.buffer[(this.writeIndex + i) % this.windowSize];
    }

    return calculateShannonEntropy(orderedBuffer, this.numBins);
  }

  reset(): void {
    this.writeIndex = 0;
    this.isFull = false;
    this.buffer.fill(0);
  }
}

// ============================================================
// Wavelet Transform
// ============================================================

/**
 * Supported wavelet types
 */
export type WaveletType =
  | "haar"
  | "db1"
  | "db2"
  | "db3"
  | "db4"
  | "db5"
  | "db6"
  | "db7"
  | "db8"
  | "db9"
  | "db10";

/**
 * Discrete Wavelet Transform (DWT) - Single Level
 *
 * Decomposes a signal into approximation and detail coefficients using Daubechies wavelets.
 * This is implemented in C++ with SIMD optimizations for performance.
 *
 * Algorithm:
 * 1. Apply symmetric padding
 * 2. Convolve with low-pass filter (scaling function) → approximation coefficients
 * 3. Convolve with high-pass filter (wavelet function) → detail coefficients
 * 4. Downsample both by factor of 2
 *
 * Output format: [approximation_coeffs | detail_coeffs]
 *
 * @param signal - Input signal
 * @param wavelet - Wavelet type (e.g., 'haar', 'db4', 'db10')
 * @returns Array containing [approximation, detail] coefficients
 *
 * @example
 * ```typescript
 * const signal = new Float32Array([1, 2, 3, 4, 5, 4, 3, 2, 1]);
 * const result = dwt(signal, 'db4');
 * console.log('Approximation:', result.slice(0, result.length / 2));
 * console.log('Detail:', result.slice(result.length / 2));
 * ```
 */
export function dwt(signal: Float32Array, wavelet: WaveletType): Float32Array {
  // Note: This would be a native binding to the C++ WaveletTransformStage
  // For now, returning a placeholder
  throw new Error(
    "DWT is implemented as a pipeline stage. Use pipeline.addStage('waveletTransform', { wavelet: 'db4' })"
  );
}

/**
 * Hilbert Envelope - Instantaneous Amplitude
 *
 * Computes the instantaneous amplitude (envelope) of a signal using the Hilbert transform.
 * This is implemented in C++ with FFT-based algorithm and SIMD optimizations.
 *
 * Algorithm:
 * 1. FFT of windowed signal
 * 2. Create analytic signal (zero negative frequencies, double positive)
 * 3. IFFT back to time domain
 * 4. Compute magnitude: |z(t)| = sqrt(real² + imag²)
 *
 * The envelope represents the instantaneous amplitude, useful for:
 * - Amplitude modulation detection
 * - Beat detection
 * - Transient analysis
 * - Power envelope estimation
 *
 * @param signal - Input signal
 * @param windowSize - FFT window size (power of 2 recommended)
 * @param hopSize - Samples to advance between windows (default: windowSize/2)
 * @returns Envelope values
 *
 * @example
 * ```typescript
 * // Detect amplitude modulation
 * const signal = new Float32Array(1024);
 * // ... fill with modulated signal
 * const envelope = hilbertEnvelope(signal, 256, 128);
 * ```
 */
export function hilbertEnvelope(
  signal: Float32Array,
  windowSize: number = 512,
  hopSize?: number
): Float32Array {
  // Note: This would be a native binding to the C++ HilbertEnvelopeStage
  // For now, returning a placeholder
  throw new Error(
    "Hilbert envelope is implemented as a pipeline stage. Use pipeline.addStage('hilbertEnvelope', { windowSize: 512, hopSize: 256 })"
  );
}
