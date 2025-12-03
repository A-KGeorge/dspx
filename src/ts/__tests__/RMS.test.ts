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

describe("RMS Filter", () => {
  let processor: DspProcessor;

  beforeEach(() => {
    processor = createDspPipeline();
  });

  afterEach(() => {
    processor.dispose();
  });

  describe("Basic Functionality", () => {
    test("should compute RMS with window size 3", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([3, 4, 0, 6, 8]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // First value: sqrt(3²) = 3
      // Second value: sqrt((3² + 4²) / 2) = sqrt(25 / 2) = 3.5355...
      // Third value: sqrt((3² + 4² + 0²) / 3) = sqrt(25 / 3) = 2.8867...
      // Fourth value: sqrt((4² + 0² + 6²) / 3) = sqrt(52 / 3) = 4.1633...
      // Fifth value: sqrt((0² + 6² + 8²) / 3) = sqrt(100 / 3) = 5.7735...
      assert.equal(output.length, 5);
      assertCloseTo(output[0], 3);
      assertCloseTo(output[1], 3.5355, 4);
      assertCloseTo(output[2], 2.8867, 4);
      assertCloseTo(output[3], 4.1633, 4);
      assertCloseTo(output[4], 5.7735, 4);
    });

    test("should handle single sample window", async () => {
      processor.Rms({ mode: "moving", windowSize: 1 });

      const input = new Float32Array([3, 4, 5]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // RMS of single sample is just the absolute value
      assertCloseTo(output[0], 3);
      assertCloseTo(output[1], 4);
      assertCloseTo(output[2], 5);
    });

    test("should compute RMS correctly for negative values", async () => {
      processor.Rms({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([-3, 4, -5, 12]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // First: sqrt((-3)²) = 3
      // Second: sqrt((9 + 16) / 2) = sqrt(12.5) = 3.5355...
      // Third: sqrt((16 + 25) / 2) = sqrt(20.5) = 4.5277...
      // Fourth: sqrt((25 + 144) / 2) = sqrt(84.5) = 9.1924...
      assertCloseTo(output[0], 3);
      assertCloseTo(output[1], 3.5355, 4);
      assertCloseTo(output[2], 4.5277, 4);
      assertCloseTo(output[3], 9.1924, 4);
    });

    test("should maintain state across multiple process calls", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });

      // First batch: [3, 4]
      const output1 = await processor.process(
        new Float32Array([3, 4]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output1[0], 3);
      assertCloseTo(output1[1], 3.5355, 4);

      // Second batch: [5] - should use [3, 4, 5] for RMS
      const output2 = await processor.process(
        new Float32Array([5]),
        DEFAULT_OPTIONS
      );
      // sqrt((3² + 4² + 5²) / 3) = sqrt(50 / 3) = 4.0824...
      assertCloseTo(output2[0], 4.0824, 4);
    });
  });

  describe("State Management", () => {
    test("should serialize and deserialize state correctly", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });

      // Build state
      await processor.process(new Float32Array([3, 4, 5]), DEFAULT_OPTIONS);

      const stateJson = await processor.saveState();
      const state = JSON.parse(stateJson);

      assert.ok(state);
      assert.ok(state.timestamp);
      assert.equal(state.stages.length, 1);
      assert.equal(state.stages[0].type, "rms");
      assert.equal(state.stages[0].state.windowSize, 3);
      assert.ok(state.stages[0].state.channels);

      // Create new processor and load state
      const processor2 = createDspPipeline();
      processor2.Rms({ mode: "moving", windowSize: 3 });
      await processor2.loadState(stateJson);

      // Both should produce same output for next sample
      const output1 = await processor.process(
        new Float32Array([6]),
        DEFAULT_OPTIONS
      );
      const output2 = await processor2.process(
        new Float32Array([6]),
        DEFAULT_OPTIONS
      );

      assertCloseTo(output2[0], output1[0]);
    });

    test("should reset state correctly", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });

      // Build state
      await processor.process(new Float32Array([3, 4, 5]), DEFAULT_OPTIONS);

      // Reset
      processor.clearState();

      // Should start fresh
      const output = await processor.process(
        new Float32Array([10]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output[0], 10); // RMS of single value
    });

    test("should validate runningSumOfSquares on state load", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });
      await processor.process(new Float32Array([3, 4, 5]), DEFAULT_OPTIONS);

      const stateJson = await processor.saveState();
      const state = JSON.parse(stateJson);

      // Corrupt runningSumOfSquares
      if (state.stages[0].state.channels && state.stages[0].state.channels[0]) {
        state.stages[0].state.channels[0].runningSumOfSquares = 9999;
      }

      // Should throw when loading corrupted state
      const processor2 = createDspPipeline();
      processor2.Rms({ mode: "moving", windowSize: 3 });
      await assert.rejects(
        async () => await processor2.loadState(JSON.stringify(state)),
        /Running sum of squares validation failed/
      );
    });

    test("should validate window size on state load", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });
      await processor.process(new Float32Array([3, 4, 5]), DEFAULT_OPTIONS);

      const stateJson = await processor.saveState();
      const state = JSON.parse(stateJson);

      // Corrupt window size
      if (state.stages[0].state) {
        state.stages[0].state.windowSize = 5;
      }

      // Should throw when loading corrupted state
      const processor2 = createDspPipeline();
      processor2.Rms({ mode: "moving", windowSize: 3 });
      await assert.rejects(
        async () => await processor2.loadState(JSON.stringify(state)),
        /Window size mismatch/
      );
    });
  });

  describe("Mathematical Properties", () => {
    test("should compute RMS of constant signal correctly", async () => {
      processor.Rms({ mode: "moving", windowSize: 4 });

      const input = new Float32Array([5, 5, 5, 5]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // RMS of constant signal equals the constant
      output.forEach((val) => assertCloseTo(val, 5));
    });

    test("should compute RMS of zero signal", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([0, 0, 0, 0]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      output.forEach((val) => assertCloseTo(val, 0));
    });

    test("should handle alternating positive/negative", async () => {
      processor.Rms({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([3, -3, 4, -4]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // First: sqrt(9) = 3
      // Second: sqrt((9 + 9) / 2) = 3
      // Third: sqrt((9 + 16) / 2) = sqrt(12.5) = 3.5355...
      // Fourth: sqrt((16 + 16) / 2) = 4
      assertCloseTo(output[0], 3);
      assertCloseTo(output[1], 3);
      assertCloseTo(output[2], 3.5355, 4);
      assertCloseTo(output[3], 4);
    });

    test("should produce value equal to or less than max absolute value", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([1, 5, 2]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // RMS should be <= max absolute value for each window
      assert.ok(output[0] <= 1);
      assert.ok(output[1] <= 5);
      assert.ok(output[2] <= 5);
    });
  });

  describe("Edge Cases", () => {
    test("should handle empty input array", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });

      const output = await processor.process(
        new Float32Array([]),
        DEFAULT_OPTIONS
      );
      assert.equal(output.length, 0);
    });

    test("should handle very small values", async () => {
      processor.Rms({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([0.0001, 0.0002, 0.0001]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.ok(output.every((v) => v > 0 && v < 0.001));
    });

    test("should handle very large values", async () => {
      processor.Rms({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([1e6, 1e6]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assertCloseTo(output[0], 1e6);
      assertCloseTo(output[1], 1e6);
    });

    test("should handle mixed magnitude ranges", async () => {
      processor.Rms({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([0.001, 1000, 0.001]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.ok(output.every((v) => v >= 0));
      assert.ok(output.some((v) => v > 100)); // Large value influences RMS
    });
  });

  describe("Multi-channel Processing", () => {
    test("should process data with stateful continuity", async () => {
      processor.Rms({ mode: "moving", windowSize: 2 });

      // First batch
      const output1 = await processor.process(
        new Float32Array([3, 4]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output1[0], 3);
      assertCloseTo(output1[1], 3.5355, 4);

      // Second batch - continues from previous
      const output2 = await processor.process(
        new Float32Array([5, 0]),
        DEFAULT_OPTIONS
      );
      // sqrt((16 + 25) / 2) = sqrt(20.5) = 4.5277...
      assertCloseTo(output2[0], 4.5277, 4);
      // sqrt((25 + 0) / 2) = sqrt(12.5) = 3.5355...
      assertCloseTo(output2[1], 3.5355, 4);
    });

    test("should maintain separate state across multiple batches", async () => {
      processor.Rms({ mode: "moving", windowSize: 2 });

      const batches = [
        new Float32Array([1, 2]),
        new Float32Array([3, 4]),
        new Float32Array([5, 6]),
      ];

      for (const batch of batches) {
        const output = await processor.process(batch, DEFAULT_OPTIONS);
        assert.equal(output.length, batch.length);
        assert.ok(output.every((v) => v > 0));
      }
    });
  });
});
