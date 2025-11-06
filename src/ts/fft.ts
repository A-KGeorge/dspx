/**
 * FFT/DFT TypeScript Bindings
 *
 * Provides all 8 Fourier transforms with full type safety:
 * - FFT/IFFT: Fast Fourier Transform (complex, O(N log N))
 * - DFT/IDFT: Discrete Fourier Transform (complex, O(N²))
 * - RFFT/IRFFT: Real-input FFT (outputs N/2+1 bins)
 * - RDFT/IRDFT: Real-input DFT (outputs N/2+1 bins)
 *
 * Plus moving/batched FFT for streaming applications
 */

import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import nodeGypBuild from "node-gyp-build";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

let DspAddon: any; // Or DspAddon
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
    console.error("Failed to load native DspAddon module.");
    console.error("Tried using both relative paths.");
    console.error(
      "Attempt 1 error (installed path ../):",
      (e as Error).message
    );
    console.error("Attempt 2 error (local path ../../):", err.message);
    throw new Error(
      `Could not load native module. Is the build complete? Search paths tried: ${join(
        __dirname,
        ".."
      )} and ${join(__dirname, "..", "..")}`
    );
  }
}

/**
 * Complex number representation
 */
export interface ComplexArray {
  /** Real part */
  real: Float32Array;
  /** Imaginary part */
  imag: Float32Array;
}

/**
 * Window function types for spectral analysis
 */
export type WindowType =
  | "none" // Rectangular (no windowing)
  | "hann" // Hann window (cosine taper)
  | "hamming" // Hamming window
  | "blackman" // Blackman window (better sidelobe rejection)
  | "bartlett"; // Triangular window

/**
 * FFT processing mode
 */
export type FftMode =
  | "moving" // Sliding window, updates on every sample
  | "batched"; // Process complete frames

/**
 * FFT Processor - Core transform engine
 *
 * **IMPORTANT: Radix-2 (Power-of-2) Requirement**
 *
 * The FFT/IFFT/RFFT/IRFFT transforms use the **Cooley-Tukey radix-2 algorithm**,
 * which requires the input size to be a power of 2 (e.g., 64, 128, 256, 512, 1024, 2048, 4096, ...).
 *
 * If your data length is not a power of 2:
 * 1. **Use DFT/IDFT/RDFT/IRDFT** - These work with any size but are slower (O(N²) vs O(N log N))
 * 2. **Zero-pad your signal** - Use `FftUtils.padToPowerOfTwo()` to automatically pad to next power of 2
 * 3. **Truncate or resample** - Adjust your signal to match a power-of-2 size
 *
 * @example
 * ```ts
 * // Example 1: Direct use with power-of-2 size
 * const fft = new FftProcessor(512);
 * const signal = new Float32Array(512);
 * const spectrum = fft.rfft(signal); // Fast O(N log N)
 *
 * // Example 2: Auto-padding for non-power-of-2 signals
 * const rawSignal = new Float32Array(1000); // Not power of 2!
 * const padded = FftUtils.padToPowerOfTwo(rawSignal); // Pads to 1024
 * const fft2 = new FftProcessor(padded.length);
 * const spectrum2 = fft2.rfft(padded);
 *
 * // Example 3: Use DFT for arbitrary sizes
 * const fft3 = new FftProcessor(1000); // Any size
 * const complexIn = { real: new Float32Array(1000), imag: new Float32Array(1000) };
 * const spectrum3 = fft3.rdft(rawSignal); // Slower O(N²) but no padding needed
 *
 * // Get magnitude spectrum
 * const magnitudes = fft.getMagnitude(spectrum);
 *
 * // Inverse transform
 * const reconstructed = fft.irfft(spectrum);
 * ```
 */
export class FftProcessor {
  private native: any;

  /**
   * Create FFT processor
   *
   * @param size FFT size
   * - For FFT/IFFT/RFFT/IRFFT: **MUST be power of 2** (64, 128, 256, 512, 1024, 2048, 4096, ...)
   * - For DFT/IDFT/RDFT/IRDFT: Can be any positive integer
   *
   * @throws {Error} If size is not positive
   * @throws {Error} If using FFT/RFFT methods with non-power-of-2 size
   */
  constructor(size: number) {
    this.native = new DspAddon.FftProcessor(size);
  }

  // ========== Complex Transforms ==========

  /**
   * Forward FFT (complex -> complex)
   *
   * Computes: X[k] = Σ x[n] * e^(-j2πkn/N)
   *
   * Time complexity: O(N log N)
   * Requires: size must be power of 2
   *
   * @param input Complex input signal { real, imag }
   * @returns Complex frequency spectrum
   */
  fft(input: ComplexArray): ComplexArray {
    return this.native.fft(input);
  }

  /**
   * Inverse FFT (complex -> complex)
   *
   * Computes: x[n] = (1/N) * Σ X[k] * e^(j2πkn/N)
   *
   * @param spectrum Complex frequency spectrum
   * @returns Complex time-domain signal
   */
  ifft(spectrum: ComplexArray): ComplexArray {
    return this.native.ifft(spectrum);
  }

  /**
   * Forward DFT (complex -> complex)
   *
   * Direct computation, slower but works for any size
   * Time complexity: O(N²)
   *
   * @param input Complex input signal
   * @returns Complex frequency spectrum
   */
  dft(input: ComplexArray): ComplexArray {
    return this.native.dft(input);
  }

  /**
   * Inverse DFT (complex -> complex)
   *
   * @param spectrum Complex frequency spectrum
   * @returns Complex time-domain signal
   */
  idft(spectrum: ComplexArray): ComplexArray {
    return this.native.idft(spectrum);
  }

  // ========== Real-Input Transforms ==========

  /**
   * Forward RFFT (real -> complex half-spectrum)
   *
   * Exploits Hermitian symmetry for real inputs: X[k] = X*[N-k]
   * Returns only positive frequencies (N/2+1 bins)
   *
   * Time complexity: O(N log N)
   * Output size: N/2 + 1 (includes DC and Nyquist)
   *
   * @param input Real input signal (size N)
   * @returns Complex half-spectrum (size N/2+1)
   *
   * @example
   * ```ts
   * const fft = new FftProcessor(1024);
   * const signal = new Float32Array(1024);
   * const spectrum = fft.rfft(signal);
   * // spectrum has 513 bins (DC + 512 positive frequencies)
   * ```
   */
  rfft(input: Float32Array): ComplexArray {
    return this.native.rfft(input);
  }

  /**
   * Inverse RFFT (complex half-spectrum -> real)
   *
   * Reconstructs real signal from half spectrum using Hermitian symmetry
   *
   * @param spectrum Complex half-spectrum (size N/2+1)
   * @returns Real time-domain signal (size N)
   */
  irfft(spectrum: ComplexArray): Float32Array {
    return this.native.irfft(spectrum);
  }

  /**
   * Forward RDFT (real -> complex half-spectrum)
   *
   * Direct computation version of RFFT
   * Time complexity: O(N²)
   *
   * @param input Real input signal
   * @returns Complex half-spectrum
   */
  rdft(input: Float32Array): ComplexArray {
    return this.native.rdft(input);
  }

  /**
   * Inverse RDFT (complex half-spectrum -> real)
   *
   * Direct computation version of IRFFT
   *
   * @param spectrum Complex half-spectrum
   * @returns Real time-domain signal
   */
  irdft(spectrum: ComplexArray): Float32Array {
    return this.native.irdft(spectrum);
  }

  // ========== Utility Methods ==========

  /**
   * Get FFT size
   */
  getSize(): number {
    return this.native.getSize();
  }

  /**
   * Get half-spectrum size (for real transforms)
   * Returns N/2 + 1
   */
  getHalfSize(): number {
    return this.native.getHalfSize();
  }

  /**
   * Check if FFT size is power of 2
   */
  isPowerOfTwo(): boolean {
    return this.native.isPowerOfTwo();
  }

  /**
   * Get magnitude spectrum from complex spectrum
   *
   * Computes: |X[k]| = sqrt(Re²(X[k]) + Im²(X[k]))
   *
   * @param spectrum Complex spectrum
   * @returns Magnitude array
   */
  getMagnitude(spectrum: ComplexArray): Float32Array {
    return this.native.getMagnitude(spectrum);
  }

  /**
   * Get phase spectrum from complex spectrum
   *
   * Computes: ∠X[k] = atan2(Im(X[k]), Re(X[k]))
   *
   * @param spectrum Complex spectrum
   * @returns Phase array (radians, -π to π)
   */
  getPhase(spectrum: ComplexArray): Float32Array {
    return this.native.getPhase(spectrum);
  }

  /**
   * Get power spectrum (magnitude squared)
   *
   * Computes: P[k] = |X[k]|²
   *
   * @param spectrum Complex spectrum
   * @returns Power array
   */
  getPower(spectrum: ComplexArray): Float32Array {
    return this.native.getPower(spectrum);
  }

  /**
   * Compute frequency bins for spectrum
   *
   * @param sampleRate Sample rate in Hz
   * @returns Frequency array in Hz
   *
   * @example
   * ```ts
   * const fft = new FftProcessor(1024);
   * const freqs = fft.getFrequencyBins(44100); // 44.1 kHz sample rate
   * // freqs[0] = 0 Hz (DC)
   * // freqs[1] = 43.07 Hz
   * // freqs[512] = 22050 Hz (Nyquist)
   * ```
   */
  getFrequencyBins(sampleRate: number): Float32Array {
    const size = this.isPowerOfTwo() ? this.getHalfSize() : this.getSize();
    const freqs = new Float32Array(size);
    const binWidth = sampleRate / this.getSize();

    for (let i = 0; i < size; i++) {
      freqs[i] = i * binWidth;
    }

    return freqs;
  }
}

/**
 * Moving FFT Processor - Streaming/batched transforms
 *
 * Provides sliding-window and frame-based FFT processing:
 * - Moving mode: Updates spectrum on every sample
 * - Batched mode: Processes complete frames with hop size
 * - **Automatic windowing** to reduce spectral leakage
 * - Overlap-add support
 *
 * **Windowing for Spectral Leakage Reduction:**
 *
 * When performing FFT on finite-length signals, discontinuities at the boundaries
 * cause **spectral leakage** - energy from one frequency bin "leaking" into others.
 * Window functions taper the signal at the edges to reduce this effect.
 *
 * Available window types:
 * - `none`: Rectangular (no windowing) - fastest but most leakage
 * - `hann`: Hann window - good general-purpose choice (default for audio)
 * - `hamming`: Hamming window - slightly better frequency resolution than Hann
 * - `blackman`: Blackman window - best sidelobe rejection, wider main lobe
 * - `bartlett`: Triangular window - simple linear taper
 *
 * **Choosing a window:**
 * - Audio analysis: Use `hann` (most common)
 * - Narrowband signals: Use `hamming`
 * - Wideband signals with interfering tones: Use `blackman`
 * - Quick testing: Use `none` (but expect leakage)
 *
 * Uses native C++ implementation for high performance.
 *
 * @example
 * ```ts
 * // Batched processing with 50% overlap and Hann windowing
 * const movingFft = new MovingFftProcessor({
 *   fftSize: 2048,
 *   hopSize: 1024,
 *   mode: "batched",
 *   windowType: "hann" // Reduces spectral leakage!
 * });
 *
 * // Stream audio samples
 * const samples = new Float32Array(4096);
 * movingFft.addSamples(samples, (spectrum, size) => {
 *   console.log(`Spectrum ready: ${size} bins`);
 * });
 *
 * // Compare windowing effects
 * const noWindow = new MovingFftProcessor({ fftSize: 1024, windowType: "none" });
 * const hannWindow = new MovingFftProcessor({ fftSize: 1024, windowType: "hann" });
 * // hannWindow will show much cleaner spectral peaks!
 * ```
 */
export class MovingFftProcessor {
  private native: any;

  /**
   * Create Moving FFT processor
   *
   * @param options Configuration object
   * @param options.fftSize FFT size (must be power of 2 for FFT, any size for DFT)
   * @param options.hopSize Hop size in samples (default: fftSize, i.e., no overlap)
   * @param options.mode Processing mode (default: "batched")
   * @param options.windowType Window function (default: "hann" for spectral leakage reduction)
   * @param options.realInput Use real-input transforms (default: true)
   *
   * @throws {Error} If fftSize is invalid
   * @throws {Error} If hopSize > fftSize
   *
   * @example
   * ```ts
   * // Audio spectral analysis with 75% overlap
   * const audioFFT = new MovingFftProcessor({
   *   fftSize: 2048,
   *   hopSize: 512,        // 75% overlap
   *   windowType: "hann"   // Reduce spectral leakage
   * });
   *
   * // Vibration analysis with Blackman window
   * const vibrationFFT = new MovingFftProcessor({
   *   fftSize: 4096,
   *   hopSize: 4096,        // No overlap
   *   windowType: "blackman" // Best sidelobe rejection
   * });
   * ```
   */
  constructor(options: {
    fftSize: number;
    hopSize?: number;
    mode?: FftMode;
    windowType?: WindowType;
    realInput?: boolean;
  }) {
    // Build options object with only defined properties
    const nativeOptions: any = {
      fftSize: options.fftSize,
      realInput: options.realInput ?? true,
    };

    if (options.hopSize !== undefined) {
      nativeOptions.hopSize = options.hopSize;
    }

    if (options.mode !== undefined) {
      nativeOptions.mode = options.mode;
    }

    if (options.windowType !== undefined) {
      nativeOptions.windowType = options.windowType;
    }

    this.native = new DspAddon.MovingFftProcessor(nativeOptions);
  }

  /**
   * Add single sample and optionally compute FFT
   *
   * @param sample Input sample
   * @returns Spectrum if computed, null otherwise
   */
  addSample(sample: number): ComplexArray | null {
    return this.native.addSample(sample);
  }

  /**
   * Add batch of samples
   *
   * @param samples Input samples
   * @param callback Called for each computed spectrum
   * @returns Number of spectra computed
   */
  addSamples(
    samples: Float32Array,
    callback?: (spectrum: ComplexArray, size: number) => void
  ): number {
    if (!callback) {
      // If no callback, just process and return count
      return this.native.addSamples(samples, () => {});
    }
    return this.native.addSamples(samples, callback);
  }

  /**
   * Force compute spectrum from current buffer
   */
  computeSpectrum(): ComplexArray {
    return this.native.computeSpectrum();
  }

  /**
   * Reset processor state
   */
  reset(): void {
    this.native.reset();
  }

  /**
   * Get FFT size
   */
  getFftSize(): number {
    return this.native.getFftSize();
  }

  /**
   * Get spectrum size (N/2+1 for real, N for complex)
   */
  getSpectrumSize(): number {
    return this.native.getSpectrumSize();
  }

  /**
   * Get hop size
   */
  getHopSize(): number {
    return this.native.getHopSize();
  }

  /**
   * Get buffer fill level
   */
  getFillLevel(): number {
    return this.native.getFillLevel();
  }

  /**
   * Check if ready to compute FFT
   */
  isReady(): boolean {
    return this.native.isReady();
  }

  /**
   * Set window type
   */
  setWindowType(type: WindowType): void {
    this.native.setWindowType(type);
  }

  /**
   * Get magnitude spectrum
   */
  getMagnitudeSpectrum(): Float32Array {
    return this.native.getMagnitudeSpectrum();
  }

  /**
   * Get power spectrum
   */
  getPowerSpectrum(): Float32Array {
    return this.native.getPowerSpectrum();
  }

  /**
   * Get phase spectrum
   */
  getPhaseSpectrum(): Float32Array {
    return this.native.getPhaseSpectrum();
  }

  /**
   * Get frequency bins
   */
  getFrequencyBins(sampleRate: number): Float32Array {
    return this.native.getFrequencyBins(sampleRate);
  }
}

/**
 * Helper functions for common FFT operations
 */
export namespace FftUtils {
  /**
   * Pad signal to next power of 2 with zeros
   *
   * This is the recommended approach for using FFT with arbitrary-length signals.
   * Zero-padding allows you to use the fast FFT algorithm (O(N log N)) instead of
   * the slower DFT (O(N²)).
   *
   * **Note on spectral resolution:**
   * - Zero-padding does NOT increase spectral resolution
   * - It only increases the number of frequency bins (interpolation)
   * - True resolution is still limited by original signal length
   *
   * @param signal Input signal (any length)
   * @returns Zero-padded signal (power-of-2 length)
   *
   * @example
   * ```ts
   * const signal = new Float32Array(1000); // Not power of 2
   * const padded = FftUtils.padToPowerOfTwo(signal); // 1024 samples
   * const fft = new FftProcessor(padded.length);
   * const spectrum = fft.rfft(padded);
   * ```
   */
  export function padToPowerOfTwo(signal: Float32Array): Float32Array {
    const nextPow2 = nextPowerOfTwo(signal.length);

    if (nextPow2 === signal.length) {
      // Already power of 2, return as-is
      return signal;
    }

    // Create zero-padded array
    const padded = new Float32Array(nextPow2);
    padded.set(signal);

    return padded;
  }

  /**
   * Check if number is a power of 2
   *
   * @param n Number to check
   * @returns True if n is a power of 2
   *
   * @example
   * ```ts
   * FftUtils.isPowerOfTwo(512);  // true
   * FftUtils.isPowerOfTwo(1000); // false
   * FftUtils.isPowerOfTwo(1024); // true
   * ```
   */
  export function isPowerOfTwo(n: number): boolean {
    return n > 0 && (n & (n - 1)) === 0;
  }

  /**
   * Find peak frequency in spectrum
   *
   * @param magnitudes Magnitude spectrum
   * @param sampleRate Sample rate in Hz
   * @param fftSize FFT size
   * @returns Peak frequency in Hz
   */
  export function findPeakFrequency(
    magnitudes: Float32Array,
    sampleRate: number,
    fftSize: number
  ): number {
    let maxIdx = 0;
    let maxVal = magnitudes[0];

    for (let i = 1; i < magnitudes.length; i++) {
      if (magnitudes[i] > maxVal) {
        maxVal = magnitudes[i];
        maxIdx = i;
      }
    }

    return (maxIdx * sampleRate) / fftSize;
  }

  /**
   * Convert magnitude spectrum to decibels
   *
   * @param magnitudes Magnitude spectrum
   * @param refLevel Reference level (default: 1.0)
   * @returns Spectrum in dB
   */
  export function toDecibels(
    magnitudes: Float32Array,
    refLevel: number = 1.0
  ): Float32Array {
    const db = new Float32Array(magnitudes.length);

    for (let i = 0; i < magnitudes.length; i++) {
      db[i] = 20 * Math.log10(Math.max(magnitudes[i], 1e-10) / refLevel);
    }

    return db;
  }

  /**
   * Apply A-weighting to frequency spectrum (perceptual audio)
   *
   * @param magnitudes Magnitude spectrum
   * @param frequencies Frequency bins in Hz
   * @returns A-weighted magnitudes
   */
  export function applyAWeighting(
    magnitudes: Float32Array,
    frequencies: Float32Array
  ): Float32Array {
    const weighted = new Float32Array(magnitudes.length);

    for (let i = 0; i < magnitudes.length; i++) {
      const f = frequencies[i];
      const f2 = f * f;
      const f4 = f2 * f2;

      // A-weighting formula
      const numerator = 12194 * 12194 * f4;
      const denominator =
        (f2 + 20.6 * 20.6) *
        Math.sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) *
        (f2 + 12194 * 12194);

      const weight = numerator / denominator;
      weighted[i] = magnitudes[i] * weight;
    }

    return weighted;
  }

  /**
   * Compute next power of 2
   */
  export function nextPowerOfTwo(n: number): number {
    if (n <= 0) return 1;

    let power = 1;
    while (power < n) {
      power *= 2;
    }

    return power;
  }

  /**
   * Zero-pad signal to target length
   */
  export function zeroPad(
    signal: Float32Array,
    targetLength: number
  ): Float32Array {
    if (signal.length >= targetLength) {
      return signal;
    }

    const padded = new Float32Array(targetLength);
    padded.set(signal);

    return padded;
  }
}

/**
 * Parallel batch FFT processor with result caching
 *
 * Processes multiple FFTs concurrently using a thread pool, with optional
 * caching to eliminate redundant computation for repeated signals.
 *
 * @example
 * ```typescript
 * // Process multiple audio channels in parallel
 * const processor = new FftBatchProcessor({
 *   numThreads: 8,      // Use 8 worker threads
 *   enableCache: true,  // Enable result caching
 *   cacheSize: 256      // Cache up to 256 unique signals
 * });
 *
 * const channels: Float32Array[] = [...]; // 16 audio channels
 * const signals = channels.map(ch => ({ input: ch }));
 * const spectra = processor.processBatch(signals);
 *
 * console.log(`Threads: ${processor.getNumThreads()}`);
 * console.log(`Cache hit rate: ${(processor.getCacheHitRate() * 100).toFixed(1)}%`);
 * ```
 *
 * @example
 * ```typescript
 * // Batch spectrogram generation
 * const windowSize = 1024;
 * const hopSize = 256;
 * const windows: Float32Array[] = extractWindows(audio, windowSize, hopSize);
 *
 * const processor = new FftBatchProcessor({ numThreads: 4 });
 * const spectrogram = processor.processBatch(
 *   windows.map(w => ({ input: w }))
 * );
 * ```
 */
export class FftBatchProcessor {
  private processor: any;

  /**
   * Create a new parallel batch FFT processor
   *
   * @param options Configuration options
   * @param options.numThreads Number of worker threads (0 = auto-detect physical cores)
   * @param options.enableCache Enable FFT result caching (default: true)
   * @param options.cacheSize Maximum number of cache entries (default: 128)
   */
  constructor(options?: {
    numThreads?: number;
    enableCache?: boolean;
    cacheSize?: number;
  }) {
    this.processor = new DspAddon.FftBatchProcessor(options || {});
  }

  /**
   * Process multiple FFTs in parallel
   *
   * Takes an array of signals and computes their FFTs concurrently using
   * a thread pool. Each signal is a real-valued input that will be
   * processed using RFFT (Real FFT).
   *
   * @param signals Array of input signals, each with a Float32Array input
   * @returns Array of complex spectra (one per input signal)
   *
   * @example
   * ```typescript
   * const signals = [
   *   { input: new Float32Array([1, 2, 3, 4]) },
   *   { input: new Float32Array([5, 6, 7, 8]) }
   * ];
   *
   * const spectra = processor.processBatch(signals);
   * // spectra[0] = { real: Float32Array, imag: Float32Array }
   * // spectra[1] = { real: Float32Array, imag: Float32Array }
   * ```
   */
  processBatch(signals: Array<{ input: Float32Array }>): ComplexArray[] {
    return this.processor.processBatch(signals);
  }

  /**
   * Get cache hit rate (0.0 to 1.0)
   *
   * Returns the fraction of FFT requests that were served from cache
   * rather than requiring computation. Higher is better.
   *
   * @returns Cache hit rate between 0.0 (no cache hits) and 1.0 (all hits)
   *
   * @example
   * ```typescript
   * const hitRate = processor.getCacheHitRate();
   * console.log(`${(hitRate * 100).toFixed(1)}% cache hits`);
   * ```
   */
  getCacheHitRate(): number {
    return this.processor.getCacheHitRate();
  }

  /**
   * Get detailed cache statistics
   *
   * @returns Object with hits, misses, and hit rate
   *
   * @example
   * ```typescript
   * const stats = processor.getCacheStats();
   * console.log(`Hits: ${stats.hits}, Misses: ${stats.misses}`);
   * console.log(`Hit rate: ${(stats.hitRate * 100).toFixed(1)}%`);
   * ```
   */
  getCacheStats(): { hits: number; misses: number; hitRate: number } {
    return this.processor.getCacheStats();
  }

  /**
   * Clear the FFT result cache
   *
   * Removes all cached FFT results and resets cache statistics.
   * Useful for benchmarking or when processing data with different
   * characteristics.
   *
   * @example
   * ```typescript
   * processor.clearCache();
   * // Next batch will have 0% cache hit rate
   * ```
   */
  clearCache(): void {
    this.processor.clearCache();
  }

  /**
   * Get number of worker threads
   *
   * @returns The number of threads used for parallel processing
   *
   * @example
   * ```typescript
   * const numThreads = processor.getNumThreads();
   * console.log(`Using ${numThreads} threads`);
   * ```
   */
  getNumThreads(): number {
    return this.processor.getNumThreads();
  }
}
