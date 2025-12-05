/**
 * Filter Bank Design
 *
 * Utilities for designing psychoacoustic and mathematical filter banks.
 * These are stateless functions that generate filter coefficients.
 */

import nodeGypBuild from "node-gyp-build";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

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
    throw new Error(
      `Could not load native module for FilterBankDesign. Is the build complete?`
    );
  }
}

/**
 * Frequency spacing scale for filter bank
 * - 'linear': Linear spacing in Hz (equal bandwidth)
 * - 'log': Logarithmic spacing (constant Q)
 * - 'mel': Mel scale (mimics human hearing perception)
 * - 'bark': Bark scale (critical band rate)
 */
export type FilterScale = "linear" | "log" | "mel" | "bark";
/**
 * Filter topology type
 * - 'butterworth': Maximally flat passband response
 * - 'chebyshev': Equiripple passband response (steeper rolloff)
 */
export type FilterBankType = "butterworth" | "chebyshev";

/**
 * Options for filter bank design
 */
export interface FilterBankOptions {
  /**
   * Frequency spacing scale
   * - 'mel': Recommended for speech/audio analysis (mimics human hearing)
   * - 'bark': Alternative psychoacoustic scale
   * - 'log': Constant-Q filter bank (octave bands)
   * - 'linear': Equal bandwidth bands
   */
  scale: FilterScale;

  /**
   * Number of frequency bands
   * Typical values:
   * - Mel spectrograms: 20-40 bands
   * - Audio analysis: 10-30 bands
   * - Octave bands: 10 bands (use 'log' scale)
   */
  count: number;

  /**
   * Sample rate in Hz
   */
  sampleRate: number;

  /**
   * Frequency range [minFreq, maxFreq] in Hz
   * @example [20, 8000] // 20 Hz to 8 kHz for speech
   * @example [0, sampleRate/2] // Full spectrum
   */
  frequencyRange: [number, number];

  /**
   * Filter topology (optional, defaults to 'butterworth')
   * - 'butterworth': Smoother response, more gradual rolloff
   * - 'chebyshev': Steeper rolloff, slight passband ripple
   */
  type?: FilterBankType;

  /**
   * Filter order per band (optional, defaults to 2)
   * Higher order = steeper slopes but more computation
   * Typical range: 2-8
   */
  order?: number;

  /**
   * Passband ripple for Chebyshev filters in dB (optional, defaults to 0.5)
   * Only used when type='chebyshev'
   * Typical range: 0.1-1.0 dB
   */
  rippleDb?: number;
}

/**
 * Filter coefficients for a single band
 */
export interface FilterCoefficients {
  /** Numerator coefficients (feedforward) */
  b: number[];
  /** Denominator coefficients (feedback) */
  a: number[];
}

/**
 * Filter Bank Design Utilities
 *
 * Generates sets of bandpass filters covering a frequency range according to
 * psychoacoustic (Mel, Bark) or mathematical (Linear, Log) scales.
 *
 * @example
 * // Create 24-band Mel-spaced filter bank for speech analysis
 * const melBank = FilterBankDesign.design({
 *   scale: 'mel',
 *   count: 24,
 *   sampleRate: 16000,
 *   frequencyRange: [100, 8000]
 * });
 *
 * @example
 * // Create octave-band filter bank
 * const octaveBank = FilterBankDesign.design({
 *   scale: 'log',
 *   count: 10,
 *   sampleRate: 44100,
 *   frequencyRange: [20, 20000],
 *   type: 'butterworth',
 *   order: 4
 * });
 */
export class FilterBankDesign {
  /**
   * Design a filter bank with specified options
   *
   * This is a stateless operation that returns filter coefficients
   * ready to be used with IIR filter processors or pipelines.
   *
   * @param options - Filter bank design options
   * @returns Array of filter coefficients (one per band)
   *
   * @throws Error if options are invalid:
   * - count must be positive
   * - frequency range must be valid
   * - maxFreq must be less than Nyquist frequency
   *
   * @example
   * // Design a Mel-spaced filter bank for speech
   * const bank = FilterBankDesign.design({
   *   scale: 'mel',
   *   count: 24,
   *   sampleRate: 16000,
   *   frequencyRange: [100, 8000]
   * });
   *
   * // Use the coefficients with an IIR filter
   * const pipeline = createDspPipeline();
   * bank.forEach((coeffs, i) => {
   *   pipeline.filter({
   *     type: 'iir',
   *     b: coeffs.b,
   *     a: coeffs.a
   *   });
   * });
   */
  static design(options: FilterBankOptions): FilterCoefficients[] {
    // Validation
    if (options.count < 1) {
      throw new Error("Band count must be positive");
    }
    if (options.frequencyRange[0] < 0) {
      throw new Error("Minimum frequency cannot be negative");
    }
    if (options.frequencyRange[0] >= options.frequencyRange[1]) {
      throw new Error("Invalid frequency range: minFreq must be < maxFreq");
    }
    if (options.frequencyRange[1] > options.sampleRate / 2) {
      throw new Error("Maximum frequency must be <= Nyquist frequency");
    }

    // Call native function
    return DspAddon.designFilterBank({
      ...options,
      type: options.type || "butterworth",
      order: options.order || 2,
      rippleDb: options.rippleDb || 0.5,
    });
  }

  /**
   * Create a Mel-scale filter bank (helper method)
   *
   * Mel scale mimics human auditory perception where equal distances
   * on the Mel scale correspond to equal perceived pitch differences.
   *
   * Common for: Speech recognition, audio analysis, spectrograms
   *
   * @param count - Number of bands (typically 20-40)
   * @param sampleRate - Sample rate in Hz
   * @param range - Frequency range [min, max] in Hz (defaults to [0, Nyquist])
   * @returns Array of filter coefficients
   *
   * @example
   * // Standard 40-band Mel filter bank for speech recognition
   * const melBank = FilterBankDesign.createMel(40, 16000, [100, 8000]);
   */
  static createMel(
    count: number,
    sampleRate: number,
    range: [number, number] = [0, sampleRate / 2]
  ): FilterCoefficients[] {
    return this.design({
      scale: "mel",
      count,
      sampleRate,
      frequencyRange: range,
      type: "butterworth",
      order: 2,
    });
  }

  /**
   * Create a Bark-scale filter bank (helper method)
   *
   * Bark scale represents critical band rate in human hearing.
   * Each Bark corresponds to a critical band width.
   *
   * Common for: Psychoacoustic modeling, audio compression
   *
   * @param count - Number of bands (typically 20-30)
   * @param sampleRate - Sample rate in Hz
   * @param range - Frequency range [min, max] in Hz (defaults to [0, Nyquist])
   * @returns Array of filter coefficients
   *
   * @example
   * // 24-band Bark filter bank
   * const barkBank = FilterBankDesign.createBark(24, 44100, [20, 20000]);
   */
  static createBark(
    count: number,
    sampleRate: number,
    range: [number, number] = [0, sampleRate / 2]
  ): FilterCoefficients[] {
    return this.design({
      scale: "bark",
      count,
      sampleRate,
      frequencyRange: range,
      type: "butterworth",
      order: 2,
    });
  }

  /**
   * Create a logarithmic (constant-Q) filter bank (helper method)
   *
   * Logarithmic spacing creates constant-Q bands where Q = f_center / bandwidth.
   * This is common in musical applications (octave bands).
   *
   * Common for: Musical analysis, octave band analysis, EQ
   *
   * @param count - Number of bands (typically 10 for octave bands)
   * @param sampleRate - Sample rate in Hz
   * @param range - Frequency range [min, max] in Hz (defaults to [20, Nyquist])
   * @returns Array of filter coefficients
   *
   * @example
   * // Standard 10-band octave filter bank
   * const octaveBank = FilterBankDesign.createLog(10, 44100, [20, 20000]);
   */
  static createLog(
    count: number,
    sampleRate: number,
    range: [number, number] = [20, sampleRate / 2]
  ): FilterCoefficients[] {
    return this.design({
      scale: "log",
      count,
      sampleRate,
      frequencyRange: range,
      type: "butterworth",
      order: 3, // Higher order for cleaner octave separation
    });
  }

  /**
   * Create a linear filter bank (helper method)
   *
   * Linear spacing creates equal-bandwidth bands across the spectrum.
   * Less perceptually relevant but mathematically simple.
   *
   * Common for: Analysis, research, testing
   *
   * @param count - Number of bands
   * @param sampleRate - Sample rate in Hz
   * @param range - Frequency range [min, max] in Hz (defaults to [0, Nyquist])
   * @returns Array of filter coefficients
   *
   * @example
   * // 20 equal-bandwidth bands
   * const linearBank = FilterBankDesign.createLinear(20, 44100);
   */
  static createLinear(
    count: number,
    sampleRate: number,
    range: [number, number] = [0, sampleRate / 2]
  ): FilterCoefficients[] {
    return this.design({
      scale: "linear",
      count,
      sampleRate,
      frequencyRange: range,
      type: "butterworth",
      order: 2,
    });
  }

  /**
   * Get frequency boundaries for a filter bank design
   *
   * Returns the boundary frequencies without actually designing the filters.
   * Useful for visualization and planning.
   *
   * @param options - Filter bank design options (same as design())
   * @returns Array of boundary frequencies in Hz (length = count + 1)
   *
   * @example
   * const boundaries = FilterBankDesign.getBoundaries({
   *   scale: 'mel',
   *   count: 24,
   *   sampleRate: 16000,
   *   frequencyRange: [100, 8000]
   * });
   * console.log('Band edges:', boundaries);
   * // [100, 145.2, 195.8, ..., 8000]
   */
  static getBoundaries(options: FilterBankOptions): number[] {
    // Call native function
    return DspAddon.getFilterBankBoundaries({
      ...options,
      type: options.type || "butterworth",
      order: options.order || 2,
      rippleDb: options.rippleDb || 0.5,
    });
  }
}
