/**
 * Configuration for state persistence resilience
 */
export interface StateResilienceConfig {
  /**
   * Maximum number of retry attempts for saveState/loadState
   * Default: 3
   */
  maxRetries?: number;

  /**
   * Fall back to clearState() when loadState fails
   * Enables cold start recovery
   * Default: false
   */
  fallbackOnLoadFailure?: boolean;
}
