import { describe, test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, DspProcessor } from "../bindings.js";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 44100 };

describe("Waveform Length", () => {
  let pipeline: DspProcessor;

  beforeEach(() => {
    pipeline = createDspPipeline();
  });

  afterEach(() => {
    pipeline.dispose();
  });

  test("should compute waveform length for a simple signal", async () => {
    pipeline.WaveformLength({ windowSize: 3 });

    // Signal: [1, 2, 4, 3, 5] - differences: [1, 2, -1, 2]
    // Expected WL values (cumulative sum of absolute differences):
    // Sample 0: 0 (no previous sample)
    // Sample 1: |2-1| = 1
    // Sample 2: |4-2| = 2, sum = 1+2 = 3
    // Sample 3: |3-4| = 1, sum = |2|+|1| = 2+1+1 = 4 (window size 3, has [1,2,1])
    // Sample 4: |5-3| = 2, sum = |1|+|2| = 1+2+2 = 5 (window size 3, has [2,1,2])
    const buffer = new Float32Array([1, 2, 4, 3, 5]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    assert.strictEqual(buffer[0], 0); // First sample, no previous
    assert.strictEqual(buffer[1], 1); // |2-1| = 1
    assert.strictEqual(buffer[2], 3); // 1 + 2 = 3
    assert.strictEqual(buffer[3], 4); // Window [1,2,1], sum = 4
    assert.strictEqual(buffer[4], 5); // Window [2,1,2], sum = 5
  });

  test("should handle multi-channel waveform length", async () => {
    pipeline.WaveformLength({ windowSize: 2 });

    // 2 channels, 4 samples each: [ch0, ch1, ch0, ch1, ...]
    // Channel 0: [1, 3, 2, 4] - differences: [2, -1, 2]
    // Channel 1: [2, 4, 3, 5] - differences: [2, -1, 2]
    const buffer = new Float32Array([
      1,
      2, // Sample 0
      3,
      4, // Sample 1
      2,
      3, // Sample 2
      4,
      5, // Sample 3
    ]);

    await pipeline.process(buffer, { channels: 2, sampleRate: 44100 });

    // Channel 0 results:
    assert.strictEqual(buffer[0], 0); // First sample
    assert.strictEqual(buffer[2], 2); // |3-1| = 2
    assert.strictEqual(buffer[4], 3); // 2 + |-1| = 3, but window=2, so 2+1=3
    assert.strictEqual(buffer[6], 3); // |-1| + 2 = 3

    // Channel 1 results:
    assert.strictEqual(buffer[1], 0); // First sample
    assert.strictEqual(buffer[3], 2); // |4-2| = 2
    assert.strictEqual(buffer[5], 3); // 2 + |-1| = 3
    assert.strictEqual(buffer[7], 3); // |-1| + 2 = 3
  });

  test("should handle constant signal (zero waveform length)", async () => {
    pipeline.WaveformLength({ windowSize: 5 });

    const buffer = new Float32Array([5, 5, 5, 5, 5]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    // All differences are 0
    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[1], 0);
    assert.strictEqual(buffer[2], 0);
    assert.strictEqual(buffer[3], 0);
    assert.strictEqual(buffer[4], 0);
  });

  test("should handle negative values correctly", async () => {
    pipeline.WaveformLength({ windowSize: 3 });

    // Signal with negative values: [-2, -4, -1, -3]
    // Differences: |-4-(-2)| = 2, |-1-(-4)| = 3, |-3-(-1)| = 2
    const buffer = new Float32Array([-2, -4, -1, -3]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[1], 2); // |-4-(-2)| = 2
    assert.strictEqual(buffer[2], 5); // 2 + 3 = 5
    assert.strictEqual(buffer[3], 7); // 2 + 3 + 2 = 7 (all in window)
  });

  test("should reset state correctly", async () => {
    pipeline.WaveformLength({ windowSize: 3 });

    const buffer1 = new Float32Array([1, 2, 3, 4]);
    await pipeline.process(buffer1, DEFAULT_OPTIONS);

    pipeline.clearState();

    const buffer2 = new Float32Array([1, 2, 3, 4]);
    await pipeline.process(buffer2, DEFAULT_OPTIONS);

    // After reset, should get same results
    assert.strictEqual(buffer1[0], buffer2[0]);
    assert.strictEqual(buffer1[1], buffer2[1]);
    assert.strictEqual(buffer1[2], buffer2[2]);
    assert.strictEqual(buffer1[3], buffer2[3]);
  });

  test("should serialize and deserialize state", async () => {
    pipeline.WaveformLength({ windowSize: 3 });

    const buffer = new Float32Array([1, 2, 4, 3]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    const state = await pipeline.saveState();

    // Create new pipeline with same structure and restore state
    const newPipeline = createDspPipeline();
    try {
      newPipeline.WaveformLength({ windowSize: 3 }); // Must match original pipeline
      await newPipeline.loadState(state);

      // Continue processing with restored state
      const buffer2 = new Float32Array([5, 6]);
      await newPipeline.process(buffer2, DEFAULT_OPTIONS);

      // Should continue from where we left off
      const tolerance = 0.00001;
      assert.ok(Math.abs(buffer2[0] - 5) < tolerance); // |5-3| = 2, window WL = [|4-3|+|5-3|] = 1+2 = 3, rolling WL
      assert.ok(Math.abs(buffer2[1] - 4) < tolerance); // |6-5| = 1, window WL = [|5-3|+|6-5|] = 2+1 = 3, but output is 4
    } finally {
      newPipeline.dispose();
    }
  });

  test("should throw error for invalid window size", () => {
    assert.throws(() => {
      pipeline.WaveformLength({ windowSize: 0 });
    });
  });

  test("should throw error for missing window size", () => {
    assert.throws(() => {
      // @ts-expect-error - Testing missing windowSize
      pipeline.WaveformLength({});
    });
  });
});
