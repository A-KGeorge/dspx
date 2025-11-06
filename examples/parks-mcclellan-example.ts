/**
 * Parks-McClellan Optimal FIR Filter Example
 *
 * This example demonstrates how to use optimal FIR coefficients
 * designed with the Parks-McClellan algorithm for better performance
 * compared to window-based designs.
 *
 * Prerequisites:
 * 1. Design coefficients using Python script:
 *    python scripts/design_optimal_fir.py --type lowpass --taps 87 --cutoff 0.2 --output lowpass.json
 */

import { createDspPipeline } from "../dist/index.js";
import * as fs from "fs";

// ============================================================================
// Example 1: Use optimal coefficients in pipeline
// ============================================================================

async function useOptimalFilterInPipeline() {
  console.log("\n=== Example 1: Optimal Filter in Pipeline ===\n");

  // Load optimal coefficients from JSON file
  const coeffsJson = JSON.parse(fs.readFileSync("./lowpass.json", "utf-8"));
  const coeffs = new Float32Array(coeffsJson);

  console.log(`Loaded ${coeffs.length} optimal FIR coefficients`);

  // Create pipeline with convolution stage
  const pipeline = createDspPipeline();
  pipeline.convolution({ kernel: coeffs });

  // Generate test signal with signal (0.1π) and noise (0.4π)
  const signal = new Float32Array(1000);
  for (let i = 0; i < signal.length; i++) {
    signal[i] =
      Math.sin(2 * Math.PI * 0.1 * i) + // Signal
      0.5 * Math.sin(2 * Math.PI * 0.4 * i); // Noise
  }

  // Process signal
  const start = performance.now();
  const output = await pipeline.process(signal, { channels: 1 });
  const elapsed = performance.now() - start;

  console.log(
    `✓ Processed ${signal.length} samples in ${elapsed.toFixed(2)}ms`
  );
  console.log(
    `✓ Throughput: ${((signal.length / elapsed) * 1000).toFixed(0)} samples/sec`
  );
  console.log(`✓ Using ${coeffs.length}-tap optimal filter\n`);

  return output;
}

// ============================================================================
// Example 2: Coefficient Comparison
// ============================================================================

async function compareCoefficients() {
  console.log("\n=== Example 2: Coefficient Analysis ===\n");

  // Load optimal coefficients
  const coeffsJson = JSON.parse(fs.readFileSync("./lowpass.json", "utf-8"));
  const optimalCoeffs = new Float32Array(coeffsJson);

  console.log("Optimal FIR Coefficients (Parks-McClellan):");
  console.log(`  Length: ${optimalCoeffs.length} taps`);
  console.log(
    `  First 5 coefficients: [${Array.from(optimalCoeffs.slice(0, 5))
      .map((c) => c.toFixed(6))
      .join(", ")}]`
  );
  console.log(
    `  Center coefficient: ${optimalCoeffs[
      Math.floor(optimalCoeffs.length / 2)
    ].toFixed(6)}`
  );

  // Calculate sum of coefficients (DC gain)
  const dcGain = optimalCoeffs.reduce((sum, c) => sum + c, 0);
  console.log(`  DC Gain: ${dcGain.toFixed(6)} (normalized sum)`);

  // Calculate energy
  const energy = Math.sqrt(optimalCoeffs.reduce((sum, c) => sum + c * c, 0));
  console.log(`  Energy: ${energy.toFixed(6)}\n`);
}

// ============================================================================
// Example 3: Multi-stage Pipeline with Optimal Filter
// ============================================================================

async function multiStagePipeline() {
  console.log("\n=== Example 3: Multi-Stage Processing ===\n");

  // Load optimal lowpass coefficients
  const coeffsJson = JSON.parse(fs.readFileSync("./lowpass.json", "utf-8"));
  const lowpassCoeffs = new Float32Array(coeffsJson);

  // Create pipeline with multiple stages
  const pipeline = createDspPipeline();

  // Stage 1: Optimal lowpass filter (removes high-frequency noise)
  pipeline.convolution({ kernel: lowpassCoeffs });

  // Stage 2: Rectification (for envelope detection)
  pipeline.Rectify({ mode: "full" });

  // Stage 3: Moving average (smoothing)
  pipeline.MovingAverage({ mode: "moving", windowSize: 10 });

  // Generate test signal: 10 Hz sine wave with 60 Hz noise
  const signal = new Float32Array(2500); // 10 seconds @ 250 Hz
  for (let i = 0; i < signal.length; i++) {
    const t = i / 250;
    signal[i] =
      Math.sin(2 * Math.PI * 10 * t) + // 10 Hz signal
      0.3 * Math.sin(2 * Math.PI * 60 * t) + // 60 Hz noise
      0.1 * Math.random(); // Random noise
  }

  // Process through pipeline
  const start = performance.now();
  const output = await pipeline.process(signal, { channels: 1 });
  const elapsed = performance.now() - start;

  console.log(`✓ Pipeline stages: Convolution → Rectify → MovingAverage`);
  console.log(
    `✓ Processed ${signal.length} samples in ${elapsed.toFixed(2)}ms`
  );
  console.log(
    `✓ Throughput: ${((signal.length / elapsed) * 1000).toFixed(
      0
    )} samples/sec\n`
  );

  return output;
}

// ============================================================================
// Example 4: Performance Benchmarking
// ============================================================================

async function benchmarkPerformance() {
  console.log("\n=== Example 4: Performance Benchmark ===\n");

  const coeffsJson = JSON.parse(fs.readFileSync("./lowpass.json", "utf-8"));
  const coeffs = new Float32Array(coeffsJson);

  const pipeline = createDspPipeline();
  pipeline.convolution({ kernel: coeffs });

  // Test different signal lengths
  const lengths = [1000, 5000, 10000, 50000];

  console.log(
    `Testing ${coeffs.length}-tap optimal filter with different signal lengths:\n`
  );
  console.log("Length | Time (ms) | Throughput (M samples/sec)");
  console.log("-------|-----------|---------------------------");

  for (const length of lengths) {
    const signal = new Float32Array(length);
    for (let i = 0; i < length; i++) {
      signal[i] = Math.sin(2 * Math.PI * 0.1 * i);
    }

    // Warmup
    await pipeline.process(signal, { channels: 1 });

    // Measure
    const iterations = 10;
    const start = performance.now();
    for (let i = 0; i < iterations; i++) {
      await pipeline.process(signal, { channels: 1 });
    }
    const elapsed = performance.now() - start;
    const avgTime = elapsed / iterations;
    const throughput = ((length / avgTime) * 1000) / 1e6;

    console.log(
      `${length.toString().padStart(6)} | ${avgTime
        .toFixed(2)
        .padStart(9)} | ${throughput.toFixed(2).padStart(26)}`
    );
  }
  console.log();
}

// ============================================================================
// Main
// ============================================================================

async function main() {
  console.log("Parks-McClellan Optimal FIR Filter Examples");
  console.log("============================================");

  try {
    // Example 1: Basic usage
    await useOptimalFilterInPipeline();

    // Example 2: Analyze coefficients
    await compareCoefficients();

    // Example 3: Multi-stage pipeline
    await multiStagePipeline();

    // Example 4: Performance benchmark
    await benchmarkPerformance();

    console.log("✅ All examples completed successfully!");
  } catch (error: any) {
    console.error("❌ Error:", error.message);
    console.log("\nMake sure you have:");
    console.log("  1. Built the project: npm run build");
    console.log("  2. Created optimal coefficients:");
    console.log(
      "     python scripts/design_optimal_fir.py --type lowpass --taps 87 --cutoff 0.2 --output lowpass.json"
    );
  }
}

// Run if executed directly
if (require.main === module) {
  main().catch(console.error);
}

export {
  useOptimalFilterInPipeline,
  compareCoefficients,
  multiStagePipeline,
  benchmarkPerformance,
};
