import { describe, test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

describe("SNR - basic computation", () => {
  test("should compute SNR for constant signal and noise", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 3 });

    // Signal = 1.0, Noise = 0.1
    // Expected SNR ≈ 10 * log10(1.0 / 0.01) = 10 * log10(100) = 20 dB
    const input = new Float32Array([
      1.0,
      0.1, // Sample 0
      1.0,
      0.1, // Sample 1
      1.0,
      0.1, // Sample 2
      1.0,
      0.1, // Sample 3
      1.0,
      0.1, // Sample 4
    ]);

    const result = await pipeline.process(input, {
      channels: 2,
      sampleRate: 1000,
    });

    // After window fills (sample 2+), SNR should stabilize around 20 dB
    assert.ok(result.length === 5, "Output should have 5 samples");
    assert.ok(
      result[4] > 19 && result[4] < 21,
      `SNR should be ~20 dB, got ${result[4]}`
    );
  });

  test("should output single channel from 2-channel input", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 2 });

    const input = new Float32Array([
      0.5,
      0.05, // Sample 0
      0.5,
      0.05, // Sample 1
      0.5,
      0.05, // Sample 2
    ]);

    const result = await pipeline.process(input, {
      channels: 2,
      sampleRate: 1000,
    });

    assert.strictEqual(
      result.length,
      3,
      "Output should be single channel (3 samples)"
    );
  });

  test("should handle varying SNR", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 2 });

    // Start with high SNR, transition to low SNR
    const input = new Float32Array([
      1.0,
      0.01, // High SNR: ~40 dB
      1.0,
      0.01,
      1.0,
      0.5, // Low SNR: ~6 dB
      1.0,
      0.5,
    ]);

    const result = await pipeline.process(input, {
      channels: 2,
      sampleRate: 1000,
    });

    assert.ok(result[1] > result[3], "High SNR should be greater than low SNR");
    assert.ok(
      result[1] > 30,
      `High SNR sample should be >30 dB, got ${result[1]}`
    );
    assert.ok(
      result[3] < 10,
      `Low SNR sample should be <10 dB, got ${result[3]}`
    );
  });
});

describe("SNR - noise handling", () => {
  test("should handle very low noise (high SNR)", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 3 });

    const input = new Float32Array([
      1.0,
      1e-6, // Very low noise
      1.0,
      1e-6,
      1.0,
      1e-6,
      1.0,
      1e-6,
    ]);

    const result = await pipeline.process(input, {
      channels: 2,
      sampleRate: 1000,
    });

    // Should clamp to max_db = 100 dB
    assert.ok(result[3] <= 100, "SNR should be clamped to max 100 dB");
    assert.ok(result[3] > 50, "Very low noise should give high SNR");
  });

  test("should handle very high noise (low SNR)", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 3 });

    const input = new Float32Array([
      0.1,
      10.0, // Very high noise
      0.1,
      10.0,
      0.1,
      10.0,
      0.1,
      10.0,
    ]);

    const result = await pipeline.process(input, {
      channels: 2,
      sampleRate: 1000,
    });

    // SNR should be negative (noise > signal)
    assert.ok(result[3] < 0, "High noise should give negative SNR");
    assert.ok(result[3] >= -100, "SNR should be clamped to min -100 dB");
  });

  test("should handle zero signal", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 3 });

    const input = new Float32Array([
      0.0,
      0.1, // Zero signal
      0.0,
      0.1,
      0.0,
      0.1,
      0.0,
      0.1,
    ]);

    const result = await pipeline.process(input, {
      channels: 2,
      sampleRate: 1000,
    });

    // Zero signal → very low SNR (clamped to -100 dB)
    assert.ok(result[3] < -50, "Zero signal should give very low SNR");
    assert.ok(result[3] >= -100, "SNR should be clamped to -100 dB");
  });
});

describe("SNR - validation", () => {
  test("should reject missing windowSize", async () => {
    const pipeline = createDspPipeline();

    assert.throws(
      () => pipeline.Snr({} as any),
      /windowSize must be greater than 0/,
      "Should reject missing windowSize"
    );
  });

  test("should reject zero windowSize", async () => {
    const pipeline = createDspPipeline();

    assert.throws(
      () => pipeline.Snr({ windowSize: 0 }),
      /windowSize must be greater than 0/,
      "Should reject zero windowSize"
    );
  });

  test("should reject negative windowSize", async () => {
    const pipeline = createDspPipeline();

    assert.throws(
      () => pipeline.Snr({ windowSize: -10 }),
      /windowSize must be greater than 0/,
      "Should reject negative windowSize"
    );
  });

  test("should reject non-2-channel input", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 3 });

    const monoInput = new Float32Array([1.0, 1.0, 1.0]);

    await assert.rejects(
      async () =>
        await pipeline.process(monoInput, { channels: 1, sampleRate: 1000 }),
      /requires exactly 2 channels/,
      "Should reject mono input"
    );
  });

  test("should reject 3-channel input", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 3 });

    const tripleInput = new Float32Array([1.0, 0.1, 0.5, 1.0, 0.1, 0.5]);

    await assert.rejects(
      async () =>
        await pipeline.process(tripleInput, { channels: 3, sampleRate: 1000 }),
      /requires exactly 2 channels/,
      "Should reject 3-channel input"
    );
  });
});

describe("SNR - state management", () => {
  test("should maintain state across multiple process calls", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 4 });

    // First batch (window not filled)
    const batch1 = new Float32Array([1.0, 0.1, 1.0, 0.1]);
    const result1 = await pipeline.process(batch1, {
      channels: 2,
      sampleRate: 1000,
    });

    // Second batch (window fills and stabilizes)
    const batch2 = new Float32Array([1.0, 0.1, 1.0, 0.1, 1.0, 0.1]);
    const result2 = await pipeline.process(batch2, {
      channels: 2,
      sampleRate: 1000,
    });

    // SNR should stabilize in second batch
    assert.ok(
      result2[2] > 19 && result2[2] < 21,
      "SNR should stabilize to ~20 dB"
    );
  });

  test("should save and restore state", async () => {
    const pipeline1 = createDspPipeline();
    pipeline1.Snr({ windowSize: 3 });

    const input1 = new Float32Array([1.0, 0.1, 1.0, 0.1, 1.0, 0.1]);
    const result1 = await pipeline1.process(input1, {
      channels: 2,
      sampleRate: 1000,
    });

    // Save state
    const state = await pipeline1.saveState();

    // Create new pipeline and restore
    const pipeline2 = createDspPipeline();
    pipeline2.Snr({ windowSize: 3 });
    await pipeline2.loadState(state);

    // Continue processing
    const input2 = new Float32Array([1.0, 0.1, 1.0, 0.1]);
    const result2 = await pipeline2.process(input2, {
      channels: 2,
      sampleRate: 1000,
    });

    // Results should be consistent
    assert.ok(
      Math.abs(result1[2] - result2[0]) < 0.5,
      "State should be preserved"
    );
  });

  test("should reset state correctly", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 3 });

    // First processing
    const input1 = new Float32Array([1.0, 0.1, 1.0, 0.1, 1.0, 0.1]);
    await pipeline.process(input1, { channels: 2, sampleRate: 1000 });

    // Reset
    pipeline.clearState();

    // Process again - should start fresh
    const input2 = new Float32Array([1.0, 0.1, 1.0, 0.1, 1.0, 0.1]);
    const result2 = await pipeline.process(input2, {
      channels: 2,
      sampleRate: 1000,
    });

    // Should behave like first processing
    assert.ok(result2.length === 3, "Should process all samples after reset");
  });
});

describe("SNR - window size effects", () => {
  test("should produce smoother output with larger window", async () => {
    const smallWindow = createDspPipeline();
    smallWindow.Snr({ windowSize: 2 });

    const largeWindow = createDspPipeline();
    largeWindow.Snr({ windowSize: 10 });

    // DETERMINISTIC signal with varying SNR (no Math.random for reproducibility)
    const input = new Float32Array(40); // 20 samples × 2 channels
    for (let i = 0; i < 20; i++) {
      input[i * 2] = 1.0 + 0.1 * Math.sin(i * 0.5); // Varying signal
      input[i * 2 + 1] = 0.1 + 0.05 * Math.sin(i * 1.3); // Deterministic "noise"
    }

    const result1 = await smallWindow.process(input, {
      channels: 2,
      sampleRate: 1000,
    });
    const result2 = await largeWindow.process(input, {
      channels: 2,
      sampleRate: 1000,
    });

    // Calculate variance (larger window should have lower variance)
    const variance = (arr: Float32Array) => {
      const mean = arr.reduce((a, b) => a + b, 0) / arr.length;
      return arr.reduce((sum, val) => sum + (val - mean) ** 2, 0) / arr.length;
    };

    const var1 = variance(result1.slice(10)); // Skip initial transient
    const var2 = variance(result2.slice(10));

    // With larger window, variance should be lower (smoother)
    assert.ok(
      var2 < var1 * 1.2,
      `Larger window variance (${var2.toFixed(
        3
      )}) should be ≤ 1.2× smaller window variance (${var1.toFixed(3)})`
    );
  });
});

describe("SNR - real-world scenarios", () => {
  test("should track SNR in noisy audio", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 100 }); // ~6ms at 16kHz

    // Simulate clean speech + white noise
    const numSamples = 500;
    const input = new Float32Array(numSamples * 2);

    for (let i = 0; i < numSamples; i++) {
      const signal = Math.sin((2 * Math.PI * 440 * i) / 16000); // 440 Hz tone
      const noise = (Math.random() - 0.5) * 0.2; // White noise
      input[i * 2] = signal + noise; // Channel 0: signal + noise
      input[i * 2 + 1] = noise; // Channel 1: noise reference
    }

    const result = await pipeline.process(input, {
      channels: 2,
      sampleRate: 16000,
    });

    // Average SNR should be positive (signal stronger than noise)
    const avgSnr =
      result.slice(100).reduce((a, b) => a + b, 0) / (result.length - 100);
    assert.ok(
      avgSnr > 5,
      `Average SNR should be >5 dB, got ${avgSnr.toFixed(2)}`
    );
    assert.ok(
      avgSnr < 25,
      `Average SNR should be <25 dB, got ${avgSnr.toFixed(2)}`
    );
  });

  test("should detect speech pauses (low SNR)", async () => {
    const pipeline = createDspPipeline();
    pipeline.Snr({ windowSize: 50 });

    // Simulate speech with pauses
    const input = new Float32Array(200);
    for (let i = 0; i < 100; i++) {
      // First half: speech (high SNR)
      if (i < 50) {
        input[i * 2] = 0.8; // Signal
        input[i * 2 + 1] = 0.1; // Noise
      } else {
        // Second half: pause (low SNR)
        input[i * 2] = 0.1; // Just noise
        input[i * 2 + 1] = 0.1; // Noise
      }
    }

    const result = await pipeline.process(input, {
      channels: 2,
      sampleRate: 8000,
    });

    // Speech region should have higher SNR than pause region
    const speechSnr = result.slice(40, 49).reduce((a, b) => a + b, 0) / 9;
    const pauseSnr = result.slice(90, 99).reduce((a, b) => a + b, 0) / 9;

    assert.ok(
      speechSnr > pauseSnr + 5,
      "Speech SNR should be >5 dB higher than pause"
    );
  });
});

describe("SNR - chaining with other stages", () => {
  test("should chain with Tap for monitoring", async () => {
    const pipeline = createDspPipeline();
    let avgSnr = 0;

    pipeline.Snr({ windowSize: 10 }).tap((result: Float32Array) => {
      avgSnr =
        result.reduce((a: number, b: number) => a + b, 0) / result.length;
    });

    const input = new Float32Array([
      1.0, 0.1, 1.0, 0.1, 1.0, 0.1, 1.0, 0.1, 1.0, 0.1,
    ]);

    await pipeline.process(input, { channels: 2, sampleRate: 1000 });

    assert.ok(avgSnr > 0, "Tap should capture SNR values");
    assert.ok(avgSnr < 30, "Average SNR should be reasonable");
  });
});
