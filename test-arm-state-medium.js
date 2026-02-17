/**
 * ARM State Debug Test - Medium Signal
 * Uses 200 samples with sine wave to see real filter output
 */

import { createDspPipeline } from "./dist/index.js";
import crypto from "crypto";
import os from "os";

console.log("🔍 ARM State Debug Test - Medium Signal\n");
console.log(`Platform: ${os.platform()}`);
console.log(`Architecture: ${os.arch()}`);
console.log(`Node: ${process.version}\n`);

async function debugARMState() {
  try {
    // Longer signal with sine wave (50 Hz at 10kHz sample rate)
    const length = 200;
    const signal = new Float32Array(length);
    for (let i = 0; i < length; i++) {
      signal[i] = Math.sin((2 * Math.PI * 50 * i) / 10000);
    }

    const halfLength = 100;

    console.log("Signal: 200 samples, sine wave 50 Hz @ 10kHz sample rate");
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

    console.log(
      "Control output (first 10):",
      Array.from(outControl.slice(0, 10).map((v) => v.toFixed(6))),
    );
    console.log(
      "Control output (around split):",
      Array.from(outControl.slice(95, 105).map((v) => v.toFixed(6))),
    );
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

    const out1 = await p1.process(
      new Float32Array(signal.slice(0, halfLength)),
      { sampleRate: 10000, channels: 1 },
    );
    console.log(
      "First half output (last 10):",
      Array.from(out1.slice(-10).map((v) => v.toFixed(6))),
    );

    // Save state
    const stateJson = await p1.saveState();
    const stateParsed = JSON.parse(stateJson);
    const stateBuffer = stateParsed.stages[0].state.channels[0].stateBuffer;
    const stateIndex = stateParsed.stages[0].state.channels[0].stateIndex;

    console.log(`State buffer size: ${stateBuffer.length}`);
    console.log(`State index: ${stateIndex}`);
    console.log("State buffer (last 20 elements):");
    console.log(stateBuffer.slice(-20).map((v) => v.toFixed(6)));
    console.log("State buffer (first 20 elements):");
    console.log(stateBuffer.slice(0, 20).map((v) => v.toFixed(6)));
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

    await p2.loadState(stateJson);
    console.log("✓ State loaded\n");

    // Process second half
    const out2 = await p2.process(new Float32Array(signal.slice(halfLength)), {
      sampleRate: 10000,
      channels: 1,
    });
    console.log(
      "Second half output (first 10):",
      Array.from(out2.slice(0, 10).map((v) => v.toFixed(6))),
    );
    console.log();

    // Combine
    const outTest = new Float32Array(out1.length + out2.length);
    outTest.set(out1, 0);
    outTest.set(out2, out1.length);

    console.log(
      "Combined test output (around split):",
      Array.from(outTest.slice(95, 105).map((v) => v.toFixed(6))),
    );
    console.log();

    // =========================================================================
    // COMPARE
    // =========================================================================
    console.log("=".repeat(80));
    console.log("COMPARISON AT SEAM (samples 95-105)");
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
      console.log("Detailed comparison around the seam (samples 95-105):");
      console.log("Index | Control       | Test          | Diff");
      console.log("------|---------------|---------------|-------------");

      for (let i = 95; i < 105; i++) {
        const diff = Math.abs(outControl[i] - outTest[i]);
        const marker = diff > 1e-6 ? " ❌" : "";
        console.log(
          `${i.toString().padStart(5)} | ` +
            `${outControl[i].toFixed(9).padStart(13)} | ` +
            `${outTest[i].toFixed(9).padStart(13)} | ` +
            `${diff.toExponential(2).padStart(11)}${marker}`,
        );
      }

      console.log();
      console.log("Full divergence analysis:");
      let firstDiff = -1;
      let diffCount = 0;
      let maxDiff = 0;

      for (let i = 0; i < Math.min(outControl.length, outTest.length); i++) {
        const diff = Math.abs(outControl[i] - outTest[i]);
        if (diff > 1e-6) {
          if (firstDiff === -1) firstDiff = i;
          diffCount++;
          maxDiff = Math.max(maxDiff, diff);
        }
      }

      console.log(`First difference at index: ${firstDiff}`);
      console.log(`Total samples differing: ${diffCount}`);
      console.log(`Maximum difference: ${maxDiff.toExponential(3)}`);
    }

    console.log();
    process.exit(hashControl === hashTest ? 0 : 1);
  } catch (error) {
    console.error("❌ Test failed with error:");
    console.error(error);
    console.error(error.stack);
    process.exit(1);
  }
}

debugARMState();
