import nodeGypBuild from "node-gyp-build";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

import type {
  ProcessOptions,
  RedisConfig,
  MovingAverageParams,
  RmsParams,
  RectifyParams,
  VarianceParams,
  ZScoreNormalizeParams,
  MeanAbsoluteValueParams,
  WaveformLengthParams,
  SlopeSignChangeParams,
  WillisonAmplitudeParams,
  LinearRegressionParams,
  LmsFilterParams,
  InterpolateParams,
  DecimateParams,
  ResampleParams,
  ConvolutionParams,
  WaveletTransformParams,
  HilbertEnvelopeParams,
  PipelineCallbacks,
  RlsFilterParams,
  LogEntry,
  SampleBatch,
  TapCallback,
  PipelineStateSummary,
  BeamformerWeightsResult,
  CspResult,
  GscPreprocessorParams,
  ChannelSelectorParams,
  ChannelSelectParams,
  ChannelMergeParams,
  ClipDetectionParams,
  PeakDetectionParams,
  CspTransformParams,
  fftParams,
  stftParams,
  MelSpectrogramParams,
  MfccParams,
} from "./types.js";
import { CircularLogBuffer } from "./CircularLogBuffer.js";
import { DriftDetector } from "./DriftDetector.js";
import {
  FirFilter,
  IirFilter,
  type FilterOptions,
  type FilterType,
  type FilterMode,
} from "./filters.js";

// Get the directory of the current file
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
 * DSP Processor class that wraps the native C++ DspPipeline
 * Provides a fluent API for building and processing DSP pipelines
 */
class DspProcessor {
  private stages: string[] = [];
  private callbacks?: PipelineCallbacks;
  private logBuffer: CircularLogBuffer;
  private tapCallbacks: Array<{ stageName: string; callback: TapCallback }> =
    [];
  private driftDetector: DriftDetector | null = null;

  constructor(private nativeInstance: any) {
    // Initialize circular buffer with capacity for typical log volume
    // (2-3 logs per process call, supports bursts up to 32)
    this.logBuffer = new CircularLogBuffer(32);
  }

  /**
   * Generate a Kafka-style topic for a log entry
   */
  private generateLogTopic(
    level: "debug" | "info" | "warn" | "error",
    context?: any
  ): string {
    const stage = context?.stage;
    const category = context?.category || level;

    if (stage) {
      // Stage-specific topic: pipeline.stage.<stageName>.<category>
      return `pipeline.stage.${stage}.${category}`;
    } else {
      // General topic: pipeline.<level>
      return `pipeline.${level}`;
    }
  }

  /**
   * Check if a topic matches the configured topic filter
   */
  private matchesTopicFilter(topic: string): boolean {
    const filter = this.callbacks?.topicFilter;
    if (!filter) {
      return true; // No filter, accept all
    }

    const filters = Array.isArray(filter) ? filter : [filter];

    for (const pattern of filters) {
      // Convert wildcard pattern to regex
      // pipeline.stage.* -> ^pipeline\.stage\.[^.]+$
      // pipeline.*.error -> ^pipeline\.[^.]+\.error$
      const regexPattern = pattern
        .replace(/\./g, "\\.")
        .replace(/\*/g, "[^.]+");
      const regex = new RegExp(`^${regexPattern}$`);

      if (regex.test(topic)) {
        return true;
      }
    }

    return false;
  }

  /**
   * Add a log entry to the circular buffer for batched processing
   */
  /**
   * Map log level to default priority
   * debug: 2, info: 5, warn: 7, error: 9
   */
  private getDefaultPriority(
    level: "debug" | "info" | "warn" | "error"
  ): 2 | 5 | 7 | 9 {
    switch (level) {
      case "debug":
        return 2;
      case "info":
        return 5;
      case "warn":
        return 7;
      case "error":
        return 9;
    }
  }

  /**
   * Pool a log entry in the circular buffer for batch delivery
   */
  private poolLog(
    level: "debug" | "info" | "warn" | "error",
    message: string,
    context?: any
  ): void {
    const topic = this.generateLogTopic(level, context);

    // If onLogBatch is configured, pool the log in circular buffer (with topic filtering)
    if (this.callbacks?.onLogBatch && this.matchesTopicFilter(topic)) {
      this.logBuffer.push({
        topic,
        level,
        message,
        context,
        timestamp: performance.now(),
        priority: this.getDefaultPriority(level),
      });
    }

    // If onLog is also configured, call it immediately (backwards compatible, with topic filtering)
    if (this.callbacks?.onLog && this.matchesTopicFilter(topic)) {
      this.callbacks.onLog(topic, level, message, context);
    }
  }

  /**
   * Flush all pooled logs from circular buffer to the onLogBatch callback
   */
  private flushLogs(): void {
    if (this.callbacks?.onLogBatch && this.logBuffer.hasEntries()) {
      const logs = this.logBuffer.flush();
      this.callbacks.onLogBatch(logs);
    }
  }

  /**
   * Add a moving average filter stage to the pipeline
   * @param params - Configuration for the moving average filter
   * @param params.mode - "batch" for stateless averaging (all samples → single average), "moving" for windowed averaging
   * @param params.windowSize - Required for "moving" mode when using sample-based processing
   * @param params.windowDuration - Required for "moving" mode when using time-based processing (milliseconds)
   * @returns this instance for method chaining
   *
   * @example
   * // Batch mode (stateless)
   * pipeline.MovingAverage({ mode: "batch" });
   *
   * @example
   * // Moving mode with sample window (legacy)
   * pipeline.MovingAverage({ mode: "moving", windowSize: 10 });
   *
   * @example
   * // Moving mode with time window (recommended)
   * pipeline.MovingAverage({ mode: "moving", windowDuration: 5000 }); // 5 seconds
   */
  MovingAverage(params: MovingAverageParams): this {
    if (params.mode === "moving") {
      if (
        params.windowSize === undefined &&
        params.windowDuration === undefined
      ) {
        throw new TypeError(
          `MovingAverage: either windowSize or windowDuration must be specified for "moving" mode`
        );
      }
      if (
        params.windowSize !== undefined &&
        (params.windowSize <= 0 || !Number.isInteger(params.windowSize))
      ) {
        throw new TypeError(
          `MovingAverage: windowSize must be a positive integer for "moving" mode, got ${params.windowSize}`
        );
      }
      if (params.windowDuration !== undefined && params.windowDuration <= 0) {
        throw new TypeError(
          `MovingAverage: windowDuration must be positive, got ${params.windowDuration}`
        );
      }
    }
    this.nativeInstance.addStage("movingAverage", params);
    this.stages.push(`movingAverage:${params.mode}`);
    return this;
  }

  /**
   * Add a RMS (root mean square) stage to the pipeline
   * @param params - Configuration for the RMS filter
   * @param params.mode - "batch" for stateless RMS (all samples → single RMS), "moving" for windowed RMS
   * @param params.windowSize - Required for "moving" mode when using sample-based processing
   * @param params.windowDuration - Required for "moving" mode when using time-based processing (milliseconds)
   * @returns this instance for method chaining
   *
   * @example
   * // Batch mode (stateless)
   * pipeline.Rms({ mode: "batch" });
   *
   * @example
   * // Moving mode with sample window (legacy)
   * pipeline.Rms({ mode: "moving", windowSize: 50 });
   *
   * @example
   * // Moving mode with time window (recommended)
   * pipeline.Rms({ mode: "moving", windowDuration: 10000 }); // 10 seconds
   */
  Rms(params: RmsParams): this {
    if (params.mode === "moving") {
      if (
        params.windowSize === undefined &&
        params.windowDuration === undefined
      ) {
        throw new TypeError(
          `RMS: either windowSize or windowDuration must be specified for "moving" mode`
        );
      }
      if (
        params.windowSize !== undefined &&
        (params.windowSize <= 0 || !Number.isInteger(params.windowSize))
      ) {
        throw new TypeError(
          `RMS: windowSize must be a positive integer for "moving" mode, got ${params.windowSize}`
        );
      }
      if (params.windowDuration !== undefined && params.windowDuration <= 0) {
        throw new TypeError(
          `RMS: windowDuration must be positive, got ${params.windowDuration}`
        );
      }
    }
    this.nativeInstance.addStage("rms", params);
    this.stages.push(`rms:${params.mode}`);
    return this;
  }

  /**
   * Add a rectify stage to the pipeline
   * @param params - Configuration for the rectify filter (optional)
   * @returns this instance for method chaining
   */
  Rectify(params?: RectifyParams): this {
    this.nativeInstance.addStage("rectify", params || { mode: "full" });
    this.stages.push(`rectify:${params?.mode || "full"}`);
    return this;
  }

  /**
   * Add a variance stage to the pipeline
   * Variance measures the spread of data around the mean
   *
   * @param params - Configuration for the variance filter
   * @param params.mode - "batch" for stateless variance (all samples → single value), "moving" for windowed variance
   * @param params.windowSize - Required for "moving" mode when using sample-based processing
   * @param params.windowDuration - Required for "moving" mode when using time-based processing (milliseconds)
   * @returns this instance for method chaining
   *
   * @example
   * // Batch variance (stateless)
   * pipeline.Variance({ mode: "batch" });
   *
   * @example
   * // Moving variance with sample window (legacy)
   * pipeline.Variance({ mode: "moving", windowSize: 100 });
   *
   * @example
   * // Moving variance with time window (recommended)
   * pipeline.Variance({ mode: "moving", windowDuration: 60000 }); // 1 minute
   */
  Variance(params: VarianceParams): this {
    if (params.mode === "moving") {
      if (
        params.windowSize === undefined &&
        params.windowDuration === undefined
      ) {
        throw new TypeError(
          `Variance: either windowSize or windowDuration must be specified for "moving" mode`
        );
      }
      if (
        params.windowSize !== undefined &&
        (params.windowSize <= 0 || !Number.isInteger(params.windowSize))
      ) {
        throw new TypeError(
          `Variance: windowSize must be a positive integer for "moving" mode, got ${params.windowSize}`
        );
      }
      if (params.windowDuration !== undefined && params.windowDuration <= 0) {
        throw new TypeError(
          `Variance: windowDuration must be positive, got ${params.windowDuration}`
        );
      }
    }
    this.nativeInstance.addStage("variance", params);
    this.stages.push(`variance:${params.mode}`);
    return this;
  }

  /**
   * Add a Z-Score Normalization stage to the pipeline
   * Z-Score Normalization standardizes data to have mean 0 and standard deviation 1
   * @param params - Configuration for the Z-Score Normalization filter
   * @param params.mode - "batch" for stateless normalization, "moving" for windowed normalization
   * @param params.windowSize - Required for "moving" mode when using sample-based processing
   * @param params.windowDuration - Required for "moving" mode when using time-based processing (milliseconds)
   * @return this instance for method chaining
   * @example
   * // Batch Z-Score Normalization (stateless)
   * pipeline.ZScoreNormalize({ mode: "batch" });
   * @example
   * // Moving Z-Score Normalization with sample window (legacy)
   * pipeline.ZScoreNormalize({ mode: "moving", windowSize: 100 });
   * @example
   * // Moving Z-Score Normalization with time window (recommended)
   * pipeline.ZScoreNormalize({ mode: "moving", windowDuration: 30000 }); // 30 seconds
   */
  ZScoreNormalize(params: ZScoreNormalizeParams): this {
    if (params.mode === "moving") {
      if (
        params.windowSize === undefined &&
        params.windowDuration === undefined
      ) {
        throw new TypeError(
          `Z-Score Normalize: either windowSize or windowDuration must be specified for "moving" mode`
        );
      }
      if (
        params.windowSize !== undefined &&
        (params.windowSize <= 0 || !Number.isInteger(params.windowSize))
      ) {
        throw new TypeError(
          `Z-Score Normalize: windowSize must be a positive integer for "moving" mode, got ${params.windowSize}`
        );
      }
      if (params.windowDuration !== undefined && params.windowDuration <= 0) {
        throw new TypeError(
          `Z-Score Normalize: windowDuration must be positive, got ${params.windowDuration}`
        );
      }
    }
    this.nativeInstance.addStage("zScoreNormalize", params);
    this.stages.push(`zScoreNormalize:${params.mode}`);
    return this;
  }

  /**
   * Add a Mean Absolute Value (MAV) stage to the pipeline
   * Mean Absolute Value computes the average of the absolute values of the samples
   * @param params - Configuration for the MAV filter
   * @param params.mode - "batch" for stateless MAV (all samples → single value), "moving" for windowed MAV
   * @param params.windowSize - Required for "moving" mode when using sample-based processing
   * @param params.windowDuration - Required for "moving" mode when using time-based processing (milliseconds)
   * @return this instance for method chaining
   * @example
   * // Batch MAV (stateless)
   * pipeline.MeanAbsoluteValue({ mode: "batch" });
   * @example
   * // Moving MAV with sample window (legacy)
   * pipeline.MeanAbsoluteValue({ mode: "moving", windowSize: 50 });
   * @example
   * // Moving MAV with time window (recommended)
   * pipeline.MeanAbsoluteValue({ mode: "moving", windowDuration: 2000 }); // 2 seconds
   */
  MeanAbsoluteValue(params: MeanAbsoluteValueParams): this {
    if (params.mode === "moving") {
      if (
        params.windowSize === undefined &&
        params.windowDuration === undefined
      ) {
        throw new TypeError(
          `Mean Absolute Value: either windowSize or windowDuration must be specified for "moving" mode`
        );
      }
      if (
        params.windowSize !== undefined &&
        (params.windowSize <= 0 || !Number.isInteger(params.windowSize))
      ) {
        throw new TypeError(
          `Mean Absolute Value: windowSize must be a positive integer for "moving" mode, got ${params.windowSize}`
        );
      }
      if (params.windowDuration !== undefined && params.windowDuration <= 0) {
        throw new TypeError(
          `Mean Absolute Value: windowDuration must be positive, got ${params.windowDuration}`
        );
      }
    }
    this.nativeInstance.addStage("meanAbsoluteValue", params);
    this.stages.push(`meanAbsoluteValue:${params.mode}`);
    return this;
  }

  /**
   * Add a Waveform Length stage to the pipeline
   * Computes the cumulative length of the signal path (sum of absolute differences between consecutive samples)
   * Useful for EMG activity detection and signal complexity analysis
   *
   * @param params - Configuration for the waveform length filter
   * @param params.windowSize - Number of samples in the sliding window
   * @returns this instance for method chaining
   *
   * @example
   * // Basic waveform length calculation
   * pipeline.WaveformLength({ windowSize: 100 });
   *
   * @example
   * // Multi-stage EMG pipeline
   * pipeline
   *   .Rectify({ mode: "full" })
   *   .WaveformLength({ windowSize: 50 })
   *   .tap((samples) => console.log('WL:', samples[0]));
   */
  WaveformLength(params: WaveformLengthParams): this {
    if (params.windowSize <= 0 || !Number.isInteger(params.windowSize)) {
      throw new TypeError(
        `WaveformLength: windowSize must be a positive integer, got ${params.windowSize}`
      );
    }
    this.nativeInstance.addStage("waveformLength", params);
    this.stages.push(`waveformLength:${params.windowSize}`);
    return this;
  }

  /**
   * Add a Slope Sign Change (SSC) stage to the pipeline
   * Counts the number of times the signal slope changes sign within a window
   * Useful for EMG frequency content analysis and muscle fatigue detection
   *
   * @param params - Configuration for the SSC filter
   * @param params.windowSize - Number of samples in the sliding window
   * @param params.threshold - Noise suppression threshold (default: 0.0)
   * @returns this instance for method chaining
   *
   * @example
   * // Basic SSC with no threshold
   * pipeline.SlopeSignChange({ windowSize: 100 });
   *
   * @example
   * // SSC with noise threshold
   * pipeline.SlopeSignChange({ windowSize: 100, threshold: 0.01 });
   *
   * @example
   * // EMG frequency analysis pipeline
   * pipeline
   *   .Rectify({ mode: "full" })
   *   .SlopeSignChange({ windowSize: 50, threshold: 0.005 })
   *   .tap((samples) => console.log('SSC count:', samples[0]));
   */
  SlopeSignChange(params: SlopeSignChangeParams): this {
    if (params.windowSize <= 0 || !Number.isInteger(params.windowSize)) {
      throw new TypeError(
        `SlopeSignChange: windowSize must be a positive integer, got ${params.windowSize}`
      );
    }
    if (params.threshold !== undefined && params.threshold < 0) {
      throw new TypeError(
        `SlopeSignChange: threshold must be non-negative, got ${params.threshold}`
      );
    }
    this.nativeInstance.addStage("slopeSignChange", params);
    this.stages.push(`slopeSignChange:${params.windowSize}`);
    return this;
  }

  /**
   * Add a Willison Amplitude (WAMP) stage to the pipeline
   * Counts the number of times consecutive samples differ by more than a threshold
   * Useful for EMG burst detection and muscle activity classification
   *
   * @param params - Configuration for the WAMP filter
   * @param params.windowSize - Number of samples in the sliding window
   * @param params.threshold - Difference threshold for counting (default: 0.0)
   * @returns this instance for method chaining
   *
   * @example
   * // Basic WAMP with no threshold
   * pipeline.WillisonAmplitude({ windowSize: 100 });
   *
   * @example
   * // WAMP with threshold for burst detection
   * pipeline.WillisonAmplitude({ windowSize: 100, threshold: 0.05 });
   *
   * @example
   * // EMG burst detection pipeline
   * pipeline
   *   .Rectify({ mode: "full" })
   *   .WillisonAmplitude({ windowSize: 50, threshold: 0.02 })
   *   .tap((samples) => console.log('WAMP count:', samples[0]));
   */
  WillisonAmplitude(params: WillisonAmplitudeParams): this {
    if (params.windowSize <= 0 || !Number.isInteger(params.windowSize)) {
      throw new TypeError(
        `WillisonAmplitude: windowSize must be a positive integer, got ${params.windowSize}`
      );
    }
    if (params.threshold !== undefined && params.threshold < 0) {
      throw new TypeError(
        `WillisonAmplitude: threshold must be non-negative, got ${params.threshold}`
      );
    }
    this.nativeInstance.addStage("willisonAmplitude", params);
    this.stages.push(`willisonAmplitude:${params.windowSize}`);
    return this;
  }

  /**
   * Interpolate (upsample) the signal by an integer factor L
   * Increases the sample rate by inserting zeros and applying anti-imaging filter
   * Uses efficient polyphase FIR filtering
   *
   * @param params - Configuration for interpolation
   * @param params.factor - Interpolation factor L (output rate = input rate * L)
   * @param params.sampleRate - Input sample rate in Hz
   * @param params.order - FIR filter order (default: 51, must be odd)
   * @returns this instance for method chaining
   *
   * @example
   * // Upsample from 8 kHz to 16 kHz
   * pipeline.Interpolate({ factor: 2, sampleRate: 8000 });
   *
   * @example
   * // Upsample with custom filter order
   * pipeline.Interpolate({ factor: 4, sampleRate: 1000, order: 101 });
   */
  Interpolate(params: InterpolateParams): this {
    if (params.factor < 2 || !Number.isInteger(params.factor)) {
      throw new TypeError(
        `Interpolate: factor must be an integer >= 2, got ${params.factor}`
      );
    }
    if (params.sampleRate <= 0) {
      throw new TypeError(
        `Interpolate: sampleRate must be positive, got ${params.sampleRate}`
      );
    }
    if (
      params.order !== undefined &&
      (params.order < 3 || params.order % 2 === 0)
    ) {
      throw new TypeError(
        `Interpolate: order must be odd and >= 3, got ${params.order}`
      );
    }
    this.nativeInstance.addStage("interpolate", params);
    this.stages.push(`interpolate:${params.factor}`);
    return this;
  }

  /**
   * Decimate (downsample) the signal by an integer factor M
   * Reduces the sample rate by applying anti-aliasing filter and keeping every M-th sample
   * Uses efficient polyphase FIR filtering
   *
   * @param params - Configuration for decimation
   * @param params.factor - Decimation factor M (output rate = input rate / M)
   * @param params.sampleRate - Input sample rate in Hz
   * @param params.order - FIR filter order (default: 51, must be odd)
   * @returns this instance for method chaining
   *
   * @example
   * // Downsample from 16 kHz to 8 kHz
   * pipeline.Decimate({ factor: 2, sampleRate: 16000 });
   *
   * @example
   * // Downsample with custom filter order
   * pipeline.Decimate({ factor: 4, sampleRate: 8000, order: 101 });
   */
  Decimate(params: DecimateParams): this {
    if (params.factor < 2 || !Number.isInteger(params.factor)) {
      throw new TypeError(
        `Decimate: factor must be an integer >= 2, got ${params.factor}`
      );
    }
    if (params.sampleRate <= 0) {
      throw new TypeError(
        `Decimate: sampleRate must be positive, got ${params.sampleRate}`
      );
    }
    if (
      params.order !== undefined &&
      (params.order < 3 || params.order % 2 === 0)
    ) {
      throw new TypeError(
        `Decimate: order must be odd and >= 3, got ${params.order}`
      );
    }
    this.nativeInstance.addStage("decimate", params);
    this.stages.push(`decimate:${params.factor}`);
    return this;
  }

  /**
   * Resample the signal by a rational factor L/M
   * Changes sample rate by combining interpolation and decimation
   * Uses efficient polyphase FIR filtering with automatic GCD reduction
   *
   * @param params - Configuration for resampling
   * @param params.upFactor - Interpolation factor L
   * @param params.downFactor - Decimation factor M (output rate = input rate * L / M)
   * @param params.sampleRate - Input sample rate in Hz
   * @param params.order - FIR filter order (default: 51, must be odd)
   * @returns this instance for method chaining
   *
   * @example
   * // Resample from 44.1 kHz to 48 kHz
   * pipeline.Resample({ upFactor: 160, downFactor: 147, sampleRate: 44100 });
   *
   * @example
   * // Resample from 8 kHz to 11.025 kHz
   * pipeline.Resample({ upFactor: 441, downFactor: 320, sampleRate: 8000 });
   */
  Resample(params: ResampleParams): this {
    if (params.upFactor < 1 || !Number.isInteger(params.upFactor)) {
      throw new TypeError(
        `Resample: upFactor must be a positive integer, got ${params.upFactor}`
      );
    }
    if (params.downFactor < 1 || !Number.isInteger(params.downFactor)) {
      throw new TypeError(
        `Resample: downFactor must be a positive integer, got ${params.downFactor}`
      );
    }
    if (params.sampleRate <= 0) {
      throw new TypeError(
        `Resample: sampleRate must be positive, got ${params.sampleRate}`
      );
    }
    if (
      params.order !== undefined &&
      (params.order < 3 || params.order % 2 === 0)
    ) {
      throw new TypeError(
        `Resample: order must be odd and >= 3, got ${params.order}`
      );
    }
    this.nativeInstance.addStage("resample", params);
    this.stages.push(`resample:${params.upFactor}/${params.downFactor}`);
    return this;
  }

  /**
   * Add a convolution stage to the pipeline
   * Applies a custom 1D kernel to the signal using either direct or FFT-based convolution
   *
   * @param params - Convolution parameters
   * @returns this instance for method chaining
   *
   * @example
   * // Simple smoothing kernel
   * const smoothKernel = new Float32Array([0.2, 0.6, 0.2]);
   * pipeline.convolution({ kernel: smoothKernel });
   *
   * @example
   * // Gaussian kernel with explicit method
   * const gaussianKernel = new Float32Array([0.06, 0.24, 0.4, 0.24, 0.06]);
   * pipeline.convolution({
   *   kernel: gaussianKernel,
   *   mode: "moving",
   *   method: "direct"
   * });
   *
   * @example
   * // Large kernel with FFT convolution
   * const largeKernel = new Float32Array(128).fill(1/128); // Moving average
   * pipeline.convolution({
   *   kernel: largeKernel,
   *   method: "fft" // Force FFT for large kernel
   * });
   *
   * @example
   * // Multi-channel EMG grid with custom kernel
   * const emgGrid = new Float32Array(80000); // 10x8 grid, 1000 samples
   * pipeline.convolution({ kernel: mySmoothingKernel });
   * const output = await pipeline.process(emgGrid, {
   *   sampleRate: 2000,
   *   channels: 80 // 10 * 8 sensors
   * });\
   */
  convolution(params: ConvolutionParams): this {
    if (!params.kernel || params.kernel.length === 0) {
      throw new TypeError(
        "Convolution: kernel is required and cannot be empty"
      );
    }

    // Convert to Float32Array if needed
    const kernel =
      params.kernel instanceof Float32Array
        ? params.kernel
        : new Float32Array(params.kernel);

    const mode = params.mode || "moving";
    const method = params.method || "auto";

    // Warn about ARM experimental status for moving mode (uses FirFilterNeon)
    if (
      mode === "moving" &&
      (process.arch === "arm64" || process.arch === "arm")
    ) {
      // Use static flag to warn only once
      if (!(globalThis as any).__dspx_arm_convolution_warned) {
        console.warn(
          "\n⚠️  ARM NEON convolution optimization is experimental for moving mode.\n" +
            "   Mobile devices may not show speedup vs. scalar due to thermal/power constraints.\n" +
            "   See: https://github.com/A-KGeorge/dspx#arm-platform-notice\n"
        );
        (globalThis as any).__dspx_arm_convolution_warned = true;
      }
    }

    if (mode !== "moving" && mode !== "batch") {
      throw new TypeError(
        `Convolution: mode must be 'moving' or 'batch', got '${mode}'`
      );
    }

    if (method !== "auto" && method !== "direct" && method !== "fft") {
      throw new TypeError(
        `Convolution: method must be 'auto', 'direct', or 'fft', got '${method}'`
      );
    }

    const stageParams: Record<string, unknown> = {
      kernel,
      mode,
      method,
    };

    // Only include autoThreshold if explicitly set
    if (params.autoThreshold !== undefined) {
      stageParams.autoThreshold = params.autoThreshold;
    }

    this.nativeInstance.addStage("convolution", stageParams);
    this.stages.push(`convolution:${mode}:${method}:${kernel.length}`);
    return this;
  }

  /**
   * Add a Linear Regression stage to the pipeline
   * Performs least squares linear regression over a sliding window to analyze trends
   *
   * @param params - Configuration for the linear regression stage
   * @param params.windowSize - Window size in samples (must be positive integer)
   * @param params.output - Output mode: 'slope', 'intercept', 'residuals', or 'predictions'
   * @returns this instance for method chaining
   *
   * @example
   * // Extract trend (slope) from signal
   * pipeline.LinearRegression({ windowSize: 100, output: 'slope' });
   *
   * @example
   * // Detrend signal (remove linear trend)
   * pipeline.LinearRegression({ windowSize: 50, output: 'residuals' });
   *
   * @example
   * // Get baseline value (intercept)
   * pipeline.LinearRegression({ windowSize: 200, output: 'intercept' });
   *
   * @example
   * // Get fitted values from regression
   * pipeline.LinearRegression({ windowSize: 100, output: 'predictions' });
   */
  LinearRegression(params: LinearRegressionParams): this {
    // Validate window size
    if (
      params.windowSize === undefined ||
      params.windowSize <= 0 ||
      !Number.isInteger(params.windowSize)
    ) {
      throw new TypeError(
        `LinearRegression: windowSize must be a positive integer, got ${params.windowSize}`
      );
    }

    // Validate output mode
    const validOutputs: Array<LinearRegressionParams["output"]> = [
      "slope",
      "intercept",
      "residuals",
      "predictions",
    ];
    if (!validOutputs.includes(params.output)) {
      throw new TypeError(
        `LinearRegression: output must be one of ${validOutputs.join(
          ", "
        )}, got '${params.output}'`
      );
    }

    // Map output mode to C++ stage name
    const stageNameMap = {
      slope: "linearRegressionSlope",
      intercept: "linearRegressionIntercept",
      residuals: "linearRegressionResiduals",
      predictions: "linearRegressionPredictions",
    };

    const stageName = stageNameMap[params.output];
    this.nativeInstance.addStage(stageName, { windowSize: params.windowSize });
    this.stages.push(`linearRegression:${params.output}`);
    return this;
  }

  /**
   * Add an Adaptive LMS (Least Mean Squares) Filter stage for noise cancellation,
   * echo cancellation, or system identification
   *
   * **CRITICAL**: This stage REQUIRES exactly 2 channels in the input buffer:
   * - Channel 0: Primary signal x[n] (the signal to be processed/cleaned)
   * - Channel 1: Desired/reference signal d[n] (noise reference or target signal)
   *
   * The stage outputs the error signal e[n] = d[n] - y[n] where y[n] is the
   * adaptive filter's prediction. For noise cancellation, this error represents
   * the cleaned signal.
   *
   * The filter continuously adapts its weights using the LMS algorithm:
   * ```
   * y[n] = w^T * x[n]              (filter output)
   * e[n] = d[n] - y[n]             (error signal)
   * w[n+1] = w[n] + mu * e[n] * x[n]  (weight update - LMS)
   * w[n+1] = w[n] + (mu / ||x||^2) * e[n] * x[n]  (weight update - NLMS)
   * ```
   *
   * **NLMS (Normalized LMS)**: Set `normalized: true` to enable the Normalized LMS
   * algorithm, which normalizes the update by the input signal power. This provides
   * more stable convergence when the input signal amplitude varies significantly.
   *
   * @param params - LMS filter configuration
   * @param params.numTaps - Number of filter taps (8-128 typical)
   * @param params.learningRate - Learning rate mu (0.001-0.1), or use params.mu
   * @param params.normalized - Use NLMS (Normalized LMS) for better stability (default: false)
   * @param params.lambda - Leaky LMS regularization (default: 0.0)
   *
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If numTaps is invalid
   * @throws {RangeError} If learningRate or lambda is out of range
   *
   * @example
   * ```typescript
   * // Standard LMS for noise cancellation
   * const pipeline = createDspPipeline();
   * pipeline.LmsFilter({ numTaps: 32, learningRate: 0.01 });
   *
   * // Create interleaved 2-channel buffer
   * const samples = 1000;
   * const interleaved = new Float32Array(samples * 2);
   * for (let i = 0; i < samples; i++) {
   *   interleaved[i * 2 + 0] = noisyAudio[i];     // Channel 0: noisy signal
   *   interleaved[i * 2 + 1] = noiseReference[i]; // Channel 1: noise reference
   * }
   *
   * const cleaned = await pipeline.process(interleaved, {
   *   channels: 2,  // MUST be 2!
   *   sampleRate: 8000
   * });
   * ```
   *
   * @example
   * ```typescript
   * // Echo cancellation: Remove far-end echo from microphone
   * pipeline.LmsFilter({ numTaps: 128, mu: 0.005, normalized: true });
   *
   * const interleaved = new Float32Array(samples * 2);
   * for (let i = 0; i < samples; i++) {
   *   interleaved[i * 2 + 0] = farEndSignal[i];  // Channel 0: speaker output
   *   interleaved[i * 2 + 1] = microphoneIn[i];  // Channel 1: microphone input
   * }
   *
   * const echoFree = await pipeline.process(interleaved, { channels: 2 });
   * ```
   *
   * @example
   * ```typescript
   * // NLMS for better stability with varying signal amplitudes
   * pipeline.LmsFilter({ numTaps: 64, mu: 0.1, normalized: true });
   *
   * // NLMS normalizes by input power, making it more robust to:
   * // - Varying signal amplitudes
   * // - Different channel power levels
   * // - Non-stationary signals
   * ```
   */
  LmsFilter(params: LmsFilterParams): this {
    // Validate numTaps
    if (
      params.numTaps === undefined ||
      params.numTaps <= 0 ||
      !Number.isInteger(params.numTaps)
    ) {
      throw new TypeError(
        `LmsFilter: numTaps must be a positive integer, got ${params.numTaps}`
      );
    }

    // Learning rate can be specified as either 'learningRate' or 'mu' (alias)
    const learningRate = params.learningRate ?? params.mu ?? 0.01;
    if (learningRate <= 0 || learningRate > 1.0) {
      throw new RangeError(
        `LmsFilter: learningRate (mu) must be in (0, 1], got ${learningRate}`
      );
    }

    // Optional parameters with validation
    const normalized = params.normalized ?? false;
    const lambda = params.lambda ?? 0.0;

    if (lambda < 0 || lambda >= 1.0) {
      throw new RangeError(
        `LmsFilter: lambda must be in [0, 1), got ${lambda}`
      );
    }

    // Add stage to native pipeline
    this.nativeInstance.addStage("lmsFilter", {
      numTaps: params.numTaps,
      learningRate: learningRate,
      normalized: normalized,
      lambda: lambda,
    });

    this.stages.push(`lmsFilter:${params.numTaps}taps`);
    return this;
  }

  /**
   * Add an RLS (Recursive Least Squares) adaptive filter stage to the pipeline
   * Provides faster convergence than LMS/NLMS at the cost of O(N^2) complexity
   *
   * **Requires exactly 2 channels:**
   * - Channel 0: Primary signal x[n] (the signal to be filtered)
   * - Channel 1: Desired signal d[n] (the reference signal)
   *
   * **Output:** Error signal e[n] = d[n] - y[n] on both channels
   *
   * @param params - RLS filter configuration
   * @param params.numTaps - Filter length (number of coefficients), typically 8-64
   * @param params.lambda - Forgetting factor (0 < λ ≤ 1), typically 0.98-0.9999
   * @param params.delta - Regularization parameter (optional, default: 0.01)
   * @returns this instance for method chaining
   *
   * @example
   * // Basic system identification with RLS
   * pipeline.RlsFilter({ numTaps: 32, lambda: 0.99 });
   *
   * @example
   * // Faster tracking with lower lambda
   * pipeline.RlsFilter({ numTaps: 16, lambda: 0.95, delta: 0.1 });
   *
   * @example
   * // Acoustic echo cancellation
   * pipeline.RlsFilter({ numTaps: 128, lambda: 0.9999, delta: 0.01 });
   */
  RlsFilter(params: RlsFilterParams): this {
    // Validate numTaps
    if (
      params.numTaps === undefined ||
      params.numTaps <= 0 ||
      !Number.isInteger(params.numTaps)
    ) {
      throw new TypeError(
        `RlsFilter: numTaps must be a positive integer, got ${params.numTaps}`
      );
    }

    // Lambda (forgetting factor) is required
    if (params.lambda === undefined) {
      throw new TypeError("RlsFilter: lambda (forgetting factor) is required");
    }

    if (params.lambda <= 0 || params.lambda > 1.0) {
      throw new RangeError(
        `RlsFilter: lambda must be in (0, 1], got ${params.lambda}`
      );
    }

    // Optional delta parameter with validation
    const delta = params.delta ?? 0.01;
    if (delta <= 0) {
      throw new RangeError(`RlsFilter: delta must be > 0, got ${delta}`);
    }

    // Add stage to native pipeline
    this.nativeInstance.addStage("rlsFilter", {
      numTaps: params.numTaps,
      lambda: params.lambda,
      delta: delta,
    });

    this.stages.push(`rlsFilter:${params.numTaps}taps:λ=${params.lambda}`);
    return this;
  }

  /**
   * Apply pre-trained PCA transformation to pipeline in real-time.
   *
   * **Prerequisites**: Must first train PCA using `calculatePca()` on representative data.
   *
   * Applies transformation: `y = W^T * (x - mean)` where W is the PCA matrix.
   *
   * @param params - PCA transformation parameters from calculatePca()
   * @param params.pcaMatrix - Pre-trained PCA matrix (from calculatePca)
   * @param params.mean - Mean vector (from calculatePca)
   * @param params.numChannels - Number of input channels
   * @param params.numComponents - Number of output components (≤ numChannels for reduction)
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If required parameters are missing or invalid
   * @throws {Error} If matrix dimensions don't match
   *
   * @example
   * // Train PCA offline
   * const trainingData = new Float32Array(3000); // 3 channels, 1000 samples
   * const pca = calculatePca(trainingData, 3);
   *
   * // Apply in real-time pipeline
   * const pipeline = createDspPipeline();
   * pipeline.PcaTransform({
   *   pcaMatrix: pca.pcaMatrix,
   *   mean: pca.mean,
   *   numChannels: 3,
   *   numComponents: 3  // Keep all components
   * });
   *
   * @example
   * // Dimensionality reduction: 8 → 3 channels
   * const pca8 = calculatePca(eightChannelData, 8);
   * pipeline.PcaTransform({
   *   pcaMatrix: pca8.pcaMatrix,
   *   mean: pca8.mean,
   *   numChannels: 8,
   *   numComponents: 3  // Reduce to 3 channels
   * });
   */
  PcaTransform(params: PcaTransformParams): this {
    if (!(params.pcaMatrix instanceof Float32Array)) {
      throw new TypeError("PcaTransform: pcaMatrix must be a Float32Array");
    }
    if (!(params.mean instanceof Float32Array)) {
      throw new TypeError("PcaTransform: mean must be a Float32Array");
    }
    if (!Number.isInteger(params.numChannels) || params.numChannels <= 0) {
      throw new TypeError(
        "PcaTransform: numChannels must be a positive integer"
      );
    }
    if (!Number.isInteger(params.numComponents) || params.numComponents <= 0) {
      throw new TypeError(
        "PcaTransform: numComponents must be a positive integer"
      );
    }
    if (params.numComponents > params.numChannels) {
      throw new RangeError(
        "PcaTransform: numComponents cannot exceed numChannels"
      );
    }

    this.nativeInstance.addStage("pcaTransform", params);
    this.stages.push(
      `pcaTransform:${params.numChannels}→${params.numComponents}`
    );
    return this;
  }

  /**
   * Apply pre-trained ICA unmixing to pipeline in real-time.
   *
   * **Prerequisites**: Must first train ICA using `calculateIca()` on representative mixed data.
   *
   * Separates mixed signals into independent components.
   *
   * @param params - ICA transformation parameters from calculateIca()
   * @param params.icaMatrix - Pre-trained ICA unmixing matrix (from calculateIca)
   * @param params.mean - Mean vector (from calculateIca)
   * @param params.numChannels - Number of input channels (mixed signals)
   * @param params.numComponents - Number of output components (separated sources)
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If required parameters are missing or invalid
   * @throws {Error} If matrix dimensions don't match
   *
   * @example
   * // Train ICA offline on mixed EEG data
   * const mixedEeg = new Float32Array(8000); // 8 channels
   * const ica = calculateIca(mixedEeg, 8);
   *
   * // Apply in real-time pipeline
   * const pipeline = createDspPipeline();
   * pipeline.IcaTransform({
   *   icaMatrix: ica.icaMatrix,
   *   mean: ica.mean,
   *   numChannels: 8,
   *   numComponents: 8  // Separate into 8 independent components
   * });
   *
   * @example
   * // Audio source separation
   * const mixedAudio = new Float32Array(9000); // 3 mics
   * const ica = calculateIca(mixedAudio, 3);
   *
   * if (ica.converged) {
   *   pipeline.IcaTransform({
   *     icaMatrix: ica.icaMatrix,
   *     mean: ica.mean,
   *     numChannels: 3,
   *     numComponents: 3
   *   });
   * }
   */
  IcaTransform(params: IcaTransformParams): this {
    if (!(params.icaMatrix instanceof Float32Array)) {
      throw new TypeError("IcaTransform: icaMatrix must be a Float32Array");
    }
    if (!(params.mean instanceof Float32Array)) {
      throw new TypeError("IcaTransform: mean must be a Float32Array");
    }
    if (!Number.isInteger(params.numChannels) || params.numChannels <= 0) {
      throw new TypeError(
        "IcaTransform: numChannels must be a positive integer"
      );
    }
    if (!Number.isInteger(params.numComponents) || params.numComponents <= 0) {
      throw new TypeError(
        "IcaTransform: numComponents must be a positive integer"
      );
    }

    this.nativeInstance.addStage("icaTransform", params);
    this.stages.push(`icaTransform:${params.numChannels}ch`);
    return this;
  }

  /**
   * Apply pre-trained whitening transformation to pipeline in real-time.
   *
   * **Prerequisites**: Must first train whitening using `calculateWhitening()` on representative data.
   *
   * Transforms data to have identity covariance (decorrelates and normalizes).
   * Often used as preprocessing before ICA or for ML feature normalization.
   *
   * @param params - Whitening transformation parameters from calculateWhitening()
   * @param params.whiteningMatrix - Pre-trained whitening matrix (from calculateWhitening)
   * @param params.mean - Mean vector (from calculateWhitening)
   * @param params.numChannels - Number of input channels
   * @param params.numComponents - Number of output components (typically same as numChannels)
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If required parameters are missing or invalid
   * @throws {Error} If matrix dimensions don't match
   *
   * @example
   * // Train whitening offline
   * const sensorData = new Float32Array(4000); // 4 channels
   * const whitening = calculateWhitening(sensorData, 4);
   *
   * // Apply in real-time pipeline
   * const pipeline = createDspPipeline();
   * pipeline.WhiteningTransform({
   *   whiteningMatrix: whitening.whiteningMatrix,
   *   mean: whitening.mean,
   *   numChannels: 4,
   *   numComponents: 4
   * });
   *
   * @example
   * // Preprocessing chain: Whitening → ICA
   * const data = new Float32Array(5000);
   * const whitening = calculateWhitening(data, 5);
   * const ica = calculateIca(data, 5);
   *
   * pipeline
   *   .WhiteningTransform({
   *     whiteningMatrix: whitening.whiteningMatrix,
   *     mean: whitening.mean,
   *     numChannels: 5,
   *     numComponents: 5
   *   })
   *   .IcaTransform({
   *     icaMatrix: ica.icaMatrix,
   *     mean: ica.mean,
   *     numChannels: 5,
   *     numComponents: 5
   *   });
   */
  WhiteningTransform(params: WhiteningTransformParams): this {
    if (!(params.whiteningMatrix instanceof Float32Array)) {
      throw new TypeError(
        "WhiteningTransform: whiteningMatrix must be a Float32Array"
      );
    }
    if (!(params.mean instanceof Float32Array)) {
      throw new TypeError("WhiteningTransform: mean must be a Float32Array");
    }
    if (!Number.isInteger(params.numChannels) || params.numChannels <= 0) {
      throw new TypeError(
        "WhiteningTransform: numChannels must be a positive integer"
      );
    }
    if (!Number.isInteger(params.numComponents) || params.numComponents <= 0) {
      throw new TypeError(
        "WhiteningTransform: numComponents must be a positive integer"
      );
    }

    this.nativeInstance.addStage("whiteningTransform", params);
    this.stages.push(`whiteningTransform:${params.numChannels}ch`);
    return this;
  }

  /**
   * Add GSC (Generalized Sidelobe Canceler) preprocessor for adaptive beamforming.
   *
   * Converts N-channel microphone array into 2 channels for LMS/RLS adaptive filtering:
   * - Channel 0: Noise reference (blocking matrix output)
   * - Channel 1: Desired signal (steering weights output)
   *
   * **Must be followed by LmsFilter or RlsFilter** to complete adaptive beamforming.
   *
   * **Prerequisites**: Use `calculateBeamformerWeights()` to compute weights first.
   *
   * @param params - GSC configuration from calculateBeamformerWeights()
   * @param params.numChannels - Number of input microphone channels
   * @param params.steeringWeights - Steering weights (from calculateBeamformerWeights)
   * @param params.blockingMatrix - Blocking matrix (from calculateBeamformerWeights)
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If required parameters are missing or invalid
   * @throws {Error} If matrix dimensions don't match numChannels
   *
   * @example
   * // Complete adaptive beamforming pipeline for conference call
   * const bf = calculateBeamformerWeights(8, "linear", 0.0);
   *
   * const pipeline = createDspPipeline();
   * pipeline
   *   .GscPreprocessor({
   *     numChannels: 8,
   *     steeringWeights: bf.steeringWeights,
   *     blockingMatrix: bf.blockingMatrix
   *   })
   *   .LmsFilter({
   *     numTaps: 64,
   *     learningRate: 0.005,
   *     normalized: true
   *   });
   *
   * // Process 8-channel microphone data
   * const micData = new Float32Array(800); // 100 samples × 8 channels
   * const result = await pipeline.process(micData, 100, 8);
   * // result.output: 1-channel cleaned audio
   *
   * @example
   * // Acoustic monitoring with RLS (faster convergence)
   * const bf = calculateBeamformerWeights(4, "circular", 45.0);
   *
   * pipeline
   *   .HighpassFilter({ cutoff: 100 })  // Remove DC offset
   *   .GscPreprocessor({
   *     numChannels: 4,
   *     steeringWeights: bf.steeringWeights,
   *     blockingMatrix: bf.blockingMatrix
   *   })
   *   .RlsFilter({
   *     numTaps: 32,
   *     lambda: 0.995,
   *     delta: 1.0
   *   });
   *
   * @see calculateBeamformerWeights
   * @see LmsFilter
   * @see RlsFilter
   */
  GscPreprocessor(params: GscPreprocessorParams): this {
    if (!Number.isInteger(params.numChannels) || params.numChannels < 2) {
      throw new RangeError("GscPreprocessor: numChannels must be >= 2");
    }
    if (!(params.steeringWeights instanceof Float32Array)) {
      throw new TypeError(
        "GscPreprocessor: steeringWeights must be a Float32Array"
      );
    }
    if (!(params.blockingMatrix instanceof Float32Array)) {
      throw new TypeError(
        "GscPreprocessor: blockingMatrix must be a Float32Array"
      );
    }
    if (params.steeringWeights.length !== params.numChannels) {
      throw new Error(
        `GscPreprocessor: steeringWeights length (${params.steeringWeights.length}) ` +
          `must equal numChannels (${params.numChannels})`
      );
    }
    const expectedBlockingSize = params.numChannels * (params.numChannels - 1);
    if (params.blockingMatrix.length !== expectedBlockingSize) {
      throw new Error(
        `GscPreprocessor: blockingMatrix length (${params.blockingMatrix.length}) ` +
          `must equal numChannels × (numChannels-1) (${expectedBlockingSize})`
      );
    }

    this.nativeInstance.addStage("gscPreprocessor", params);
    this.stages.push(`gscPreprocessor:${params.numChannels}ch→2ch`);
    return this;
  }

  /**
   * Select a subset of channels from multi-channel input.
   *
   * Extracts the first `numOutputChannels` from the input, discarding the rest.
   * This is a **resizing stage** that reduces the effective channel count.
   *
   * **Use case**: After GSC Preprocessor, extract only the 2 active channels
   * before feeding to LMS/RLS (which requires exactly 2 channels).
   *
   * @param params - Channel selection parameters
   * @param params.numInputChannels - Number of input channels
   * @param params.numOutputChannels - Number of channels to keep (1 to numInputChannels)
   * @returns this instance for method chaining
   *
   * @example
   * // After GSC (8 channels → 2 active channels in buffer)
   * pipeline
   *   .GscPreprocessor({ numChannels: 8, ... })
   *   .ChannelSelector({ numInputChannels: 8, numOutputChannels: 2 })
   *   .LmsFilter({ numTaps: 32, learningRate: 0.01 });
   */
  ChannelSelector(params: ChannelSelectorParams): this {
    if (
      !Number.isInteger(params.numInputChannels) ||
      params.numInputChannels <= 0
    ) {
      throw new RangeError(
        "ChannelSelector: numInputChannels must be a positive integer"
      );
    }
    if (
      !Number.isInteger(params.numOutputChannels) ||
      params.numOutputChannels <= 0 ||
      params.numOutputChannels > params.numInputChannels
    ) {
      throw new RangeError(
        `ChannelSelector: numOutputChannels must be in range [1, ${params.numInputChannels}], got ${params.numOutputChannels}`
      );
    }

    this.nativeInstance.addStage("channelSelector", params);
    this.stages.push(
      `channelSelector:${params.numInputChannels}ch→${params.numOutputChannels}ch`
    );
    return this;
  }

  /**
   * Add Channel Select stage (select channels by indices).
   *
   * Selects specific channels by index, allowing reordering and duplication.
   * This is a resizing stage that can increase, decrease, or maintain channel count.
   *
   * @param params Configuration with channels array and numInputChannels
   * @returns this pipeline for chaining
   * @example
   * // Select channels 0, 3, 7 from 8-channel input
   * pipeline.ChannelSelect({ channels: [0, 3, 7], numInputChannels: 8 });
   * @example
   * // Swap stereo channels
   * pipeline.ChannelSelect({ channels: [1, 0], numInputChannels: 2 });
   * @example
   * // Duplicate channel 0 (mono to stereo)
   * pipeline.ChannelSelect({ channels: [0, 0], numInputChannels: 1 });
   */
  ChannelSelect(params: ChannelSelectParams): this {
    if (!Array.isArray(params.channels) || params.channels.length === 0) {
      throw new Error("ChannelSelect: channels must be a non-empty array");
    }
    if (
      !Number.isInteger(params.numInputChannels) ||
      params.numInputChannels <= 0
    ) {
      throw new RangeError(
        "ChannelSelect: numInputChannels must be a positive integer"
      );
    }

    // Validate channel indices
    for (let i = 0; i < params.channels.length; i++) {
      const ch = params.channels[i];
      if (
        typeof ch !== "number" ||
        !Number.isInteger(ch) ||
        ch < 0 ||
        ch >= params.numInputChannels
      ) {
        throw new RangeError(
          `ChannelSelect: channel index ${ch} out of range [0, ${
            params.numInputChannels - 1
          }]`
        );
      }
    }

    this.nativeInstance.addStage("channelSelect", params);
    this.stages.push(`channelSelect:[${params.channels.join(",")}]`);
    return this;
  }

  /**
   * Add Channel Merge stage (merge/duplicate channels).
   *
   * Maps input channels to output channels according to mapping array.
   * Each element in mapping specifies which input channel feeds that output position.
   * This is a resizing stage that can increase, decrease, or maintain channel count.
   *
   * @param params Configuration with mapping array and numInputChannels
   * @returns this pipeline for chaining
   * @example
   * // Mono to stereo (duplicate channel 0)
   * pipeline.ChannelMerge({ mapping: [0, 0], numInputChannels: 1 });
   * @example
   * // 3-channel to 6-channel by duplicating each
   * pipeline.ChannelMerge({ mapping: [0, 0, 1, 1, 2, 2], numInputChannels: 3 });
   * @example
   * // Custom routing: [A, B, C] -> [A, C, B, A]
   * pipeline.ChannelMerge({ mapping: [0, 2, 1, 0], numInputChannels: 3 });
   */
  ChannelMerge(params: ChannelMergeParams): this {
    if (!Array.isArray(params.mapping) || params.mapping.length === 0) {
      throw new Error("ChannelMerge: mapping must be a non-empty array");
    }
    if (
      !Number.isInteger(params.numInputChannels) ||
      params.numInputChannels <= 0
    ) {
      throw new RangeError(
        "ChannelMerge: numInputChannels must be a positive integer"
      );
    }

    // Validate mapping indices
    for (let i = 0; i < params.mapping.length; i++) {
      const ch = params.mapping[i];
      if (
        typeof ch !== "number" ||
        !Number.isInteger(ch) ||
        ch < 0 ||
        ch >= params.numInputChannels
      ) {
        throw new RangeError(
          `ChannelMerge: mapping index ${ch} out of range [0, ${
            params.numInputChannels - 1
          }]`
        );
      }
    }

    this.nativeInstance.addStage("channelMerge", params);
    this.stages.push(
      `channelMerge:${params.numInputChannels}ch→${params.mapping.length}ch`
    );
    return this;
  }

  /**
   * Detect clipping (signal saturation) in the input stream.
   *
   * Outputs a binary indicator (1.0 or 0.0) showing where samples exceed
   * the specified threshold. Useful for:
   * - Audio clipping detection (overload prevention)
   * - ADC saturation detection
   * - Signal quality monitoring
   * - Data acquisition QC
   *
   * @param params - Clip detection parameters
   * @param params.threshold - Absolute amplitude threshold (must be > 0)
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If threshold is missing or invalid
   * @throws {RangeError} If threshold <= 0
   *
   * @example
   * // Detect audio clipping at 95% full scale
   * pipeline.ClipDetection({ threshold: 0.95 });
   *
   * @example
   * // Monitor ADC saturation for 16-bit signal (-32768 to +32767)
   * pipeline.ClipDetection({ threshold: 32000 });
   */
  ClipDetection(params: ClipDetectionParams): this {
    if (typeof params.threshold !== "number" || params.threshold <= 0) {
      throw new RangeError("ClipDetection: threshold must be > 0");
    }

    this.nativeInstance.addStage("clipDetection", params);
    this.stages.push(`clipDetection:${params.threshold}`);
    return this;
  }

  /**
   * Detect peaks (local maxima) in the input stream.
   *
   * Identifies peaks using three-point comparison: a peak is detected when
   * the previous sample is greater than both its neighbors AND exceeds
   * the threshold. Outputs 1.0 at peak locations, 0.0 elsewhere.
   *
   * **Use Cases:**
   * - Heart rate detection (R-peaks in ECG)
   * - Event detection in sensor data
   * - Tempo/beat detection in audio
   * - Spike detection in neural recordings
   *
   * @param params - Peak detection parameters
   * @param params.threshold - Minimum peak amplitude (must be >= 0)
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If threshold is missing or invalid
   * @throws {RangeError} If threshold < 0
   *
   * @example
   * // Detect R-peaks in ECG above 0.5mV
   * pipeline.PeakDetection({ threshold: 0.5 });
   *
   * @example
   * // Find all local maxima (no amplitude threshold)
   * pipeline.PeakDetection({ threshold: 0.0 });
   */
  PeakDetection(params: PeakDetectionParams): this {
    if (typeof params.threshold !== "number" || params.threshold < 0) {
      throw new RangeError("PeakDetection: threshold must be >= 0");
    }

    this.nativeInstance.addStage("peakDetection", params);
    this.stages.push(`peakDetection:${params.threshold}`);
    return this;
  }

  /**
   * Compute the discrete derivative of the signal.
   *
   * Implements finite difference: y[n] = x[n] - x[n-1]
   * Equivalent to an FIR filter with coefficients [1, -1].
   *
   * **Use Cases:**
   * - Edge detection in signals
   * - Velocity calculation from position data
   * - Change detection in sensor readings
   * - High-pass filtering (DC removal)
   * - Rate of change analysis
   *
   * **Note:** Amplifies high-frequency noise. Consider applying a low-pass
   * filter before differentiation for noisy signals.
   *
   * @returns this instance for method chaining
   *
   * @example
   * // Compute velocity from position
   * pipeline.Differentiator();
   *
   * @example
   * // Edge detection with noise reduction
   * pipeline
   *   .ButterworthLowpass({ cutoff: 50, order: 4, sampleRate: 1000 })
   *   .Differentiator();
   */
  Differentiator(): this {
    this.nativeInstance.addStage("differentiator", {});
    this.stages.push("differentiator");
    return this;
  }

  /**
   * Apply leaky integration (accumulation) using IIR filter.
   *
   * Implements: y[n] = x[n] + α * y[n-1]
   * Transfer function: H(z) = 1 / (1 - α*z^-1)
   *
   * The integrator accumulates signal values over time with controllable leakage.
   * α (alpha) controls the leakage rate:
   * - α = 1.0: Perfect integration (no leakage, infinite DC gain)
   * - α = 0.99: Slight leakage (DC gain ≈ 100)
   * - α = 0.9: More leakage (DC gain = 10)
   *
   * @param params - Integrator parameters (optional, defaults to α = 0.99)
   * @param params.alpha - Leakage coefficient in range (0, 1]. Default: 0.99
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If alpha is outside valid range (0, 1]
   *
   * @example
   * // Accelerometer → velocity integration
   * const pipeline = createDspPipeline();
   * pipeline
   *   .Integrator({ alpha: 0.99 })  // Slight leakage prevents drift
   *   .process(accelerometerData, { channels: 3, sampleRate: 100 });
   *
   * @example
   * // Envelope detection after rectification
   * pipeline
   *   .Rectify({ mode: "full" })
   *   .Integrator({ alpha: 0.95 })  // Smooth envelope
   *   .process(audioSignal, { channels: 1, sampleRate: 44100 });
   *
   * @example
   * // Low-pass smoothing with adjustable time constant
   * pipeline
   *   .Integrator({ alpha: 0.9 })  // τ = 1/(1-α) = 10 samples
   *   .process(noisySignal, { channels: 1, sampleRate: 1000 });
   */
  Integrator(params: { alpha?: number } = {}): this {
    const alpha = params.alpha ?? 0.99;

    if (alpha <= 0.0 || alpha > 1.0) {
      throw new TypeError(
        `Integrator alpha must be in range (0, 1], got: ${alpha}`
      );
    }

    this.nativeInstance.addStage("integrator", { alpha });
    this.stages.push(`integrator:α=${alpha.toFixed(3)}`);
    return this;
  }

  /**
   * Compute Signal-to-Noise Ratio (SNR) in dB from 2-channel input.
   *
   * **Requirements**: Exactly 2 input channels
   * - Channel 0: Signal (clean signal or signal+noise)
   * - Channel 1: Noise reference
   *
   * **Output**: Single channel containing SNR in dB
   * Formula: SNR_dB = 10 * log10(signal_power / noise_power)
   *
   * Uses dual RMS filters to compute running power estimates.
   * Output is clamped to [-100, 100] dB to avoid infinities.
   *
   * @param params - SNR parameters
   * @param params.windowSize - Window size in samples for RMS computation
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If windowSize is missing or <= 0
   * @throws {Error} If input doesn't have exactly 2 channels during process()
   *
   * @example
   * // Audio quality monitoring (100ms window at 16kHz)
   * const pipeline = createDspPipeline();
   * const twoChannelAudio = new Float32Array([
   *   1.0, 0.1,  // Sample 0: signal=1.0, noise=0.1
   *   0.8, 0.15, // Sample 1: signal=0.8, noise=0.15
   *   0.9, 0.12  // Sample 2: signal=0.9, noise=0.12
   * ]);
   * pipeline
   *   .Snr({ windowSize: 1600 }) // 100ms at 16kHz
   *   .process(twoChannelAudio, { channels: 2, sampleRate: 16000 });
   * // Output: Single channel with SNR values in dB
   *
   * @example
   * // Speech enhancement validation
   * const cleanSpeech = recordCleanSpeech();
   * const noisyChannel = addNoise(cleanSpeech);
   * const noiseReference = recordNoise();
   * const dualChannel = interleave(noisyChannel, noiseReference);
   * pipeline
   *   .Snr({ windowSize: 400 }) // 50ms at 8kHz
   *   .process(dualChannel, { channels: 2, sampleRate: 8000 });
   *
   * @example
   * // Adaptive filter performance monitoring
   * pipeline
   *   .Snr({ windowSize: 1024 })
   *   .Tap((result) => {
   *     const avgSnr = result.reduce((a, b) => a + b, 0) / result.length;
   *     console.log(`Average SNR: ${avgSnr.toFixed(2)} dB`);
   *   })
   *   .process(adaptiveFilterOutput, { channels: 2, sampleRate: 48000 });
   */
  Snr(params: { windowSize: number }): this {
    if (!params.windowSize || params.windowSize <= 0) {
      throw new TypeError(
        `SNR windowSize must be greater than 0, got: ${params.windowSize}`
      );
    }

    this.nativeInstance.addStage("snr", { windowSize: params.windowSize });
    this.stages.push(`snr:win=${params.windowSize}`);
    return this;
  }

  /**
   * Apply pre-trained CSP (Common Spatial Patterns) filters for BCI/EEG classification.
   *
   * **Prerequisites**: Must first train CSP using `calculateCommonSpatialPatterns()`
   * on labeled class data (e.g., left hand vs right hand motor imagery).
   *
   * CSP transforms multi-channel EEG into spatially filtered components that
   * maximize class separability. Essential for motor imagery BCI, P300, SSVEP.
   *
   * @param params - CSP transformation parameters from calculateCommonSpatialPatterns()
   * @param params.cspMatrix - Pre-trained CSP filters (from calculateCommonSpatialPatterns)
   * @param params.mean - Mean vector (from calculateCommonSpatialPatterns)
   * @param params.numChannels - Number of input EEG channels
   * @param params.numFilters - Number of output CSP components
   * @returns this instance for method chaining
   *
   * @throws {TypeError} If required parameters are missing or invalid
   * @throws {Error} If matrix dimensions don't match
   *
   * @example
   * // Motor imagery BCI: Left hand vs right hand classification
   *
   * // 1. Train CSP offline on labeled trials
   * const leftHandTrials = new Float32Array(400 * 8); // 50 trials × 8 channels
   * const rightHandTrials = new Float32Array(400 * 8);
   * // ... collect training data ...
   *
   * const csp = calculateCommonSpatialPatterns(
   *   leftHandTrials,
   *   rightHandTrials,
   *   8,  // 8 EEG channels
   *   4   // Top 4 most discriminative filters
   * );
   *
   * // 2. Apply in real-time classification pipeline
   * const pipeline = createDspPipeline();
   * pipeline
   *   .BandpassFilter({ lowCutoff: 8, highCutoff: 30 })  // Motor imagery band
   *   .CspTransform({
   *     cspMatrix: csp.cspMatrix,
   *     mean: csp.mean,
   *     numChannels: 8,
   *     numFilters: 4
   *   })
   *   .MovingVariance({ mode: "moving", windowSize: 100 })
   *   .MovingAverage({ mode: "moving", windowSize: 50 });
   *
   * // 3. Process live EEG stream
   * const liveEeg = new Float32Array(80); // 10 samples × 8 channels
   * const result = await pipeline.process(liveEeg, 10, 8);
   * // result.output: 4-channel CSP features with variance
   * // → Feed to classifier (SVM, LDA, etc.)
   *
   * @example
   * // P300 speller: Target vs non-target ERP classification
   * const targetErps = new Float32Array(50 * 16 * 200);
   * const nonTargetErps = new Float32Array(200 * 16 * 200);
   *
   * const csp = calculateCommonSpatialPatterns(
   *   targetErps,
   *   nonTargetErps,
   *   16,
   *   6
   * );
   *
   * pipeline
   *   .BandpassFilter({ lowCutoff: 0.5, highCutoff: 10 })  // P300 band
   *   .CspTransform({
   *     cspMatrix: csp.cspMatrix,
   *     mean: csp.mean,
   *     numChannels: 16,
   *     numFilters: 6
   *   })
   *   .Downsample({ factor: 4 });  // Reduce data rate
   *
   * @see calculateCommonSpatialPatterns
   * @see MatrixTransformStage
   */
  CspTransform(params: CspTransformParams): this {
    if (!(params.cspMatrix instanceof Float32Array)) {
      throw new TypeError("CspTransform: cspMatrix must be a Float32Array");
    }
    if (!(params.mean instanceof Float32Array)) {
      throw new TypeError("CspTransform: mean must be a Float32Array");
    }
    if (!Number.isInteger(params.numChannels) || params.numChannels <= 0) {
      throw new TypeError(
        "CspTransform: numChannels must be a positive integer"
      );
    }
    if (!Number.isInteger(params.numFilters) || params.numFilters <= 0) {
      throw new TypeError(
        "CspTransform: numFilters must be a positive integer"
      );
    }
    if (params.numFilters > params.numChannels) {
      throw new RangeError(
        "CspTransform: numFilters cannot exceed numChannels"
      );
    }
    if (params.mean.length !== params.numChannels) {
      throw new Error(
        `CspTransform: mean length (${params.mean.length}) must equal numChannels (${params.numChannels})`
      );
    }
    const expectedMatrixSize = params.numChannels * params.numFilters;
    if (params.cspMatrix.length !== expectedMatrixSize) {
      throw new Error(
        `CspTransform: cspMatrix length (${params.cspMatrix.length}) ` +
          `must equal numChannels × numFilters (${expectedMatrixSize})`
      );
    }

    // CSP uses MatrixTransformStage internally with "csp" type
    this.nativeInstance.addStage("pcaTransform", {
      pcaMatrix: params.cspMatrix, // Reuse PCA stage with CSP matrix
      mean: params.mean,
      numChannels: params.numChannels,
      numComponents: params.numFilters,
    });
    this.stages.push(`cspTransform:${params.numChannels}→${params.numFilters}`);
    return this;
  }

  /**
   * Add a Discrete Wavelet Transform (DWT) stage to the pipeline
   * Decomposes signal into approximation and detail coefficients
   * Output format: [approximation_coeffs, detail_coefficients]
   *
   * @param params - Wavelet configuration
   * @param params.wavelet - Wavelet type (haar/db1-db10)
   * @returns this instance for method chaining
   *
   * @example
   * // Haar wavelet (simplest)
   * pipeline.WaveletTransform({ wavelet: "haar" });
   *
   * @example
   * // Daubechies-4 wavelet (common choice)
   * pipeline.WaveletTransform({ wavelet: "db4" });
   *
   * @example
   * // Pipeline chaining
   * pipeline
   *   .WaveletTransform({ wavelet: "db2" })
   *   .HilbertEnvelope({ windowSize: 128 });
   */
  WaveletTransform(params: WaveletTransformParams): this {
    if (!params.wavelet) {
      throw new TypeError("WaveletTransform: wavelet parameter is required");
    }

    const validWavelets = [
      "haar",
      "db1",
      "db2",
      "db3",
      "db4",
      "db5",
      "db6",
      "db7",
      "db8",
      "db9",
      "db10",
    ];
    if (!validWavelets.includes(params.wavelet)) {
      throw new TypeError(
        `WaveletTransform: Unknown wavelet type '${
          params.wavelet
        }'. Valid types: ${validWavelets.join(", ")}`
      );
    }

    this.nativeInstance.addStage("waveletTransform", params);
    this.stages.push(`waveletTransform:${params.wavelet}`);
    return this;
  }

  /**
   * Add a Hilbert Envelope extraction stage to the pipeline
   * Computes amplitude envelope using FFT-based Hilbert transform
   * Useful for AM demodulation, envelope detection, and instantaneous amplitude
   *
   * @param params - Hilbert envelope configuration
   * @param params.windowSize - FFT window size (must be positive)
   * @param params.hopSize - Hop size between windows (default: windowSize/2)
   * @returns this instance for method chaining
   *
   * @example
   * // Basic envelope detection
   * pipeline.HilbertEnvelope({ windowSize: 256 });
   *
   * @example
   * // With custom hop size (75% overlap)
   * pipeline.HilbertEnvelope({
   *   windowSize: 512,
   *   hopSize: 128
   * });
   *
   * @example
   * // Chain with smoothing
   * pipeline
   *   .HilbertEnvelope({ windowSize: 256 })
   *   .MovingAverage({ mode: "moving", windowSize: 10 });
   */
  HilbertEnvelope(params: HilbertEnvelopeParams): this {
    if (params.windowSize === undefined || params.windowSize === null) {
      throw new TypeError("HilbertEnvelope: windowSize parameter is required");
    }

    if (params.windowSize <= 0 || !Number.isInteger(params.windowSize)) {
      throw new TypeError(
        `HilbertEnvelope: window size must be greater than 0 and an integer, got ${params.windowSize}`
      );
    }

    if (params.hopSize !== undefined) {
      if (
        params.hopSize < 1 ||
        params.hopSize > params.windowSize ||
        !Number.isInteger(params.hopSize)
      ) {
        throw new TypeError(
          `HilbertEnvelope: hop size must be between 1 and window size (${params.windowSize}), got ${params.hopSize}`
        );
      }
    }

    this.nativeInstance.addStage("hilbertEnvelope", params);
    const hopSize = params.hopSize || Math.floor(params.windowSize / 2);
    this.stages.push(`hilbertEnvelope:win${params.windowSize}:hop${hopSize}`);
    return this;
  }

  /**
   * Add FFT (Fast Fourier Transform) stage to the pipeline
   *
   * Transforms time-domain signal to frequency domain or vice versa.
   * Supports both fast (radix-2) and direct (any size) transforms.
   *
   * **Transform Types:**
   * - `fft`: Complex FFT (O(N log N), requires power-of-2 size)
   * - `dft`: Complex DFT (O(N²), works with any size)
   * - `rfft`: Real FFT for real signals (O(N log N), outputs N/2+1 bins)
   * - `rdft`: Real DFT for real signals (O(N²), outputs N/2+1 bins)
   *
   * **Output Formats:**
   * - `complex`: Returns interleaved [real0, imag0, real1, imag1, ...]
   * - `magnitude`: Returns |X[k]| = sqrt(real² + imag²)
   * - `power`: Returns |X[k]|² = real² + imag²
   * - `phase`: Returns ∠X[k] = atan2(imag, real)
   *
   * @param params - FFT configuration
   * @param params.size - FFT size (power of 2 for FFT/RFFT, any size for DFT/RDFT)
   * @param params.type - Transform type (default: 'rfft' for real signals)
   * @param params.forward - Forward (time→freq) or inverse (freq→time) (default: true)
   * @param params.output - Output format (default: 'magnitude')
   * @returns this instance for method chaining
   *
   * @example
   * // Spectral analysis with magnitude output
   * pipeline.fft({
   *   size: 1024,
   *   type: 'rfft',
   *   output: 'magnitude'
   * });
   *
   * @example
   * // Power spectrum for energy analysis
   * pipeline.fft({
   *   size: 2048,
   *   output: 'power'
   * });
   *
   * @example
   * // Complex FFT for phase information
   * pipeline.fft({
   *   size: 512,
   *   type: 'fft',
   *   output: 'complex'
   * });
   *
   * @example
   * // DFT for non-power-of-2 sizes
   * pipeline.fft({
   *   size: 1000,  // Not a power of 2
   *   type: 'rdft',
   *   output: 'magnitude'
   * });
   */
  fft(params: fftParams): this {
    if (!params.size || params.size <= 0 || !Number.isInteger(params.size)) {
      throw new TypeError(
        `Fft: size must be a positive integer, got ${params.size}`
      );
    }

    const type = params.type || "rfft";
    const forward = params.forward ?? true;
    const output = params.output || "magnitude";

    // Validate type
    const validTypes = ["fft", "dft", "rfft", "rdft"];
    if (!validTypes.includes(type)) {
      throw new TypeError(
        `Fft: type must be one of ${validTypes.join(", ")}, got '${type}'`
      );
    }

    // Validate output format
    const validOutputs = ["complex", "magnitude", "power", "phase"];
    if (!validOutputs.includes(output)) {
      throw new TypeError(
        `Fft: output must be one of ${validOutputs.join(", ")}, got '${output}'`
      );
    }

    // Check power-of-2 requirement for FFT/RFFT
    if (
      (type === "fft" || type === "rfft") &&
      !this.isPowerOfTwo(params.size)
    ) {
      throw new TypeError(
        `Fft: ${type} requires power-of-2 size. Got ${params.size}. Use 'dft' or 'rdft' for arbitrary sizes.`
      );
    }

    this.nativeInstance.addStage("fft", {
      size: params.size,
      type,
      forward,
      output,
    });

    const direction = forward ? "forward" : "inverse";
    this.stages.push(`fft:${type}:${params.size}:${direction}:${output}`);
    return this;
  }

  /**
   * Helper to check if a number is a power of 2
   */
  private isPowerOfTwo(n: number): boolean {
    return n > 0 && (n & (n - 1)) === 0;
  }

  /**
   * Add STFT (Short-Time Fourier Transform) stage to the pipeline
   *
   * Computes time-frequency representation by applying FFT/DFT to overlapping windows.
   * Produces a spectrogram showing how frequency content evolves over time.
   *
   * **Use Cases:**
   * - Audio spectrograms for music/speech analysis
   * - Non-stationary signal analysis (EEG, vibration)
   * - Feature extraction for machine learning
   * - Time-varying frequency detection
   *
   * **Time-Frequency Resolution Trade-off:**
   * - Larger window → Better frequency resolution, worse time resolution
   * - Smaller window → Better time resolution, worse frequency resolution
   *
   * **Output Format:**
   * The output is a flattened 2D array: [window0_bin0, window0_bin1, ..., window1_bin0, ...]
   * - Rows: Time windows (numWindows)
   * - Cols: Frequency bins (windowSize/2+1 for real, windowSize for complex)
   *
   * @param params - STFT configuration
   * @param params.windowSize - FFT window size (power of 2 for FFT, any size for DFT)
   * @param params.hopSize - Stride between windows (default: windowSize/2, i.e., 50% overlap)
   * @param params.method - 'fft' or 'dft' (default: auto-detect based on windowSize)
   * @param params.type - 'real' or 'complex' input (default: 'real')
   * @param params.forward - true for forward STFT, false for inverse (default: true)
   * @param params.output - 'complex', 'magnitude', 'power', or 'phase' (default: 'magnitude')
   * @param params.window - Window function: 'none', 'hann', 'hamming', 'blackman', 'bartlett' (default: 'hann')
   * @returns this instance for method chaining
   *
   * @example
   * // Audio spectrogram with default settings
   * pipeline.stft({
   *   windowSize: 1024,
   *   hopSize: 512,      // 50% overlap
   *   output: 'magnitude'
   * });
   *
   * @example
   * // High time resolution for transient detection
   * pipeline.stft({
   *   windowSize: 256,   // Small window
   *   hopSize: 64,       // 75% overlap
   *   window: 'hann'
   * });
   *
   * @example
   * // High frequency resolution for harmonic analysis
   * pipeline.stft({
   *   windowSize: 4096,  // Large window
   *   hopSize: 2048,
   *   output: 'power',
   *   window: 'blackman'
   * });
   *
   * @example
   * // Complex output for phase vocoding
   * pipeline.stft({
   *   windowSize: 2048,
   *   hopSize: 512,
   *   output: 'complex'
   * });
   *
   * @example
   * // Non-power-of-2 with DFT
   * pipeline.stft({
   *   windowSize: 1000,
   *   method: 'dft',
   *   output: 'magnitude'
   * });
   */
  stft(params: stftParams): this {
    // Validate windowSize
    if (
      !params.windowSize ||
      params.windowSize <= 0 ||
      !Number.isInteger(params.windowSize)
    ) {
      throw new TypeError(
        `Stft: windowSize must be a positive integer, got ${params.windowSize}`
      );
    }

    // Validate hopSize if provided
    const hopSize = params.hopSize ?? Math.floor(params.windowSize / 2);
    if (
      hopSize <= 0 ||
      hopSize > params.windowSize ||
      !Number.isInteger(hopSize)
    ) {
      throw new TypeError(
        `Stft: hopSize must be a positive integer <= windowSize (${params.windowSize}), got ${hopSize}`
      );
    }

    // Auto-detect method based on windowSize if not specified
    const isPow2 = this.isPowerOfTwo(params.windowSize);
    const method = params.method ?? (isPow2 ? "fft" : "dft");
    const type = params.type ?? "real";
    const forward = params.forward ?? true;
    const output = params.output ?? "magnitude";
    const windowFunc = params.window ?? "hann";

    // Validate method
    const validMethods = ["fft", "dft"];
    if (!validMethods.includes(method)) {
      throw new TypeError(
        `Stft: method must be one of ${validMethods.join(
          ", "
        )}, got '${method}'`
      );
    }

    // Validate type
    const validTypes = ["real", "complex"];
    if (!validTypes.includes(type)) {
      throw new TypeError(
        `Stft: type must be one of ${validTypes.join(", ")}, got '${type}'`
      );
    }

    // Validate output
    const validOutputs = ["complex", "magnitude", "power", "phase"];
    if (!validOutputs.includes(output)) {
      throw new TypeError(
        `Stft: output must be one of ${validOutputs.join(
          ", "
        )}, got '${output}'`
      );
    }

    // Validate window function
    const validWindows = ["none", "hann", "hamming", "blackman", "bartlett"];
    if (!validWindows.includes(windowFunc)) {
      throw new TypeError(
        `Stft: window must be one of ${validWindows.join(
          ", "
        )}, got '${windowFunc}'`
      );
    }

    // Check power-of-2 requirement for FFT
    if (method === "fft" && !isPow2) {
      throw new TypeError(
        `Stft: FFT method requires power-of-2 windowSize. Got ${params.windowSize}. Use method: 'dft' for arbitrary sizes.`
      );
    }

    this.nativeInstance.addStage("stft", {
      windowSize: params.windowSize,
      hopSize,
      method,
      type,
      forward,
      output,
      window: windowFunc,
    });

    const direction = forward ? "forward" : "inverse";
    const overlapPct = Math.round(
      ((params.windowSize - hopSize) / params.windowSize) * 100
    );
    this.stages.push(
      `stft:${method}:win${params.windowSize}:hop${hopSize}(${overlapPct}%):${windowFunc}:${direction}:${output}`
    );
    return this;
  }

  /**
   * Apply Mel Spectrogram conversion to power spectrum
   *
   * Converts linear frequency spectrum to Mel-scale representation using a pre-computed
   * filterbank matrix. The Mel scale is perceptually motivated and better matches human
   * hearing's frequency resolution.
   *
   * **Typical Pipeline:**
   * ```
   * STFT → Power → MelSpectrogram → Log → MFCC
   * ```
   *
   * **What it does:**
   * - Matrix multiplication: mel_energies = filterbank × power_spectrum
   * - Groups frequency bins into perceptually-meaningful Mel bands
   * - High-performance C++ implementation using Eigen
   *
   * @param params - Mel spectrogram configuration (filterbank, numBins, numMelBands)
   * @returns this instance for method chaining
   *
   * @example
   * // Create Mel filterbank (helper function needed)
   * const filterbank = createMelFilterbank({
   *   sampleRate: 16000,
   *   numBins: 257, // from FFT size 512
   *   numMelBands: 40,
   *   fMin: 0,
   *   fMax: 8000
   * });
   *
   * pipeline
   *   .stft({ windowSize: 512, output: 'power' })
   *   .melSpectrogram({
   *     filterbankMatrix: filterbank,
   *     numBins: 257,
   *     numMelBands: 40
   *   });
   *
   * @example
   * // Speech recognition pipeline
   * const filterbank = createMelFilterbank({
   *   sampleRate: 16000,
   *   numBins: 513,
   *   numMelBands: 26,
   *   fMin: 20,
   *   fMax: 8000
   * });
   *
   * pipeline
   *   .stft({ windowSize: 1024, output: 'power' })
   *   .melSpectrogram({
   *     filterbankMatrix: filterbank,
   *     numBins: 513,
   *     numMelBands: 26
   *   })
   *   .mfcc({ numMelBands: 26, numCoefficients: 13 });
   */
  melSpectrogram(params: MelSpectrogramParams): this {
    // Validate required parameters
    if (
      !params.filterbankMatrix ||
      !(params.filterbankMatrix instanceof Float32Array)
    ) {
      throw new TypeError(
        "melSpectrogram: filterbankMatrix must be a Float32Array"
      );
    }

    if (!Number.isInteger(params.numBins) || params.numBins <= 0) {
      throw new TypeError("melSpectrogram: numBins must be a positive integer");
    }

    if (!Number.isInteger(params.numMelBands) || params.numMelBands <= 0) {
      throw new TypeError(
        "melSpectrogram: numMelBands must be a positive integer"
      );
    }

    // Validate filterbank dimensions
    const expectedSize = params.numMelBands * params.numBins;
    if (params.filterbankMatrix.length !== expectedSize) {
      throw new RangeError(
        `melSpectrogram: filterbankMatrix length (${params.filterbankMatrix.length}) must equal numMelBands × numBins (${expectedSize})`
      );
    }

    this.nativeInstance.addStage("melSpectrogram", {
      filterbankMatrix: params.filterbankMatrix,
      numBins: params.numBins,
      numMelBands: params.numMelBands,
    });

    this.stages.push(
      `melSpectrogram:${params.numBins}bins→${params.numMelBands}mels`
    );
    return this;
  }

  /**
   * Extract MFCC (Mel-Frequency Cepstral Coefficients) features
   *
   * Applies Discrete Cosine Transform (DCT) to log Mel energies to produce MFCCs.
   * MFCCs are the standard features for speech recognition, speaker identification,
   * and audio classification because they:
   * - Decorrelate Mel energies
   * - Compress information into lower-order coefficients
   * - Mimic human auditory perception
   * - Provide compact representation for ML models
   *
   * **Typical Pipeline:**
   * ```
   * STFT → Power → MelSpectrogram → Log → MFCC
   * ```
   *
   * **What it does:**
   * - Applies DCT-II to (log) Mel energies
   * - Keeps first N coefficients (typically 13-20)
   * - Optional cepstral liftering for improved recognition
   * - High-performance C++ implementation with pre-computed cosine tables
   *
   * @param params - MFCC configuration (numMelBands, numCoefficients, options)
   * @returns this instance for method chaining
   *
   * @example
   * // Standard speech recognition pipeline
   * const filterbank = createMelFilterbank({
   *   sampleRate: 16000,
   *   numBins: 257,
   *   numMelBands: 26
   * });
   *
   * pipeline
   *   .stft({ windowSize: 512, output: 'power' })
   *   .melSpectrogram({
   *     filterbankMatrix: filterbank,
   *     numBins: 257,
   *     numMelBands: 26
   *   })
   *   .mfcc({
   *     numMelBands: 26,
   *     numCoefficients: 13,
   *     useLogEnergy: true,
   *     lifterCoefficient: 22
   *   });
   *
   * @example
   * // Music classification with more coefficients
   * pipeline
   *   .stft({ windowSize: 2048, output: 'power' })
   *   .melSpectrogram({
   *     filterbankMatrix: musicFilterbank,
   *     numBins: 1025,
   *     numMelBands: 40
   *   })
   *   .mfcc({
   *     numMelBands: 40,
   *     numCoefficients: 20
   *   });
   *
   * @example
   * // Skip log if input is already in log domain
   * pipeline
   *   .stft({ windowSize: 512, output: 'power' })
   *   .melSpectrogram({ filterbankMatrix, numBins: 257, numMelBands: 26 })
   *   .tap((samples) => {
   *     // Apply log manually
   *     for (let i = 0; i < samples.length; i++) {
   *       samples[i] = Math.log(samples[i] + 1e-10);
   *     }
   *   })
   *   .mfcc({
   *     numMelBands: 26,
   *     numCoefficients: 13,
   *     useLogEnergy: false  // Skip log since we already applied it
   *   });
   */
  mfcc(params: MfccParams): this {
    // Validate required parameters
    if (!Number.isInteger(params.numMelBands) || params.numMelBands <= 0) {
      throw new TypeError("mfcc: numMelBands must be a positive integer");
    }

    // Apply defaults
    const numCoefficients = params.numCoefficients ?? 13;
    const useLogEnergy = params.useLogEnergy ?? true;
    const lifterCoefficient = params.lifterCoefficient ?? 0;

    // Validate optional parameters
    if (!Number.isInteger(numCoefficients) || numCoefficients <= 0) {
      throw new TypeError("mfcc: numCoefficients must be a positive integer");
    }

    if (numCoefficients > params.numMelBands) {
      throw new RangeError(
        `mfcc: numCoefficients (${numCoefficients}) must be ≤ numMelBands (${params.numMelBands})`
      );
    }

    if (typeof useLogEnergy !== "boolean") {
      throw new TypeError("mfcc: useLogEnergy must be a boolean");
    }

    if (typeof lifterCoefficient !== "number" || lifterCoefficient < 0) {
      throw new TypeError(
        "mfcc: lifterCoefficient must be a non-negative number"
      );
    }

    this.nativeInstance.addStage("mfcc", {
      numMelBands: params.numMelBands,
      numCoefficients,
      useLogEnergy,
      lifterCoefficient,
    });

    const lifterStr = lifterCoefficient > 0 ? `:lift${lifterCoefficient}` : "";
    const logStr = useLogEnergy ? ":log" : "";
    this.stages.push(
      `mfcc:${params.numMelBands}mels→${numCoefficients}coeffs${logStr}${lifterStr}`
    );
    return this;
  }

  /**
   * Tap into the pipeline for debugging and inspection
   * The callback is executed synchronously after processing, allowing you to inspect
   * intermediate results without modifying the data flow
   *
   * @param callback - Function to inspect samples (receives Float32Array view and stage name)
   * @returns this instance for method chaining
   *
   * @example
   * pipeline
   *   .MovingAverage({ mode: "moving", windowSize: 10 })
   *   .tap((samples, stage) => console.log(`After ${stage}:`, samples.slice(0, 5)))
   *   .Rectify()
   *   .tap((samples) => logger.debug('After rectify:', samples.slice(0, 5)))
   *   .Rms({ mode: "moving", windowSize: 5 });
   *
   * @example
   * // Conditional logging
   * pipeline
   *   .MovingAverage({ mode: "moving", windowSize: 100 })
   *   .tap((samples, stage) => {
   *     const max = Math.max(...samples);
   *     if (max > THRESHOLD) {
   *       console.warn(`High amplitude detected at ${stage}: ${max}`);
   *     }
   *   });
   */
  tap(callback: TapCallback): this {
    const currentStageName = this.stages.join(" → ") || "start";
    this.tapCallbacks.push({ stageName: currentStageName, callback });
    return this;
  }

  /**
   * Add a filter stage to the pipeline
   * Supports FIR and IIR filters with various designs (Butterworth, Chebyshev, etc.)
   *
   * @param options - Filter configuration options
   * @returns this instance for method chaining
   *
   * @example
   * // FIR low-pass filter
   * pipeline.filter({
   *   type: "fir",
   *   mode: "lowpass",
   *   cutoffFrequency: 1000,
   *   sampleRate: 8000,
   *   order: 51
   * });
   *
   * @example
   * // Butterworth band-pass filter
   * pipeline.filter({
   *   type: "butterworth",
   *   mode: "bandpass",
   *   lowCutoffFrequency: 300,
   *   highCutoffFrequency: 3000,
   *   sampleRate: 8000,
   *   order: 4
   * });
   *
   * @example
   * // Chebyshev low-pass with ripple
   * pipeline.filter({
   *   type: "chebyshev",
   *   mode: "lowpass",
   *   cutoffFrequency: 1000,
   *   sampleRate: 8000,
   *   order: 2,
   *   ripple: 0.5
   * });
   *
   * @example
   * // Peaking EQ (biquad)
   * pipeline.filter({
   *   type: "biquad",
   *   mode: "peak",
   *   centerFrequency: 1000,
   *   sampleRate: 8000,
   *   q: 2.0,
   *   gain: 6.0
   * });
   */
  filter(options: FilterOptions): this {
    // Validate required parameters upfront
    if (!options.type) {
      throw new Error(
        "Filter 'type' is required. Valid types: 'fir', 'butterworth', 'chebyshev', 'biquad'"
      );
    }

    if (!options.mode) {
      throw new Error(
        "Filter 'mode' is required. Valid modes: 'lowpass', 'highpass', 'bandpass', 'bandstop', 'notch', 'peak', 'lowshelf', 'highshelf'"
      );
    }

    // Validate sampleRate for frequency-based filters
    if (
      ["fir", "butterworth", "chebyshev", "biquad"].includes(options.type) &&
      !options.sampleRate
    ) {
      throw new Error(
        `Filter type '${options.type}' requires 'sampleRate' parameter. Example: { type: 'fir', mode: 'lowpass', cutoffFrequency: 1000, sampleRate: 8000, order: 51, windowType: 'hamming' }`
      );
    }

    // Create the appropriate filter based on type
    let filterInstance: FirFilter | IirFilter;

    try {
      switch (options.type) {
        case "fir":
          filterInstance = this.createFirFilter(options);
          break;

        case "butterworth":
          filterInstance = this.createButterworthFilter(options);
          break;

        case "chebyshev":
          filterInstance = this.createChebyshevFilter(options);
          break;

        case "biquad":
          filterInstance = this.createBiquadFilter(options);
          break;

        case "iir":
        default:
          throw new Error(
            `Filter type "${options.type}" not yet implemented for pipeline chaining. Use standalone filter methods instead.`
          );
      }
    } catch (error) {
      // Wrap any filter creation errors with helpful context
      const err = error as Error;
      throw new Error(
        `Failed to create ${options.type} filter (mode: ${options.mode}): ${err.message}. ` +
          `Check that all required parameters are provided (sampleRate, cutoffFrequency/frequencies, order, etc.)`
      );
    }

    // Store the filter instance for processing
    // Get coefficients based on filter type
    let bCoeffs: Float64Array;
    let aCoeffs: Float64Array;

    try {
      if (filterInstance instanceof FirFilter) {
        // FIR filter: only B coefficients, A = [1]
        const coeffs = filterInstance.getCoefficients();
        if (!coeffs || coeffs.length === 0) {
          throw new Error("FIR filter returned empty coefficients");
        }
        bCoeffs = new Float64Array(coeffs);
        aCoeffs = new Float64Array([1.0]);
      } else if (filterInstance instanceof IirFilter) {
        // IIR filter: both B and A coefficients
        const bCoeffs32 = filterInstance.getBCoefficients();
        const aCoeffs32 = filterInstance.getACoefficients();
        if (!bCoeffs32 || bCoeffs32.length === 0) {
          throw new Error("IIR filter returned empty B coefficients");
        }
        if (!aCoeffs32 || aCoeffs32.length === 0) {
          throw new Error("IIR filter returned empty A coefficients");
        }
        bCoeffs = new Float64Array(bCoeffs32);
        aCoeffs = new Float64Array(aCoeffs32);
      } else {
        throw new Error("Unknown filter type");
      }

      this.nativeInstance.addFilterStage(bCoeffs, aCoeffs);
      this.stages.push(`filter:${options.type}:${options.mode}`);
    } catch (error) {
      const err = error as Error;
      throw new Error(`Failed to add filter stage to pipeline: ${err.message}`);
    }

    return this;
  }

  /**
   * Helper to create FIR filter from options
   */
  private createFirFilter(options: FilterOptions & { type: "fir" }): FirFilter {
    const { mode, cutoffFrequency, lowCutoffFrequency, highCutoffFrequency } =
      options;

    switch (mode) {
      case "lowpass":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required for lowpass filter");
        }
        return FirFilter.createLowPass(options as any);

      case "highpass":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required for highpass filter");
        }
        return FirFilter.createHighPass(options as any);

      case "bandpass":
        if (!lowCutoffFrequency || !highCutoffFrequency) {
          throw new Error(
            "lowCutoffFrequency and highCutoffFrequency required for bandpass filter"
          );
        }
        return FirFilter.createBandPass(options as any);

      case "bandstop":
      case "notch":
        if (!lowCutoffFrequency || !highCutoffFrequency) {
          throw new Error(
            "lowCutoffFrequency and highCutoffFrequency required for bandstop filter"
          );
        }
        return FirFilter.createBandStop(options as any);

      default:
        throw new Error(`Unsupported FIR filter mode: ${mode}`);
    }
  }

  /**
   * Helper to create Butterworth filter from options
   */
  private createButterworthFilter(
    options: FilterOptions & { type: "butterworth" }
  ): IirFilter {
    const { mode, cutoffFrequency, lowCutoffFrequency, highCutoffFrequency } =
      options;

    switch (mode) {
      case "lowpass":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required for lowpass filter");
        }
        return IirFilter.createButterworthLowPass(options as any);

      case "highpass":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required for highpass filter");
        }
        return IirFilter.createButterworthHighPass(options as any);

      case "bandpass":
        if (!lowCutoffFrequency || !highCutoffFrequency) {
          throw new Error(
            "lowCutoffFrequency and highCutoffFrequency required for bandpass filter"
          );
        }
        return IirFilter.createButterworthBandPass(options as any);

      default:
        throw new Error(`Unsupported Butterworth filter mode: ${mode}`);
    }
  }

  /**
   * Helper to create Chebyshev filter from options
   */
  private createChebyshevFilter(
    options: FilterOptions & { type: "chebyshev" }
  ): IirFilter {
    const {
      mode,
      cutoffFrequency,
      lowCutoffFrequency,
      highCutoffFrequency,
      ripple = 0.5,
    } = options as any;

    switch (mode) {
      case "lowpass":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required for lowpass filter");
        }
        return IirFilter.createChebyshevLowPass({
          cutoffFrequency,
          sampleRate: options.sampleRate,
          order: options.order,
          rippleDb: ripple,
        });

      case "highpass":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required for highpass filter");
        }
        return IirFilter.createChebyshevHighPass({
          cutoffFrequency,
          sampleRate: options.sampleRate,
          order: options.order,
          rippleDb: ripple,
        });

      case "bandpass":
        if (!lowCutoffFrequency || !highCutoffFrequency) {
          throw new Error(
            "lowCutoffFrequency and highCutoffFrequency required for bandpass filter"
          );
        }
        return IirFilter.createChebyshevBandPass({
          lowCutoffFrequency,
          highCutoffFrequency,
          sampleRate: options.sampleRate,
          order: options.order,
          rippleDb: ripple,
        });

      default:
        throw new Error(`Unsupported Chebyshev filter mode: ${mode}`);
    }
  }

  /**
   * Helper to create Biquad filter from options
   */
  private createBiquadFilter(
    options: FilterOptions & { type: "biquad" }
  ): IirFilter {
    const { mode, cutoffFrequency, q = 0.707, gain = 0 } = options as any;
    const { sampleRate } = options;

    switch (mode) {
      case "peak":
      case "peaking":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency (center frequency) required");
        }
        return IirFilter.createPeakingEQ({
          centerFrequency: cutoffFrequency,
          sampleRate,
          Q: q,
          gainDb: gain,
        });

      case "lowshelf":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required for low-shelf filter");
        }
        return IirFilter.createLowShelf({
          cutoffFrequency,
          sampleRate,
          gainDb: gain,
          Q: q,
        });

      case "highshelf":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required for high-shelf filter");
        }
        return IirFilter.createHighShelf({
          cutoffFrequency,
          sampleRate,
          gainDb: gain,
          Q: q,
        });

      case "lowpass":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required");
        }
        // Use Butterworth for biquad lowpass (2nd order)
        return IirFilter.createButterworthLowPass({
          cutoffFrequency,
          sampleRate,
          order: 2,
        });

      case "highpass":
        if (!cutoffFrequency) {
          throw new Error("cutoffFrequency required");
        }
        // Use Butterworth for biquad highpass (2nd order)
        return IirFilter.createButterworthHighPass({
          cutoffFrequency,
          sampleRate,
          order: 2,
        });

      case "bandpass":
      case "bandstop":
      case "notch":
        throw new Error(
          `Biquad ${mode} filters not yet implemented. Use Butterworth or Chebyshev filters instead.`
        );

      default:
        throw new Error(`Unsupported Biquad filter mode: ${mode}`);
    }
  }

  /**
   * Configure pipeline callbacks for monitoring and observability
   * @param callbacks - Object containing callback functions
   * @returns this instance for method chaining
   *
   * @example
   * pipeline
   *   .pipeline({
   *     onSample: (value, i, stage) => {
   *       if (value > THRESHOLD) triggerAlert(i, stage);
   *     },
   *     onStageComplete: (stage, durationMs) => {
   *       metrics.record(`dsp.${stage}.duration`, durationMs);
   *     },
   *     onError: (stage, err) => {
   *       logger.error(`Stage ${stage} failed`, err);
   *     },
   *     onLog: (level, msg, ctx) => {
   *       if (level === "debug") return;
   *       console.log(`[${level}] ${msg}`, ctx);
   *     },
   *   })
   *   .MovingAverage({ mode: "moving", windowSize: 10 })
   *   .Rectify();
   */
  pipeline(callbacks: PipelineCallbacks): this {
    this.callbacks = callbacks;
    return this;
  }

  /**
   * Process data through the DSP pipeline
   * The native process method uses Napi::AsyncWorker and runs on a background thread
   * to avoid blocking the Node.js event loop
   *
   * Supports three modes:
   * 1. Legacy sample-based: process(samples, { sampleRate: 100, channels: 1 })
   * 2. Time-based with timestamps: process(samples, timestamps, { channels: 1 })
   * 3. Auto-generated timestamps: process(samples, { channels: 1 }) - generates [0, 1, 2, ...]
   *
   * IMPORTANT: This method modifies the input buffer in-place for performance.
   * If you need to preserve the original input, pass a copy instead.
   *
   * @param input - Float32Array containing interleaved samples (will be modified in-place)
   * @param timestampsOrOptions - Either timestamps (Float32Array) or ProcessOptions
   * @param optionsIfTimestamps - ProcessOptions if second argument is timestamps
   * @returns Promise that resolves to the processed Float32Array (same reference as input)
   */
  async process(
    input: Float32Array,
    timestampsOrOptions: Float32Array | ProcessOptions,
    optionsIfTimestamps?: ProcessOptions
  ): Promise<Float32Array> {
    let timestamps: Float32Array | undefined;
    let options: ProcessOptions;

    // Detect which overload was called
    if (timestampsOrOptions instanceof Float32Array) {
      // Time-based mode: process(samples, timestamps, options)
      timestamps = timestampsOrOptions;
      options = { channels: 1, ...optionsIfTimestamps };

      if (timestamps.length !== input.length) {
        throw new Error(
          `Timestamps length (${timestamps.length}) must match samples length (${input.length})`
        );
      }
    } else {
      // Sample-based mode or auto-timestamps: process(samples, options)
      options = { channels: 1, ...timestampsOrOptions };

      if (options.sampleRate) {
        // Legacy sample-based mode: auto-generate timestamps from sampleRate
        const dt = 1000 / options.sampleRate; // milliseconds per sample
        timestamps = new Float32Array(input.length);
        for (let i = 0; i < input.length; i++) {
          timestamps[i] = i * dt;
        }
      } else {
        // Auto-generate sequential timestamps [0, 1, 2, ...]
        timestamps = new Float32Array(input.length);
        for (let i = 0; i < input.length; i++) {
          timestamps[i] = i;
        }
      }
    }

    const startTime = performance.now();

    // Initialize drift detection if enabled
    if (options.enableDriftDetection && timestamps && options.sampleRate) {
      if (
        !this.driftDetector ||
        this.driftDetector.getExpectedSampleRate() !== options.sampleRate
      ) {
        // Create new detector if it doesn't exist or sample rate changed
        this.driftDetector = new DriftDetector({
          expectedSampleRate: options.sampleRate,
          driftThreshold: options.driftThreshold ?? 10,
          onDriftDetected: options.onDriftDetected,
        });
      }
      // Process timestamps to detect drift
      this.driftDetector.processBatch(timestamps);
    }

    try {
      // Pool the start log
      this.poolLog("debug", "Starting pipeline processing", {
        sampleCount: input.length,
        channels: options.channels,
        stages: this.stages.length,
        mode: options.sampleRate ? "sample-based" : "time-based",
      });

      // Call native process with timestamps
      // Note: The input buffer is modified in-place for zero-copy performance
      const result = await this.nativeInstance.process(
        input,
        timestamps,
        options
      );

      // Execute tap callbacks for debugging/inspection
      if (this.tapCallbacks.length > 0) {
        for (const { stageName, callback } of this.tapCallbacks) {
          try {
            callback(result, stageName);
          } catch (tapError) {
            // Don't let tap errors break the pipeline
            console.error(`Tap callback error at ${stageName}:`, tapError);
          }
        }
      }

      // Execute onBatch callback (efficient - one call per process)
      if (this.callbacks?.onBatch) {
        const stageName = this.stages.join(" → ") || "pipeline";
        const batch: SampleBatch = {
          stage: stageName,
          samples: result,
          startIndex: 0,
          count: result.length,
        };
        this.callbacks.onBatch(batch);
      }

      // Execute onSample callbacks if provided (LEGACY - expensive)
      // WARNING: This can be expensive for large buffers
      if (this.callbacks?.onSample) {
        const stageName = this.stages.join(" → ") || "pipeline";
        for (let i = 0; i < result.length; i++) {
          this.callbacks.onSample(result[i], i, stageName);
        }
      }

      // Execute onStageComplete callback
      if (this.callbacks?.onStageComplete) {
        const duration = performance.now() - startTime;
        const pipelineName = this.stages.join(" → ") || "pipeline";
        this.callbacks.onStageComplete(pipelineName, duration);
      }

      // Pool the completion log
      const duration = performance.now() - startTime;
      this.poolLog("info", "Pipeline processing completed", {
        durationMs: duration,
        sampleCount: result.length,
      });

      // Flush all pooled logs at the end
      this.flushLogs();

      return result;
    } catch (error) {
      const err = error as Error;

      // Execute onError callback
      if (this.callbacks?.onError) {
        const pipelineName = this.stages.join(" → ") || "pipeline";
        this.callbacks.onError(pipelineName, err);
      }

      // Pool the error log
      this.poolLog("error", "Pipeline processing failed", {
        error: err.message,
        stack: err.stack,
      });

      // Flush logs even on error
      this.flushLogs();

      throw error;
    }
  }

  /**
   * Process a copy of the audio data through the DSP pipeline
   * This method creates a copy of the input, so the original is preserved
   *
   * @param input - Float32Array containing interleaved audio samples (original is preserved)
   * @param timestampsOrOptions - Either timestamps array or processing options (sample rate and channel count)
   * @param optionsIfTimestamps - Processing options if timestamps were provided in second parameter
   * @returns Promise that resolves to a new Float32Array with the processed data
   *
   * @example
   * // Legacy sample-based (original preserved)
   * const output = await pipeline.processCopy(samples, { sampleRate: 100, channels: 1 });
   *
   * @example
   * // Time-based with explicit timestamps (original preserved)
   * const output = await pipeline.processCopy(samples, timestamps, { channels: 1 });
   */
  async processCopy(
    input: Float32Array,
    timestampsOrOptions: Float32Array | ProcessOptions,
    optionsIfTimestamps?: ProcessOptions
  ): Promise<Float32Array> {
    // Create a copy to preserve the original
    const copy = new Float32Array(input);

    // Handle both overloaded signatures by delegating to process()
    if (timestampsOrOptions instanceof Float32Array) {
      // Time-based mode: process(samples, timestamps, options)
      const timestampsCopy = new Float32Array(timestampsOrOptions);
      return await this.process(copy, timestampsCopy, optionsIfTimestamps!);
    } else {
      // Legacy mode: process(samples, options)
      return await this.process(copy, timestampsOrOptions);
    }
  }

  /**
   * Save the current pipeline state as a JSON string
   * TypeScript can then store this in Redis or other persistent storage
   *
   * @returns JSON string containing the pipeline state
   *
   * @example
   * const stateJson = await pipeline.saveState();
   * await redis.set('dsp:state', stateJson);
   */
  async saveState(): Promise<string> {
    return this.nativeInstance.saveState();
  }

  /**
   * Load pipeline state from a JSON string
   * TypeScript retrieves this from Redis and passes it to restore state
   *
   * @param stateJson - JSON string containing the pipeline state
   * @returns Promise that resolves to true if successful
   *
   * @example
   * const stateJson = await redis.get('dsp:state');
   * if (stateJson) {
   *   await pipeline.loadState(stateJson);
   * }
   */
  async loadState(stateJson: string): Promise<boolean> {
    return this.nativeInstance.loadState(stateJson);
  }

  /**
   * Clear all pipeline state (reset all filters to initial state)
   * This resets filter buffers without removing the stages
   *
   * @example
   * pipeline.clearState(); // Reset all filters
   */
  clearState(): void {
    this.nativeInstance.clearState();
  }

  /**
   * List current pipeline state summary
   * Returns a lightweight view of the pipeline configuration without full state data.
   * Useful for debugging, monitoring, and inspecting pipeline structure.
   *
   * @returns Object containing pipeline summary with stage info
   *
   * @example
   * const pipeline = createDspPipeline()
   *   .MovingAverage({ mode: "moving", windowSize: 100 })
   *   .Rectify({ mode: 'full' })
   *   .Rms({ mode: "moving", windowSize: 50 });
   *
   * const summary = pipeline.listState();
   * console.log(summary);
   * // {
   * //   stageCount: 3,
   * //   timestamp: 1761234567,
   * //   stages: [
   * //     { index: 0, type: 'movingAverage', windowSize: 100, numChannels: 1, bufferSize: 100, channelCount: 1 },
   * //     { index: 1, type: 'rectify', mode: 'full', numChannels: 1 },
   * //     { index: 2, type: 'rms', windowSize: 50, numChannels: 1, bufferSize: 50, channelCount: 1 }
   * //   ]
   * // }
   */
  listState(): PipelineStateSummary {
    return this.nativeInstance.listState();
  }
}

/**
 * Create a new DSP pipeline builder
 * @param config - Optional Redis configuration for state persistence
 * @returns A new DspProcessor instance
 *
 * @example
 * // Create pipeline with Redis state persistence
 * const pipeline = createDspPipeline({
 *   redisHost: 'localhost',
 *   redisPort: 6379,
 *   stateKey: 'dsp:channel1'
 * });
 *
 * @example
 * // Create pipeline without Redis (state is not persisted)
 * const pipeline = createDspPipeline();
 */
export function createDspPipeline(config?: RedisConfig): DspProcessor {
  const nativeInstance = new DspAddon.DspPipeline(config);
  return new DspProcessor(nativeInstance);
}

export { DspProcessor };

// ============================================================================
// Matrix Analysis Functions (PCA, ICA, Whitening)
// ============================================================================

import type {
  PcaResult,
  IcaResult,
  WhiteningResult,
  PcaTransformParams,
  IcaTransformParams,
  WhiteningTransformParams,
} from "./types.js";

/**
 * Calculate PCA (Principal Component Analysis) transformation matrix from training data.
 *
 * PCA finds orthogonal directions of maximum variance in multi-channel data.
 * Use for dimensionality reduction, feature extraction, and noise reduction.
 *
 * **Workflow**:
 * 1. **Train**: Call this function with representative training data
 * 2. **Apply**: Use returned matrix with `pipeline.PcaTransform()` for real-time processing
 * 3. **Reduce**: Keep only top N components for dimensionality reduction
 *
 * @param interleavedData - Multi-channel training data (interleaved: [ch0_s0, ch1_s0, ch0_s1, ch1_s1, ...])
 * @param numChannels - Number of channels in the data
 * @returns PCA result with transformation matrix, mean, eigenvalues, and explained variance
 *
 * @throws {TypeError} If data length is not divisible by numChannels
 * @throws {TypeError} If insufficient samples (need at least numChannels samples)
 * @throws {Error} If eigenvalue decomposition fails
 *
 * @example
 * // 3-channel EMG data, 1000 samples per channel
 * const trainingData = new Float32Array(3000); // Interleaved
 * // ... fill with training data ...
 *
 * const pca = calculatePca(trainingData, 3);
 * console.log('Explained variance:', pca.explainedVariance);
 * // Example output: [0.65, 0.25, 0.10] - first PC explains 65% of variance
 *
 * // Apply to real-time pipeline (keep all 3 components)
 * pipeline.PcaTransform({
 *   pcaMatrix: pca.pcaMatrix,
 *   mean: pca.mean,
 *   numChannels: 3,
 *   numComponents: 3
 * });
 *
 * @example
 * // Dimensionality reduction: 8 channels → 3 principal components
 * const pca8ch = calculatePca(eightChannelData, 8);
 *
 * // Keep only top 3 components (e.g., 95% of variance)
 * pipeline.PcaTransform({
 *   pcaMatrix: pca8ch.pcaMatrix,
 *   mean: pca8ch.mean,
 *   numChannels: 8,
 *   numComponents: 3  // Reduces to 3 channels
 * });
 *
 * @example
 * // EEG artifact removal: Remove components with low variance
 * const pcaEeg = calculatePca(eegData, 16);
 * console.log('Eigenvalues:', pcaEeg.eigenvalues);
 *
 * // Remove last 4 components (likely noise)
 * pipeline.PcaTransform({
 *   pcaMatrix: pcaEeg.pcaMatrix,
 *   mean: pcaEeg.mean,
 *   numChannels: 16,
 *   numComponents: 12  // Keep 12 out of 16
 * });
 */
export function calculatePca(
  interleavedData: Float32Array,
  numChannels: number
): PcaResult {
  if (!(interleavedData instanceof Float32Array)) {
    throw new TypeError("calculatePca: interleavedData must be a Float32Array");
  }
  if (!Number.isInteger(numChannels) || numChannels <= 0) {
    throw new TypeError("calculatePca: numChannels must be a positive integer");
  }

  return DspAddon.calculatePca(interleavedData, numChannels);
}

/**
 * Calculate Whitening (ZCA) transformation matrix from training data.
 *
 * Whitening transforms data to have identity covariance matrix (decorrelates and normalizes).
 * Essential preprocessing step before ICA. Also useful for machine learning feature normalization.
 *
 * **Workflow**:
 * 1. **Train**: Call this function with representative training data
 * 2. **Apply**: Use returned matrix with `pipeline.WhiteningTransform()` for real-time processing
 *
 * @param interleavedData - Multi-channel training data (interleaved)
 * @param numChannels - Number of channels in the data
 * @param regularization - Small value added to eigenvalues to prevent division by zero (default: 1e-5)
 * @returns Whitening result with transformation matrix and mean
 *
 * @throws {TypeError} If data length is not divisible by numChannels
 * @throws {TypeError} If insufficient samples
 * @throws {Error} If eigenvalue decomposition fails
 *
 * @example
 * // 4-channel sensor data
 * const sensorData = new Float32Array(4000); // 1000 samples × 4 channels
 * // ... fill with training data ...
 *
 * const whitening = calculateWhitening(sensorData, 4);
 *
 * // Apply to real-time pipeline
 * pipeline.WhiteningTransform({
 *   whiteningMatrix: whitening.whiteningMatrix,
 *   mean: whitening.mean,
 *   numChannels: 4,
 *   numComponents: 4
 * });
 *
 * @example
 * // Use as preprocessing before ICA
 * const whiten = calculateWhitening(mixedSignals, 5);
 * const ica = calculateIca(mixedSignals, 5); // ICA automatically whitens internally
 *
 * // Apply whitening first, then ICA
 * pipeline
 *   .WhiteningTransform({
 *     whiteningMatrix: whiten.whiteningMatrix,
 *     mean: whiten.mean,
 *     numChannels: 5,
 *     numComponents: 5
 *   })
 *   .IcaTransform({
 *     icaMatrix: ica.icaMatrix,
 *     mean: ica.mean,
 *     numChannels: 5,
 *     numComponents: 5
 *   });
 *
 * @example
 * // Custom regularization for noisy data
 * const whitening = calculateWhitening(noisyData, 3, 1e-3); // Higher regularization
 */
export function calculateWhitening(
  interleavedData: Float32Array,
  numChannels: number,
  regularization: number = 1e-5
): WhiteningResult {
  if (!(interleavedData instanceof Float32Array)) {
    throw new TypeError(
      "calculateWhitening: interleavedData must be a Float32Array"
    );
  }
  if (!Number.isInteger(numChannels) || numChannels <= 0) {
    throw new TypeError(
      "calculateWhitening: numChannels must be a positive integer"
    );
  }
  if (regularization <= 0) {
    throw new RangeError("calculateWhitening: regularization must be positive");
  }

  return DspAddon.calculateWhitening(
    interleavedData,
    numChannels,
    regularization
  );
}

/**
 * Calculate ICA (Independent Component Analysis) unmixing matrix using FastICA algorithm.
 *
 * ICA separates mixed signals into statistically independent components.
 * Use for blind source separation, artifact removal, and signal decomposition.
 *
 * **Important**: ICA internally performs whitening as a preprocessing step.
 *
 * **Workflow**:
 * 1. **Train**: Call this function with representative mixed signal data
 * 2. **Apply**: Use returned matrix with `pipeline.IcaTransform()` for real-time separation
 * 3. **Analyze**: Inspect separated components to identify artifacts/sources
 *
 * @param interleavedData - Mixed multi-channel data (interleaved)
 * @param numChannels - Number of channels (sources to separate)
 * @param maxIterations - Maximum FastICA iterations (default: 200)
 * @param tolerance - Convergence tolerance (default: 1e-4)
 * @returns ICA result with unmixing matrix, mean, convergence status, and iterations count
 *
 * @throws {TypeError} If data length is not divisible by numChannels
 * @throws {TypeError} If insufficient samples (need at least 5 × numChannels)
 * @throws {Error} If whitening or ICA algorithm fails
 *
 * @example
 * // EEG artifact removal: Separate eye blinks from brain signals
 * const eegData = new Float32Array(8000); // 8 channels, 1000 samples
 * // ... record EEG with eye blinks ...
 *
 * const ica = calculateIca(eegData, 8);
 * console.log(`Converged: ${ica.converged} in ${ica.iterations} iterations`);
 *
 * // Apply to real-time pipeline
 * pipeline.IcaTransform({
 *   icaMatrix: ica.icaMatrix,
 *   mean: ica.mean,
 *   numChannels: 8,
 *   numComponents: 8
 * });
 *
 * // After processing, analyze components to identify artifact channels
 * // Then reconstruct signal without artifact components
 *
 * @example
 * // Audio source separation: Cocktail party problem
 * const mixedAudio = new Float32Array(6000); // 3 microphones, 2000 samples
 * // ... record 3 mixed audio signals ...
 *
 * const ica = calculateIca(mixedAudio, 3, 500); // More iterations for audio
 *
 * if (ica.converged) {
 *   pipeline.IcaTransform({
 *     icaMatrix: ica.icaMatrix,
 *     mean: ica.mean,
 *     numChannels: 3,
 *     numComponents: 3  // Outputs 3 separated sources
 *   });
 * }
 *
 * @example
 * // EMG channel decomposition
 * const emgData = new Float32Array(20000); // 4 channels, 5000 samples
 * const ica = calculateIca(emgData, 4, 200, 1e-4);
 *
 * if (!ica.converged) {
 *   console.warn('ICA did not converge, try more samples or iterations');
 * }
 */
export function calculateIca(
  interleavedData: Float32Array,
  numChannels: number,
  maxIterations: number = 200,
  tolerance: number = 1e-4
): IcaResult {
  if (!(interleavedData instanceof Float32Array)) {
    throw new TypeError("calculateIca: interleavedData must be a Float32Array");
  }
  if (!Number.isInteger(numChannels) || numChannels <= 0) {
    throw new TypeError("calculateIca: numChannels must be a positive integer");
  }
  if (!Number.isInteger(maxIterations) || maxIterations <= 0) {
    throw new TypeError(
      "calculateIca: maxIterations must be a positive integer"
    );
  }
  if (tolerance <= 0) {
    throw new RangeError("calculateIca: tolerance must be positive");
  }

  return DspAddon.calculateIca(
    interleavedData,
    numChannels,
    maxIterations,
    tolerance
  );
}

/**
 * Calculate beamformer steering weights and blocking matrix for GSC adaptive beamforming.
 *
 * The Generalized Sidelobe Canceler (GSC) is a powerful architecture that converts
 * N-channel beamforming into a 2-channel adaptive filtering problem.
 *
 * **How it works**:
 * 1. Steering weights: Focus beam on target direction (delay-and-sum)
 * 2. Blocking matrix: Create N-1 noise-only references (target cancelled)
 * 3. Combine with LMS/RLS: Adaptively suppress noise while preserving target
 *
 * **Array Geometries**:
 * - `"linear"`: Uniform Linear Array (ULA) - most common for voice/speech
 * - `"circular"`: Circular array - omnidirectional coverage
 * - `"planar"`: 2D planar array - 3D spatial filtering
 *
 * **Element Spacing**:
 * - Use 0.5 wavelengths (λ/2) to avoid spatial aliasing
 * - For speech at 2 kHz: λ = 340m/s ÷ 2000Hz = 0.17m → spacing = 8.5cm
 * - For ultrasound at 40 kHz: λ = 8.5mm → spacing = 4.25mm
 *
 * @param numChannels - Number of microphones/sensors (must be ≥ 2)
 * @param arrayGeometry - Array type: "linear", "circular", or "planar"
 * @param targetAngleDeg - Target direction in degrees (0° = broadside/front)
 * @param elementSpacing - Spacing between elements in wavelengths (default: 0.5)
 * @returns Object with steeringWeights, blockingMatrix, and metadata
 *
 * @throws {TypeError} If parameters are invalid
 * @throws {RangeError} If numChannels < 2
 *
 * @example
 * // Conference call system: 8-mic linear array, focus on speaker at front
 * const bf = calculateBeamformerWeights(8, "linear", 0.0);
 *
 * pipeline
 *   .GscPreprocessor({
 *     numChannels: 8,
 *     steeringWeights: bf.steeringWeights,
 *     blockingMatrix: bf.blockingMatrix
 *   })
 *   .LmsFilter({ numTaps: 64, learningRate: 0.005 });
 *
 * @example
 * // Acoustic monitoring: 4-mic circular array, track moving source
 * let currentAngle = 0;
 * setInterval(() => {
 *   // Update beam direction every 100ms
 *   const bf = calculateBeamformerWeights(4, "circular", currentAngle);
 *   // ... update pipeline dynamically ...
 *   currentAngle += 10; // Rotate beam
 * }, 100);
 *
 * @example
 * // Underwater sonar: planar array with custom spacing
 * const bf = calculateBeamformerWeights(
 *   16,
 *   "planar",
 *   45.0,  // 45° elevation
 *   0.75   // 3λ/4 spacing for wideband signals
 * );
 *
 * @see GscPreprocessor
 * @see LmsFilter
 * @see RlsFilter
 */
export function calculateBeamformerWeights(
  numChannels: number,
  arrayGeometry: "linear" | "circular" | "planar",
  targetAngleDeg: number,
  elementSpacing: number = 0.5
): BeamformerWeightsResult {
  if (!Number.isInteger(numChannels) || numChannels < 2) {
    throw new RangeError(
      "calculateBeamformerWeights: numChannels must be >= 2"
    );
  }
  if (!["linear", "circular", "planar"].includes(arrayGeometry)) {
    throw new TypeError(
      "calculateBeamformerWeights: arrayGeometry must be 'linear', 'circular', or 'planar'"
    );
  }
  if (typeof targetAngleDeg !== "number" || !isFinite(targetAngleDeg)) {
    throw new TypeError(
      "calculateBeamformerWeights: targetAngleDeg must be a finite number"
    );
  }
  if (typeof elementSpacing !== "number" || elementSpacing <= 0) {
    throw new RangeError(
      "calculateBeamformerWeights: elementSpacing must be positive"
    );
  }

  return DspAddon.calculateBeamformerWeights(
    numChannels,
    arrayGeometry,
    targetAngleDeg,
    elementSpacing
  );
}

/**
 * Calculate Common Spatial Patterns (CSP) for binary classification in BCI/EEG.
 *
 * CSP finds spatial filters that maximize the variance of one class while minimizing
 * the variance of another class. This is the gold standard for motor imagery BCI.
 *
 * **How it works**:
 * 1. Computes covariance matrices for each class
 * 2. Solves generalized eigenvalue problem: Cov1·v = λ·Cov2·v
 * 3. Returns filters sorted by discriminability (highest eigenvalues first)
 * 4. Apply filters using MatrixTransformStage in real-time pipeline
 *
 * **Use Cases**:
 * - **Motor Imagery BCI**: Left hand vs right hand, foot vs rest
 * - **P300 Speller**: Target vs non-target ERP classification
 * - **SSVEP**: Different frequency responses (12 Hz vs 15 Hz stimulation)
 * - **Sleep Staging**: Delta waves (deep sleep) vs alpha waves (awake)
 *
 * **Best Practices**:
 * - Use 500+ samples per class (more data = more stable filters)
 * - Band-pass filter EEG first (e.g., 8-30 Hz for motor imagery)
 * - Select top 2-4 filters (most discriminative components)
 * - Validate on separate test set to avoid overfitting
 *
 * @param dataClass1 - Trials from class 1 (interleaved: samples × channels)
 * @param dataClass2 - Trials from class 2 (interleaved: samples × channels)
 * @param numChannels - Number of EEG channels
 * @param numFilters - Number of top filters to return (default: all channels)
 * @returns Object with cspMatrix, eigenvalues, mean, and metadata
 *
 * @throws {TypeError} If parameters are invalid
 * @throws {Error} If matrix dimensions don't match or decomposition fails
 *
 * @example
 * // Motor imagery BCI: Left hand vs right hand
 * // Training data: 100 trials per class, 8 channels, 500 samples per trial
 * const leftHandData = new Float32Array(100 * 8 * 500);  // Class 1
 * const rightHandData = new Float32Array(100 * 8 * 500); // Class 2
 * // ... collect and concatenate trials ...
 *
 * // Calculate CSP filters
 * const csp = calculateCommonSpatialPatterns(
 *   leftHandData,
 *   rightHandData,
 *   8,  // 8 EEG channels
 *   4   // Keep top 4 most discriminative filters
 * );
 *
 * console.log("Top eigenvalue (class separability):", csp.eigenvalues[0]);
 *
 * // Apply in real-time classification pipeline
 * const pipeline = createDspPipeline();
 * pipeline
 *   .BandpassFilter({ lowCutoff: 8, highCutoff: 30 })  // Motor imagery band
 *   .CspTransform({
 *     cspMatrix: csp.cspMatrix,
 *     mean: csp.mean,
 *     numChannels: 8,
 *     numFilters: 4
 *   })
 *   .MovingVariance({ mode: "moving", windowSize: 100 });
 * // Variance features → classifier
 *
 * @example
 * // P300 speller: Target vs non-target ERPs
 * const targetErps = new Float32Array(50 * 16 * 200);    // 50 target trials
 * const nonTargetErps = new Float32Array(200 * 16 * 200); // 200 non-target trials
 *
 * const csp = calculateCommonSpatialPatterns(
 *   targetErps,
 *   nonTargetErps,
 *   16, // 16 channels
 *   6   // Top 6 filters
 * );
 *
 * // Check if filters are discriminative
 * const ratio = csp.eigenvalues[0] / csp.eigenvalues[csp.numFilters - 1];
 * if (ratio > 10) {
 *   console.log("Excellent class separability!");
 * }
 *
 * @example
 * // SSVEP: 12 Hz vs 15 Hz stimulation
 * const data12Hz = new Float32Array(30 * 8 * 1000);  // 30 trials, 8 channels, 1s
 * const data15Hz = new Float32Array(30 * 8 * 1000);
 *
 * const csp = calculateCommonSpatialPatterns(data12Hz, data15Hz, 8, 2);
 *
 * // Apply filters and extract frequency power
 * pipeline
 *   .CspTransform({
 *     cspMatrix: csp.cspMatrix,
 *     mean: csp.mean,
 *     numChannels: 8,
 *     numFilters: 2
 *   })
 *   .fftAnalysis({ fftSize: 256 });
 * // Check power at 12 Hz vs 15 Hz
 *
 * @see MatrixTransformStage
 * @see CspTransform
 */
export function calculateCommonSpatialPatterns(
  dataClass1: Float32Array,
  dataClass2: Float32Array,
  numChannels: number,
  numFilters?: number
): CspResult {
  if (!(dataClass1 instanceof Float32Array)) {
    throw new TypeError(
      "calculateCommonSpatialPatterns: dataClass1 must be a Float32Array"
    );
  }
  if (!(dataClass2 instanceof Float32Array)) {
    throw new TypeError(
      "calculateCommonSpatialPatterns: dataClass2 must be a Float32Array"
    );
  }
  if (!Number.isInteger(numChannels) || numChannels <= 0) {
    throw new TypeError(
      "calculateCommonSpatialPatterns: numChannels must be a positive integer"
    );
  }
  if (
    numFilters !== undefined &&
    (!Number.isInteger(numFilters) ||
      numFilters <= 0 ||
      numFilters > numChannels)
  ) {
    throw new RangeError(
      "calculateCommonSpatialPatterns: numFilters must be in range [1, numChannels]"
    );
  }

  return DspAddon.calculateCommonSpatialPatterns(
    dataClass1,
    dataClass2,
    numChannels,
    numFilters
  );
}
