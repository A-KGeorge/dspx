/**
 * Drift statistics for timing diagnostics
 */
export interface DriftStatistics {
  deltaMs: number;
  expectedMs: number;
  absoluteDrift: number;
  relativeDrift: number;
  sampleIndex: number;
  currentTimestamp: number;
  previousTimestamp: number;
}

/**
 * Options for processing data
 *
 * Two modes supported:
 * 1. Sample-based (legacy): Provide sampleRate, assumes fixed intervals
 * 2. Time-based (new): Omit sampleRate, timestamps are explicit or auto-generated
 */
export interface ProcessOptions {
  /**
   * Sample rate in Hz (legacy mode)
   * If provided, assumes fixed time intervals between samples
   * If omitted, uses explicit timestamps (time-based mode)
   */
  sampleRate?: number;

  /**
   * Number of channels in the signal (default: 1)
   */
  channels?: number;

  /**
   * Enable drift detection for timing diagnostics (default: false)
   * Only works with explicit timestamps
   */
  enableDriftDetection?: boolean;

  /**
   * Drift threshold percentage (0-100, default: 10%)
   * Only used when enableDriftDetection is true
   */
  driftThreshold?: number;

  /**
   * Callback when drift is detected
   * Only used when enableDriftDetection is true
   */
  onDriftDetected?: (stats: DriftStatistics) => void;
}

/**
 * Redis configuration for state persistence
 */
export interface RedisConfig {
  redisHost?: string;
  redisPort?: number;
  stateKey?: string;
}

/**
 * Parameters for adding a moving average stage
 *
 * Two windowing modes supported:
 * 1. Sample-based (legacy): windowSize in samples (requires sampleRate in process())
 * 2. Time-based (new): windowDuration in milliseconds (works with any sample rate)
 */
export interface MovingAverageParams {
  mode: "batch" | "moving";

  /**
   * Window size in samples (legacy, sample-based mode)
   * Required for "moving" mode when using sampleRate-based processing
   */
  windowSize?: number;

  /**
   * Window duration in milliseconds (time-based mode)
   * Required for "moving" mode when using time-based processing
   * Takes precedence over windowSize if both provided
   */
  windowDuration?: number;
}

/**
 * Parameters for adding a RMS stage
 *
 * Two windowing modes supported:
 * 1. Sample-based (legacy): windowSize in samples (requires sampleRate in process())
 * 2. Time-based (new): windowDuration in milliseconds (works with any sample rate)
 */
export interface RmsParams {
  mode: "batch" | "moving";

  /**
   * Window size in samples (legacy, sample-based mode)
   * Required for "moving" mode when using sampleRate-based processing
   */
  windowSize?: number;

  /**
   * Window duration in milliseconds (time-based mode)
   * Required for "moving" mode when using time-based processing
   * Takes precedence over windowSize if both provided
   */
  windowDuration?: number;
}

/**
 * Parameters for adding a rectify stage
 */
export interface RectifyParams {
  mode?: "full" | "half"; // Default: "full"
}

/**
 * Parameters for adding a variance stage
 *
 * Two windowing modes supported:
 * 1. Sample-based (legacy): windowSize in samples (requires sampleRate in process())
 * 2. Time-based (new): windowDuration in milliseconds (works with any sample rate)
 */
export interface VarianceParams {
  mode: "batch" | "moving";

  /**
   * Window size in samples (legacy, sample-based mode)
   * Required for "moving" mode when using sampleRate-based processing
   */
  windowSize?: number;

  /**
   * Window duration in milliseconds (time-based mode)
   * Required for "moving" mode when using time-based processing
   * Takes precedence over windowSize if both provided
   */
  windowDuration?: number;
}

/**
 * Parameters for adding a Z-Score Normalization stage
 *
 * Two windowing modes supported:
 * 1. Sample-based (legacy): windowSize in samples (requires sampleRate in process())
 * 2. Time-based (new): windowDuration in milliseconds (works with any sample rate)
 */
export interface ZScoreNormalizeParams {
  mode: "batch" | "moving";

  /**
   * Window size in samples (legacy, sample-based mode)
   * Required for "moving" mode when using sampleRate-based processing
   */
  windowSize?: number;

  /**
   * Window duration in milliseconds (time-based mode)
   * Required for "moving" mode when using time-based processing
   * Takes precedence over windowSize if both provided
   */
  windowDuration?: number;

  /**
   * Small value to prevent division by zero when standard deviation is 0.
   * @default 1e-6
   */
  epsilon?: number;
}

/**
 * Parameters for adding a Mean Absolute Value (MAV) stage
 *
 * Two windowing modes supported:
 * 1. Sample-based (legacy): windowSize in samples (requires sampleRate in process())
 * 2. Time-based (new): windowDuration in milliseconds (works with any sample rate)
 */
export interface MeanAbsoluteValueParams {
  mode: "batch" | "moving";

  /**
   * Window size in samples (legacy, sample-based mode)
   * Required for "moving" mode when using sampleRate-based processing
   */
  windowSize?: number;

  /**
   * Window duration in milliseconds (time-based mode)
   * Required for "moving" mode when using time-based processing
   * Takes precedence over windowSize if both provided
   */
  windowDuration?: number;
}

/**
 * Parameters for adding a Waveform Length stage
 * Computes the cumulative length of the signal path (sum of absolute differences)
 */
export interface WaveformLengthParams {
  /**
   * Window size in samples
   * Required parameter for waveform length calculation
   */
  windowSize: number;
}

/**
 * Parameters for adding a Slope Sign Change (SSC) stage
 * Counts frequency content by detecting sign changes in slope
 */
export interface SlopeSignChangeParams {
  /**
   * Window size in samples
   * Required parameter for SSC calculation
   */
  windowSize: number;

  /**
   * Threshold for noise suppression (default: 0.0)
   * Only count sign changes when |slope| exceeds this threshold
   */
  threshold?: number;
}

/**
 * Parameters for adding a Willison Amplitude (WAMP) stage
 * Counts the number of times consecutive samples differ by more than a threshold
 */
export interface WillisonAmplitudeParams {
  /**
   * Window size in samples
   * Required parameter for WAMP calculation
   */
  windowSize: number;

  /**
   * Threshold for difference detection (default: 0.0)
   * Only count differences exceeding this threshold
   */
  threshold?: number;
}

/**
 * Tap callback function for inspecting samples at any point in the pipeline
 * @param samples - Float32Array view of the current samples
 * @param stageName - Name of the pipeline stage
 */
export type TapCallback = (samples: Float32Array, stageName: string) => void;

/**
 * Log levels for pipeline callbacks
 * Extended levels: trace (most verbose) -> debug -> info -> warn -> error -> fatal (most critical)
 */
export type LogLevel = "trace" | "debug" | "info" | "warn" | "error" | "fatal";

/**
 * Log topics following Kafka-style hierarchical structure
 * Examples:
 * - pipeline.stage.moving-average.samples
 * - pipeline.stage.rms.performance
 * - pipeline.stage.rectify.error
 * - pipeline.debug
 * - pipeline.error
 */
export type LogTopic = string;

/**
 * Log priority levels (1-10)
 * Lower numbers = lower priority, higher numbers = higher priority
 *
 * Priority Guidelines:
 * - 1-3: Low priority (debug, verbose info)
 * - 4-6: Normal priority (standard info, warnings)
 * - 7-8: High priority (errors, important events)
 * - 9-10: Critical priority (alerts, system failures)
 */
export type LogPriority = 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10;

/**
 * Context information passed to logging callbacks
 */
export interface LogContext {
  stage?: string;
  timestamp?: number;
  [key: string]: any;
}

/**
 * A single log entry with timestamp and topic
 * Supports distributed tracing with optional trace/span/correlation IDs
 */
export interface LogEntry {
  topic?: LogTopic; // Optional: generated automatically by logging system
  level: LogLevel;
  message: string;
  context?: LogContext;
  timestamp: number;
  priority?: LogPriority; // Optional: defaults to 1 (lowest priority)

  // Distributed tracing fields (for Datadog, AWS X-Ray, Jaeger, etc.)
  traceId?: string; // Unique identifier for the entire trace
  spanId?: string; // Unique identifier for this span within the trace
  correlationId?: string; // Business-level correlation (e.g., request ID)
}

/**
 * Batch of samples with metadata for efficient callback processing
 */
export interface SampleBatch {
  stage: string;
  samples: Float32Array;
  startIndex: number;
  count: number;
}

/**
 * Pipeline callback functions for monitoring and observability
 *
 * PRODUCTION ARCHITECTURE PHILOSOPHY:
 * - Individual callbacks (onSample, onLog): ~6-7M samples/sec raw speed
 *   WARNING: BLOCKS event loop with millions of synchronous calls - NOT production-safe
 *
 * - Pooled callbacks (onBatch, onLogBatch): ~3-5M samples/sec sustained
 *   RECOMMENDED: Non-blocking, batched processing - RECOMMENDED for production
 *
 * Trade-off: Pooled callbacks sacrifice raw speed for guaranteed non-blocking behavior.
 * This aligns with industry telemetry patterns (Kafka producers, Loki agents, OTLP exporters).
 */
export interface PipelineCallbacks {
  /**
   * Called for each sample after processing (use sparingly for performance)
   * WARNING: Blocks event loop with millions of synchronous calls per second
   * Raw performance: ~6-7M samples/sec, but NOT recommended for production servers
   * Consider using onBatch instead for non-blocking, production-safe processing
   * @param value - The processed sample value
   * @param index - Sample index in the buffer
   * @param stage - Name of the current stage
   */
  onSample?: (value: number, index: number, stage: string) => void;

  /**
   * Called with batches of processed samples (RECOMMENDED for production)
   * Production-safe: Non-blocking, batched processing (~3-5M samples/sec sustained)
   * Samples are provided as a view into the result buffer
   * @param batch - Contains stage name, sample data, start index, and count
   */
  onBatch?: (batch: SampleBatch) => void;

  /**
   * Called after each stage completes processing
   * @param stage - Name of the completed stage
   * @param durationMs - Processing time in milliseconds
   */
  onStageComplete?: (stage: string, durationMs: number) => void;

  /**
   * Called when an error occurs in a stage
   * @param stage - Name of the stage where error occurred
   * @param error - The error object
   */
  onError?: (stage: string, error: Error) => void;

  /**
   * Called for each logging event during pipeline execution
   * WARNING: Blocks event loop with frequent synchronous calls
   * Raw performance: ~6M samples/sec, but NOT recommended for production servers
   * Consider using onLogBatch for non-blocking, production-safe logging (~3M samples/sec)
   * @param topic - Kafka-style hierarchical topic (e.g., "pipeline.stage.rms.error")
   * @param level - Log severity level
   * @param message - Log message
   * @param context - Additional context information
   */
  onLog?: (
    topic: LogTopic,
    level: LogLevel,
    message: string,
    context?: LogContext
  ) => void;

  /**
   * Called with batched log messages (RECOMMENDED for production)
   * Production-safe: Non-blocking, batched logging with fixed-size circular buffer
   * Logs are pooled and flushed at the end of each process() call
   * Provides stable ~3M samples/sec throughput without blocking event loop
   *
   * Topic-based filtering examples:
   * - Filter by pattern: logs.filter(l => l.topic.startsWith('pipeline.stage.'))
   * - Subscribe to errors: logs.filter(l => l.topic.endsWith('.error'))
   * - Route by topic: Route errors to alerting, metrics to monitoring
   *
   * @param logs - Array of log entries with topics and timestamps
   */
  onLogBatch?: (logs: LogEntry[]) => void;

  /**
   * Topic filter for selective log subscription (optional)
   * If provided, only logs matching the topic pattern will be delivered
   * Supports wildcards: 'pipeline.stage.*', 'pipeline.*.error'
   * If omitted, all logs are delivered
   */
  topicFilter?: string | string[];
}

/**
 * Summary information for a single pipeline stage
 */
export interface StageSummary {
  /** Stage index in the pipeline */
  index: number;
  /** Stage type (e.g., 'movingAverage', 'rms', 'rectify') */
  type: string;
  /** Window size for stateful filters in samples (legacy, if applicable) */
  windowSize?: number;
  /** Window duration for stateful filters in milliseconds (time-based, if applicable) */
  windowDuration?: number;
  /** Number of channels (if applicable) */
  numChannels?: number;
  /** Rectification mode for rectify stage (if applicable) */
  mode?: "full" | "half";
  /** Buffer size for stateful filters (if applicable) */
  bufferSize?: number;
  /** Number of channels with state (if applicable) */
  channelCount?: number;
}

/**
 * Pipeline state summary (lightweight view without full buffer data)
 * Useful for debugging and monitoring pipeline structure
 */
export interface PipelineStateSummary {
  /** Total number of stages in the pipeline */
  stageCount: number;
  /** Timestamp when the summary was generated */
  timestamp: number;
  /** Array of stage summaries */
  stages: StageSummary[];
}

/**
 * Hjorth parameters - measures of signal complexity
 */
export interface HjorthParameters {
  /** Activity: Variance of the signal */
  activity: number;
  /** Mobility: Square root of (variance of first derivative / variance) */
  mobility: number;
  /** Complexity: Mobility of first derivative / Mobility of signal */
  complexity: number;
}

/**
 * Parameters for Hjorth parameters calculation
 */
export interface HjorthParams {
  mode: "batch" | "moving";
  /**
   * Window size in samples (required for moving mode)
   */
  windowSize?: number;
  /**
   * Window duration in milliseconds (alternative to windowSize)
   */
  windowDuration?: number;
}

/**
 * Parameters for decimation (downsampling)
 */
export interface DecimateParams {
  /** Decimation factor M (output rate = input rate / M) */
  factor: number;
  /** Input sample rate in Hz */
  sampleRate: number;
  /** FIR filter order for anti-aliasing (default: 51, must be odd) */
  order?: number;
}

/**
 * Parameters for interpolation (upsampling)
 */
export interface InterpolateParams {
  /** Interpolation factor L (output rate = input rate * L) */
  factor: number;
  /** Input sample rate in Hz */
  sampleRate: number;
  /** FIR filter order for anti-imaging (default: 51, must be odd) */
  order?: number;
}

/**
 * Parameters for resampling (rational rate conversion)
 */
export interface ResampleParams {
  /** Interpolation factor L */
  upFactor: number;
  /** Decimation factor M (new rate = old rate * L / M) */
  downFactor: number;
  /** Input sample rate in Hz */
  sampleRate: number;
  /** FIR filter order (default: 51, must be odd) */
  order?: number;
}

/**
 * Parameters for adding a Convolution stage
 * Applies a custom 1D kernel to the signal
 */
export interface ConvolutionParams {
  /**
   * The 1D convolution kernel (filter coefficients)
   * Must be a 1D array or Float32Array
   */
  kernel: Float32Array | number[];

  /**
   * Processing mode
   * - 'moving': (Default) Stateful, streaming convolution with state persistence
   * - 'batch': Stateless, convolves each chunk independently
   */
  mode?: "moving" | "batch";

  /**
   * Convolution method selection
   * - 'auto': (Default) Smart selection between direct and FFT based on kernel size
   * - 'direct': Force time-domain O(N*M) convolution (fast for small kernels)
   * - 'fft': Force frequency-domain O(N*logN) convolution (fast for large kernels)
   */
  method?: "auto" | "direct" | "fft";

  /**
   * Kernel size threshold for auto mode (default: 64)
   * Kernels smaller than this use direct convolution, larger use FFT
   */
  autoThreshold?: number;
}

/**
 * Spectral features extracted from FFT
 */
export interface SpectralFeatures {
  /** Spectral centroid: center of mass of spectrum (Hz) */
  centroid: number;
  /** Spectral rolloff: frequency below which X% of energy is contained (Hz) */
  rolloff: number;
  /** Spectral flux: change in spectrum from previous frame */
  flux: number;
}

/**
 * Parameters for spectral feature extraction
 */
export interface SpectralFeaturesParams {
  /** FFT size (should be power of 2, default: 2048) */
  fftSize?: number;
  /** Rolloff percentage (0-100, default: 85) */
  rolloffPercentage?: number;
}

/**
 * Parameters for Shannon entropy calculation
 */
export interface EntropyParams {
  mode: "batch" | "moving";
  /**
   * Window size in samples (required for moving mode)
   */
  windowSize?: number;
  /**
   * Window duration in milliseconds (alternative to windowSize)
   */
  windowDuration?: number;
  /**
   * Number of bins for histogram (default: 256)
   */
  numBins?: number;
}

/**
 * Parameters for Sample Entropy (SampEn)
 */
export interface SampleEntropyParams {
  mode: "batch" | "moving";
  /**
   * Window size in samples (required for moving mode)
   */
  windowSize?: number;
  /**
   * Window duration in milliseconds (alternative to windowSize)
   */
  windowDuration?: number;
  /**
   * Pattern length (default: 2)
   */
  m?: number;
  /**
   * Tolerance for matching (default: 0.2 * std deviation)
   */
  r?: number;
}

/**
 * Parameters for Approximate Entropy (ApEn)
 */
export interface ApproximateEntropyParams {
  mode: "batch" | "moving";
  /**
   * Window size in samples (required for moving mode)
   */
  windowSize?: number;
  /**
   * Window duration in milliseconds (alternative to windowSize)
   */
  windowDuration?: number;
  /**
   * Pattern length (default: 2)
   */
  m?: number;
  /**
   * Tolerance for matching (default: 0.2 * std deviation)
   */
  r?: number;
}
