/**
 * Filter Design Module
 *
 * Provides high-level API for creating digital filters:
 * - FIR filters (Finite Impulse Response)
 * - IIR filters (Infinite Impulse Response)
 * - Butterworth, Chebyshev, Bessel, Biquad
 * - Low-pass, High-pass, Band-pass, Band-stop/Notch
 *
 * All filter design math is done in C++ for performance.
 * This module provides a clean TypeScript API.
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

// ============================================================
// Types
// ============================================================

/**
 * Filter type (topology/algorithm)
 */
export type FilterType =
  | "fir"
  | "iir"
  | "butterworth"
  | "chebyshev"
  | "bessel"
  | "biquad";

/**
 * Filter mode (frequency response)
 */
export type FilterMode =
  | "lowpass"
  | "highpass"
  | "bandpass"
  | "bandstop"
  | "notch";

/**
 * Window function for FIR filter design
 */
export type WindowType = "hamming" | "hann" | "blackman" | "bartlett";

/**
 * Common filter options
 */
export interface BaseFilterOptions {
  /** Sample rate in Hz (e.g., 44100) */
  sampleRate: number;

  /** Filter mode */
  mode: FilterMode;

  /** Cutoff frequency in Hz (for lowpass/highpass) */
  cutoffFrequency?: number;

  /** Low cutoff frequency in Hz (for bandpass/bandstop) */
  lowCutoffFrequency?: number;

  /** High cutoff frequency in Hz (for bandpass/bandstop) */
  highCutoffFrequency?: number;

  /** Whether filter maintains state between process calls (default: true) */
  stateful?: boolean;
}

/**
 * FIR filter specific options
 */
export interface FirFilterOptions extends BaseFilterOptions {
  type: "fir";

  /** Number of filter taps/coefficients (higher = sharper transition) */
  order: number;

  /** Window function (default: "hamming") */
  windowType?: WindowType;
}

/**
 * IIR filter specific options (generic)
 */
export interface IirFilterOptions extends BaseFilterOptions {
  type: "iir";

  /** Filter order (1-8 recommended) */
  order: number;
}

/**
 * Butterworth filter options (maximally flat passband)
 */
export interface ButterworthFilterOptions extends BaseFilterOptions {
  type: "butterworth";

  /** Filter order (1-8 recommended, higher = sharper rolloff) */
  order: number;
}

/**
 * Chebyshev filter options (steeper rolloff than Butterworth)
 */
export interface ChebyshevFilterOptions extends BaseFilterOptions {
  type: "chebyshev";

  /** Filter order (1-8 recommended) */
  order: number;

  /** Passband ripple in dB (default: 0.5 dB) */
  ripple?: number;

  /** Chebyshev type: 1 (passband ripple) or 2 (stopband ripple) */
  chebyshevType?: 1 | 2;
}

/**
 * Biquad filter options (2nd-order IIR section)
 */
export interface BiquadFilterOptions extends BaseFilterOptions {
  type: "biquad";

  /** Q factor / bandwidth (default: 0.707 for Butterworth) */
  q?: number;

  /** Gain in dB (for peak/shelf filters, default: 0) */
  gain?: number;
}

/**
 * Union of all filter option types
 */
export type FilterOptions =
  | FirFilterOptions
  | IirFilterOptions
  | ButterworthFilterOptions
  | ChebyshevFilterOptions
  | BiquadFilterOptions;

// ============================================================
// Filter Classes (Wrappers around native code)
// ============================================================

/**
 * FIR (Finite Impulse Response) Filter
 *
 * - Always stable (no feedback)
 * - Linear phase possible
 * - Requires more coefficients than IIR for same frequency response
 * - Uses SIMD-optimized convolution
 *
 * @example
 * ```ts
 * const filter = FirFilter.createLowPass({
 *   cutoffFrequency: 1000,
 *   sampleRate: 8000,
 *   order: 51,
 *   windowType: "hamming"
 * });
 *
 * const output = await filter.processSample(input);
 * ```
 */
export class FirFilter {
  private native: any;

  private constructor(nativeFilter: any) {
    this.native = nativeFilter;
  }

  /**
   * Create low-pass FIR filter
   */
  static createLowPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
    order: number;
    windowType?: WindowType;
  }): FirFilter {
    const {
      cutoffFrequency,
      sampleRate,
      order,
      windowType = "hamming",
    } = options;

    // Normalize cutoff frequency: fc / (fs/2)
    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${
          sampleRate / 2
        } Hz (Nyquist frequency)`
      );
    }

    const nativeFilter = DspAddon.FirFilter.createLowPass(
      normalizedCutoff,
      order,
      windowType
    );
    return new FirFilter(nativeFilter);
  }

  /**
   * Create high-pass FIR filter
   */
  static createHighPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
    order: number;
    windowType?: WindowType;
  }): FirFilter {
    const {
      cutoffFrequency,
      sampleRate,
      order,
      windowType = "hamming",
    } = options;

    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    const nativeFilter = DspAddon.FirFilter.createHighPass(
      normalizedCutoff,
      order,
      windowType
    );
    return new FirFilter(nativeFilter);
  }

  /**
   * Create band-pass FIR filter
   */
  static createBandPass(options: {
    lowCutoffFrequency: number;
    highCutoffFrequency: number;
    sampleRate: number;
    order: number;
    windowType?: WindowType;
  }): FirFilter {
    const {
      lowCutoffFrequency,
      highCutoffFrequency,
      sampleRate,
      order,
      windowType = "hamming",
    } = options;

    const normalizedLow = lowCutoffFrequency / (sampleRate / 2);
    const normalizedHigh = highCutoffFrequency / (sampleRate / 2);

    if (
      normalizedLow <= 0 ||
      normalizedHigh >= 1 ||
      normalizedLow >= normalizedHigh
    ) {
      throw new Error(
        `Invalid band: low=${lowCutoffFrequency} Hz, high=${highCutoffFrequency} Hz (Nyquist=${
          sampleRate / 2
        } Hz)`
      );
    }

    const nativeFilter = DspAddon.FirFilter.createBandPass(
      normalizedLow,
      normalizedHigh,
      order,
      windowType
    );
    return new FirFilter(nativeFilter);
  }

  /**
   * Create band-stop (notch) FIR filter
   */
  static createBandStop(options: {
    lowCutoffFrequency: number;
    highCutoffFrequency: number;
    sampleRate: number;
    order: number;
    windowType?: WindowType;
  }): FirFilter {
    const {
      lowCutoffFrequency,
      highCutoffFrequency,
      sampleRate,
      order,
      windowType = "hamming",
    } = options;

    const normalizedLow = lowCutoffFrequency / (sampleRate / 2);
    const normalizedHigh = highCutoffFrequency / (sampleRate / 2);

    if (
      normalizedLow <= 0 ||
      normalizedHigh >= 1 ||
      normalizedLow >= normalizedHigh
    ) {
      throw new Error(
        `Invalid band: low=${lowCutoffFrequency} Hz, high=${highCutoffFrequency} Hz`
      );
    }

    const nativeFilter = DspAddon.FirFilter.createBandStop(
      normalizedLow,
      normalizedHigh,
      order,
      windowType
    );
    return new FirFilter(nativeFilter);
  }

  /**
   * Process single sample
   */
  async processSample(input: number): Promise<number> {
    return this.native.processSample(input);
  }

  /**
   * Process batch of samples
   */
  async process(input: Float32Array): Promise<Float32Array> {
    return this.native.process(input);
  }

  /**
   * Reset filter state
   */
  reset(): void {
    this.native.reset();
  }

  /**
   * Get filter coefficients
   */
  getCoefficients(): Float32Array {
    return this.native.getCoefficients();
  }

  /**
   * Get filter order
   */
  getOrder(): number {
    return this.native.getOrder();
  }
}

/**
 * IIR (Infinite Impulse Response) Filter
 *
 * - Recursive structure (feedback)
 * - More efficient than FIR (fewer coefficients needed)
 * - Can be unstable if poles outside unit circle
 * - Non-linear phase
 *
 * Common types: Butterworth, Chebyshev, Bessel, Biquad
 *
 * @example
 * ```ts
 * const filter = IirFilter.createButterworthLowPass({
 *   cutoffFrequency: 1000,
 *   sampleRate: 8000,
 *   order: 4
 * });
 *
 * const output = await filter.processSample(input);
 * ```
 */
export class IirFilter {
  private native: any;

  private constructor(nativeFilter: any) {
    this.native = nativeFilter;
  }

  /**
   * Create Butterworth low-pass filter (maximally flat passband)
   */
  static createButterworthLowPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
    order: number;
  }): IirFilter {
    const { cutoffFrequency, sampleRate, order } = options;

    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    if (order < 1 || order > 8) {
      throw new Error("Order must be between 1 and 8");
    }

    const nativeFilter = DspAddon.IirFilter.createButterworthLowPass(
      normalizedCutoff,
      order
    );
    return new IirFilter(nativeFilter);
  }

  /**
   * Create Butterworth high-pass filter
   */
  static createButterworthHighPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
    order: number;
  }): IirFilter {
    const { cutoffFrequency, sampleRate, order } = options;

    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    if (order < 1 || order > 8) {
      throw new Error("Order must be between 1 and 8");
    }

    const nativeFilter = DspAddon.IirFilter.createButterworthHighPass(
      normalizedCutoff,
      order
    );
    return new IirFilter(nativeFilter);
  }

  /**
   * Create Butterworth band-pass filter
   */
  static createButterworthBandPass(options: {
    lowCutoffFrequency: number;
    highCutoffFrequency: number;
    sampleRate: number;
    order: number;
  }): IirFilter {
    const { lowCutoffFrequency, highCutoffFrequency, sampleRate, order } =
      options;

    const normalizedLow = lowCutoffFrequency / (sampleRate / 2);
    const normalizedHigh = highCutoffFrequency / (sampleRate / 2);

    if (
      normalizedLow <= 0 ||
      normalizedHigh >= 1 ||
      normalizedLow >= normalizedHigh
    ) {
      throw new Error(
        `Invalid band: low=${lowCutoffFrequency} Hz, high=${highCutoffFrequency} Hz`
      );
    }

    if (order < 1 || order > 8) {
      throw new Error("Order must be between 1 and 8");
    }

    const nativeFilter = DspAddon.IirFilter.createButterworthBandPass(
      normalizedLow,
      normalizedHigh,
      order
    );
    return new IirFilter(nativeFilter);
  }

  /**
   * Create first-order low-pass filter (simple RC filter)
   */
  static createFirstOrderLowPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
  }): IirFilter {
    const { cutoffFrequency, sampleRate } = options;

    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    const nativeFilter =
      DspAddon.IirFilter.createFirstOrderLowPass(normalizedCutoff);
    return new IirFilter(nativeFilter);
  }

  /**
   * Create first-order high-pass filter
   */
  static createFirstOrderHighPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
  }): IirFilter {
    const { cutoffFrequency, sampleRate } = options;

    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    const nativeFilter =
      DspAddon.IirFilter.createFirstOrderHighPass(normalizedCutoff);
    return new IirFilter(nativeFilter);
  }

  /**
   * Create Chebyshev Type I low-pass filter (passband ripple)
   */
  static createChebyshevLowPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
    order: number;
    rippleDb?: number;
  }): IirFilter {
    const { cutoffFrequency, sampleRate, order, rippleDb = 0.5 } = options;

    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    if (order < 1 || order > 8) {
      throw new Error("Order must be between 1 and 8");
    }

    if (rippleDb <= 0 || rippleDb > 3) {
      throw new Error("Ripple must be between 0 and 3 dB");
    }

    const nativeFilter = DspAddon.IirFilter.createChebyshevLowPass(
      normalizedCutoff,
      order,
      rippleDb
    );
    return new IirFilter(nativeFilter);
  }

  /**
   * Create Chebyshev Type I high-pass filter (passband ripple)
   */
  static createChebyshevHighPass(options: {
    cutoffFrequency: number;
    sampleRate: number;
    order: number;
    rippleDb?: number;
  }): IirFilter {
    const { cutoffFrequency, sampleRate, order, rippleDb = 0.5 } = options;

    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    if (order < 1 || order > 8) {
      throw new Error("Order must be between 1 and 8");
    }

    if (rippleDb <= 0 || rippleDb > 3) {
      throw new Error("Ripple must be between 0 and 3 dB");
    }

    const nativeFilter = DspAddon.IirFilter.createChebyshevHighPass(
      normalizedCutoff,
      order,
      rippleDb
    );
    return new IirFilter(nativeFilter);
  }

  /**
   * Create Chebyshev Type I band-pass filter (passband ripple)
   */
  static createChebyshevBandPass(options: {
    lowCutoffFrequency: number;
    highCutoffFrequency: number;
    sampleRate: number;
    order: number;
    rippleDb?: number;
  }): IirFilter {
    const {
      lowCutoffFrequency,
      highCutoffFrequency,
      sampleRate,
      order,
      rippleDb = 0.5,
    } = options;

    const normalizedLow = lowCutoffFrequency / (sampleRate / 2);
    const normalizedHigh = highCutoffFrequency / (sampleRate / 2);

    if (
      normalizedLow <= 0 ||
      normalizedHigh >= 1 ||
      normalizedLow >= normalizedHigh
    ) {
      throw new Error("Invalid cutoff frequencies");
    }

    if (order < 1 || order > 8) {
      throw new Error("Order must be between 1 and 8");
    }

    const nativeFilter = DspAddon.IirFilter.createChebyshevBandPass(
      normalizedLow,
      normalizedHigh,
      order,
      rippleDb
    );
    return new IirFilter(nativeFilter);
  }

  /**
   * Create peaking EQ biquad filter
   * Useful for parametric EQ, boosting/cutting specific frequencies
   */
  static createPeakingEQ(options: {
    centerFrequency: number;
    sampleRate: number;
    Q: number;
    gainDb: number;
  }): IirFilter {
    const { centerFrequency, sampleRate, Q, gainDb } = options;

    const normalizedFreq = centerFrequency / (sampleRate / 2);

    if (normalizedFreq <= 0 || normalizedFreq >= 1) {
      throw new Error(
        `Center frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    if (Q <= 0) {
      throw new Error("Q must be positive");
    }

    const nativeFilter = DspAddon.IirFilter.createPeakingEQ(
      normalizedFreq,
      Q,
      gainDb
    );
    return new IirFilter(nativeFilter);
  }

  /**
   * Create low-shelf biquad filter
   * Boosts or cuts all frequencies below cutoff
   */
  static createLowShelf(options: {
    cutoffFrequency: number;
    sampleRate: number;
    gainDb: number;
    Q?: number;
  }): IirFilter {
    const { cutoffFrequency, sampleRate, gainDb, Q = 0.707 } = options;

    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    if (Q <= 0) {
      throw new Error("Q must be positive");
    }

    const nativeFilter = DspAddon.IirFilter.createLowShelf(
      normalizedCutoff,
      gainDb,
      Q
    );
    return new IirFilter(nativeFilter);
  }

  /**
   * Create high-shelf biquad filter
   * Boosts or cuts all frequencies above cutoff
   */
  static createHighShelf(options: {
    cutoffFrequency: number;
    sampleRate: number;
    gainDb: number;
    Q?: number;
  }): IirFilter {
    const { cutoffFrequency, sampleRate, gainDb, Q = 0.707 } = options;

    const normalizedCutoff = cutoffFrequency / (sampleRate / 2);

    if (normalizedCutoff <= 0 || normalizedCutoff >= 1) {
      throw new Error(
        `Cutoff frequency must be between 0 and ${sampleRate / 2} Hz`
      );
    }

    if (Q <= 0) {
      throw new Error("Q must be positive");
    }

    const nativeFilter = DspAddon.IirFilter.createHighShelf(
      normalizedCutoff,
      gainDb,
      Q
    );
    return new IirFilter(nativeFilter);
  }

  /**
   * Process single sample
   */
  async processSample(input: number): Promise<number> {
    return this.native.processSample(input);
  }

  /**
   * Process batch of samples
   */
  async process(input: Float32Array): Promise<Float32Array> {
    return this.native.process(input);
  }

  /**
   * Reset filter state
   */
  reset(): void {
    this.native.reset();
  }

  /**
   * Get feedforward (B) coefficients
   */
  getBCoefficients(): Float32Array {
    return this.native.getBCoefficients();
  }

  /**
   * Get feedback (A) coefficients
   */
  getACoefficients(): Float32Array {
    return this.native.getACoefficients();
  }

  /**
   * Get filter order
   */
  getOrder(): number {
    return this.native.getOrder();
  }

  /**
   * Check if filter is stable
   */
  isStable(): boolean {
    return this.native.isStable();
  }
}

// ============================================================
// Unified Filter Design API
// ============================================================

/**
 * Create a digital filter with unified API
 *
 * Automatically dispatches to the appropriate filter type based on options.
 * All filter design math is done in C++ for performance.
 *
 * @example
 * ```ts
 * // FIR low-pass filter
 * const fir = createFilter({
 *   type: "fir",
 *   mode: "lowpass",
 *   cutoffFrequency: 1000,
 *   sampleRate: 8000,
 *   order: 51,
 *   windowType: "hamming"
 * });
 *
 * // Butterworth high-pass filter
 * const butter = createFilter({
 *   type: "butterworth",
 *   mode: "highpass",
 *   cutoffFrequency: 500,
 *   sampleRate: 8000,
 *   order: 4
 * });
 *
 * // Band-pass filter
 * const bandpass = createFilter({
 *   type: "fir",
 *   mode: "bandpass",
 *   lowCutoffFrequency: 300,
 *   highCutoffFrequency: 3400,
 *   sampleRate: 8000,
 *   order: 101
 * });
 * ```
 * @internal - Not exposed to users. Use specific filter constructors instead.
 */
function createFilter(options: FilterOptions): FirFilter | IirFilter {
  const { type, mode } = options;

  // FIR Filters
  if (type === "fir") {
    const firOpts = options as FirFilterOptions;

    switch (mode) {
      case "lowpass":
        if (!firOpts.cutoffFrequency)
          throw new Error("cutoffFrequency required for lowpass");
        return FirFilter.createLowPass({
          cutoffFrequency: firOpts.cutoffFrequency,
          sampleRate: firOpts.sampleRate,
          order: firOpts.order,
          windowType: firOpts.windowType,
        });

      case "highpass":
        if (!firOpts.cutoffFrequency)
          throw new Error("cutoffFrequency required for highpass");
        return FirFilter.createHighPass({
          cutoffFrequency: firOpts.cutoffFrequency,
          sampleRate: firOpts.sampleRate,
          order: firOpts.order,
          windowType: firOpts.windowType,
        });

      case "bandpass":
        if (!firOpts.lowCutoffFrequency || !firOpts.highCutoffFrequency) {
          throw new Error(
            "lowCutoffFrequency and highCutoffFrequency required for bandpass"
          );
        }
        return FirFilter.createBandPass({
          lowCutoffFrequency: firOpts.lowCutoffFrequency,
          highCutoffFrequency: firOpts.highCutoffFrequency,
          sampleRate: firOpts.sampleRate,
          order: firOpts.order,
          windowType: firOpts.windowType,
        });

      case "bandstop":
      case "notch":
        if (!firOpts.lowCutoffFrequency || !firOpts.highCutoffFrequency) {
          throw new Error(
            "lowCutoffFrequency and highCutoffFrequency required for bandstop"
          );
        }
        return FirFilter.createBandStop({
          lowCutoffFrequency: firOpts.lowCutoffFrequency,
          highCutoffFrequency: firOpts.highCutoffFrequency,
          sampleRate: firOpts.sampleRate,
          order: firOpts.order,
          windowType: firOpts.windowType,
        });

      default:
        throw new Error(`Unsupported FIR mode: ${mode}`);
    }
  }

  // Butterworth Filters
  if (type === "butterworth") {
    const butterOpts = options as ButterworthFilterOptions;

    switch (mode) {
      case "lowpass":
        if (!butterOpts.cutoffFrequency)
          throw new Error("cutoffFrequency required");
        return IirFilter.createButterworthLowPass({
          cutoffFrequency: butterOpts.cutoffFrequency,
          sampleRate: butterOpts.sampleRate,
          order: butterOpts.order,
        });

      case "highpass":
        if (!butterOpts.cutoffFrequency)
          throw new Error("cutoffFrequency required");
        return IirFilter.createButterworthHighPass({
          cutoffFrequency: butterOpts.cutoffFrequency,
          sampleRate: butterOpts.sampleRate,
          order: butterOpts.order,
        });

      case "bandpass":
        if (!butterOpts.lowCutoffFrequency || !butterOpts.highCutoffFrequency) {
          throw new Error(
            "lowCutoffFrequency and highCutoffFrequency required"
          );
        }
        return IirFilter.createButterworthBandPass({
          lowCutoffFrequency: butterOpts.lowCutoffFrequency,
          highCutoffFrequency: butterOpts.highCutoffFrequency,
          sampleRate: butterOpts.sampleRate,
          order: butterOpts.order,
        });

      default:
        throw new Error(`Unsupported Butterworth mode: ${mode}`);
    }
  }

  // Chebyshev Filters
  if (type === "chebyshev") {
    const chebyOpts = options as ChebyshevFilterOptions;
    const rippleDb = chebyOpts.ripple ?? 0.5;

    switch (mode) {
      case "lowpass":
        if (!chebyOpts.cutoffFrequency)
          throw new Error("cutoffFrequency required");
        return IirFilter.createChebyshevLowPass({
          cutoffFrequency: chebyOpts.cutoffFrequency,
          sampleRate: chebyOpts.sampleRate,
          order: chebyOpts.order,
          rippleDb,
        });

      case "highpass":
        if (!chebyOpts.cutoffFrequency)
          throw new Error("cutoffFrequency required");
        return IirFilter.createChebyshevHighPass({
          cutoffFrequency: chebyOpts.cutoffFrequency,
          sampleRate: chebyOpts.sampleRate,
          order: chebyOpts.order,
          rippleDb,
        });

      case "bandpass":
        if (!chebyOpts.lowCutoffFrequency || !chebyOpts.highCutoffFrequency) {
          throw new Error(
            "lowCutoffFrequency and highCutoffFrequency required"
          );
        }
        return IirFilter.createChebyshevBandPass({
          lowCutoffFrequency: chebyOpts.lowCutoffFrequency,
          highCutoffFrequency: chebyOpts.highCutoffFrequency,
          sampleRate: chebyOpts.sampleRate,
          order: chebyOpts.order,
          rippleDb,
        });

      default:
        throw new Error(`Unsupported Chebyshev mode: ${mode}`);
    }
  }

  // Biquad Filters (EQ, Shelf)
  if (type === "biquad") {
    const biquadOpts = options as BiquadFilterOptions;
    const Q = biquadOpts.q ?? 0.707;
    const gainDb = biquadOpts.gain ?? 0;

    switch (mode) {
      case "lowpass":
        // Use Butterworth for biquad lowpass
        if (!biquadOpts.cutoffFrequency)
          throw new Error("cutoffFrequency required");
        return IirFilter.createButterworthLowPass({
          cutoffFrequency: biquadOpts.cutoffFrequency,
          sampleRate: biquadOpts.sampleRate,
          order: 2,
        });

      case "highpass":
        if (!biquadOpts.cutoffFrequency)
          throw new Error("cutoffFrequency required");
        return IirFilter.createButterworthHighPass({
          cutoffFrequency: biquadOpts.cutoffFrequency,
          sampleRate: biquadOpts.sampleRate,
          order: 2,
        });

      default:
        throw new Error(
          `Unsupported Biquad mode: ${mode}. Use IirFilter.createPeakingEQ/createLowShelf/createHighShelf for EQ/shelf filters.`
        );
    }
  }

  // Generic IIR (first-order for now)
  if (type === "iir") {
    const iirOpts = options as IirFilterOptions;

    if (iirOpts.order === 1) {
      switch (mode) {
        case "lowpass":
          if (!iirOpts.cutoffFrequency)
            throw new Error("cutoffFrequency required");
          return IirFilter.createFirstOrderLowPass({
            cutoffFrequency: iirOpts.cutoffFrequency,
            sampleRate: iirOpts.sampleRate,
          });

        case "highpass":
          if (!iirOpts.cutoffFrequency)
            throw new Error("cutoffFrequency required");
          return IirFilter.createFirstOrderHighPass({
            cutoffFrequency: iirOpts.cutoffFrequency,
            sampleRate: iirOpts.sampleRate,
          });

        default:
          throw new Error(`Unsupported IIR mode for order 1: ${mode}`);
      }
    }

    throw new Error(
      `Generic IIR filters with order > 1 not yet supported. Use 'butterworth' type.`
    );
  }

  throw new Error(`Unsupported filter type: ${type}`);
}

/**
 * Adaptive LMS Filter (Least Mean Squares)
 *
 * An adaptive FIR filter that learns optimal coefficients in real-time using
 * the LMS algorithm. Adjusts weights based on error between filtered output
 * and a desired signal.
 *
 * Key Features:
 * - Adaptive coefficient learning with gradient descent
 * - SIMD-accelerated filtering and dot product operations
 * - Configurable learning rate (mu) and regularization (lambda)
 * - Normalized LMS (NLMS) option for stable convergence
 * - Per-channel independent adaptation
 * - Weight get/set for transfer learning
 *
 * Use Cases:
 * - Noise cancellation (adaptive noise filtering)
 * - Echo cancellation
 * - Channel equalization
 * - Predictive filtering
 * - System identification
 *
 * @example
 * ```ts
 * // Create adaptive filter with 32 taps
 * const lms = new AdaptiveLMSFilter(32, {
 *   mu: 0.01,           // Learning rate
 *   lambda: 0.001,      // Regularization (leaky LMS)
 *   normalized: true    // Use NLMS for stable convergence
 * });
 *
 * // Initialize for 1 channel
 * lms.init(1);
 *
 * // Training mode: adapt weights based on error
 * const input = new Float32Array([...]);     // Input signal x[n]
 * const desired = new Float32Array([...]);   // Desired signal d[n]
 * const output = new Float32Array(input.length);
 * const error = new Float32Array(input.length);
 *
 * lms.process(input, desired, output, error, true);
 *
 * // Inference mode: filter without adaptation
 * const filtered = new Float32Array(input.length);
 * lms.filter(input, filtered);
 *
 * // Save/load weights for transfer learning
 * const weights = lms.getWeights(0);
 * lms.setWeights(0, weights);
 * ```
 */
export class AdaptiveLMSFilter {
  private native: any;

  /**
   * Create a new adaptive LMS filter
   *
   * @param numTaps - Number of filter coefficients (filter order)
   * @param options - Configuration options
   * @param options.mu - Learning rate / step size (0 < mu <= 1). Default: 0.01
   *                     Lower = slower convergence but more stable
   *                     Higher = faster convergence but may oscillate
   * @param options.lambda - Regularization parameter for leaky LMS (0 <= lambda < 1). Default: 0
   *                        Prevents weight explosion, improves stability
   * @param options.normalized - Use Normalized LMS (NLMS). Default: false
   *                            Normalizes step size by input power for stable convergence
   *
   * @throws {Error} If numTaps is 0, mu is out of range, or lambda is out of range
   */
  constructor(
    numTaps: number,
    options: {
      mu?: number;
      lambda?: number;
      normalized?: boolean;
    } = {}
  ) {
    const { mu = 0.01, lambda = 0.0, normalized = false } = options;

    if (numTaps === 0) {
      throw new Error("numTaps must be greater than 0");
    }

    if (mu <= 0 || mu > 1) {
      throw new Error("Learning rate mu must be in range (0, 1]");
    }

    if (lambda < 0 || lambda >= 1) {
      throw new Error("Regularization lambda must be in range [0, 1)");
    }

    this.native = new DspAddon.DifferentiableFilter(
      numTaps,
      mu,
      lambda,
      normalized
    );
  }

  /**
   * Initialize the filter for a specific number of channels
   *
   * Must be called before processing. Each channel maintains independent state.
   *
   * @param numChannels - Number of channels (typically 1 for mono, 2 for stereo)
   */
  init(numChannels: number): void {
    this.native.init(numChannels);
  }

  /**
   * Process samples through adaptive filter with weight adaptation
   *
   * This is the "training mode" where filter weights are updated based on error.
   * All arrays must have length = numSamples * numChannels (for multi-channel,
   * data is interleaved: [ch0_s0, ch1_s0, ch0_s1, ch1_s1, ...])
   *
   * Algorithm:
   *   y[n] = w^T * x[n]              (filtering)
   *   e[n] = d[n] - y[n]             (error computation)
   *   w[n+1] = w[n] + mu * e[n] * x[n]  (weight update, if adapt=true)
   *
   * @param input - Input signal x[n] (Float32Array)
   * @param desired - Desired signal d[n] (Float32Array)
   * @param output - Output signal y[n] (Float32Array, modified in-place)
   * @param error - Error signal e[n] = d[n] - y[n] (Float32Array, modified in-place)
   * @param adapt - If true, update weights. If false, only filter. Default: true
   */
  process(
    input: Float32Array,
    desired: Float32Array,
    output: Float32Array,
    error: Float32Array,
    adapt: boolean = true
  ): void {
    this.native.process(input, desired, output, error, adapt);
  }

  /**
   * Filter signal without adapting weights (inference mode)
   *
   * Uses current filter weights to filter the input signal.
   * No weight updates occur. Useful after training is complete.
   *
   * @param input - Input signal (Float32Array)
   * @param output - Filtered output (Float32Array, modified in-place)
   */
  filter(input: Float32Array, output: Float32Array): void {
    this.native.filter(input, output);
  }

  /**
   * Reset filter state to initial conditions
   *
   * - Clears all filter weights (sets to 0)
   * - Clears input buffer history
   * - Resets input power estimate (for NLMS)
   */
  reset(): void {
    this.native.reset();
  }

  /**
   * Get current filter weights for a specific channel
   *
   * Useful for:
   * - Inspecting learned filter response
   * - Transfer learning (save/restore weights)
   * - Debugging convergence
   *
   * @param channel - Channel index (0-based)
   * @returns Float32Array of filter coefficients (length = numTaps)
   * @throws {Error} If channel index is out of range
   */
  getWeights(channel: number): Float32Array {
    return this.native.getWeights(channel);
  }

  /**
   * Set filter weights for a specific channel
   *
   * Useful for:
   * - Transfer learning (initialize with pre-trained weights)
   * - Manual coefficient design
   * - Restoring saved state
   *
   * @param channel - Channel index (0-based)
   * @param weights - New filter coefficients (Float32Array, length must equal numTaps)
   * @throws {Error} If channel index is out of range or weights.length != numTaps
   */
  setWeights(channel: number, weights: Float32Array): void {
    this.native.setWeights(channel, weights);
  }

  /**
   * Update learning rate (mu) during operation
   *
   * Can be used to implement:
   * - Annealing schedules (decrease mu over time)
   * - Adaptive step size algorithms
   * - Two-phase training (fast convergence → fine-tuning)
   *
   * @param mu - New learning rate (0 < mu <= 1)
   * @throws {Error} If mu is out of range
   */
  setLearningRate(mu: number): void {
    this.native.setLearningRate(mu);
  }

  /**
   * Get current learning rate
   *
   * @returns Current mu value
   */
  getLearningRate(): number {
    return this.native.getLearningRate();
  }

  /**
   * Get filter order (number of taps/coefficients)
   *
   * @returns Number of filter taps
   */
  getNumTaps(): number {
    return this.native.getNumTaps();
  }
}

// ============================================================
// Exports
// ============================================================

export {
  // Classes
  FirFilter as Fir,
  IirFilter as Iir,
};
