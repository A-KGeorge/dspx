/**
 * Quick test to verify irregular timestamps work with convolution pipeline
 */

import { createDspPipeline } from "../../../dist/bindings.js";

async function main() {
  console.log("=== Testing Irregular Timestamps with Convolution ===\n");

  const pipeline = createDspPipeline();

  // 1. Convolution Stage
  const kernel = new Float32Array([0.2, 0.6, 0.2]);
  pipeline.convolution({
    kernel,
    mode: "moving",
  });

  // 2. RMS Stage with time-based window
  pipeline.Rms({
    mode: "moving",
    windowDuration: 100, // 100ms time window
  });

  // Irregular data
  const irregularSamples = new Float32Array([
    1.2, 3.4, 2.1, 4.5, 3.3, 2.8, 4.1, 3.9, 2.5, 3.7,
  ]);
  const irregularTimestamps = new Float32Array([
    0, 100, 250, 400, 500, 650, 750, 900, 1000, 1150,
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

  // Process
  const output = await pipeline.process(irregularSamples, irregularTimestamps, {
    channels: 1,
  });

  console.log("\nOutput data (after convolution + time-based RMS):");
  for (let i = 0; i < output.length; i++) {
    console.log(`  [${i}] ${output[i].toFixed(4)}`);
  }

  console.log("\n✓ Success! Pipeline processed irregular timestamps.");
  console.log("  - Convolution: sample-domain processing");
  console.log("  - RMS: time-domain (100ms window)");
}

main().catch((err) => {
  console.error("Error:", err.message);
  process.exit(1);
});
