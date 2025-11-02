/**
 * Processing Irregular Timestamps with Pipeline Stages
 *
 * This example demonstrates how to process irregular timestamp data
 * through a pipeline including convolution and other stages.
 *
 * IMPORTANT: The current implementation handles irregular timestamps throughout
 * the pipeline - there's no need for an explicit "resampling to regular" stage.
 * All stages receive the original timestamps and process accordingly.
 */

import { createDspPipeline } from "../../bindings";

async function main() {
  console.log("=== Irregular Timestamps with Convolution Pipeline ===\n");

  const pipeline = createDspPipeline();

  // --- PIPELINE STAGES ---

  // 1. Convolution Stage
  // Works with irregular timestamps - the kernel is applied sample-by-sample
  const kernel = new Float32Array([0.2, 0.6, 0.2]); // Simple 3-tap smoothing kernel
  pipeline.convolution({
    kernel,
    mode: "moving",
  });

  // 2. RMS Stage with time-based window
  // This stage DOES use the timestamps to maintain a 100ms time window
  // even though samples arrive irregularly
  pipeline.Rms({
    mode: "moving",
    windowDuration: 100, // 100ms time window (not samples!)
  });

  // --- GENERATE IRREGULAR DATA ---
  // Simulate sensor data arriving at irregular intervals
  const irregularSamples = new Float32Array([
    1.2, 3.4, 2.1, 4.5, 3.3, 2.8, 4.1, 3.9, 2.5, 3.7,
  ]);
  const irregularTimestamps = new Float32Array([
    0, // t=0ms
    100, // Δ=100ms
    250, // Δ=150ms (jitter!)
    400, // Δ=150ms
    500, // Δ=100ms
    650, // Δ=150ms
    750, // Δ=100ms
    900, // Δ=150ms
    1000, // Δ=100ms
    1150, // Δ=150ms
  ]);

  console.log("Input data (irregular timing):");
  for (let i = 0; i < irregularSamples.length; i++) {
    const delta =
      i > 0 ? irregularTimestamps[i] - irregularTimestamps[i - 1] : 0;
    console.log(
      `  [${i}] value=${irregularSamples[i].toFixed(2)}, ` +
        `t=${irregularTimestamps[i].toFixed(0)}ms, Δ=${delta.toFixed(0)}ms`
    );
  }

  // --- PROCESS ---
  // The pipeline handles:
  // 1. Convolution: Applied sample-by-sample (doesn't care about timestamps)
  // 2. RMS: Uses timestamps to maintain 100ms time window
  const output = await pipeline.process(irregularSamples, irregularTimestamps, {
    channels: 1,
  });

  console.log("\nOutput data (after convolution + time-based RMS):");
  for (let i = 0; i < Math.min(output.length, 10); i++) {
    console.log(`  [${i}] ${output[i].toFixed(4)}`);
  }

  console.log("\n✓ Pipeline processed irregular timestamps successfully!");
  console.log("  - Convolution worked in sample-domain");
  console.log(
    "  - RMS used time-domain (100ms window regardless of sample rate)"
  );
}

main().catch(console.error);
