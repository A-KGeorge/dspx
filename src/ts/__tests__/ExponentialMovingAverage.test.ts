import { describe, test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, DspProcessor } from "../bindings.js";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 1000 };

function assertCloseTo(actual: number, expected: number, precision = 5) {
  const tolerance = Math.pow(10, -precision);
  assert.ok(
    Math.abs(actual - expected) < tolerance,
    `Expected ${actual} to be close to ${expected} (tolerance: ${tolerance})`
  );
}

describe("ExponentialMovingAverage Stage", () => {
  let processor: DspProcessor;

  beforeEach(() => {
    processor = createDspPipeline();
  });

  afterEach(() => {
    processor.dispose();
  });

  describe("Batch Mode", () => {
    test("should calculate EMA correctly for single channel in batch mode", async () => {
      // Add EMA stage with alpha = 0.5
      processor.ExponentialMovingAverage({ mode: "batch", alpha: 0.5 });

      const input = new Float32Array([1, 2, 3, 4, 5]);
      const result = await processor.process(input, DEFAULT_OPTIONS);

      // EMA calculation with alpha = 0.5:
      // ema[0] = 1 (first sample)
      // ema[1] = 0.5 * 2 + 0.5 * 1 = 1.5
      // ema[2] = 0.5 * 3 + 0.5 * 1.5 = 2.25
      // ema[3] = 0.5 * 4 + 0.5 * 2.25 = 3.125
      // ema[4] = 0.5 * 5 + 0.5 * 3.125 = 4.0625
      assert.strictEqual(result.length, 5);
      assertCloseTo(result[0], 1, 5);
      assertCloseTo(result[1], 1.5, 5);
      assertCloseTo(result[2], 2.25, 5);
      assertCloseTo(result[3], 3.125, 5);
      assertCloseTo(result[4], 4.0625, 5);
    });

    test("should reset state between batch chunks", async () => {
      processor.ExponentialMovingAverage({ mode: "batch", alpha: 0.5 });

      // First chunk
      const input1 = new Float32Array([1, 2, 3]);
      const result1 = await processor.process(input1, DEFAULT_OPTIONS);

      // Second chunk should reset and start fresh
      const input2 = new Float32Array([10, 20, 30]);
      const result2 = await processor.process(input2, DEFAULT_OPTIONS);
      // Second chunk should start from 10, not continue from previous EMA
      assertCloseTo(result2[0], 10, 5);
      assertCloseTo(result2[1], 15, 5); // 0.5 * 20 + 0.5 * 10
      assertCloseTo(result2[2], 22.5, 5); // 0.5 * 30 + 0.5 * 15
    });

    test("should handle multiple channels independently in batch mode", async () => {
      const multiChannelProcessor = createDspPipeline();
      multiChannelProcessor.ExponentialMovingAverage({
        mode: "batch",
        alpha: 0.3,
      });

      const input = new Float32Array([
        10,
        5, // Sample 0: [Ch0, Ch1]
        20,
        10, // Sample 1: [Ch0, Ch1]
        30,
        15, // Sample 2: [Ch0, Ch1]
      ]);

      const result = await multiChannelProcessor.process(input, {
        channels: 2,
        sampleRate: 1000,
      });

      // Channel 0: alpha = 0.3
      // ema[0] = 10
      // ema[1] = 0.3 * 20 + 0.7 * 10 = 13
      // ema[2] = 0.3 * 30 + 0.7 * 13 = 18.1
      assertCloseTo(result[0], 10, 5); // Ch0 sample 0
      assertCloseTo(result[2], 13, 5); // Ch0 sample 1
      assertCloseTo(result[4], 18.1, 5); // Ch0 sample 2

      // Channel 1
      // ema[0] = 5
      // ema[1] = 0.3 * 10 + 0.7 * 5 = 6.5
      // ema[2] = 0.3 * 15 + 0.7 * 6.5 = 9.05
      assertCloseTo(result[1], 5, 5); // Ch1 sample 0
      assertCloseTo(result[3], 6.5, 5); // Ch1 sample 1
      assertCloseTo(result[5], 9.05, 5); // Ch1 sample 2

      multiChannelProcessor.dispose();
    });
  });

  describe("Moving Mode", () => {
    test("should maintain state across chunks in moving mode", async () => {
      processor.ExponentialMovingAverage({ mode: "moving", alpha: 0.5 });

      // First chunk
      const input1 = new Float32Array([2, 4, 6]);
      const result1 = await processor.process(input1, DEFAULT_OPTIONS);

      // EMA at end of first chunk
      // ema[0] = 2
      // ema[1] = 0.5 * 4 + 0.5 * 2 = 3
      // ema[2] = 0.5 * 6 + 0.5 * 3 = 4.5
      assertCloseTo(result1[2], 4.5, 5);

      // Second chunk should continue from 4.5
      const input2 = new Float32Array([8, 10]);
      const result2 = await processor.process(input2, DEFAULT_OPTIONS);

      // ema[3] = 0.5 * 8 + 0.5 * 4.5 = 6.25
      // ema[4] = 0.5 * 10 + 0.5 * 6.25 = 8.125
      assertCloseTo(result2[0], 6.25, 5);
      assertCloseTo(result2[1], 8.125, 5);
    });

    test("should maintain state across chunks without serialization", async () => {
      processor.ExponentialMovingAverage({ mode: "moving", alpha: 0.4 });

      // Process first chunk
      const input1 = new Float32Array([5, 10, 15]);
      await processor.process(input1, DEFAULT_OPTIONS);

      // Process next chunk - should continue from previous state
      const input2 = new Float32Array([20, 25]);
      const result2 = await processor.process(input2, DEFAULT_OPTIONS);
      // Should continue from previous state
      // Previous EMA: 5 -> 7 -> 10.2
      // ema[3] = 0.4 * 20 + 0.6 * 10.2 = 14.12
      // ema[4] = 0.4 * 25 + 0.6 * 14.12 = 18.472
      assertCloseTo(result2[0], 14.12, 4);
      assertCloseTo(result2[1], 18.472, 4);
    });

    test("should handle multiple channels with state persistence", async () => {
      const multiChannelProcessor = createDspPipeline();
      multiChannelProcessor.ExponentialMovingAverage({
        mode: "moving",
        alpha: 0.6,
      });

      // First chunk - 2 channels, 2 samples
      const input1 = new Float32Array([
        10,
        5, // Sample 0: [Ch0, Ch1]
        20,
        15, // Sample 1: [Ch0, Ch1]
      ]);
      await multiChannelProcessor.process(input1, {
        channels: 2,
        sampleRate: 1000,
      });

      // Second chunk
      const input2 = new Float32Array([30, 25]); // Sample 2: [Ch0, Ch1]
      const result2 = await multiChannelProcessor.process(input2, {
        channels: 2,
        sampleRate: 1000,
      });

      // Channel 0: 10 -> 16 -> 24.4
      // ema[2] = 0.6 * 30 + 0.4 * 16 = 24.4
      assertCloseTo(result2[0], 24.4, 5);

      // Channel 1: 5 -> 11 -> 19.4
      // ema[2] = 0.6 * 25 + 0.4 * 11 = 19.4
      assertCloseTo(result2[1], 19.4, 5);

      multiChannelProcessor.dispose();
    });
  });

  describe("Edge Cases", () => {
    test("should handle alpha = 1.0 (no smoothing)", async () => {
      processor.ExponentialMovingAverage({ mode: "batch", alpha: 1.0 });

      const input = new Float32Array([3, 7, 2, 9]);
      const result = await processor.process(input, DEFAULT_OPTIONS);

      // With alpha = 1.0, EMA should equal input samples (no smoothing)
      assert.strictEqual(result.length, 4);
      assertCloseTo(result[0], 3, 5);
      assertCloseTo(result[1], 7, 5);
      assertCloseTo(result[2], 2, 5);
      assertCloseTo(result[3], 9, 5);
    });

    test("should handle small alpha values (heavy smoothing)", async () => {
      processor.ExponentialMovingAverage({ mode: "batch", alpha: 0.1 });

      const input = new Float32Array([100, 100, 100, 100, 0]); // Step change at end

      const result = await processor.process(input, DEFAULT_OPTIONS);

      // With small alpha, EMA should change slowly
      // ema[0] = 100
      // ema[1] = 0.1 * 100 + 0.9 * 100 = 100
      // ema[2] = 0.1 * 100 + 0.9 * 100 = 100
      // ema[3] = 0.1 * 100 + 0.9 * 100 = 100
      // ema[4] = 0.1 * 0 + 0.9 * 100 = 90
      assertCloseTo(result[4], 90, 5);
    });

    test("should throw error for invalid alpha", () => {
      // Alpha must be in range (0, 1]
      assert.throws(() => {
        processor.ExponentialMovingAverage({ mode: "batch", alpha: 0 });
      });

      assert.throws(() => {
        processor.ExponentialMovingAverage({ mode: "batch", alpha: 1.5 });
      });

      assert.throws(() => {
        processor.ExponentialMovingAverage({ mode: "batch", alpha: -0.5 });
      });
    });

    test("should handle single sample input", async () => {
      processor.ExponentialMovingAverage({ mode: "batch", alpha: 0.5 });

      const input = new Float32Array([42]);
      const result = await processor.process(input, DEFAULT_OPTIONS);

      // First sample is always the input value
      assertCloseTo(result[0], 42, 5);
    });

    test("should handle empty input gracefully", async () => {
      processor.ExponentialMovingAverage({ mode: "batch", alpha: 0.5 });

      const input = new Float32Array([]);
      const result = await processor.process(input, DEFAULT_OPTIONS);
      assert.strictEqual(result.length, 0);
    });
  });

  describe("Different Alpha Values", () => {
    test("should respond faster with higher alpha", async () => {
      const input = new Float32Array([0, 0, 0, 100]); // Step change

      // High alpha (fast response)
      const processor1 = createDspPipeline();
      processor1.ExponentialMovingAverage({ mode: "batch", alpha: 0.8 });
      const result1 = await processor1.process(input, DEFAULT_OPTIONS);

      // Low alpha (slow response)
      const processor2 = createDspPipeline();
      processor2.ExponentialMovingAverage({ mode: "batch", alpha: 0.2 });
      const result2 = await processor2.process(input, DEFAULT_OPTIONS);

      // High alpha should reach closer to 100
      assert.ok(result1[3] > result2[3]);

      processor1.dispose();
      processor2.dispose();
    });
  });
});
