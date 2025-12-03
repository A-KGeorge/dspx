import { describe, test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, DspProcessor } from "../bindings.js";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 44100 };

describe("Slope Sign Change (SSC)", () => {
  let pipeline: DspProcessor;

  beforeEach(() => {
    pipeline = createDspPipeline();
  });

  afterEach(() => {
    pipeline.dispose();
  });

  test("should count slope sign changes with zero threshold", async () => {
    pipeline.SlopeSignChange({ windowSize: 5, threshold: 0 });

    // Signal: [1, 3, 2, 4, 3, 5]
    // Slopes: +, -, +, -, +
    // Sign changes: at indices 2, 3, 4, 5
    // Windowed counting: returns count WITHIN last 5 samples
    const buffer = new Float32Array([1, 3, 2, 4, 3, 5]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    assert.strictEqual(buffer[0], 0); // Need 2 previous samples
    assert.strictEqual(buffer[1], 0); // Need 1 more previous sample
    assert.strictEqual(buffer[2], 0); // First sign change, but window filling
    assert.strictEqual(buffer[3], 1); // Sign change detected in window
    assert.strictEqual(buffer[4], 1); // Count within sliding window (window size 5)
    assert.strictEqual(buffer[5], 1); // Count within sliding window
  });

  test("should apply threshold correctly", async () => {
    pipeline.SlopeSignChange({ windowSize: 4, threshold: 1.0 });

    // Signal: [0, 0.5, 1.0, 0.5, 1.5, 0.5]
    // Differences: 0.5, 0.5, -0.5, 1.0, -1.0
    // With threshold 1.0, the PRODUCT of consecutive differences must exceed threshold
    // SSC checks: (diff1 * diff2) > threshold
    // Windowed counting: returns count WITHIN last 4 samples
    const buffer = new Float32Array([0, 0.5, 1.0, 0.5, 1.5, 0.5]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[1], 0);
    assert.strictEqual(buffer[2], 0); // Product: 0.5*0.5 = 0.25 < 1.0
    assert.strictEqual(buffer[3], 0); // Product: 0.5*-0.5 = -0.25 < 1.0
    assert.strictEqual(buffer[4], 0); // Product: -0.5*1.0 = -0.5 < 1.0
    assert.strictEqual(buffer[5], 0); // Product: 1.0*-1.0 = -1.0 < 1.0
  });

  test("should handle multi-channel SSC", async () => {
    pipeline.SlopeSignChange({ windowSize: 3, threshold: 0 });

    // 2 channels
    // Ch0: [1, 2, 1, 2] - slopes: +, -, +
    // Ch1: [2, 1, 2, 1] - slopes: -, +, -
    // Windowed counting: returns count WITHIN last 3 samples
    const buffer = new Float32Array([
      1,
      2, // Sample 0
      2,
      1, // Sample 1
      1,
      2, // Sample 2
      2,
      1, // Sample 3
    ]);

    await pipeline.process(buffer, { channels: 2, sampleRate: 44100 });

    // Channel 0
    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[2], 0);
    assert.strictEqual(buffer[4], 0); // First sign change, but window filling
    assert.strictEqual(buffer[6], 1); // Count within sliding window (window size 3)

    // Channel 1
    assert.strictEqual(buffer[1], 0);
    assert.strictEqual(buffer[3], 0);
    assert.strictEqual(buffer[5], 0); // First sign change, but window filling
    assert.strictEqual(buffer[7], 1); // Count within sliding window
  });

  test("should handle monotonic signal (no sign changes)", async () => {
    pipeline.SlopeSignChange({ windowSize: 5, threshold: 0 });

    const buffer = new Float32Array([1, 2, 3, 4, 5]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    // All slopes are positive, no sign changes
    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[1], 0);
    assert.strictEqual(buffer[2], 0);
    assert.strictEqual(buffer[3], 0);
    assert.strictEqual(buffer[4], 0);
  });

  test("should handle constant signal", async () => {
    pipeline.SlopeSignChange({ windowSize: 4, threshold: 0 });

    const buffer = new Float32Array([5, 5, 5, 5]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    // No slopes, no sign changes
    assert.strictEqual(buffer[0], 0);
    assert.strictEqual(buffer[1], 0);
    assert.strictEqual(buffer[2], 0);
    assert.strictEqual(buffer[3], 0);
  });

  test("should reset state correctly", async () => {
    pipeline.SlopeSignChange({ windowSize: 3, threshold: 0 });

    const buffer1 = new Float32Array([1, 3, 2, 4]);
    await pipeline.process(buffer1, DEFAULT_OPTIONS);

    pipeline.clearState();

    const buffer2 = new Float32Array([1, 3, 2, 4]);
    await pipeline.process(buffer2, DEFAULT_OPTIONS);

    // After reset, should get same results
    for (let i = 0; i < buffer1.length; i++) {
      assert.strictEqual(buffer1[i], buffer2[i]);
    }
  });

  test("should serialize and deserialize state", async () => {
    pipeline.SlopeSignChange({ windowSize: 4, threshold: 0 });

    const buffer = new Float32Array([1, 3, 2, 4, 3]);
    await pipeline.process(buffer, DEFAULT_OPTIONS);

    const state = await pipeline.saveState();

    const newPipeline = createDspPipeline();
    newPipeline.SlopeSignChange({ windowSize: 4, threshold: 0 }); // Must match original pipeline
    await newPipeline.loadState(state);

    const buffer2 = new Float32Array([5, 4]);
    await newPipeline.process(buffer2, DEFAULT_OPTIONS);

    // Should continue counting from where we left off
    assert.ok(buffer2[0] > 0);
    assert.ok(buffer2[1] > 0);
  });

  test("should throw error for invalid window size", () => {
    assert.throws(() => {
      pipeline.SlopeSignChange({ windowSize: 0, threshold: 0 });
    });
  });

  test("should throw error for missing window size", () => {
    assert.throws(() => {
      // @ts-expect-error - Testing missing windowSize
      pipeline.SlopeSignChange({ threshold: 0 });
    });
  });

  test("should default to zero threshold when not specified", async () => {
    assert.doesNotThrow(() => {
      pipeline.SlopeSignChange({ windowSize: 5 });
    });
  });
});
