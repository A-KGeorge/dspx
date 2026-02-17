/**
 * ARM State Serialization Test
 * Tests that FIR filter circular buffer state serialization works correctly
 * on both x64 and ARM64 architectures.
 */

import { createDspPipeline } from "./dist/index.js";
import crypto from "crypto";
import os from "os";

console.log("🧪 ARM State Serialization Test\n");
console.log(`Platform: ${os.platform()}`);
console.log(`Architecture: ${os.arch()}`);
console.log(`Node: ${process.version}\n`);

async function testARMState() {
  try {
    // Generate test signal
    const length = 2048;
    const signal = new Float32Array(length);
    for (let i = 0; i < length; i++) {
      signal[i] =
        Math.sin((2 * Math.PI * 50 * i) / 10000) +
        0.5 * Math.sin((2 * Math.PI * 120 * i) / 10000);
    }

    console.log(`Signal length: ${length} samples`);
    const halfLength = Math.floor(length / 2);
    console.log(`Split point: ${halfLength}\n`);

    // =========================================================================
    // CONTROL: Process full signal in one go
    // =========================================================================
    console.log("📊 Control: Processing full signal");
    const pipelineControl = createDspPipeline();
    pipelineControl
      .filter({
        type: "fir",
        mode: "lowpass",
        cutoffFrequency: 3000,
        sampleRate: 10000,
        order: 51,
        windowType: "hamming",
      })
      .Rms({ mode: "moving", windowSize: 100 })
      .ZScoreNormalize({ mode: "moving", windowSize: 20 })
      .Rectify({ mode: "full" });

    const outputControl = await pipelineControl.process(
      new Float32Array(signal),
      { sampleRate: 10000, channels: 1 },
    );

    const hashControl = crypto
      .createHash("sha256")
      .update(Buffer.from(outputControl.buffer))
      .digest("hex");

    console.log(`   ✓ Output length: ${outputControl.length}`);
    console.log(`   ✓ SHA-256: ${hashControl.substring(0, 16)}...\n`);

    // =========================================================================
    // TEST JSON: Split processing with JSON state save/load
    // =========================================================================
    console.log("📊 Test JSON: Split processing with state save/load");

    // Process first half
    const pipeline1Json = createDspPipeline();
    pipeline1Json
      .filter({
        type: "fir",
        mode: "lowpass",
        cutoffFrequency: 3000,
        sampleRate: 10000,
        order: 51,
        windowType: "hamming",
      })
      .Rms({ mode: "moving", windowSize: 100 })
      .ZScoreNormalize({ mode: "moving", windowSize: 20 })
      .Rectify({ mode: "full" });

    const output1Json = await pipeline1Json.process(
      new Float32Array(signal.slice(0, halfLength)),
      { sampleRate: 10000, channels: 1 },
    );

    // Save state
    const stateJson = await pipeline1Json.saveState();
    const stateJsonSize = Buffer.byteLength(stateJson, "utf8");
    console.log(`   ✓ First half processed: ${output1Json.length} samples`);
    console.log(`   ✓ State saved (JSON): ${stateJsonSize} bytes`);

    // Load state into new pipeline
    const pipeline2Json = createDspPipeline();
    pipeline2Json
      .filter({
        type: "fir",
        mode: "lowpass",
        cutoffFrequency: 3000,
        sampleRate: 10000,
        order: 51,
        windowType: "hamming",
      })
      .Rms({ mode: "moving", windowSize: 100 })
      .ZScoreNormalize({ mode: "moving", windowSize: 20 })
      .Rectify({ mode: "full" });

    await pipeline2Json.loadState(stateJson);
    console.log(`   ✓ State loaded into new pipeline`);

    // Process second half
    const output2Json = await pipeline2Json.process(
      new Float32Array(signal.slice(halfLength)),
      { sampleRate: 10000, channels: 1 },
    );

    console.log(`   ✓ Second half processed: ${output2Json.length} samples`);

    // Combine outputs
    const outputTestJson = new Float32Array(
      output1Json.length + output2Json.length,
    );
    outputTestJson.set(output1Json, 0);
    outputTestJson.set(output2Json, output1Json.length);

    const hashTestJson = crypto
      .createHash("sha256")
      .update(Buffer.from(outputTestJson.buffer))
      .digest("hex");

    console.log(`   ✓ Combined length: ${outputTestJson.length}`);
    console.log(`   ✓ SHA-256: ${hashTestJson.substring(0, 16)}...\n`);

    // =========================================================================
    // TEST TOON: Split processing with TOON state save/load
    // =========================================================================
    console.log("📊 Test TOON: Split processing with binary state save/load");

    // Process first half
    const pipeline1Toon = createDspPipeline();
    pipeline1Toon
      .filter({
        type: "fir",
        mode: "lowpass",
        cutoffFrequency: 3000,
        sampleRate: 10000,
        order: 51,
        windowType: "hamming",
      })
      .Rms({ mode: "moving", windowSize: 100 })
      .ZScoreNormalize({ mode: "moving", windowSize: 20 })
      .Rectify({ mode: "full" });

    const output1Toon = await pipeline1Toon.process(
      new Float32Array(signal.slice(0, halfLength)),
      { sampleRate: 10000, channels: 1 },
    );

    // Save state
    const stateToon = await pipeline1Toon.saveState({ format: "toon" });
    const stateToonSize = Buffer.from(
      stateToon instanceof Uint8Array ? stateToon : new Uint8Array(stateToon),
    ).length;
    console.log(`   ✓ First half processed: ${output1Toon.length} samples`);
    console.log(`   ✓ State saved (TOON): ${stateToonSize} bytes`);

    // Load state into new pipeline
    const pipeline2Toon = createDspPipeline();
    pipeline2Toon
      .filter({
        type: "fir",
        mode: "lowpass",
        cutoffFrequency: 3000,
        sampleRate: 10000,
        order: 51,
        windowType: "hamming",
      })
      .Rms({ mode: "moving", windowSize: 100 })
      .ZScoreNormalize({ mode: "moving", windowSize: 20 })
      .Rectify({ mode: "full" });

    await pipeline2Toon.loadState(stateToon);
    console.log(`   ✓ State loaded into new pipeline`);

    // Process second half
    const output2Toon = await pipeline2Toon.process(
      new Float32Array(signal.slice(halfLength)),
      { sampleRate: 10000, channels: 1 },
    );

    console.log(`   ✓ Second half processed: ${output2Toon.length} samples`);

    // Combine outputs
    const outputTestToon = new Float32Array(
      output1Toon.length + output2Toon.length,
    );
    outputTestToon.set(output1Toon, 0);
    outputTestToon.set(output2Toon, output1Toon.length);

    const hashTestToon = crypto
      .createHash("sha256")
      .update(Buffer.from(outputTestToon.buffer))
      .digest("hex");

    console.log(`   ✓ Combined length: ${outputTestToon.length}`);
    console.log(`   ✓ SHA-256: ${hashTestToon.substring(0, 16)}...\n`);

    // =========================================================================
    // VALIDATION
    // =========================================================================
    console.log("=".repeat(80));
    console.log("VALIDATION RESULTS");
    console.log("=".repeat(80));

    const jsonSeamless = hashControl === hashTestJson;
    const toonSeamless = hashControl === hashTestToon;

    console.log(`\nJSON Format:`);
    console.log(`   Seamless: ${jsonSeamless ? "✅ YES" : "❌ NO"}`);
    console.log(`   Control:  ${hashControl.substring(0, 32)}`);
    console.log(`   Test:     ${hashTestJson.substring(0, 32)}`);

    if (!jsonSeamless) {
      // Calculate differences
      let diffCount = 0;
      let maxDiff = 0;
      const threshold = 1e-6;

      for (
        let i = 0;
        i < Math.min(outputControl.length, outputTestJson.length);
        i++
      ) {
        const diff = Math.abs(outputControl[i] - outputTestJson[i]);
        if (diff > threshold) {
          diffCount++;
          maxDiff = Math.max(maxDiff, diff);
        }
      }

      console.log(`   Samples differing: ${diffCount}`);
      console.log(`   Max difference: ${maxDiff.toExponential(3)}`);
    }

    console.log(`\nTOON Format:`);
    console.log(`   Seamless: ${toonSeamless ? "✅ YES" : "❌ NO"}`);
    console.log(`   Control:  ${hashControl.substring(0, 32)}`);
    console.log(`   Test:     ${hashTestToon.substring(0, 32)}`);

    if (!toonSeamless) {
      // Calculate differences
      let diffCount = 0;
      let maxDiff = 0;
      const threshold = 1e-6;

      for (
        let i = 0;
        i < Math.min(outputControl.length, outputTestToon.length);
        i++
      ) {
        const diff = Math.abs(outputControl[i] - outputTestToon[i]);
        if (diff > threshold) {
          diffCount++;
          maxDiff = Math.max(maxDiff, diff);
        }
      }

      console.log(`   Samples differing: ${diffCount}`);
      console.log(`   Max difference: ${maxDiff.toExponential(3)}`);
    }

    console.log("\n" + "=".repeat(80));
    console.log(
      `OVERALL: ${jsonSeamless && toonSeamless ? "✅ PASS" : "❌ FAIL"}`,
    );
    console.log("=".repeat(80) + "\n");

    // Exit with appropriate code
    process.exit(jsonSeamless && toonSeamless ? 0 : 1);
  } catch (error) {
    console.error("❌ Test failed with error:");
    console.error(error);
    process.exit(1);
  }
}

testARMState();
