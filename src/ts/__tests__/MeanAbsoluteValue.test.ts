import { describe, test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, DspProcessor } from "../bindings.js";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 44100 };

function assertCloseTo(actual: number, expected: number, precision = 5) {
  const tolerance = Math.pow(10, -precision);
  assert.ok(
    Math.abs(actual - expected) < tolerance,
    `Expected ${actual} to be close to ${expected} (tolerance: ${tolerance})`
  );
}

describe("Mean Absolute Value Filter", () => {
  let processor: DspProcessor;

  beforeEach(() => {
    processor = createDspPipeline();
  });

  afterEach(() => {
    processor.dispose();
  });

  describe("Basic Functionality", () => {
    test("should compute MAV with window size 3", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([1, -2, 3, -4, 5]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // First value: |1| = 1
      // Second value: (|1| + |-2|) / 2 = 1.5
      // Third value: (|1| + |-2| + |3|) / 3 = 2.0
      // Fourth value: (|-2| + |3| + |-4|) / 3 = 3.0
      // Fifth value: (|3| + |-4| + |5|) / 3 = 4.0
      assert.equal(output.length, 5);
      assertCloseTo(output[0], 1.0);
      assertCloseTo(output[1], 1.5);
      assertCloseTo(output[2], 2.0);
      assertCloseTo(output[3], 3.0);
      assertCloseTo(output[4], 4.0);
    });

    test("should handle single sample window", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 1 });

      const input = new Float32Array([3, -4, 5]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // MAV of single sample is just the absolute value
      assertCloseTo(output[0], 3);
      assertCloseTo(output[1], 4);
      assertCloseTo(output[2], 5);
    });

    test("should compute MAV correctly for all negative values", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([-3, -4, -5, -12]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // First: |-3| = 3
      // Second: (|-3| + |-4|) / 2 = 3.5
      // Third: (|-4| + |-5|) / 2 = 4.5
      // Fourth: (|-5| + |-12|) / 2 = 8.5
      assertCloseTo(output[0], 3.0);
      assertCloseTo(output[1], 3.5);
      assertCloseTo(output[2], 4.5);
      assertCloseTo(output[3], 8.5);
    });

    test("should maintain state across multiple process calls", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      // First batch: [3, -4]
      const output1 = await processor.process(
        new Float32Array([3, -4]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output1[0], 3);
      assertCloseTo(output1[1], 3.5);

      // Second batch: [5] - should use [3, -4, 5] for MAV
      const output2 = await processor.process(
        new Float32Array([5]),
        DEFAULT_OPTIONS
      );
      // (|3| + |-4| + |5|) / 3 = 4.0
      assertCloseTo(output2[0], 4.0);
    });

    test("should handle zero values correctly", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([0, 0, 0, 5, 0]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assertCloseTo(output[0], 0);
      assertCloseTo(output[1], 0);
      assertCloseTo(output[2], 0);
      assertCloseTo(output[3], 5 / 3);
      assertCloseTo(output[4], 5 / 3);
    });
  });

  describe("State Management", () => {
    test("should serialize and deserialize state correctly", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      // Build state
      await processor.process(new Float32Array([1, -2, 3]), DEFAULT_OPTIONS);

      const rawState = await processor.saveState();
      const stateJson =
        typeof rawState === "string" ? rawState : rawState.toString("utf-8");
      const state = JSON.parse(stateJson);

      assert.ok(state);
      assert.ok(state.timestamp);
      assert.equal(state.stages.length, 1);
      assert.equal(state.stages[0].type, "meanAbsoluteValue");
      assert.equal(state.stages[0].state.windowSize, 3);
      assert.ok(state.stages[0].state.channels);

      // Create new processor and load state
      const processor2 = createDspPipeline();
      processor2.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });
      await processor2.loadState(stateJson);

      // Both should produce same output for next sample
      const output1 = await processor.process(
        new Float32Array([-4, 5]),
        DEFAULT_OPTIONS
      );
      const output2 = await processor2.process(
        new Float32Array([-4, 5]),
        DEFAULT_OPTIONS
      );

      assertCloseTo(output2[0], output1[0]);
      assertCloseTo(output2[1], output1[1]);
    });

    test("should reset state correctly", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      // Build state
      await processor.process(
        new Float32Array([1, -2, 3, -4]),
        DEFAULT_OPTIONS
      );

      // Reset
      processor.clearState();

      // Should start fresh
      const output = await processor.process(
        new Float32Array([10]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output[0], 10); // MAV of single value
    });

    test("should validate state on load", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });
      await processor.process(new Float32Array([1, -2, 3]), DEFAULT_OPTIONS);

      const rawState = await processor.saveState();
      const stateJson =
        typeof rawState === "string" ? rawState : rawState.toString("utf-8");
      const state = JSON.parse(stateJson);

      // Corrupt the buffer
      if (state.stages[0].state.channels && state.stages[0].state.channels[0]) {
        state.stages[0].state.channels[0].buffer = null;
      }

      // Create new processor with fallback disabled for this validation test
      const processor2 = createDspPipeline({
        fallbackOnLoadFailure: false,
        maxRetries: 0,
      });
      processor2.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      await assert.rejects(
        async () => processor2.loadState(JSON.stringify(state)),
        /array/i
      );
    });
  });

  describe("Mathematical Properties", () => {
    test("should always be non-negative", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([-10, -20, -30, -40]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      for (let i = 0; i < output.length; i++) {
        assert.ok(
          output[i] >= 0,
          `Output at index ${i} should be non-negative`
        );
      }
    });

    test("should be scale-invariant", async () => {
      const windowSize = 3;
      const input = new Float32Array([2, 4, 6]);

      // Test with original values
      const processor1 = createDspPipeline();
      processor1.MeanAbsoluteValue({ mode: "moving", windowSize });
      const output1 = await processor1.process(input, DEFAULT_OPTIONS);

      // Test with scaled values (multiply by 2)
      const processor2 = createDspPipeline();
      processor2.MeanAbsoluteValue({ mode: "moving", windowSize });
      const scaledInput = new Float32Array([4, 8, 12]);
      const output2 = await processor2.process(scaledInput, DEFAULT_OPTIONS);

      // MAV should scale linearly
      for (let i = 0; i < output1.length; i++) {
        assertCloseTo(output2[i], output1[i] * 2);
      }
    });

    test("should be equal to mean for all positive values", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([2, 4, 6, 8]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // For all positive values, MAV = mean
      assertCloseTo(output[0], 2);
      assertCloseTo(output[1], 3);
      assertCloseTo(output[2], 4);
      assertCloseTo(output[3], 6);
    });

    test("should be bounded by max absolute value", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([1, -10, 2, -15, 3]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      for (let i = 0; i < output.length; i++) {
        const windowStart = Math.max(0, i - 2);
        const windowValues = input.slice(windowStart, i + 1);
        const maxAbs = Math.max(...Array.from(windowValues).map(Math.abs));
        assert.ok(
          output[i] <= maxAbs,
          `MAV at ${i} (${output[i]}) should be <= max absolute value (${maxAbs})`
        );
      }
    });
  });

  describe("Edge Cases", () => {
    test("should handle very large window sizes", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 1000 });

      const input = new Float32Array([1, -2, 3, -4, 5]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // With window larger than input, each sample averages all previous samples
      assert.equal(output.length, 5);
      assertCloseTo(output[0], 1);
      assertCloseTo(output[1], 1.5);
      assertCloseTo(output[2], 2.0);
      assertCloseTo(output[3], 2.5);
      assertCloseTo(output[4], 3.0);
    });

    test("should handle very small values", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([1e-10, -2e-10, 3e-10]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.ok(output.every((v) => !isNaN(v) && isFinite(v)));
      assertCloseTo(output[0], 1e-10);
      assertCloseTo(output[1], 1.5e-10);
      assertCloseTo(output[2], 2.5e-10);
    });

    test("should handle empty input", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.equal(output.length, 0);
    });

    test("should handle single sample input", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 5 });

      const input = new Float32Array([-7]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.equal(output.length, 1);
      assertCloseTo(output[0], 7);
    });
  });

  describe("Multi-channel Processing", () => {
    test("should process multiple channels independently", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 2 });

      // Interleaved: [L1, R1, L2, R2, L3, R3]
      const input = new Float32Array([1, -10, -2, 20, 3, -30]);
      const output = await processor.process(input, {
        channels: 2,
        sampleRate: 44100,
      });

      // Channel 0 (L): 1, -2, 3
      // Channel 1 (R): -10, 20, -30
      assert.equal(output.length, 6);

      // Left channel
      assertCloseTo(output[0], 1); // |1|
      assertCloseTo(output[2], 1.5); // (|1| + |-2|) / 2
      assertCloseTo(output[4], 2.5); // (|-2| + |3|) / 2

      // Right channel
      assertCloseTo(output[1], 10); // |-10|
      assertCloseTo(output[3], 15); // (|-10| + |20|) / 2
      assertCloseTo(output[5], 25); // (|20| + |-30|) / 2
    });

    test("should maintain separate state per channel", async () => {
      processor.MeanAbsoluteValue({ mode: "moving", windowSize: 3 });

      // First batch
      await processor.process(new Float32Array([1, -10, -2, 20]), {
        channels: 2,
        sampleRate: 44100,
      });

      // Second batch - should maintain separate windows
      const output = await processor.process(new Float32Array([3, -30]), {
        channels: 2,
        sampleRate: 44100,
      });

      // Left: (|1| + |-2| + |3|) / 3
      // Right: (|-10| + |20| + |-30|) / 3
      assertCloseTo(output[0], 2.0);
      assertCloseTo(output[1], 20.0);
    });
  });

  describe("Error Handling", () => {
    test("should require window size parameter", async () => {
      assert.throws(() => {
        processor.MeanAbsoluteValue({ mode: "moving" });
      }, /windowSize/i);
    });

    test("should reject invalid window size", async () => {
      assert.throws(() => {
        processor.MeanAbsoluteValue({ mode: "moving", windowSize: 0 });
      }, /windowSize/i);

      assert.throws(() => {
        processor.MeanAbsoluteValue({ mode: "moving", windowSize: -1 });
      }, /windowSize/i);
    });

    test("should reject non-integer window size", async () => {
      assert.throws(() => {
        processor.MeanAbsoluteValue({ mode: "moving", windowSize: 2.5 });
      }, /integer/i);
    });
  });

  describe("Batch Mode", () => {
    test("should compute MAV over entire input in batch mode", async () => {
      processor.MeanAbsoluteValue({ mode: "batch" });

      const input = new Float32Array([1, -2, 3, -4, 5]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // All outputs should be the same: (|1| + |-2| + |3| + |-4| + |5|) / 5 = 3.0
      assert.equal(output.length, 5);
      for (let i = 0; i < output.length; i++) {
        assertCloseTo(output[i], 3.0);
      }
    });

    test("should recompute each time in batch mode", async () => {
      processor.MeanAbsoluteValue({ mode: "batch" });

      // First batch
      const output1 = await processor.process(
        new Float32Array([2, -4, 6]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output1[0], 4.0); // (2 + 4 + 6) / 3

      // Second batch - should not use state from first
      const output2 = await processor.process(
        new Float32Array([10, -10]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output2[0], 10.0); // (10 + 10) / 2
    });
  });
});
