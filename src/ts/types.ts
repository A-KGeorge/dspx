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
 * Parameters for Linear Regression stage
 * Performs least squares linear regression over a sliding window
 * Policy-based design with 4 output modes
 */
export interface LinearRegressionParams {
  /**
   * Window size in samples
   * Required parameter for regression calculation
   */
  windowSize: number;

  /**
   * Output mode determining what the stage produces
   * - 'slope': Outputs the slope (trend) of the regression line
   * - 'intercept': Outputs the intercept (baseline) value
   * - 'residuals': Outputs detrended signal (original - fitted values)
   * - 'predictions': Outputs the fitted values from regression
   */
  output: "slope" | "intercept" | "residuals" | "predictions";
}

/**
 * Parameters for Adaptive LMS Filter stage
 *
 * **IMPORTANT**: This stage REQUIRES exactly 2 channels:
 * - Channel 0: Primary signal x[n] (the signal to be processed)
 * - Channel 1: Desired/reference signal d[n] (the target or noise reference)
 *
 * The stage outputs the error signal e[n] = d[n] - y[n], which for noise cancellation
 * represents the cleaned signal with the adaptive filter having removed the correlated noise.
 *
 * @example
 * ```typescript
 * // Noise cancellation example
 * const pipeline = createDspPipeline();
 * pipeline.LmsFilter({ numTaps: 32, learningRate: 0.01 });
 *
 * // Create 2-channel interleaved buffer: [x[0], d[0], x[1], d[1], ...]
 * const interleaved = new Float32Array(2000); // 1000 samples × 2 channels
 * for (let i = 0; i < 1000; i++) {
 *   interleaved[i * 2 + 0] = noisySignal[i];      // Channel 0: noisy signal
 *   interleaved[i * 2 + 1] = noiseReference[i];   // Channel 1: noise reference
 * }
 *
 * const cleaned = await pipeline.process(interleaved, { channels: 2, sampleRate: 8000 });
 * ```
 */
export interface LmsFilterParams {
  /**
   * Number of filter taps (filter order)
   * Higher values allow modeling more complex systems but increase computation
   * Typical values: 8-128 depending on application
   */
  numTaps: number;

  /**
   * Learning rate (mu) for weight updates
   * Controls adaptation speed vs stability tradeoff
   * - Too high: fast adaptation but unstable (may diverge)
   * - Too low: slow adaptation but stable convergence
   * Typical range: 0.001 to 0.1
   * Alias: 'mu' is also accepted
   */
  learningRate?: number;

  /**
   * Alias for learningRate (commonly used in DSP literature)
   */
  mu?: number;

  /**
   * Use Normalized LMS (NLMS) algorithm
   * NLMS normalizes by input power, providing more stable convergence
   * with varying signal amplitudes
   * Default: false (standard LMS)
   */
  normalized?: boolean;

  /**
   * Regularization parameter for leaky LMS
   * Prevents weight explosion by adding small decay term
   * Range: [0, 1), typically 0.0001 to 0.01
   * Default: 0.0 (no regularization)
   */
  lambda?: number;
}

/**
 * Parameters for RLS (Recursive Least Squares) adaptive filter
 *
 * RLS provides faster convergence than LMS/NLMS at the cost of O(N^2) complexity.
 * Maintains an N×N inverse covariance matrix for optimal weight updates.
 *
 * Requires exactly 2 channels:
 * - Channel 0: Primary signal x[n]
 * - Channel 1: Desired/reference signal d[n]
 *
 * Output: Error signal e[n] = d[n] - y[n]
 *
 * Example:
 * ```typescript
 * // System identification with faster convergence than LMS
 * pipeline.RlsFilter({ numTaps: 32, lambda: 0.99 });
 * ```
 */
export interface RlsFilterParams {
  /**
   * Number of filter taps (filter order)
   * Higher values model more complex systems but require O(N^2) computation
   * Typical values: 8-64 (avoid very large values due to computational cost)
   */
  numTaps: number;

  /**
   * Forgetting factor (0 < λ ≤ 1)
   * Controls how much weight is given to past vs. recent data
   * - Higher values (0.999): Long memory, slower adaptation to changes
   * - Lower values (0.95): Short memory, faster tracking of time-varying systems
   * Typical range: 0.98 to 0.9999
   * Default: none (required parameter)
   */
  lambda: number;

  /**
   * Regularization parameter for P matrix initialization
   * Controls initial uncertainty: P(0) = δ * I
   * - Larger values (1.0): More initial uncertainty, faster initial convergence
   * - Smaller values (0.01): Less initial uncertainty, slower startup
   * Typical range: 0.01 to 1.0
   * Default: 0.01
   */
  delta?: number;
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
 * Valid wavelet types for discrete wavelet transform
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
 * Parameters for wavelet transform stage
 */
export interface WaveletTransformParams {
  /**
   * Wavelet type to use for decomposition
   * - Haar / db1: Simplest, 2 coefficients
   * - db2-db10: Daubechies wavelets with increasing smoothness
   */
  wavelet: WaveletType;
}

/**
 * Parameters for Hilbert envelope stage
 */
export interface HilbertEnvelopeParams {
  /**
   * FFT window size for Hilbert transform
   * Must be a positive integer
   */
  windowSize: number;

  /**
   * Hop size (stride) between windows
   * Must be between 1 and windowSize
   * Default: windowSize / 2 (50% overlap)
   */
  hopSize?: number;
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

/**
 * Result from PCA (Principal Component Analysis) calculation
 *
 * PCA finds orthogonal directions of maximum variance in the data.
 * Use for dimensionality reduction, feature extraction, noise reduction.
 */
export interface PcaResult {
  /** Channel mean vector (size: numChannels) */
  mean: Float32Array;

  /** PCA transformation matrix (size: numChannels  numChannels, column-major) */
  pcaMatrix: Float32Array;

  /** Eigenvalues sorted descending (size: numChannels) */
  eigenvalues: Float32Array;

  /** Explained variance ratio for each component (size: numChannels) */
  explainedVariance: Float32Array;

  /** Number of input channels */
  numChannels: number;

  /** Number of output components (same as numChannels for full PCA) */
  numComponents: number;
}

/**
 * Result from Whitening transformation calculation
 *
 * Whitening transforms data to have identity covariance matrix.
 * Use for preprocessing before ICA, decorrelation, normalization.
 */
export interface WhiteningResult {
  /** Channel mean vector (size: numChannels) */
  mean: Float32Array;

  /** Whitening transformation matrix (size: numChannels  numChannels, column-major) */
  whiteningMatrix: Float32Array;

  /** Number of input channels */
  numChannels: number;

  /** Number of output components (same as numChannels) */
  numComponents: number;

  /** Regularization parameter used */
  regularization: number;
}

/**
 * Result from ICA (Independent Component Analysis) calculation
 *
 * ICA finds statistically independent components from mixed signals.
 * Use for blind source separation, artifact removal, signal decomposition.
 */
export interface IcaResult {
  /** Channel mean vector (size: numChannels) */
  mean: Float32Array;

  /** ICA unmixing matrix (size: numChannels  numChannels, column-major) */
  icaMatrix: Float32Array;

  /** Number of input channels */
  numChannels: number;

  /** Number of output components (same as numChannels) */
  numComponents: number;

  /** Whether FastICA algorithm converged */
  converged: boolean;

  /** Number of iterations performed */
  iterations: number;
}

/**
 * Parameters for PCA Transform stage (applies pre-trained PCA)
 */
export interface PcaTransformParams {
  /** Pre-trained PCA matrix from calculatePca() */
  pcaMatrix: Float32Array;

  /** Mean vector from calculatePca() */
  mean: Float32Array;

  /** Number of input channels */
  numChannels: number;

  /** Number of output components ( numChannels for dimensionality reduction) */
  numComponents: number;
}

/**
 * Parameters for ICA Transform stage (applies pre-trained ICA)
 */
export interface IcaTransformParams {
  /** Pre-trained ICA matrix from calculateIca() */
  icaMatrix: Float32Array;

  /** Mean vector from calculateIca() */
  mean: Float32Array;

  /** Number of input channels */
  numChannels: number;

  /** Number of output components */
  numComponents: number;
}

/**
 * Parameters for Whitening Transform stage (applies pre-trained whitening)
 */
export interface WhiteningTransformParams {
  /** Pre-trained whitening matrix from calculateWhitening() */
  whiteningMatrix: Float32Array;

  /** Mean vector from calculateWhitening() */
  mean: Float32Array;

  /** Number of input channels */
  numChannels: number;

  /** Number of output components */
  numComponents: number;
}

/**
 * Result from beamformer weights calculation
 *
 * Provides steering weights and blocking matrix for Generalized Sidelobe Canceler (GSC).
 * Use with GscPreprocessor + LmsFilter/RlsFilter for adaptive beamforming.
 */
export interface BeamformerWeightsResult {
  /** Steering weights for delay-and-sum beamforming (size: numChannels) */
  steeringWeights: Float32Array;

  /** Blocking matrix for noise reference creation (size: numChannels × (numChannels-1), column-major) */
  blockingMatrix: Float32Array;

  /** Number of microphone channels */
  numChannels: number;

  /** Array geometry used */
  geometry: string;

  /** Target angle in degrees */
  targetAngleDeg: number;
}

/**
 * Result from Common Spatial Patterns (CSP) calculation
 *
 * CSP finds spatial filters that maximize class separability for BCI/EEG classification.
 * Use for motor imagery, P300, SSVEP, and other paradigms.
 */
export interface CspResult {
  /** CSP spatial filter matrix (size: numChannels × numFilters, column-major) */
  cspMatrix: Float32Array;

  /** Eigenvalues corresponding to filters (size: numFilters, sorted descending) */
  eigenvalues: Float32Array;

  /** Channel mean vector (size: numChannels) */
  mean: Float32Array;

  /** Number of input channels */
  numChannels: number;

  /** Number of output filters */
  numFilters: number;
}

/**
 * Parameters for GSC Preprocessor stage (adaptive beamforming)
 */
export interface GscPreprocessorParams {
  /** Number of input channels (microphones) */
  numChannels: number;

  /** Steering weights from calculateBeamformerWeights() */
  steeringWeights: Float32Array;

  /** Blocking matrix from calculateBeamformerWeights() */
  blockingMatrix: Float32Array;
}

/**
 * Parameters for Channel Selector stage (extract specific channels)
 */
export interface ChannelSelectorParams {
  /** Number of input channels */
  numInputChannels: number;

  /** Number of output channels to extract (keeps first N channels) */
  numOutputChannels: number;
}

/**
 * Parameters for Channel Select stage (select channels by indices)
 */
export interface ChannelSelectParams {
  /** Array of channel indices to select (0-based)
   * Can select, reorder, or duplicate channels.
   * Example: [0, 3, 7] selects channels 0, 3, 7 from input
   * Example: [1, 0] swaps stereo channels
   * Example: [0, 0] duplicates channel 0 to create stereo from mono
   */
  channels: number[];

  /** Number of input channels (for validation) */
  numInputChannels: number;
}

/**
 * Parameters for Channel Merge stage (merge/duplicate channels)
 */
export interface ChannelMergeParams {
  /** Mapping of input channels to output channels.
   * Each element specifies which input channel goes to that output position.
   * Length determines output channel count.
   * Example: [0, 0] duplicates channel 0 (mono to stereo)
   * Example: [0, 1, 2] keeps 3 channels as-is
   * Example: [0, 0, 1, 1] duplicates channels 0 and 1
   */
  mapping: number[];

  /** Number of input channels (for validation) */
  numInputChannels: number;
}

/**
 * Parameters for Clip Detection stage
 */
export interface ClipDetectionParams {
  /** Absolute amplitude threshold for clipping detection.
   * Samples with |value| >= threshold are marked as clipped (output: 1.0).
   * Example: 0.95 detects clipping at 95% of full scale.
   */
  threshold: number;
}

/**
 * Parameters for Peak Detection stage
 */
export interface PeakDetectionParams {
  /** Minimum amplitude threshold for peak detection.
   * Only local maxima above this threshold are detected.
   * Example: 0.5 detects peaks above 50% amplitude.
   */
  threshold: number;
  /**
   * Processing mode
   * - 'batch': Stateless, processes each chunk independently
   * - 'moving': Stateful, maintains state across chunks for continuity
   * Default: 'batch'
   */
  mode?: "batch" | "moving";

  /**
   * Domain for peak detection
   * - 'time': Detect peaks in time domain signal (default)
   * - 'frequency': Detect peaks in frequency domain magnitude spectrum
   * Default: 'time'
   */
  domain?: "time" | "frequency";

  /**
   * Window size in samples for 'moving' mode
   * Required for 'moving' mode in time domain
   */
  windowSize?: number;

  /**
   * Minimum distance between peaks in samples (default: 1, optional)
   * Prevents detecting multiple peaks that are too close together.
   * Example: 100 ensures at least 100 samples between detected peaks.
   *
   */
  minPeakDistance?: number;
}

/**
 * Parameters for Integrator stage (leaky integrator using IIR filter)
 */
export interface IntegratorParams {
  /** Leakage coefficient (0 < α <= 1). Default 0.99.
   * - α = 1.0: Perfect integration (no leakage, DC gain = ∞)
   * - α = 0.99: Slight leakage (DC gain ≈ 100)
   * - α = 0.9: More leakage (DC gain = 10)
   *
   * Formula: y[n] = x[n] + α * y[n-1]
   *
   * Use cases:
   * - Accelerometer → velocity: alpha = 0.99
   * - Low-pass smoothing: alpha = 0.9-0.99
   * - Envelope detection: alpha = 0.95
   */
  alpha?: number;
}

/**
 * Parameters for SNR (Signal-to-Noise Ratio) stage.
 *
 * Requires exactly 2 input channels:
 * - Channel 0: Signal (clean or signal+noise)
 * - Channel 1: Noise reference
 *
 * Outputs single channel containing SNR in dB:
 * SNR_dB = 10 * log10(signal_power / noise_power)
 *
 * Uses dual RMS filters with specified window size to compute
 * running power estimates. Output is clamped to [-100, 100] dB.
 *
 * @example
 * ```typescript
 * // Audio quality monitoring (100ms window at 16kHz)
 * processor.Snr({ windowSize: 1600 })
 *   .process(twoChannelAudio, 16000, 2);
 *
 * // Speech enhancement validation (50ms window at 8kHz)
 * processor.Snr({ windowSize: 400 })
 *   .process(speechWithNoise, 8000, 2);
 * ```
 */
export interface SnrParams {
  /**
   * Window size in samples for RMS computation.
   * Larger windows provide smoother SNR estimates.
   *
   * Typical values:
   * - 100-500 samples: Fast response for dynamic signals
   * - 500-2000 samples: Balanced smoothing
   * - 2000+ samples: Very smooth, slow-changing SNR
   */
  windowSize: number;
}

/**
 * Parameters for CSP Transform stage (applies pre-trained CSP filters)
 */
export interface CspTransformParams {
  /** Pre-trained CSP matrix from calculateCommonSpatialPatterns() */
  cspMatrix: Float32Array;

  /** Mean vector from calculateCommonSpatialPatterns() */
  mean: Float32Array;

  /** Number of input channels */
  numChannels: number;

  /** Number of output filters/components */
  numFilters: number;
}

/**
 * Options for detrend utility function
 */
export interface DetrendOptions {
  /**
   * Type of detrending to apply:
   * - "linear": Remove linear trend (y = mx + b) using least-squares regression
   * - "constant": Remove mean only (simpler, faster)
   *
   * **Default**: "linear"
   *
   * **Use Cases:**
   * - "linear": For signals with drift or gradual baseline changes (EEG, ECG, sensors)
   * - "constant": For removing DC offset when signal has no trend
   *
   * **Example:**
   * ```typescript
   * // Remove linear drift from sensor data
   * const detrended = DspUtils.detrend(signal, { type: "linear" });
   *
   * // Remove DC offset (faster)
   * const centered = DspUtils.detrend(signal, { type: "constant" });
   * ```
   */
  type?: "linear" | "constant";
}

/**
 * Options for autocorrelation computation
 *
 * Autocorrelation measures the similarity of a signal with a delayed version of itself.
 * It's computed efficiently using FFT: autocorr(x) = IFFT(|FFT(x)|²)
 *
 * **Applications:**
 * - **Pitch detection**: Find fundamental frequency in speech/audio
 * - **Periodicity analysis**: Detect repeating patterns
 * - **Echo detection**: Identify time delays in reflected signals
 * - **Spectral estimation**: Power spectral density via Wiener-Khinchin theorem
 *
 * **Example:**
 * ```typescript
 * // Detect pitch in audio signal
 * const autocorr = DspUtils.autocorrelation(audioSignal);
 *
 * // Find first peak after zero lag (fundamental period)
 * let maxLag = 0;
 * let maxVal = 0;
 * for (let lag = 1; lag < autocorr.length / 2; lag++) {
 *   if (autocorr[lag] > maxVal) {
 *     maxVal = autocorr[lag];
 *     maxLag = lag;
 *   }
 * }
 * const fundamentalFreq = sampleRate / maxLag;
 * ```
 *
 * **Normalization:**
 * The result is NOT normalized. To normalize, divide by `autocorr[0]`
 * (the zero-lag value, equal to signal energy).
 */
export interface AutocorrelationOptions {
  // Currently no options, but interface reserved for future extensions
  // (e.g., normalization, window functions, max lag)
}

/**
 * Options for cross-correlation computation.
 *
 * Cross-correlation measures the similarity between two signals as a function of time lag.
 * The result xcorr[k] represents the correlation when signal y is shifted by k samples
 * relative to signal x.
 *
 * **Applications:**
 *
 * 1. **Time Delay Estimation**: Find the lag where two signals are most similar
 *    - Acoustic echo cancellation (finding microphone-to-speaker delay)
 *    - Seismic analysis (finding arrival time differences)
 *    - Radar/sonar (measuring round-trip time)
 *
 * 2. **Pattern Matching**: Find where a template signal appears in a larger signal
 *    - Template matching in signal processing
 *    - Finding repeated patterns or motifs
 *    - Detecting known signatures in noisy data
 *
 * 3. **Signal Alignment**: Synchronize two related signals
 *    - Multi-sensor data fusion
 *    - Aligning audio tracks
 *    - Synchronizing video and audio streams
 *
 * **Example - Time Delay Estimation:**
 * ```typescript
 * // Find delay between microphone and reference signal
 * const reference = new Float32Array([...]);  // Clean reference
 * const measured = new Float32Array([...]);   // Delayed + noisy measurement
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
 * console.log(`Detected delay: ${delay} samples`);
 * ```
 *
 * **Note:** The result is not normalized. To get normalized cross-correlation (range [-1, 1]),
 * divide by sqrt(energy(x) * energy(y)) where energy = sum of squares.
 */
export interface CrossCorrelationOptions {
  // Currently no options, but interface reserved for future extensions
  // (e.g., normalization, mode selection, max lag)
}

/**
 * Parameters for FFT processing in pipeline
 */
export interface fftParams {
  /**
   * Processing mode
   * - 'batch': Stateless FFT (entire input → single FFT output)
   * - 'moving': Stateful sliding window FFT (overlapping windows, STFT-style)
   * Default: 'batch'
   */
  mode?: "batch" | "moving";

  /** FFT size (must be power of 2 for FFT, any size for DFT) */
  size: number;

  /**
   * Hop size (stride) between windows in 'moving' mode
   * - Must be positive integer <= size
   * - Default: size/2 (50% overlap)
   * - Only used in 'moving' mode
   */
  hopSize?: number;

  /**
   * Transform type
   * - 'fft': Fast Fourier Transform (O(N log N), requires power of 2)
   * - 'dft': Discrete Fourier Transform (O(N²), works with any size)
   * - 'rfft': Real FFT (O(N log N), for real-valued signals)
   * - 'rdft': Real DFT (O(N²), for real-valued signals)
   */
  type?: "fft" | "dft" | "rfft" | "rdft";

  /**
   * Forward or inverse transform
   * - true: Forward transform (time -> frequency)
   * - false: Inverse transform (frequency -> time)
   */
  forward?: boolean;

  /**
   * Output format
   * - 'complex': Return {real, imag} arrays
   * - 'magnitude': Return magnitude spectrum
   * - 'power': Return power spectrum (magnitude squared)
   * - 'phase': Return phase spectrum
   */
  output?: "complex" | "magnitude" | "power" | "phase";
}

/**
 * Parameters for STFT (Short-Time Fourier Transform) processing in pipeline
 *
 * STFT computes the Fourier transform over sliding windows, producing a time-frequency
 * representation (spectrogram). Useful for analyzing non-stationary signals where
 * frequency content changes over time.
 *
 * **Window Size vs Hop Size Trade-offs:**
 * - Larger window: Better frequency resolution, worse time resolution
 * - Smaller window: Better time resolution, worse frequency resolution
 * - Larger hop: Faster processing, less overlap
 * - Smaller hop: Smoother spectrogram, more overlap
 *
 * **Common Overlap Settings:**
 * - 50% overlap: hopSize = windowSize / 2 (standard)
 * - 75% overlap: hopSize = windowSize / 4 (high quality)
 * - No overlap: hopSize = windowSize (fast, blocky)
 */
export interface stftParams {
  /**
   * Window size (FFT size) in samples
   * - For FFT/RFFT: Must be power of 2 (e.g., 256, 512, 1024, 2048)
   * - For DFT/RDFT: Can be any positive integer
   *
   * Typical values:
   * - Audio analysis: 1024-4096 samples
   * - Speech: 256-512 samples
   * - Vibration: 512-2048 samples
   */
  windowSize: number;

  /**
   * Hop size (stride) in samples between consecutive windows
   *
   * Default: windowSize / 2 (50% overlap)
   *
   * Examples:
   * - windowSize=1024, hopSize=512 → 50% overlap
   * - windowSize=1024, hopSize=256 → 75% overlap
   * - windowSize=1024, hopSize=1024 → No overlap
   */
  hopSize?: number;

  /**
   * Transform method
   * - 'fft': Fast transform (O(N log N), requires power-of-2 windowSize)
   * - 'dft': Direct transform (O(N²), works with any windowSize)
   *
   * Default: 'fft' if windowSize is power of 2, else 'dft'
   */
  method?: "fft" | "dft";

  /**
   * Input signal type
   * - 'real': Real-valued input (uses RFFT/RDFT, outputs N/2+1 bins)
   * - 'complex': Complex-valued input (uses FFT/DFT, outputs N bins)
   *
   * Default: 'real' (most common for audio/sensor data)
   */
  type?: "real" | "complex";

  /**
   * Transform direction
   * - true: Forward STFT (time → time-frequency)
   * - false: Inverse STFT (time-frequency → time)
   *
   * Default: true (forward)
   */
  forward?: boolean;

  /**
   * Output format for each time window
   * - 'complex': Complex spectrum {real, imag}
   * - 'magnitude': Magnitude spectrum |X[k]|
   * - 'power': Power spectrum |X[k]|²
   * - 'phase': Phase spectrum ∠X[k]
   *
   * Default: 'magnitude'
   *
   * Output shape: [numWindows, numFreqBins] flattened
   */
  output?: "complex" | "magnitude" | "power" | "phase";

  /**
   * Window function to reduce spectral leakage
   * - 'none': Rectangular window (no tapering)
   * - 'hann': Hann window (smooth, general purpose)
   * - 'hamming': Hamming window (better frequency resolution)
   * - 'blackman': Blackman window (best sidelobe suppression)
   * - 'bartlett': Triangular window
   *
   * Default: 'hann'
   */
  window?: "none" | "hann" | "hamming" | "blackman" | "bartlett";
}

/**
 * Parameters for Mel Spectrogram processing in pipeline
 *
 * Converts power spectrum to Mel-scale representation using filterbank matrix multiplication.
 * The Mel scale is a perceptual scale of pitches judged by listeners to be equal in distance.
 *
 * **Pipeline Position:**
 * Typically used after STFT + Power: STFT → Power → MelSpectrogram → Log → MFCC
 *
 * **Filterbank Creation:**
 * The filterbank matrix must be pre-computed in TypeScript using a helper function
 * that creates triangular filters distributed along the Mel scale.
 *
 * **What it does:**
 * Applies matrix multiplication: mel_energies = filterbank × power_spectrum
 * This groups frequency bins into perceptually-meaningful Mel bands.
 */
export interface MelSpectrogramParams {
  /**
   * Pre-computed Mel filterbank matrix (numMelBands × numBins)
   *
   * This matrix contains triangular filters that convert linear frequency bins
   * to Mel-scale bands. Must be pre-computed using a Mel filterbank generator.
   *
   * Shape: Float32Array of length (numMelBands * numBins), row-major order
   */
  filterbankMatrix: Float32Array;

  /**
   * Number of input frequency bins (from STFT/FFT)
   *
   * For real signals: numBins = fftSize / 2 + 1
   * For complex signals: numBins = fftSize
   *
   * Example: FFT size 512 → 257 bins (real)
   */
  numBins: number;

  /**
   * Number of output Mel frequency bands
   *
   * Common values:
   * - Speech: 20-40 bands
   * - Music: 40-128 bands
   * - MFCC: 20-40 bands (13-20 MFCCs extracted from these)
   *
   * More bands = finer frequency resolution but higher computational cost
   */
  numMelBands: number;
}

/**
 * Parameters for MFCC (Mel-Frequency Cepstral Coefficients) processing in pipeline
 *
 * Applies Discrete Cosine Transform (DCT) to log Mel energies to produce MFCCs.
 * MFCCs are widely used in speech recognition, audio classification, and speaker
 * identification because they:
 * - Decorrelate Mel energies
 * - Compress information into lower-order coefficients
 * - Mimic human auditory perception
 * - Provide compact representation suitable for ML models
 *
 * **Pipeline Position:**
 * Final stage: STFT → Power → MelSpectrogram → Log → MFCC
 *
 * **Typical Output:**
 * 13-20 MFCC coefficients per frame, representing the spectral envelope shape.
 * Often the first coefficient (C0) represents energy and may be discarded.
 */
export interface MfccParams {
  /**
   * Number of input Mel frequency bands (from MelSpectrogram)
   *
   * Must match the numMelBands from the preceding MelSpectrogram stage.
   *
   * Typical values: 20-40 bands
   */
  numMelBands: number;

  /**
   * Number of MFCC coefficients to output
   *
   * Common values:
   * - Speech recognition: 13 coefficients
   * - Speaker recognition: 20 coefficients
   * - Music: 13-20 coefficients
   *
   * Must be ≤ numMelBands. Lower coefficients capture envelope,
   * higher coefficients capture fine spectral detail.
   *
   * Default: 13
   */
  numCoefficients?: number;

  /**
   * Apply logarithm to input energies before DCT
   *
   * - true: Apply log (standard for MFCCs, matches human perception)
   * - false: Skip log (use when input is already in log domain)
   *
   * Default: true
   *
   * The log compression mimics human hearing's logarithmic loudness perception.
   */
  useLogEnergy?: boolean;

  /**
   * Cepstral liftering coefficient
   *
   * Liftering weights lower-order coefficients more than higher-order ones,
   * improving recognition performance by emphasizing the spectral envelope.
   *
   * - 0: No liftering (disabled)
   * - 22: Common value for speech (HTK standard)
   * - Higher values: Stronger emphasis on lower coefficients
   *
   * Default: 0 (disabled)
   *
   * Lifter formula: L[n] = 1 + (Q/2) * sin(πn/Q)
   * where Q is the liftering coefficient.
   */
  lifterCoefficient?: number;
}
