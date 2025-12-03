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

describe("Rectify Filter", () => {
  let processor: DspProcessor;

  beforeEach(() => {
    processor = createDspPipeline();
  });

  afterEach(() => {
    processor.dispose();
  });

  describe("Full Rectification", () => {
    test("should convert all values to absolute (full-wave rectification)", async () => {
      processor.Rectify({ mode: "full" });

      const input = new Float32Array([1, -2, 3, -4, 5, -6]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.equal(output.length, 6);
      assertCloseTo(output[0], 1);
      assertCloseTo(output[1], 2); // |-2| = 2
      assertCloseTo(output[2], 3);
      assertCloseTo(output[3], 4); // |-4| = 4
      assertCloseTo(output[4], 5);
      assertCloseTo(output[5], 6); // |-6| = 6
    });

    test("should default to full-wave when no mode specified", async () => {
      processor.Rectify(); // No params

      const input = new Float32Array([-5, 10, -15]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assertCloseTo(output[0], 5);
      assertCloseTo(output[1], 10);
      assertCloseTo(output[2], 15);
    });

    test("should handle already positive values", async () => {
      processor.Rectify({ mode: "full" });

      const input = new Float32Array([1, 2, 3, 4, 5]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.deepEqual(Array.from(output), [1, 2, 3, 4, 5]);
    });

    test("should handle zero values", async () => {
      processor.Rectify({ mode: "full" });

      const input = new Float32Array([0, -0, 0]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.deepEqual(Array.from(output), [0, 0, 0]);
    });
  });

  describe("Half-Wave Rectification", () => {
    test("should keep positive values and zero negative values", async () => {
      processor.Rectify({ mode: "half" });

      const input = new Float32Array([1, -2, 3, -4, 5, -6]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.equal(output.length, 6);
      assertCloseTo(output[0], 1);
      assertCloseTo(output[1], 0); // -2 → 0
      assertCloseTo(output[2], 3);
      assertCloseTo(output[3], 0); // -4 → 0
      assertCloseTo(output[4], 5);
      assertCloseTo(output[5], 0); // -6 → 0
    });

    test("should not affect positive values", async () => {
      processor.Rectify({ mode: "half" });

      const input = new Float32Array([10, 20, 30]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.deepEqual(Array.from(output), [10, 20, 30]);
    });

    test("should handle mixed positive/negative pattern", async () => {
      processor.Rectify({ mode: "half" });

      const input = new Float32Array([5, -5, 10, -10, 15, -15]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assertCloseTo(output[0], 5);
      assertCloseTo(output[1], 0);
      assertCloseTo(output[2], 10);
      assertCloseTo(output[3], 0);
      assertCloseTo(output[4], 15);
      assertCloseTo(output[5], 0);
    });
  });

  describe("State Management", () => {
    test("should serialize and deserialize full-wave mode correctly", async () => {
      processor.Rectify({ mode: "full" });

      await processor.process(new Float32Array([1, -2, 3]), DEFAULT_OPTIONS);

      const stateJson = await processor.saveState();
      const state = JSON.parse(stateJson);

      assert.ok(state);
      assert.equal(state.stages.length, 1);
      assert.equal(state.stages[0].type, "rectify");
      assert.equal(state.stages[0].state.mode, "full");

      // Load into new processor
      const processor2 = createDspPipeline();
      processor2.Rectify({ mode: "full" });
      await processor2.loadState(stateJson);

      const output = await processor2.process(
        new Float32Array([-5, 10]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output[0], 5);
      assertCloseTo(output[1], 10);
    });

    test("should serialize and deserialize half-wave mode correctly", async () => {
      processor.Rectify({ mode: "half" });

      await processor.process(new Float32Array([1, -2, 3]), DEFAULT_OPTIONS);

      const stateJson = await processor.saveState();
      const state = JSON.parse(stateJson);

      assert.equal(state.stages[0].state.mode, "half");

      // Load into new processor
      const processor2 = createDspPipeline();
      processor2.Rectify({ mode: "half" });
      await processor2.loadState(stateJson);

      const output = await processor2.process(
        new Float32Array([-5, 10]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output[0], 0);
      assertCloseTo(output[1], 10);
    });

    test("should preserve mode across state save/load", async () => {
      processor.Rectify({ mode: "half" });

      const stateJson = await processor.saveState();

      // Create new processor with different mode
      const processor2 = createDspPipeline();
      processor2.Rectify({ mode: "half" }); // Must match for deserialization

      await processor2.loadState(stateJson);

      // Verify mode is preserved
      const output = await processor2.process(
        new Float32Array([-10, 10]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output[0], 0); // Half-wave behavior
      assertCloseTo(output[1], 10);
    });

    test("should handle reset (no-op for stateless filter)", async () => {
      processor.Rectify({ mode: "full" });

      await processor.process(new Float32Array([1, -2]), DEFAULT_OPTIONS);
      processor.clearState();

      const output = await processor.process(
        new Float32Array([-5]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output[0], 5); // Still works after reset
    });
  });

  describe("Edge Cases", () => {
    test("should handle empty input array", async () => {
      processor.Rectify({ mode: "full" });

      const output = await processor.process(
        new Float32Array([]),
        DEFAULT_OPTIONS
      );
      assert.equal(output.length, 0);
    });

    test("should handle single sample", async () => {
      processor.Rectify({ mode: "half" });

      const output1 = await processor.process(
        new Float32Array([-10]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output1[0], 0);

      const output2 = await processor.process(
        new Float32Array([10]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output2[0], 10);
    });

    test("should handle very small negative values (half-wave)", async () => {
      processor.Rectify({ mode: "half" });

      const input = new Float32Array([-0.0001, -0.00001, -0.000001]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.deepEqual(Array.from(output), [0, 0, 0]);
    });

    test("should handle very large values", async () => {
      processor.Rectify({ mode: "full" });

      const input = new Float32Array([-1e6, 1e6, -1e9]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assertCloseTo(output[0], 1e6);
      assertCloseTo(output[1], 1e6);
      assertCloseTo(output[2], 1e9);
    });
  });

  describe("Multi-channel Processing", () => {
    test("should rectify multiple channels independently", async () => {
      processor.Rectify({ mode: "full" });

      const input = new Float32Array([-1, -2, -3, -4, -5, -6]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // All should be positive
      assert.ok(Array.from(output).every((v) => v >= 0));
      assertCloseTo(output[0], 1);
      assertCloseTo(output[1], 2);
      assertCloseTo(output[2], 3);
    });

    test("should handle multiple process calls with state continuity", async () => {
      processor.Rectify({ mode: "half" });

      const output1 = await processor.process(
        new Float32Array([5, -5]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output1[0], 5);
      assertCloseTo(output1[1], 0);

      const output2 = await processor.process(
        new Float32Array([10, -10]),
        DEFAULT_OPTIONS
      );
      assertCloseTo(output2[0], 10);
      assertCloseTo(output2[1], 0);
    });
  });
});
