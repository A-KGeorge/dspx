/**
 * Minimal ARM State Debug Test
 * Tests with tiny signal to see exact state values
 */

import { createDspPipeline } from "./dist/index.js";
import crypto from "crypto";
import os from "os";

console.log("🔍 ARM State Debug Test\n");
console.log(`Platform: ${os.platform()}`);
console.log(`Architecture: ${os.arch()}`);
console.log(`Node: ${process.version}\n`);

async function debugARMState() {
  try {
    // Tiny signal for easy debugging
    const signal = new Float32Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
    const halfLength = 5;

    console.log("Input signal:", Array.from(signal));
    console.log("Split point:", halfLength, "\n");

    // =========================================================================
    // CONTROL: Process full signal
    // =========================================================================
    console.log("📊 CONTROL: Full signal processing");
    const pControl = createDspPipeline();
    pControl.filter({
      type: "fir",
      mode: "lowpass",
      cutoffFrequency: 3000,
      sampleRate: 10000,
      order: 51,
      windowType: "hamming",
    });

    const outControl = await pControl.process(new Float32Array(signal), {
      sampleRate: 10000,
      channels: 1,
    });

    console.log("Control output:", Array.from(outControl.slice(0, 10)));
    console.log();

    // =========================================================================
    // TEST: Split with JSON state
    // =========================================================================
    console.log("📊 TEST: Split processing with JSON state\n");

    // First half
    const p1 = createDspPipeline();
    p1.filter({
      type: "fir",
      mode: "lowpass",
      cutoffFrequency: 3000,
      sampleRate: 10000,
      order: 51,
      windowType: "hamming",
    });

    console.log(
      "Processing first half:",
      Array.from(signal.slice(0, halfLength)),
    );
    const out1 = await p1.process(
      new Float32Array(signal.slice(0, halfLength)),
      { sampleRate: 10000, channels: 1 },
    );
    console.log("First half output:", Array.from(out1));
    console.log();

    // Save state
    const stateJson = await p1.saveState();
    console.log("Saved state (JSON):");
    const stateParsed = JSON.parse(stateJson);
    console.log(JSON.stringify(stateParsed, null, 2));
    console.log();

    // Load into new pipeline
    const p2 = createDspPipeline();
    p2.filter({
      type: "fir",
      mode: "lowpass",
      cutoffFrequency: 3000,
      sampleRate: 10000,
      order: 51,
      windowType: "hamming",
    });

    console.log("Loading state into new pipeline...");
    await p2.loadState(stateJson);
    console.log("✓ State loaded\n");

    // Process second half
    console.log(
      "Processing second half:",
      Array.from(signal.slice(halfLength)),
    );
    const out2 = await p2.process(new Float32Array(signal.slice(halfLength)), {
      sampleRate: 10000,
      channels: 1,
    });
    console.log("Second half output:", Array.from(out2));
    console.log();

    // Combine
    const outTest = new Float32Array(out1.length + out2.length);
    outTest.set(out1, 0);
    outTest.set(out2, out1.length);

    console.log("Combined test output:", Array.from(outTest.slice(0, 10)));
    console.log();

    // =========================================================================
    // COMPARE
    // =========================================================================
    console.log("=".repeat(80));
    console.log("COMPARISON");
    console.log("=".repeat(80));
    console.log();

    const hashControl = crypto
      .createHash("sha256")
      .update(Buffer.from(outControl.buffer))
      .digest("hex");

    const hashTest = crypto
      .createHash("sha256")
      .update(Buffer.from(outTest.buffer))
      .digest("hex");

    console.log("Control hash:", hashControl.substring(0, 32));
    console.log("Test hash:   ", hashTest.substring(0, 32));
    console.log();

    if (hashControl === hashTest) {
      console.log("✅ SEAMLESS: Hashes match!");
    } else {
      console.log("❌ NOT SEAMLESS: Hashes differ");
      console.log();
      console.log("Sample-by-sample comparison:");
      console.log("Index | Control       | Test          | Diff");
      console.log("------|---------------|---------------|-------------");

      for (let i = 0; i < Math.min(outControl.length, outTest.length); i++) {
        const diff = Math.abs(outControl[i] - outTest[i]);
        const marker = diff > 1e-6 ? " ❌" : "";
        console.log(
          `${i.toString().padStart(5)} | ` +
            `${outControl[i].toFixed(6).padStart(13)} | ` +
            `${outTest[i].toFixed(6).padStart(13)} | ` +
            `${diff.toExponential(2).padStart(11)}${marker}`,
        );
      }
    }

    console.log();
    process.exit(hashControl === hashTest ? 0 : 1);
  } catch (error) {
    console.error("❌ Test failed with error:");
    console.error(error);
    process.exit(1);
  }
}

debugARMState();
