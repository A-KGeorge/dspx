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
            "   See: https://github.com/A-KGeorge/dsp_ts_redis#arm-platform-notice\n"
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
