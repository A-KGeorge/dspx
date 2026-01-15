/**
 * Resilience with Opossum Circuit Breaker
 *
 * This example demonstrates production-grade circuit breaking for state persistence
 * using the opossum library. Run with: node examples/resilience-with-opossum.cjs
 *
 * Install: npm install opossum ioredis
 */

const CircuitBreaker = require("opossum");
const { createDspPipeline } = require("../dist/bindings.js");
const Redis = require("ioredis");

// Initialize Redis and DSP pipeline
const redis = new Redis({
  host: "localhost",
  port: 6379,
  retryStrategy: (times) => Math.min(times * 50, 2000),
});

const pipeline = createDspPipeline({
  maxRetries: 3, // Built-in retry for transient failures
  fallbackOnLoadFailure: false, // Let opossum handle fallback
});

// Configure pipeline
pipeline.MovingAverage({ mode: "moving", windowSize: 100 });
pipeline.ZScoreNormalize({ mode: "moving", windowSize: 50 });

// Circuit breaker for saveState operations
const saveBreaker = new CircuitBreaker(
  async (state) => {
    console.log(
      `[SaveBreaker] Attempting to save state (${state.length} bytes)`
    );
    await redis.set("dsp:state", state);
    console.log("[SaveBreaker] State saved successfully");
  },
  {
    timeout: 2000, // Fail if Redis takes >2s
    errorThresholdPercentage: 50, // Trip after 50% failures
    resetTimeout: 30000, // Try recovery after 30s
    rollingCountTimeout: 10000, // Stats window: 10s
    volumeThreshold: 5, // Minimum requests before tripping
    name: "DspStateSave",
  }
);

// Fallback strategy when circuit is open
saveBreaker.fallback(() => {
  console.warn("[SaveBreaker] Circuit OPEN - skipping state save");
  return false; // Indicate save was skipped
});

// Monitor circuit state changes
saveBreaker.on("open", () => {
  console.error("[SaveBreaker] Circuit OPEN - Redis persistence failing");
  console.error("[SaveBreaker] State saves will be skipped for 30s");
});

saveBreaker.on("halfOpen", () => {
  console.info("[SaveBreaker] Circuit HALF_OPEN - testing Redis recovery");
});

saveBreaker.on("close", () => {
  console.info("[SaveBreaker] Circuit CLOSED - Redis persistence healthy");
});

// Track failures for alerting
saveBreaker.on("failure", (error) => {
  console.error("[SaveBreaker] Save failed:", error.message);
  // In production: Send to PagerDuty/Opsgenie
});

// Circuit breaker for loadState operations
const loadBreaker = new CircuitBreaker(
  async (key) => {
    console.log(`[LoadBreaker] Attempting to load state from ${key}`);
    const state = await redis.get(key);
    if (state) {
      await pipeline.loadState(state);
      console.log("[LoadBreaker] State loaded successfully");
      return true;
    }
    console.log("[LoadBreaker] No state found - starting fresh");
    return false;
  },
  {
    timeout: 2000,
    errorThresholdPercentage: 50,
    resetTimeout: 30000,
    name: "DspStateLoad",
  }
);

// Fallback to fresh state when load circuit opens
loadBreaker.fallback(() => {
  console.warn("[LoadBreaker] Circuit OPEN - starting with fresh state");
  pipeline.clearState();
  return false;
});

loadBreaker.on("open", () => {
  console.error(
    "[LoadBreaker] Circuit OPEN - state loads will use fresh state"
  );
});

// Simulate real-time signal processing
async function processSignals() {
  console.log("\n=== Starting signal processing ===\n");

  // Try to load previous state on startup
  try {
    await loadBreaker.fire("dsp:state");
  } catch (error) {
    console.error("Failed to load state:", error.message);
    pipeline.clearState();
  }

  // Processing loop (simulates 10Hz sampling rate)
  let iteration = 0;
  const interval = setInterval(async () => {
    try {
      // Generate simulated sensor data
      const samples = new Float32Array(10);
      for (let i = 0; i < samples.length; i++) {
        samples[i] = Math.sin((iteration * 10 + i) * 0.1) + Math.random() * 0.2;
      }

      // Process through pipeline
      const output = await pipeline.process(samples, {
        channels: 1,
        sampleRate: 1000,
      });

      console.log(
        `[${iteration}] Processed ${samples.length} samples -> ${output.length} outputs`
      );

      // Periodically save state (every 10 iterations = 1 second)
      if (iteration % 10 === 0) {
        try {
          const state = await pipeline.saveState({ format: "toon" });
          await saveBreaker.fire(state);

          // Log circuit stats
          const stats = saveBreaker.stats;
          console.log(
            `[Stats] Fires: ${stats.fires}, Successes: ${stats.successes}, ` +
              `Failures: ${stats.failures}, Open: ${saveBreaker.opened}`
          );
        } catch (error) {
          console.error("State save error:", error.message);
        }
      }

      iteration++;

      // Run for 50 iterations (~5 seconds)
      if (iteration >= 50) {
        clearInterval(interval);
        await shutdown();
      }
    } catch (error) {
      console.error("Processing error:", error);
    }
  }, 100);
}

// Graceful shutdown with state persistence
async function shutdown() {
  console.log("\n=== Shutting down gracefully ===\n");

  try {
    // Final state save
    const state = await pipeline.saveState({ format: "toon" });
    await saveBreaker.fire(state);
    console.log("Final state saved");
  } catch (error) {
    console.error("Failed to save final state:", error.message);
  }

  // Print final circuit stats
  console.log("\n--- Circuit Breaker Stats ---");
  console.log("Save Breaker:", {
    fires: saveBreaker.stats.fires,
    successes: saveBreaker.stats.successes,
    failures: saveBreaker.stats.failures,
    circuitOpen: saveBreaker.opened,
  });
  console.log("Load Breaker:", {
    fires: loadBreaker.stats.fires,
    successes: loadBreaker.stats.successes,
    failures: loadBreaker.stats.failures,
    circuitOpen: loadBreaker.opened,
  });

  // Cleanup
  pipeline.dispose();
  await redis.quit();
  console.log("\nShutdown complete");
  process.exit(0);
}

// Simulate Redis failure scenario (optional)
async function simulateRedisFailure() {
  console.log("\n!!! Simulating Redis failure in 2 seconds !!!\n");

  setTimeout(async () => {
    console.log("--- Disconnecting Redis ---");
    await redis.disconnect();

    // Reconnect after 5 seconds
    setTimeout(async () => {
      console.log("--- Reconnecting Redis ---");
      await redis.connect();
    }, 5000);
  }, 2000);
}

// Handle SIGTERM for graceful shutdown
process.on("SIGTERM", shutdown);
process.on("SIGINT", shutdown);

// Main execution
(async () => {
  console.log("=== Resilience with Opossum Circuit Breaker ===");
  console.log(
    "This demo shows production-grade state persistence resilience\n"
  );

  // Uncomment to test circuit breaker behavior with Redis failures
  // await simulateRedisFailure();

  await processSignals();
})();
