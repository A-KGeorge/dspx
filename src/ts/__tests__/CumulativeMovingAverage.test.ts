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

describe("CumulativeMovingAverage Stage", () => {
  let processor: DspProcessor;

  beforeEach(() => {
    processor = createDspPipeline();
  });

  afterEach(() => {
    processor.dispose();
  });

  describe("Batch Mode", () => {
    test("should calculate CMA correctly for single channel in batch mode", async () => {
      processor.CumulativeMovingAverage({ mode: "batch" });

      const input = new Float32Array([10, 20, 30, 40, 50]);
      const result = await processor.process(input, DEFAULT_OPTIONS);

      // CMA calculation (cumulative average):
      // cma[0] = 10 / 1 = 10
      // cma[1] = (10 + 20) / 2 = 15
      // cma[2] = (10 + 20 + 30) / 3 = 20
      // cma[3] = (10 + 20 + 30 + 40) / 4 = 25
      // cma[4] = (10 + 20 + 30 + 40 + 50) / 5 = 30
      assert.strictEqual(result.length, 5);
      assertCloseTo(result[0], 10, 5);
      assertCloseTo(result[1], 15, 5);
      assertCloseTo(result[2], 20, 5);
      assertCloseTo(result[3], 25, 5);
      assertCloseTo(result[4], 30, 5);
    });

    test("should reset state between batch chunks", async () => {
      processor.CumulativeMovingAverage({ mode: "batch" });

      // First chunk
      const input1 = new Float32Array([5, 10, 15]);
      const result1 = await processor.process(input1, DEFAULT_OPTIONS);

      // CMA at end of first chunk: (5 + 10 + 15) / 3 = 10
      assertCloseTo(result1[2], 10, 5);

      // Second chunk should reset and start fresh
      const input2 = new Float32Array([100, 200]);
      const result2 = await processor.process(input2, DEFAULT_OPTIONS);

      // Second chunk resets: starts at 100, then (100 + 200) / 2 = 150
      assertCloseTo(result2[0], 100, 5);
      assertCloseTo(result2[1], 150, 5);
    });

    test("should handle multiple channels independently in batch mode", async () => {
      const multiChannelProcessor = createDspPipeline();
      multiChannelProcessor.CumulativeMovingAverage({ mode: "batch" });

      const input = new Float32Array([
        10,
        5, // Sample 0: [Ch0, Ch1]
        20,
        15, // Sample 1: [Ch0, Ch1]
        30,
        25, // Sample 2: [Ch0, Ch1]
      ]);

      const result = await multiChannelProcessor.process(input, {
        channels: 2,
        sampleRate: 1000,
      });

      // Channel 0: 10, 15, 20
      assertCloseTo(result[0], 10, 5); // Ch0 sample 0
      assertCloseTo(result[2], 15, 5); // Ch0 sample 1
      assertCloseTo(result[4], 20, 5); // Ch0 sample 2

      // Channel 1: 5, 10, 15
      assertCloseTo(result[1], 5, 5); // Ch1 sample 0
      assertCloseTo(result[3], 10, 5); // Ch1 sample 1
      assertCloseTo(result[5], 15, 5); // Ch1 sample 2

      multiChannelProcessor.dispose();
    });
  });

  describe("Moving Mode", () => {
    test("should maintain state across chunks in moving mode", async () => {
      processor.CumulativeMovingAverage({ mode: "moving" });

      // First chunk
      const input1 = new Float32Array([10, 20, 30]);
      const result1 = await processor.process(input1, DEFAULT_OPTIONS);

      // CMA at end of first chunk: (10 + 20 + 30) / 3 = 20
      assertCloseTo(result1[0], 10, 5);
      assertCloseTo(result1[1], 15, 5);
      assertCloseTo(result1[2], 20, 5);

      // Second chunk should continue accumulating
      const input2 = new Float32Array([40, 50]);
      const result2 = await processor.process(input2, DEFAULT_OPTIONS);

      // cma[3] = (10 + 20 + 30 + 40) / 4 = 25
      // cma[4] = (10 + 20 + 30 + 40 + 50) / 5 = 30
      assertCloseTo(result2[0], 25, 5);
      assertCloseTo(result2[1], 30, 5);
    });

    test("should maintain state across chunks automatically", async () => {
      processor.CumulativeMovingAverage({ mode: "moving" });

      // Process first chunk
      const input1 = new Float32Array([2, 4, 6, 8]);
      await processor.process(input1, DEFAULT_OPTIONS);
      // State: sum = 20, count = 4, cma = 5

      // In moving mode, state is maintained automatically across calls
      // Process next chunk - it should continue from previous state
      const input2 = new Float32Array([10, 12]);
      const result2 = await processor.process(input2, DEFAULT_OPTIONS);

      // Should continue from previous state
      // cma[4] = (2 + 4 + 6 + 8 + 10) / 5 = 6
      // cma[5] = (2 + 4 + 6 + 8 + 10 + 12) / 6 = 7
      assertCloseTo(result2[0], 6, 5);
      assertCloseTo(result2[1], 7, 5);
    });

    test("should handle multiple channels with state persistence", async () => {
      const multiChannelProcessor = createDspPipeline();
      multiChannelProcessor.CumulativeMovingAverage({ mode: "moving" });

      // First chunk
      const input1 = new Float32Array([
        10,
        5, // Sample 0: [Ch0, Ch1]
        30,
        15, // Sample 1: [Ch0, Ch1]
      ]);
      await multiChannelProcessor.process(input1, {
        channels: 2,
        sampleRate: 1000,
      });

      // Second chunk
      const input2 = new Float32Array([50, 25]); // Sample 2: [Ch0, Ch1]
      const result2 = await multiChannelProcessor.process(input2, {
        channels: 2,
        sampleRate: 1000,
      });

      // Channel 0: (10 + 30 + 50) / 3 = 30
      assertCloseTo(result2[0], 30, 5);

      // Channel 1: (5 + 15 + 25) / 3 = 15
      assertCloseTo(result2[1], 15, 5);

      multiChannelProcessor.dispose();
    });

    test("should accumulate over long sequences", async () => {
      processor.CumulativeMovingAverage({ mode: "moving" });

      // Process multiple chunks
      for (let i = 0; i < 5; i++) {
        const input = new Float32Array([100, 100]); // Constant value
        await processor.process(input, DEFAULT_OPTIONS);
      }

      // After 10 samples of value 100, CMA should be 100
      const finalInput = new Float32Array([100]);
      const result = await processor.process(finalInput, DEFAULT_OPTIONS);

      assertCloseTo(result[0], 100, 5);
    });
  });

  describe("Edge Cases", () => {
    test("should handle single sample input", async () => {
      processor.CumulativeMovingAverage({ mode: "batch" });

      const input = new Float32Array([42]);
      const result = await processor.process(input, DEFAULT_OPTIONS);

      // First sample CMA equals the sample itself
      assertCloseTo(result[0], 42, 5);
    });

    test("should handle empty input gracefully", async () => {
      processor.CumulativeMovingAverage({ mode: "batch" });

      const input = new Float32Array([]);
      const result = await processor.process(input, DEFAULT_OPTIONS);
      assert.strictEqual(result.length, 0);
    });

    test("should handle negative values", async () => {
      processor.CumulativeMovingAverage({ mode: "batch" });

      const input = new Float32Array([-10, -20, -30]);
      const result = await processor.process(input, DEFAULT_OPTIONS);

      // cma[0] = -10
      // cma[1] = (-10 + -20) / 2 = -15
      // cma[2] = (-10 + -20 + -30) / 3 = -20
      assertCloseTo(result[0], -10, 5);
      assertCloseTo(result[1], -15, 5);
      assertCloseTo(result[2], -20, 5);
    });

    test("should handle mixed positive and negative values", async () => {
      processor.CumulativeMovingAverage({ mode: "batch" });

      const input = new Float32Array([10, -10, 20, -20]);
      const result = await processor.process(input, DEFAULT_OPTIONS);

      // cma[0] = 10
      // cma[1] = (10 + -10) / 2 = 0
      // cma[2] = (10 + -10 + 20) / 3 = 6.666...
      // cma[3] = (10 + -10 + 20 + -20) / 4 = 0
      assertCloseTo(result[0], 10, 5);
      assertCloseTo(result[1], 0, 5);
      assertCloseTo(result[2], 6.666666, 5);
      assertCloseTo(result[3], 0, 5);
    });

    test("should handle very large accumulated counts", async () => {
      processor.CumulativeMovingAverage({ mode: "moving" });

      // Process many samples
      for (let i = 0; i < 100; i++) {
        const input = new Float32Array([i]);
        await processor.process(input, DEFAULT_OPTIONS);
      }

      // Final CMA should be average of 0..99 = 49.5
      const finalInput = new Float32Array([100]);
      const result = await processor.process(finalInput, DEFAULT_OPTIONS);

      // Average of 0..100 = 50.5
      assertCloseTo(result[0], 50, 1);
    });
  });

  describe("Comparison with Simple Moving Average", () => {
    test("should differ from SMA for non-uniform sequences", async () => {
      const input = new Float32Array([1, 2, 3, 100]); // Outlier at end

      // CMA accumulates all history
      processor.CumulativeMovingAverage({ mode: "batch" });
      const cmaResult = await processor.process(input, DEFAULT_OPTIONS);

      // CMA at index 3: (1 + 2 + 3 + 100) / 4 = 26.5
      assertCloseTo(cmaResult[3], 26.5, 5);

      // For comparison, if this were a windowed MA with window=2:
      // It would only consider [3, 100] at index 3
      // But CMA considers entire history [1, 2, 3, 100]
    });
  });

  describe("Convergence Properties", () => {
    test("should converge to constant value for constant input", async () => {
      processor.CumulativeMovingAverage({ mode: "moving" });

      // Feed constant value
      for (let i = 0; i < 10; i++) {
        const input = new Float32Array([50]);
        await processor.process(input, DEFAULT_OPTIONS);
      }

      const input = new Float32Array([50]);
      const result = await processor.process(input, DEFAULT_OPTIONS);

      // All samples are 50, so CMA should be 50
      assertCloseTo(result[0], 50, 5);
    });

    test("should show diminishing influence of new samples over time", async () => {
      processor.CumulativeMovingAverage({ mode: "moving" });

      // Establish baseline with many zeros
      for (let i = 0; i < 100; i++) {
        const input = new Float32Array([0]);
        await processor.process(input, DEFAULT_OPTIONS);
      }

      // Add a spike
      const spikeInput = new Float32Array([1000]);
      const spikeResult = await processor.process(spikeInput, DEFAULT_OPTIONS);

      // With 100 zeros and 1 spike, CMA = 1000/101 ≈ 9.9
      // The spike has less influence due to large history
      assertCloseTo(spikeResult[0], 9.9, 1);
    });
  });
});
