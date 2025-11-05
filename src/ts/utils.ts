import nodeGypBuild from "node-gyp-build";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import type {
  DetrendOptions,
  AutocorrelationOptions,
  CrossCorrelationOptions,
} from "./types.js";

// Get the directory of the current file
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

let DspAddon: any;
// Load the addon using node-gyp-build
try {
  // First, try the path that works when installed
  DspAddon = nodeGypBuild(join(__dirname, ".."));
} catch (e) {
  try {
    // If that fails, try the path that works locally during testing/dev
    DspAddon = nodeGypBuild(join(__dirname, "..", ".."));
  } catch (err: any) {
    // If both fail, throw a more informative error
    throw new Error(
      `Failed to load native DSP addon: ${err?.message || String(err)}`
    );
  }
}

/**
 * Computes the dot product of two vectors using SIMD-accelerated native code.
 *
 * The dot product is computed as: result = sum(a[i] * b[i]) for i = 0 to n-1
 *
 * This implementation uses SSE2/AVX2 SIMD instructions for optimal performance
 * on x86-64 architectures, processing 4-8 elements per instruction.
 *
 * @param a - First input vector (Float32Array)
 * @param b - Second input vector (Float32Array)
 * @returns The dot product as a scalar number
 * @throws {TypeError} If inputs are not Float32Arrays
 * @throws {RangeError} If vectors have different lengths
 *
 * @example
 * ```typescript
 * const a = new Float32Array([1, 2, 3, 4]);
 * const b = new Float32Array([5, 6, 7, 8]);
 * const result = dotProduct(a, b); // 70 = 1*5 + 2*6 + 3*7 + 4*8
 * ```
 *
 * @example
 * ```typescript
 * // Computing vector similarity (cosine similarity requires normalization)
 * const v1 = new Float32Array([1, 2, 3]);
 * const v2 = new Float32Array([4, 5, 6]);
 * const dot = dotProduct(v1, v2); // 32
 *
 * // For cosine similarity:
 * const norm1 = Math.sqrt(dotProduct(v1, v1)); // sqrt(14)
 * const norm2 = Math.sqrt(dotProduct(v2, v2)); // sqrt(77)
 * const cosineSimilarity = dot / (norm1 * norm2); // ~0.9746
 * ```
 *
 * @example
 * ```typescript
 * // Computing signal energy
 * const signal = new Float32Array([0.5, 0.8, -0.3, 0.2]);
 * const energy = dotProduct(signal, signal); // 1.02 = sum of squares
 * ```
 */
export function dotProduct(a: Float32Array, b: Float32Array): number {
  if (!(a instanceof Float32Array)) {
    throw new TypeError("First argument must be a Float32Array");
  }
  if (!(b instanceof Float32Array)) {
    throw new TypeError("Second argument must be a Float32Array");
  }
  if (a.length !== b.length) {
    throw new RangeError(
      `Vector lengths must match: a.length=${a.length}, b.length=${b.length}`
    );
  }

  return DspAddon.dotProduct(a, b);
}

/**
 * Computes the sum of array elements using SIMD-accelerated native code.
 *
 * This implementation uses ARM NEON (4-wide) or x86 SSE2/AVX2 (4-8 wide) SIMD
 * instructions with double-precision accumulation for numerical accuracy.
 *
 * @param buffer - Input array (Float32Array)
 * @returns The sum of all elements
 * @throws {TypeError} If input is not a Float32Array
 *
 * @example
 * ```typescript
 * const data = new Float32Array([1, 2, 3, 4, 5]);
 * const total = sum(data); // 15
 * ```
 */
export function sum(buffer: Float32Array): number {
  if (!(buffer instanceof Float32Array)) {
    throw new TypeError("Argument must be a Float32Array");
  }
  return DspAddon.sum(buffer);
}

/**
 * Computes the sum of squared elements using SIMD-accelerated native code.
 *
 * This implementation uses ARM NEON vmlaq_f32 (fused multiply-add) or x86
 * SSE2/AVX2 for optimal performance. Result is accumulated in double precision.
 *
 * Useful for computing RMS, variance, power, energy, and L2 norm.
 *
 * @param buffer - Input array (Float32Array)
 * @returns Sum of squares: buffer[0]² + buffer[1]² + ... + buffer[n-1]²
 * @throws {TypeError} If input is not a Float32Array
 *
 * @example
 * ```typescript
 * const signal = new Float32Array([3, 4]); // 3-4-5 triangle
 * const energy = sumOfSquares(signal); // 9 + 16 = 25
 * const rms = Math.sqrt(energy / signal.length); // 5 / sqrt(2) ≈ 3.536
 * ```
 *
 * @example
 * ```typescript
 * // Compute L2 norm (Euclidean length)
 * const vector = new Float32Array([1, 2, 2]);
 * const norm = Math.sqrt(sumOfSquares(vector)); // sqrt(9) = 3
 * ```
 */
export function sumOfSquares(buffer: Float32Array): number {
  if (!(buffer instanceof Float32Array)) {
    throw new TypeError("Argument must be a Float32Array");
  }
  return DspAddon.sumOfSquares(buffer);
}

/**
 * Removes linear trend or constant offset from a signal using least-squares regression.
 *
 * Detrending is essential for removing baseline drift, DC offsets, and slowly-varying
 * trends that can interfere with signal analysis.
 *
 * **Linear detrending** fits a line y = mx + b using least-squares and subtracts it.
 * **Constant detrending** simply removes the mean (faster, simpler).
 *
 * @param signal - Input signal (Float32Array)
 * @param options - Optional configuration object
 * @param options.type - "linear" (default) or "constant"
 * @returns Detrended signal (Float32Array, same length as input)
 * @throws {TypeError} If signal is not a Float32Array
 * @throws {TypeError} If type is not "linear" or "constant"
 *
 * @example
 * ```typescript
 * // Remove linear drift from EEG/ECG signal
 * const eegSignal = new Float32Array([1.0, 1.2, 1.4, 1.6, 1.8, 2.0]);
 * const detrended = DspUtils.detrend(eegSignal, { type: "linear" });
 * // Result oscillates around zero
 * ```
 *
 * @example
 * ```typescript
 * // Remove DC offset (mean removal)
 * const sensorData = new Float32Array([5.1, 5.2, 4.9, 5.0, 5.1]);
 * const centered = DspUtils.detrend(sensorData, { type: "constant" });
 * // Result has mean of ~0
 * ```
 *
 * @example
 * ```typescript
 * // Pipeline: detrend → bandpass → feature extraction
 * const raw = new Float32Array(1000);
 * const detrended = DspUtils.detrend(raw); // Default: linear
 *
 * const pipeline = createDspPipeline()
 *   .bandpass({ lowFreq: 0.5, highFreq: 40, sampleRate: 250 });
 *
 * const filtered = await pipeline.process(detrended, {
 *   channels: 1,
 *   sampleRate: 250
 * });
 * ```
 *
 * **Use Cases:**
 * - **EEG/ECG**: Remove baseline wander before feature extraction
 * - **Sensor data**: Remove drift from temperature, pressure sensors
 * - **Audio**: Remove DC offset before processing
 * - **Spectral analysis**: Detrend before FFT to reduce spectral leakage
 */
export function detrend(
  signal: Float32Array,
  options?: DetrendOptions
): Float32Array {
  if (!(signal instanceof Float32Array)) {
    throw new TypeError("Signal must be a Float32Array");
  }

  const type = options?.type || "linear";
  if (type !== "linear" && type !== "constant") {
    throw new TypeError("Detrend type must be 'linear' or 'constant'");
  }

  return DspAddon.detrend(signal, { type });
}

/**
 * Compute autocorrelation using FFT-based method.
 *
 * Autocorrelation measures how similar a signal is to a delayed version of itself.
 * This implementation uses the efficient FFT method: autocorr(x) = IFFT(|FFT(x)|²)
 *
 * **Use Cases:**
 * - **Pitch detection**: Find fundamental frequency in speech/music
 * - **Periodicity analysis**: Detect repeating patterns in sensor data
 * - **Echo detection**: Identify time delays in acoustic/radar signals
 * - **Pattern matching**: Find similar subsequences in time series
 * - **Spectral estimation**: Compute power spectral density (Wiener-Khinchin)
 *
 * **Performance:**
 * - O(n log n) complexity using FFT (vs O(n²) for direct computation)
 * - Handles signals up to millions of samples efficiently
 *
 * **Interpretation:**
 * - `result[0]` = signal energy (sum of squares)
 * - `result[k]` = correlation at lag k
 * - Peak at lag k indicates periodicity with period k samples
 * - To normalize: divide by `result[0]` to get values in [-1, 1]
 *
 * @param signal - Input signal (Float32Array)
 * @param options - Optional configuration (reserved for future use)
 * @returns Autocorrelation result (same length as input)
 *
 * @example
 * ```typescript
 * // Pitch detection in audio
 * const autocorr = DspUtils.autocorrelation(audioSignal);
 * const normalized = autocorr.map(v => v / autocorr[0]);
 *
 * // Find first peak after lag 0
 * let peakLag = 0;
 * let peakVal = 0;
 * for (let i = minPeriod; i < maxPeriod; i++) {
 *   if (normalized[i] > peakVal) {
 *     peakVal = normalized[i];
 *     peakLag = i;
 *   }
 * }
 * const pitch = sampleRate / peakLag; // Hz
 * ```
 *
 * @example
 * ```typescript
 * // Detect periodicity in ECG signal
 * const autocorr = DspUtils.autocorrelation(ecgSignal);
 *
 * // Heart rate is typically 40-200 bpm (0.67-3.33 Hz at 250 Hz sampling)
 * const minSamples = Math.floor(250 / 3.33); // ~75 samples
 * const maxSamples = Math.floor(250 / 0.67); // ~373 samples
 *
 * let heartbeatPeriod = 0;
 * let maxCorr = 0;
 * for (let lag = minSamples; lag < maxSamples; lag++) {
 *   if (autocorr[lag] > maxCorr) {
 *     maxCorr = autocorr[lag];
 *     heartbeatPeriod = lag;
 *   }
 * }
 * const bpm = (250 / heartbeatPeriod) * 60;
 * ```
 */
export function autocorrelation(
  signal: Float32Array,
  options?: AutocorrelationOptions
): Float32Array {
  if (!(signal instanceof Float32Array)) {
    throw new TypeError("Signal must be a Float32Array");
  }

  return DspAddon.autocorrelation(signal);
}

/**
 * Compute cross-correlation using FFT-based method.
 *
 * Cross-correlation measures the similarity between two signals as a function of time lag.
 * This implementation uses the efficient FFT method: xcorr(x, y) = IFFT(FFT(x) * conj(FFT(y)))
 *
 * **Use Cases:**
 * - **Time delay estimation**: Find the lag where two signals align (radar, sonar, echo cancellation)
 * - **Pattern matching**: Find where a template signal appears in a larger signal
 * - **Signal alignment**: Synchronize two related signals (multi-sensor fusion, A/V sync)
 * - **Template detection**: Find known signatures in noisy data
 *
 * **Performance:**
 * - O(n log n) complexity using FFT (vs O(n²) for direct computation)
 * - Handles signals up to millions of samples efficiently
 *
 * **Interpretation:**
 * - `result[k]` = correlation when signal y is shifted by k samples relative to x
 * - Peak at lag k indicates best alignment when y is delayed by k samples
 * - Positive lag k means y should be shifted right (delayed) to align with x
 * - To normalize: divide by `sqrt(energy(x) * energy(y))` for values in [-1, 1]
 *
 * @param x - First signal (Float32Array)
 * @param y - Second signal (Float32Array), must have same length as x
 * @param options - Optional configuration (reserved for future use)
 * @returns Cross-correlation result (same length as inputs)
 *
 * @throws {TypeError} If signals are not Float32Array
 * @throws {RangeError} If signals have different lengths
 *
 * @example
 * ```typescript
 * // Time delay estimation (acoustic echo cancellation)
 * const reference = new Float32Array(1000);  // Clean reference signal
 * const measured = new Float32Array(1000);   // Delayed + noisy measurement
 *
 * const xcorr = DspUtils.crossCorrelation(reference, measured);
 *
 * // Find peak to determine delay
 * let maxCorr = -Infinity;
 * let delay = 0;
 * for (let i = 0; i < xcorr.length; i++) {
 *   if (xcorr[i] > maxCorr) {
 *     maxCorr = xcorr[i];
 *     delay = i;
 *   }
 * }
 * console.log(`Detected delay: ${delay} samples (${delay / sampleRate * 1000} ms)`);
 * ```
 *
 * @example
 * ```typescript
 * // Pattern matching: find template in signal
 * const signal = new Float32Array(10000);    // Long signal
 * const template = new Float32Array(100);    // Short template pattern
 *
 * // Pad template to match signal length
 * const paddedTemplate = new Float32Array(10000);
 * paddedTemplate.set(template);
 *
 * const xcorr = DspUtils.crossCorrelation(signal, paddedTemplate);
 *
 * // Normalize for better pattern detection
 * const signalEnergy = DspUtils.sumOfSquares(signal);
 * const templateEnergy = DspUtils.sumOfSquares(template);
 * const normalized = xcorr.map(v => v / Math.sqrt(signalEnergy * templateEnergy));
 *
 * // Find peaks above threshold
 * const threshold = 0.7;
 * const matches = [];
 * for (let i = 0; i < normalized.length; i++) {
 *   if (normalized[i] > threshold) {
 *     matches.push({ position: i, correlation: normalized[i] });
 *   }
 * }
 * ```
 */
export function crossCorrelation(
  x: Float32Array,
  y: Float32Array,
  options?: CrossCorrelationOptions
): Float32Array {
  if (!(x instanceof Float32Array)) {
    throw new TypeError("First signal must be a Float32Array");
  }
  if (!(y instanceof Float32Array)) {
    throw new TypeError("Second signal must be a Float32Array");
  }

  return DspAddon.crossCorrelation(x, y);
}

/**
 * Utility functions for DSP operations.
 *
 * @namespace DspUtils
 */
export const DspUtils = {
  /**
   * Computes the dot product of two vectors using SIMD-accelerated native code.
   * @see {@link dotProduct} for detailed documentation
   */
  dotProduct,

  /**
   * Computes the sum of array elements using SIMD-accelerated native code.
   * @see {@link sum} for detailed documentation
   */
  sum,

  /**
   * Computes the sum of squared elements using SIMD-accelerated native code.
   * @see {@link sumOfSquares} for detailed documentation
   */
  sumOfSquares,

  /**
   * Removes linear trend or constant offset from signals.
   * @see {@link detrend} for detailed documentation
   */
  detrend,

  /**
   * Computes FFT-based autocorrelation for pitch and periodicity detection.
   * @see {@link autocorrelation} for detailed documentation
   */
  autocorrelation,

  /**
   * Computes FFT-based cross-correlation for time delay estimation and pattern matching.
   * @see {@link crossCorrelation} for detailed documentation
   */
  crossCorrelation,
};
