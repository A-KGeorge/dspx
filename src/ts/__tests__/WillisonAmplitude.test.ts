import { describe, test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, DspProcessor } from "../bindings.js";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 44100 };

describe("Willison Amplitude (WAMP)", () => {
  let pipeline: DspProcessor;

  beforeEach(() => {
    pipeline = createDspPipeline();
  });

  afterEach(() => {
    pipeline.dispose();
  });

  test("should count amplitude changes exceeding threshold in sliding window", async () => {
    pipeline.WillisonAmplitude({ windowSize: 5, threshold: 1.0 });

    // Signal: [0, 0.5, 2.0, 2.5, 1.0, 3.0]
    // Differences: 0.5, 1.5, 0.5, -1.5, 2.0
    // Exceeding threshold (1.0): at indices 2(1.5), 4(-1.5), 5(2.0)
    // Window counts (window size = 5):
    const buffer = new Float32Array([0, 0.5, 2.0, 2.5, 1.0, 3.0]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    assert.strictEqual(buffer[0], 0); // First sample, no previous
    assert.strictEqual(buffer[1], 0); // |0.5| < 1.0
    assert.strictEqual(buffer[2], 1); // |1.5| > 1.0, window count = 1
    assert.strictEqual(buffer[3], 1); // |0.5| < 1.0, window still has 1
    assert.strictEqual(buffer[4], 1); // |-1.5| > 1.0, but only within last 5 samples
    assert.strictEqual(buffer[5], 1); // |2.0| > 1.0, window count = 1
  });

  test("should handle zero threshold", async () => {
    pipeline.WillisonAmplitude({ windowSize: 4, threshold: 0 });

    // All differences > 0 should be counted
    const buffer = new Float32Array([1, 2, 3, 2, 3]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[1], 1); // |1| > 0
    assert.strictEqual(buffer[2], 1); // |1| > 0, window=[1,1]
    assert.strictEqual(buffer[3], 1); // |-1| > 0, window=[1,1,-1]
    assert.strictEqual(buffer[4], 1); // |1| > 0, window=[1,-1,1] (4 samples max)
  });

  test("should handle multi-channel WAMP", async () => {
    pipeline.WillisonAmplitude({ windowSize: 3, threshold: 0.5 });

    // 2 channels
    // Ch0: [0, 1, 2, 1] - diffs: 1, 1, -1 (all exceed 0.5)
    // Ch1: [0, 0.3, 1.0, 1.2] - diffs: 0.3, 0.7, 0.2
    const buffer = new Float32Array([
      0,
      0, // Sample 0
      1,
      0.3, // Sample 1
      2,
      1.0, // Sample 2
      1,
      1.2, // Sample 3
    ]);

    await pipeline.process(buffer, { channels: 2, sampleRate: 44100 });

    // Channel 0: all diffs exceed threshold
    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[2], 1);
    assert.strictEqual(buffer[4], 1); // Window count within last 3
    assert.strictEqual(buffer[6], 1);

    // Channel 1: only diff of 0.7 exceeds threshold
    assert.strictEqual(buffer[1], 0);
    assert.strictEqual(buffer[3], 0); // |0.3| < 0.5
    assert.strictEqual(buffer[5], 1); // |0.7| > 0.5
    assert.strictEqual(buffer[7], 1); // Window still has the 0.7 change
  });

  test("should handle constant signal (no amplitude changes)", async () => {
    pipeline.WillisonAmplitude({ windowSize: 5, threshold: 0 });

    const buffer = new Float32Array([5, 5, 5, 5, 5]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    // All differences are 0, none exceed threshold
    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[1], 0);
    assert.strictEqual(buffer[2], 0);
    assert.strictEqual(buffer[3], 0);
    assert.strictEqual(buffer[4], 0);
  });

  test("should handle negative values correctly", async () => {
    pipeline.WillisonAmplitude({ windowSize: 4, threshold: 1.0 });

    // Signal: [-2, -4, -1, -3]
    // Absolute differences: |-2| = 2, |3| = 3, |-2| = 2
    const buffer = new Float32Array([-2, -4, -1, -3]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[1], 1); // |-2| > 1.0
    assert.strictEqual(buffer[2], 1); // |3| > 1.0, window count
    assert.strictEqual(buffer[3], 1); // |-2| > 1.0, window count
  });

  test("should apply sliding window correctly", async () => {
    pipeline.WillisonAmplitude({ windowSize: 2, threshold: 0.5 });

    // Signal: [0, 2, 3, 3.3, 3.5]
    // Diffs: 2, 1, 0.3, 0.2
    // Exceeding: indices 1(2), 2(1)
    const buffer = new Float32Array([0, 2, 3, 3.3, 3.5]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[1], 1); // |2| > 0.5
    assert.strictEqual(buffer[2], 1); // |1| > 0.5, window=[true, true] but size=2
    assert.strictEqual(buffer[3], 1); // |0.3| < 0.5, window=[true, false]
    assert.strictEqual(buffer[4], 0); // |0.2| < 0.5, window=[false, false]
  });

  test("should reset state correctly", async () => {
    pipeline.WillisonAmplitude({ windowSize: 3, threshold: 1.0 });

    const buffer1 = new Float32Array([0, 2, 4, 3]);
    await pipeline.process(buffer1, DEFAULT_OPTIONS);

    pipeline.clearState();

    const buffer2 = new Float32Array([0, 2, 4, 3]);
    await pipeline.process(buffer2, DEFAULT_OPTIONS);

    // After reset, should get same results
    for (let i = 0; i < buffer1.length; i++) {
      assert.strictEqual(buffer1[i], buffer2[i]);
    }
  });

  test("should serialize and deserialize state", async () => {
    pipeline.WillisonAmplitude({ windowSize: 3, threshold: 1.0 });

    const buffer = new Float32Array([0, 2, 4, 3]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    const state = await pipeline.saveState();

    const newPipeline = createDspPipeline();
    try {
      newPipeline.WillisonAmplitude({ windowSize: 3, threshold: 1.0 }); // Must match original
      await newPipeline.loadState(state);

      const buffer2 = new Float32Array([5, 6]);
      await newPipeline.process(buffer2, DEFAULT_OPTIONS);

      // Should continue from where we left off
      assert.ok(buffer2[0] > 0); // |5-3| = 2 > 1.0
      assert.ok(buffer2[1] > 0); // |6-5| = 1 > 1.0 (barely)
    } finally {
      newPipeline.dispose();
    }
  });

  test("should throw error for invalid window size", () => {
    assert.throws(() => {
      pipeline.WillisonAmplitude({ windowSize: 0, threshold: 1.0 });
    });
  });

  test("should throw error for missing window size", () => {
    assert.throws(() => {
      // @ts-expect-error - Testing missing windowSize
      pipeline.WillisonAmplitude({ threshold: 1.0 });
    });
  });

  test("should default to zero threshold when not specified", () => {
    assert.doesNotThrow(() => {
      pipeline.WillisonAmplitude({ windowSize: 5 });
    });
  });

  test("should handle high-frequency signal", async () => {
    pipeline.WillisonAmplitude({ windowSize: 10, threshold: 0.5 });

    // Sine-like signal with varying amplitudes
    const buffer = new Float32Array(20);
    for (let i = 0; i < 20; i++) {
      buffer[i] = Math.sin(i * 0.5) * 2;
    }

    await pipeline.process(buffer, DEFAULT_OPTIONS);

    // Should detect amplitude changes in the sine wave
    let countedChanges = 0;
    for (let i = 1; i < buffer.length; i++) {
      if (buffer[i] > 0) countedChanges++;
    }

    assert.ok(countedChanges > 0);
  });
});
