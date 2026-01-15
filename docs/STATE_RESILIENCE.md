# State Persistence Resilience

## Overview

State persistence includes **lightweight built-in resilience** for transient failures, with optional integration of production-grade circuit breakers:

- **Built-in Retry**: Exponential backoff for network/Redis flakes (enabled by default)
- **Optional Fallback**: Auto-recovery to fresh state on persistent failures
- **External Circuit Breakers**: Integrate battle-tested libraries like [opossum](https://www.npmjs.com/package/opossum), [polly.js](https://www.npmjs.com/package/pollyjs), or [cockatiel](https://www.npmjs.com/package/cockatiel)
- **Native Validation**: C++ layer validates state integrity (stage count, types) before applying

## Design Philosophy

**dspx focuses on being a high-performance DSP primitive, not a full resilience framework.** The built-in retry mechanism handles common transient failures (network blips, brief Redis failovers), while critical production workloads should layer on established circuit breaker libraries that have battle-tested edge case handling, metrics integration, and extensive community adoption.

### Why External Circuit Breakers?

Libraries like **opossum** are purpose-built for this with features dspx won't replicate:

- AbortController integration for proper cancellation
- Prometheus/OpenTelemetry metrics exporters
- Coalescing for thundering herd protection
- Proven in production at scale (Red Hat, major cloud providers)
- Active maintenance and extensive test coverage

## Built-in Features

### 🔄 Automatic Retries

- **3 attempts by default** (configurable via `maxRetries`)
- **Exponential backoff**: 100ms → 400ms → 1600ms with jitter
- Prevents thundering herd during transient failures

### 🛡️ Fallback Strategy

- `loadState()` can automatically call `clearState()` on persistent failures
- **Cold start recovery**: Application continues with fresh state
- **Disabled by default**: Set `fallbackOnLoadFailure: true` to enable

## Usage

### Default Configuration (Simple Retries)

```typescript
import { createDspPipeline } from "dspx";

const pipeline = createDspPipeline(); // 3 retries enabled by default

pipeline.MovingAverage({ mode: "moving", windowSize: 100 });

// Save with automatic retries
const state = await pipeline.saveState();
await redis.set("dsp:state", state);

// Load with automatic retries (throws on failure)
const stateFromRedis = await redis.get("dsp:state");
if (stateFromRedis) {
  await pipeline.loadState(stateFromRedis);
}
```

### Enable Fallback for Auto-Recovery

```typescript
import { createDspPipeline } from "dspx";

const config = {
  fallbackOnLoadFailure: true, // Auto-clear state on failure
};

const pipeline = createDspPipeline(config);

// Load with automatic fallback to fresh state
const stateFromRedis = await redis.get("dsp:state");
if (stateFromRedis) {
  const success = await pipeline.loadState(stateFromRedis);
  if (!success) {
    console.log("State load failed - started with fresh state");
  }
}
```

## Production-Grade Circuit Breaking with Opossum

For critical production workloads, wrap state persistence with **[opossum](https://www.npmjs.com/package/opossum)**:

### Installation

```bash
npm install opossum
```

### Example: Circuit Breaker Wrapper

```typescript
import CircuitBreaker from "opossum";
import { createDspPipeline } from "dspx";
import { Redis } from "ioredis";

const redis = new Redis();
const pipeline = createDspPipeline();

// Configure circuit breaker for saveState
const saveBreaker = new CircuitBreaker(
  async (state: string | Buffer) => {
    await redis.set("dsp:state", state);
  },
  {
    timeout: 2000, // Fail if >2s
    errorThresholdPercentage: 50, // Trip after 50% failures
    resetTimeout: 30000, // Try recovery after 30s
    rollingCountTimeout: 10000, // Sliding window for stats
    volumeThreshold: 5, // Minimum requests before tripping
  }
);

// Fallback strategy
saveBreaker.fallback(() => {
  console.warn("Circuit OPEN - skipping state save");
  // Optional: Log to monitoring, alert PagerDuty
});

// Monitor circuit state
saveBreaker.on("open", () => {
  console.error("State persistence circuit OPEN - Redis failing");
});

saveBreaker.on("halfOpen", () => {
  console.info("State persistence circuit HALF_OPEN - testing recovery");
});

saveBreaker.on("close", () => {
  console.info("State persistence circuit CLOSED - Redis healthy");
});

// Usage in processing loop
setInterval(async () => {
  try {
    const samples = new Float32Array([
      /* ... */
    ]);
    const output = await pipeline.process(samples, {
      channels: 1,
      sampleRate: 1000,
    });

    // Save state with circuit breaker protection
    const state = await pipeline.saveState({ format: "toon" });
    await saveBreaker.fire(state);
  } catch (error) {
    console.error("Processing error:", error);
  }
}, 100);

// Access circuit stats
console.log(saveBreaker.stats); // { fires, successes, failures, timeouts, ... }
```

### Example: Load State with Circuit Breaker

```typescript
const loadBreaker = new CircuitBreaker(
  async (key: string) => {
    const state = await redis.get(key);
    if (state) {
      await pipeline.loadState(state);
    }
  },
  {
    timeout: 2000,
    errorThresholdPercentage: 50,
    resetTimeout: 30000,
  }
);

loadBreaker.fallback(() => {
  console.warn("Circuit OPEN - starting with fresh state");
  pipeline.clearState();
});

// Worker startup
try {
  await loadBreaker.fire("dsp:state");
} catch (error) {
  console.error("Failed to load state:", error);
}
```

### Monitoring Breaker Stats

```typescript
// Prometheus metrics export
app.get("/metrics", (req, res) => {
  const stats = saveBreaker.stats;
  res.send(`
# HELP dsp_state_persistence_fires Total state save attempts
# TYPE dsp_state_persistence_fires counter
dsp_state_persistence_fires ${stats.fires}

# HELP dsp_state_persistence_failures State save failures
# TYPE dsp_state_persistence_failures counter
dsp_state_persistence_failures ${stats.failures}

# HELP dsp_state_persistence_circuit_open Circuit breaker open status
# TYPE dsp_state_persistence_circuit_open gauge
dsp_state_persistence_circuit_open ${saveBreaker.opened ? 1 : 0}
  `);
});
```

## Alternative Libraries

### Polly.js (Retry-Only)

For simpler retry-only patterns without circuit breaking:

```bash
npm install polly-js
```

```typescript
import { retry } from "polly-js";

const state = await retry(
  async () => {
    const s = await pipeline.saveState();
    await redis.set("dsp:state", s);
  },
  {
    maxAttempts: 5,
    backoff: "exponential",
    delay: 100,
  }
);
```

### Cockatiel (Comprehensive Resilience)

For TypeScript-first resilience with retry, circuit breaker, timeout, and bulkhead:

```bash
npm install cockatiel
```

```typescript
import { Policy, CircuitBreakerPolicy, ExponentialBackoff } from "cockatiel";

const policy = Policy.handleAll()
  .retry()
  .attempts(3)
  .exponential({ maxDelay: 2000 })
  .circuitBreaker({
    halfOpenAfter: 30 * 1000,
    breaker: new SamplingBreaker({ threshold: 0.5 }),
  });

await policy.execute(async () => {
  const state = await pipeline.saveState();
  await redis.set("dsp:state", state);
});
```

## Configuration Reference

| Option                  | Type      | Default | Description                                              |
| ----------------------- | --------- | ------- | -------------------------------------------------------- |
| `maxRetries`            | `number`  | `3`     | Maximum retry attempts                                   |
| `fallbackOnLoadFailure` | `boolean` | `false` | Call `clearState()` on persistent `loadState()` failures |

## Error Handling

### saveState() Behavior

- Retries on transient failures (network, timeouts)
- Throws error after all retries exhausted
- **Recommendation**: Catch errors and continue processing

```typescript
try {
  const state = await pipeline.saveState();
  await redis.set("dsp:state", state);
} catch (error) {
  console.error("State save failed - continuing without persistence");
  // Application continues, state not saved
}
```

### loadState() Behavior

- Retries on transient failures
- Throws error by default after retries exhausted
- **Optional fallback**: Enable `fallbackOnLoadFailure: true` to auto-clear

```typescript
// Default: throws on failure
try {
  const state = await redis.get("dsp:state");
  if (state) {
    await pipeline.loadState(state);
  }
} catch (error) {
  console.error("State load failed:", error);
  pipeline.clearState(); // Manual fallback
}

// With auto-fallback enabled
const pipeline2 = createDspPipeline({ fallbackOnLoadFailure: true });
const success = await pipeline2.loadState(state);
if (!success) {
  console.log("Cold start - no previous state loaded");
}
```

## Production Patterns

### Worker Graceful Shutdown

```typescript
process.on("SIGTERM", async () => {
  try {
    // Save state before shutdown
    const state = await pipeline.saveState();
    await redis.set("worker:state", state);
    console.log("State saved before shutdown");
  } catch (error) {
    console.error("Failed to save state:", error);
    // Shutdown anyway
  }

  process.exit(0);
});
```

### Serverless Function Handler

```typescript
import { Redis } from "ioredis";
import { createDspPipeline } from "dspx";

const redis = new Redis();
let pipeline: DspProcessor | null = null;

export async function handler(event) {
  // Cold start: Load state
  if (!pipeline) {
    pipeline = createDspPipeline({ fallbackOnLoadFailure: true });
    pipeline.MovingAverage({ mode: "moving", windowSize: 100 });

    const state = await redis.get("function:state");
    if (state) {
      await pipeline.loadState(state);
    }
  }

  // Process samples
  const output = await pipeline.process(event.samples, {
    channels: 1,
    sampleRate: 1000,
  });

  // Save state for next invocation
  try {
    const state = await pipeline.saveState({ format: "toon" });
    await redis.set("function:state", state);
  } catch (error) {
    console.error("State save failed:", error);
  }

  return { output };
}
```

### Multi-Worker State Sharding

```typescript
const workerId = process.env.WORKER_ID || "0";
const stateKey = `worker:${workerId}:state`;

// Each worker maintains independent state
const state = await redis.get(stateKey);
if (state) {
  await pipeline.loadState(state);
}

// Save to worker-specific key
await redis.set(stateKey, await pipeline.saveState());
```

## Performance Impact

| Operation      | Overhead    | Notes                                            |
| -------------- | ----------- | ------------------------------------------------ |
| Retry logic    | ~5-10µs     | Negligible per-call overhead                     |
| saveState()    | < 0.1ms     | Serialization cost (TOON format fastest)         |
| loadState()    | < 0.1ms     | Deserialization cost                             |
| Retry attempts | ~100-1600ms | Exponential backoff on failures (transient only) |

**Recommendation**: The retry overhead is minimal (<0.1%) compared to typical Redis latency (1-5ms). For ultra-low-latency paths, disable retries (`maxRetries: 1`) and handle failures upstream.
