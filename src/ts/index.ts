// Export the main API
export { createDspPipeline, DspProcessor } from "./bindings.js";
export {
  TopicRouter,
  TopicRouterBuilder,
  createTopicRouter,
} from "./TopicRouter.js";
export {
  createPagerDutyHandler,
  createPrometheusHandler,
  createLokiHandler,
  createCloudWatchHandler,
  createDatadogHandler,
  createConsoleHandler,
  createMockHandler,
  createKafkaProducerHandler,
  createKafkaConsumer,
  Logger,
  JSONFormatter,
  TextFormatter,
  SEVERITY_MAPPINGS,
  tracingContext,
  getTracingContext,
  withTracingContext,
  generateTraceparent,
  type Formatter,
  type SeverityMapping,
  type HandlerWithFlush,
  type LoggerMetrics,
  type SamplingConfig,
  type LoggerOptions,
  type KafkaProducerConfig,
  type KafkaConsumerConfig,
} from "./backends.js";
export {
  DriftDetector,
  detectGaps,
  validateMonotonicity,
  estimateSampleRate,
} from "./DriftDetector.js";
export {
  FftProcessor,
  MovingFftProcessor,
  FftUtils,
  type ComplexArray,
  type WindowType,
  type FftMode,
} from "./fft.js";
export {
  FirFilter,
  IirFilter,
  AdaptiveLMSFilter,
  type FilterType,
  type FilterMode,
  type FilterOptions,
  type FirFilterOptions,
  type IirFilterOptions,
  type ButterworthFilterOptions,
  type ChebyshevFilterOptions,
  type BiquadFilterOptions,
} from "./filters.js";
export {
  calculateHjorthParameters,
  calculateSpectralCentroid,
  calculateSpectralRolloff,
  calculateSpectralFlux,
  calculateSpectralFeatures,
  calculateShannonEntropy,
  calculateSampleEntropy,
  calculateApproximateEntropy,
  HjorthTracker,
  SpectralFeaturesTracker,
  EntropyTracker,
} from "./advanced-dsp.js";
export { egg, credits } from "./easter-egg.js";
export { dotProduct, DspUtils } from "./utils.js";
export type {
  DriftStatistics,
  DriftDetectorOptions,
  TimingMetrics,
  GapDetection,
  MonotonicityViolation,
  SampleRateEstimate,
} from "./DriftDetector.js";
export type {
  ProcessOptions,
  MovingAverageParams,
  RedisConfig,
  RmsParams,
  RectifyParams,
  VarianceParams,
  ZScoreNormalizeParams,
  MeanAbsoluteValueParams,

  // logging and monitoring interfaces
  PipelineCallbacks,
  LogLevel,
  LogContext,
  LogEntry,
  LogTopic,
  LogPriority,
  SampleBatch,
  TapCallback,
  PipelineStateSummary,
  StageSummary,

  // Advanced DSP types
  HjorthParameters,
  HjorthParams,
  SpectralFeatures,
  SpectralFeaturesParams,
  EntropyParams,
  SampleEntropyParams,
  ApproximateEntropyParams,
  DecimateParams,
  InterpolateParams,
  ResampleParams,
  ConvolutionParams,

  // Wavelet and Hilbert
  WaveletType,
  WaveletTransformParams,
  HilbertEnvelopeParams,
} from "./types.js";
export type {
  RouteHandler,
  Route,
  RouteOptions,
  RouteMetrics,
  PatternMatcher,
} from "./TopicRouter.js";
export type { BackendConfig } from "./backends.js";
